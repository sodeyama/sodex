#!/usr/bin/env python3
"""QEMU 上のカーネル統合テストを安定実行するランナー。"""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

QEMU_SUCCESS = 1
DEFAULT_TIMEOUT = 60


def _dump_file(path: pathlib.Path) -> None:
    if not path.exists():
        return
    data = path.read_text(errors="replace")
    if data:
        print(data, end="")


def _query_monitor(sock_path: pathlib.Path) -> str:
    if not sock_path.exists():
        return ""

    chunks: list[bytes] = []
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.5)
            sock.connect(str(sock_path))
            time.sleep(0.1)
            try:
                chunks.append(sock.recv(4096))
            except OSError:
                pass

            for command in ("info registers", "info pic", "info lapic", "x/8i $eip", "quit"):
                sock.sendall((command + "\n").encode("ascii"))
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
    if len(sys.argv) != 3:
        print("usage: run_qemu_ktest.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    echo_server = repo_root / "src/test/echo_server.py"
    halfclose_server = repo_root / "src/test/halfclose_server.py"

    logdir.mkdir(parents=True, exist_ok=True)
    serial_log = logdir / "test_serial.log"
    qemu_log = logdir / "test_qemu_debug.log"
    echo_log = logdir / "test_echo.log"
    monitor_log = logdir / "test_monitor.log"
    monitor_sock = logdir / "test_monitor.sock"

    for path in (serial_log, qemu_log, echo_log, monitor_log, monitor_sock):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    halfclose_log = logdir / "test_halfclose.log"
    if halfclose_log.exists():
        halfclose_log.unlink()

    with echo_log.open("w") as echo_fp, halfclose_log.open("w") as hc_fp:
        echo_proc = subprocess.Popen(
            [sys.executable, str(echo_server)],
            cwd=repo_root,
            stdout=echo_fp,
            stderr=subprocess.STDOUT,
            text=True,
        )
        halfclose_proc = subprocess.Popen(
            [sys.executable, str(halfclose_server)],
            cwd=repo_root,
            stdout=hc_fp,
            stderr=subprocess.STDOUT,
            text=True,
        )

        try:
            time.sleep(1.0)
            if echo_proc.poll() is not None:
                print("echo server failed to start")
                _dump_file(echo_log)
                return 1
            if halfclose_proc.poll() is not None:
                print("halfclose server failed to start")
                _dump_file(halfclose_log)
                return 1

            qemu_args = [
                qemu_bin,
                "-drive",
                f"file={fsboot},format=raw,if=ide",
                "-m",
                str(qemu_memory_mb),
                "-nographic",
                "-device",
                "isa-debug-exit,iobase=0xf4,iosize=0x04",
                "-no-reboot",
                "-monitor",
                f"unix:{monitor_sock},server,nowait",
                "-serial",
                f"file:{serial_log}",
                "-D",
                str(qemu_log),
                "-netdev",
                "user,id=net0,guestfwd=tcp:10.0.2.100:7777-tcp:127.0.0.1:17777,guestfwd=tcp:10.0.2.100:7778-tcp:127.0.0.1:17778",
                "-device",
                "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
            ]

            qemu_proc = subprocess.Popen(
                qemu_args,
                cwd=repo_root,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

            timed_out = False
            try:
                qemu_rc = qemu_proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                monitor_log.write_text(_query_monitor(monitor_sock), errors="replace")
                qemu_proc.terminate()
                try:
                    qemu_rc = qemu_proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    qemu_proc.kill()
                    qemu_rc = qemu_proc.wait()

            echo_proc.terminate()
            halfclose_proc.terminate()
            try:
                echo_proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                echo_proc.kill()
                echo_proc.wait()
            try:
                halfclose_proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                halfclose_proc.kill()
                halfclose_proc.wait()

            print("")
            print("=== KERNEL INTEGRATION TESTS DONE ===")
            _dump_file(serial_log)
            print(f"Logs: {serial_log}, {qemu_log}, {echo_log}, {monitor_log}")

            if timed_out:
                print(f"QEMU timed out after {timeout}s", file=sys.stderr)
                return 124
            if qemu_rc == QEMU_SUCCESS:
                return 0
            return qemu_rc
        finally:
            if echo_proc.poll() is None:
                echo_proc.kill()
                echo_proc.wait()


if __name__ == "__main__":
    raise SystemExit(main())
