#!/usr/bin/env python3
"""rcS 失敗時に rescue へ落ちることを QEMU で確認する。"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import time
from typing import TextIO

from qemu_config import get_qemu_memory_mb
from run_qemu_ssh_smoke import (
    assert_no_guest_failure,
    assert_qemu_running,
    dump_file,
    read_text,
)

DEFAULT_TIMEOUT = 45
REQUIRED_MARKERS = (
    "AUDIT init_rc_begin",
    "AUDIT rcS_fail_begin",
    "AUDIT init_rc_done fail",
    "AUDIT init_enter_rescue",
    "AUDIT eshell_ready",
)


def wait_until_ready(deadline: float, serial_log: pathlib.Path,
                     qemu_log: pathlib.Path,
                     qemu_proc: subprocess.Popen[bytes],
                     qemu_stderr_log: pathlib.Path) -> None:
    while time.time() < deadline:
        assert_qemu_running(qemu_proc, qemu_stderr_log)
        assert_no_guest_failure(serial_log, qemu_log)
        serial_text = read_text(serial_log)
        if all(marker in serial_text for marker in REQUIRED_MARKERS):
            return
        time.sleep(0.5)
    raise AssertionError("init failure markers did not become ready in time")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_init_failure_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    logdir.mkdir(parents=True, exist_ok=True)

    serial_log = logdir / "init_failure_serial.log"
    qemu_log = logdir / "init_failure_qemu_debug.log"
    qemu_stderr_log = logdir / "init_failure_qemu_stderr.log"

    for path in (serial_log, qemu_log, qemu_stderr_log):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    qemu_cmd = [
        "script",
        "-q",
        "/dev/null",
        qemu_bin,
        "-drive",
        f"file={fsboot},format=raw,if=ide",
        "-m",
        str(qemu_memory_mb),
        "-nographic",
        "-serial",
        f"file:{serial_log}",
        "-D",
        str(qemu_log),
        "-netdev",
        "user,id=net0",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_proc = None
    qemu_stderr_fp: TextIO | None = None
    try:
        qemu_stderr_fp = qemu_stderr_log.open("w", encoding="utf-8")
        qemu_proc = subprocess.Popen(
            qemu_cmd,
            stdout=subprocess.DEVNULL,
            stderr=qemu_stderr_fp,
        )
        wait_until_ready(time.time() + timeout, serial_log, qemu_log,
                         qemu_proc, qemu_stderr_log)
        print("=== INIT FAILURE SMOKE DONE ===")
        dump_file(serial_log)
        return 0
    except Exception:
        dump_file(serial_log)
        dump_file(qemu_log)
        dump_file(qemu_stderr_log)
        raise
    finally:
        if qemu_proc is not None:
            qemu_proc.terminate()
            try:
                qemu_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu_proc.kill()
                qemu_proc.wait()
        if qemu_stderr_fp is not None:
            qemu_stderr_fp.close()


if __name__ == "__main__":
    raise SystemExit(main())
