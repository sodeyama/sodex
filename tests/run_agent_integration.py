#!/usr/bin/env python3
"""
QEMU Agent Integration test runner (Plan 18).

1. Generates TLS certs (if missing)
2. Starts mock Claude SSE server with TLS (multi-scenario support)
3. Rebuilds with overlay that runs 'agent_integ' command
4. Launches QEMU
5. Checks serial output for [AGENT-INTEG] PASS/FAIL markers
"""

from __future__ import annotations

import os
import pathlib
import re
import socket
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "src" / "test"))
from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 120
CLAUDE_TLS_PORT = 4443


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


def query_monitor(sock_path: pathlib.Path) -> str:
    if not sock_path.exists():
        return ""
    chunks: list[bytes] = []
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.5)
            sock.connect(str(sock_path))
            time.sleep(0.1)
            for cmd in ("info registers", "info pic", "quit"):
                sock.sendall((cmd + "\n").encode("ascii"))
                time.sleep(0.1)
                while True:
                    try:
                        data = sock.recv(4096)
                    except OSError:
                        break
                    if not data:
                        break
                    chunks.append(data)
                    if len(data) < 4096:
                        break
    except OSError as exc:
        return f"monitor query failed: {exc}\n"
    return b"".join(chunks).decode(errors="replace")


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    logdir = repo_root / "build" / "log"
    logdir.mkdir(parents=True, exist_ok=True)
    fsboot = repo_root / "build" / "bin" / "fsboot.bin"
    claude_server = repo_root / "tests" / "mock_claude_server.py"

    serial_log = logdir / "agent_integ_serial.log"
    qemu_log = logdir / "agent_integ_qemu_debug.log"
    claude_log = logdir / "agent_integ_claude_mock.log"
    monitor_log = logdir / "agent_integ_monitor.log"
    monitor_sock = logdir / "agent_integ_monitor.sock"

    for p in (serial_log, qemu_log, claude_log, monitor_log, monitor_sock):
        if p.exists():
            p.unlink()

    # Build with agent_integ overlay
    print("Building with agent_integ overlay...")
    subprocess.run(
        ["make", "clean"],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )

    # Create rootfs overlay that runs 'agent_integ' command
    default_overlay = repo_root / "src" / "rootfs-overlay"
    overlay_dir = logdir / "agent-integ-rootfs-overlay"
    if overlay_dir.exists():
        import shutil
        shutil.rmtree(overlay_dir)
    import shutil
    logdir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(default_overlay, overlay_dir)
    (overlay_dir / "etc" / "inittab").write_text(
        "# Agent integration test\n"
        "initdefault:user\n"
        "sysinit:/etc/init.d/rcS\n"
        "respawn:user:/usr/bin/agent_integ\n"
        "respawn:rescue:/usr/bin/eshell\n"
    )
    (overlay_dir / "etc" / "init.d" / "rcS").write_text(
        "echo AUDIT rcS_begin\n"
        "echo AUDIT rcS_done\n"
        "exit 0\n"
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

    # Start mock Claude TLS server (with multi-scenario support)
    with claude_log.open("w") as claude_fp:
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
        print(f"Mock Claude TLS server on port {CLAUDE_TLS_PORT}")

        # QEMU: SLiRP maps guest 10.0.2.2 -> host 127.0.0.1
        qemu_args = [
            qemu_bin,
            "-drive", f"file={fsboot},format=raw,if=ide",
            "-m", str(qemu_memory_mb),
            "-nographic",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-no-reboot",
            "-monitor", f"unix:{monitor_sock},server,nowait",
            "-serial", f"file:{serial_log}",
            "-D", str(qemu_log),
            "-netdev", "user,id=net0",
            "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
        ]

        print(f"Starting QEMU (timeout={timeout}s)...")
        qemu_proc = subprocess.Popen(
            qemu_args,
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # Poll for completion
        timed_out = False
        test_done = False
        qemu_rc = -1
        deadline = time.time() + timeout
        while time.time() < deadline:
            rc = qemu_proc.poll()
            if rc is not None:
                qemu_rc = rc
                break
            if serial_log.exists():
                text = serial_log.read_text(errors="replace")
                if "ALL TESTS PASSED" in text or "RESULT:" in text:
                    test_done = True
                    time.sleep(0.5)
                    qemu_proc.terminate()
                    try:
                        qemu_rc = qemu_proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        qemu_proc.kill()
                        qemu_rc = qemu_proc.wait()
                    break
                for marker in ("PF:", "PageFault", "General Protection"):
                    if marker in text:
                        test_done = True
                        qemu_proc.terminate()
                        try:
                            qemu_rc = qemu_proc.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            qemu_proc.kill()
                            qemu_rc = qemu_proc.wait()
                        break
                if test_done:
                    break
            time.sleep(1.0)
        else:
            timed_out = True
            monitor_log.write_text(query_monitor(monitor_sock), errors="replace")
            qemu_proc.terminate()
            try:
                qemu_rc = qemu_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu_proc.kill()
                qemu_rc = qemu_proc.wait()

        # Report
        print("")
        print("=== AGENT INTEGRATION TEST RESULTS ===")
        dump_file(serial_log)
        print(f"\nLogs: {serial_log}")
        print(f"QEMU debug: {qemu_log}")
        print(f"Claude mock: {claude_log}")

        if timed_out:
            print(f"\nQEMU timed out after {timeout}s", file=sys.stderr)
            dump_file(monitor_log)
            return 124

        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")

            for marker in ("PF:", "PageFault", "General Protection Exception"):
                if marker in serial_text:
                    print(f"\nFAILURE: Crash detected ({marker})")
                    return 1

            result_match = re.search(
                r"RESULT:\s+(\d+)/(\d+)\s+passed", serial_text
            )
            if result_match:
                passed = int(result_match.group(1))
                total = int(result_match.group(2))
                if passed == total and total > 0:
                    print(f"\nSUCCESS: {passed}/{total} tests passed")
                    return 0
                else:
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


if __name__ == "__main__":
    raise SystemExit(main())
