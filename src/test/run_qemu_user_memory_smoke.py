#!/usr/bin/env python3
"""execve と brk の userland 回帰を QEMU で確認する。"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb
from run_qemu_shell_io_smoke import (
    DEFAULT_TIMEOUT,
    QemuMonitor,
    SODEX_ROOT_INO,
    read_dir_entries,
    read_file,
    wait_for_metric,
    wait_for_path,
)


def parse_report(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        fields[key.strip()] = value.strip()
    return fields


def assert_user_memory_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()
    root_entries = read_dir_entries(image, SODEX_ROOT_INO)
    report_entry = root_entries.get("memgrow.txt")
    after_entry = root_entries.get("after.touch")

    if report_entry is None:
        raise AssertionError("memgrow.txt was not created")
    if after_entry is None:
        raise AssertionError("after.touch was not created after memgrow")

    report = read_file(image, report_entry[0]).decode("ascii", errors="ignore")
    fields = parse_report(report)
    status = fields.get("status")
    if status != "ok":
        raise AssertionError(f"memgrow status is not ok: {status!r}")

    alloc_before = int(fields["alloc_before"], 16)
    alloc_after = int(fields["alloc_after"], 16)
    touched_bytes = int(fields["touched_bytes"], 16)
    min_growth = touched_bytes // 2

    if alloc_after <= alloc_before:
        raise AssertionError("alloc_after did not grow")
    if alloc_after - alloc_before < min_growth:
        raise AssertionError(
            f"allocpoint growth is too small: before=0x{alloc_before:08x} after=0x{alloc_after:08x}"
        )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_user_memory_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = logdir / f"user_memory_monitor_{os.getpid()}.sock"
    serial_log = logdir / f"user_memory_serial_{os.getpid()}.log"
    qemu_log = logdir / f"user_memory_qemu_{os.getpid()}.log"

    for path in (monitor_sock, serial_log, qemu_log):
        if path.exists():
            path.unlink()

    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_memory_mb = get_qemu_memory_mb()
    qemu_args = [
        qemu_bin,
        "-drive", f"file={fsboot},format=raw,if=ide",
        "-m", str(qemu_memory_mb),
        "-display", "none",
        "-no-reboot",
        "-serial", f"file:{serial_log}",
        "-monitor", f"unix:{monitor_sock},server,nowait",
        "-D", str(qemu_log),
        "-netdev", "user,id=net0",
        "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    commands = [
        ("memgrow\n", 2.5),
        ("touch after.touch\n", 1.0),
    ]

    qemu = subprocess.Popen(
        qemu_args,
        cwd=repo_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    monitor: QemuMonitor | None = None
    try:
        wait_for_path(monitor_sock, 10)
        monitor = QemuMonitor(monitor_sock)
        wait_for_metric(serial_log, "full_redraw", timeout)

        for command, delay in commands:
            monitor.send_text(command)
            time.sleep(delay)

        time.sleep(1.0)
        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)

        assert_user_memory_state(fsboot)
        serial_text = serial_log.read_text(errors="replace")
        if "PF:" in serial_text or "PageFault" in serial_text:
            raise AssertionError("page fault was detected during user memory smoke")

        print("=== USER MEMORY QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}")
        return 0
    except Exception as exc:
        print(f"user memory smoke failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if monitor is not None:
            monitor.close()
        if qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()


if __name__ == "__main__":
    raise SystemExit(main())
