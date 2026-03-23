#!/usr/bin/env python3
"""sxi agent workflow を QEMU 上で確認する。"""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 120
CLAUDE_TLS_PORT = 4443
FAILURE_MARKERS = ("PF:", "PageFault", "General Protection Exception")


def dump_file(path: pathlib.Path) -> None:
    if not path.exists():
        return
    data = path.read_text(errors="replace")
    if data:
        print(data, end="")


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
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    logdir = repo_root / "build" / "log"
    logdir.mkdir(parents=True, exist_ok=True)
    fsboot = repo_root / "build" / "bin" / "fsboot.bin"
    claude_server = repo_root / "tests" / "mock_claude_server.py"

    serial_log = logdir / "sxi_agent_serial.log"
    qemu_log = logdir / "sxi_agent_qemu_debug.log"
    claude_log = logdir / "sxi_agent_claude_mock.log"

    for path in (serial_log, qemu_log, claude_log):
        if path.exists():
            path.unlink()

    overlay_dir = logdir / "sxi-agent-rootfs-overlay"
    if overlay_dir.exists():
        shutil.rmtree(overlay_dir)
    shutil.copytree(repo_root / "src" / "rootfs-overlay", overlay_dir)
    (overlay_dir / "etc" / "inittab").write_text(
        "# sxi agent workflow smoke\n"
        "initdefault:user\n"
        "sysinit:/etc/init.d/rcS\n"
        "respawn:user:/usr/bin/agent_integ sxi-workflow\n"
        "respawn:rescue:/usr/bin/eshell\n",
        encoding="ascii",
    )
    (overlay_dir / "etc" / "init.d" / "rcS").write_text(
        "echo AUDIT rcS_begin\n"
        "echo AUDIT rcS_done\n"
        "exit 0\n",
        encoding="ascii",
    )

    build_result = subprocess.run(
        ["make", f"SODEX_ROOTFS_OVERLAY={overlay_dir}", "all"],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    if build_result.returncode != 0:
        print("BUILD FAILED:")
        print(build_result.stdout[-2000:] if len(build_result.stdout) > 2000 else build_result.stdout)
        print(build_result.stderr[-2000:] if len(build_result.stderr) > 2000 else build_result.stderr)
        return 1

    if not fsboot.exists():
        print(f"ERROR: {fsboot} not found after build", file=sys.stderr)
        return 1

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    claude_fp = claude_log.open("w")
    claude_proc = subprocess.Popen(
        [sys.executable, str(claude_server), str(CLAUDE_TLS_PORT), "--tls"],
        cwd=repo_root,
        stdout=claude_fp,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        if not wait_for_port("127.0.0.1", CLAUDE_TLS_PORT, timeout=5.0):
            print("ERROR: Mock Claude server failed to start", file=sys.stderr)
            dump_file(claude_log)
            claude_proc.kill()
            return 1

        qemu_cmd = [
            qemu_bin,
            "-drive", f"file={fsboot},format=raw,if=ide",
            "-m", str(qemu_memory_mb),
            "-nographic",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-no-reboot",
            "-serial", f"file:{serial_log}",
            "-D", str(qemu_log),
            "-netdev", "user,id=net0",
            "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
        ]

        qemu_proc = subprocess.Popen(
            qemu_cmd,
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        timed_out = False
        qemu_rc = -1
        deadline = time.time() + timeout
        while time.time() < deadline:
            rc = qemu_proc.poll()
            if rc is not None:
                qemu_rc = rc
                break
            if serial_log.exists():
                serial_text = serial_log.read_text(errors="replace")
                if "ALL TESTS PASSED" in serial_text or "RESULT:" in serial_text:
                    time.sleep(0.5)
                    qemu_proc.terminate()
                    try:
                        qemu_rc = qemu_proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        qemu_proc.kill()
                        qemu_rc = qemu_proc.wait()
                    break
            time.sleep(1.0)
        else:
            timed_out = True
            qemu_proc.terminate()
            try:
                qemu_rc = qemu_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu_proc.kill()
                qemu_rc = qemu_proc.wait()

        print("")
        print("=== SXI AGENT SMOKE RESULTS ===")
        dump_file(serial_log)
        print(f"\nLogs: {serial_log}")
        print(f"QEMU debug: {qemu_log}")
        print(f"Claude mock: {claude_log}")

        if timed_out:
            print(f"\nQEMU timed out after {timeout}s", file=sys.stderr)
            return 124

        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")
            for marker in FAILURE_MARKERS:
                if marker in serial_text:
                    print(f"\nFAILURE: Crash detected ({marker})")
                    return 1
            result_match = re.search(r"RESULT:\s+(\d+)/(\d+)\s+passed", serial_text)
            if result_match:
                passed = int(result_match.group(1))
                total = int(result_match.group(2))
                if passed == total and total > 0:
                    print(f"\nSUCCESS: {passed}/{total} tests passed")
                    return 0
                print(f"\nFAILURE: {passed}/{total} tests passed")
                return 1
            if "ALL TESTS PASSED" in serial_text:
                print("\nSUCCESS: All tests passed")
                return 0

        print(f"\nQEMU exit code: {qemu_rc}")
        return 1
    finally:
        if claude_proc.poll() is None:
            claude_proc.kill()
            claude_proc.wait()
        claude_fp.close()


if __name__ == "__main__":
    raise SystemExit(main())
