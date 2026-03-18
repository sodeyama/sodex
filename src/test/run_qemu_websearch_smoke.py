#!/usr/bin/env python3
"""QEMU smoke test: websearch via SearXNG (HTTP, no TLS)."""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 60


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_websearch_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    serial_log = logdir / "test_websearch_serial.log"
    qemu_log = logdir / "test_websearch_qemu_debug.log"

    for path in (serial_log, qemu_log):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    qemu_args = [
        qemu_bin,
        "-drive", f"file={fsboot},format=raw,if=ide",
        "-m", str(qemu_memory_mb),
        "-nographic",
        "-no-reboot",
        "-serial", f"file:{serial_log}",
        "-D", str(qemu_log),
        "-netdev", "user,id=net0",
        "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_proc = subprocess.Popen(
        qemu_args,
        cwd=repo_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    timed_out = False
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if qemu_proc.poll() is not None:
                break
            if serial_log.exists():
                text = serial_log.read_text(errors="replace")
                if "init_rc_done" in text:
                    break
            time.sleep(1)
        else:
            timed_out = True
    finally:
        qemu_proc.terminate()
        try:
            qemu_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            qemu_proc.kill()
            qemu_proc.wait()

    print("")
    print("=== WEBSEARCH SMOKE TEST ===")
    text = ""
    if serial_log.exists():
        text = serial_log.read_text(errors="replace")
        for line in text.splitlines():
            if any(kw in line for kw in ("Search:", "[", "websearch", "init_rc",
                                          "http", "connect", "error")):
                print(line)
    print(f"Logs: {serial_log}, {qemu_log}")

    if timed_out:
        print(f"QEMU timed out after {timeout}s", file=sys.stderr)
        return 124

    init_ok = "init_rc_done ok" in text
    init_fail = "init_rc_done fail" in text
    http_ok = "200 OK" in text

    if init_ok and http_ok:
        print("RESULT: PASS — websearch completed successfully (HTTP 200, exit 0)")
        return 0

    if init_fail:
        print("RESULT: FAIL — websearch exited with error")
    elif not http_ok:
        print("RESULT: FAIL — HTTP request did not succeed")
    else:
        print("RESULT: FAIL — unknown reason")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
