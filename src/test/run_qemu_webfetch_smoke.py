#!/usr/bin/env python3
"""QEMU smoke test: webfetch via host-side structured gateway."""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 60
SOURCE_PORT = 18081
GATEWAY_PORT = 8081


def wait_for_port(host: str, port: int, timeout: float = 10.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.2)
    return False


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_webfetch_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    gateway_script = repo_root / "src" / "tools" / "web_fetch_gateway.py"
    source_script = repo_root / "tests" / "mock_web_fetch_source.py"

    logdir.mkdir(parents=True, exist_ok=True)
    serial_log = logdir / "test_webfetch_serial.log"
    qemu_log = logdir / "test_webfetch_qemu_debug.log"
    gateway_log = logdir / "test_webfetch_gateway.log"
    source_log = logdir / "test_webfetch_source.log"

    for path in (serial_log, qemu_log, gateway_log, source_log):
        if path.exists():
            path.unlink()

    source_fp = source_log.open("w")
    gateway_fp = gateway_log.open("w")
    source_proc = subprocess.Popen(
        [sys.executable, str(source_script), str(SOURCE_PORT)],
        cwd=repo_root,
        stdout=source_fp,
        stderr=subprocess.STDOUT,
        text=True,
    )
    gateway_env = os.environ.copy()
    gateway_env["SODEX_WEBFETCH_ALLOWLIST"] = "127.0.0.1,localhost"
    gateway_proc = subprocess.Popen(
        [sys.executable, str(gateway_script), str(GATEWAY_PORT)],
        cwd=repo_root,
        env=gateway_env,
        stdout=gateway_fp,
        stderr=subprocess.STDOUT,
        text=True,
    )

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    try:
        if not wait_for_port("127.0.0.1", SOURCE_PORT, timeout=5.0):
            print("source server did not start", file=sys.stderr)
            return 1
        if not wait_for_port("127.0.0.1", GATEWAY_PORT, timeout=5.0):
            print("gateway server did not start", file=sys.stderr)
            return 1

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
                time.sleep(1.0)
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
        print("=== WEBFETCH SMOKE TEST ===")
        text = ""
        if serial_log.exists():
            text = serial_log.read_text(errors="replace")
            for line in text.splitlines():
                if any(key in line for key in (
                    "[WEBFETCH]", "AUDIT init_webfetch", "init_rc_done"
                )):
                    print(line)
        print(f"Logs: {serial_log}, {qemu_log}")

        if timed_out:
            print(f"QEMU timed out after {timeout}s", file=sys.stderr)
            return 124

        article_ok = "AUDIT init_webfetch_article 0" in text
        long_ok = "AUDIT init_webfetch_long 0" in text
        deny_ok = "AUDIT init_webfetch_deny 1" in text
        title_ok = "title=Sodex Article" in text
        trunc_ok = "truncated=1" in text and "source_url=http://127.0.0.1:18081/long" in text
        deny_code_ok = "code=allowlist_denied" in text and "url=http://denied.invalid/article" in text

        if article_ok and long_ok and deny_ok and title_ok and trunc_ok and deny_code_ok:
            print("RESULT: PASS — webfetch success, truncation, and deny path validated")
            return 0

        print("RESULT: FAIL — webfetch smoke expectations were not met")
        return 1
    finally:
        gateway_proc.terminate()
        source_proc.terminate()
        try:
            gateway_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            gateway_proc.kill()
            gateway_proc.wait()
        try:
            source_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            source_proc.kill()
            source_proc.wait()
        gateway_fp.close()
        source_fp.close()


if __name__ == "__main__":
    raise SystemExit(main())
