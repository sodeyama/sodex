#!/usr/bin/env python3
"""debug shell の QEMU smoke を実行する。"""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 45
HOST_HTTP_PORT = 18080
HOST_ADMIN_PORT = 10023
HOST_DEBUG_SHELL_PORT = int(os.environ.get("SODEX_HOST_DEBUG_SHELL_PORT", "10024"))
GUEST_DEBUG_SHELL_PORT = int(os.environ.get("SODEX_DEBUG_SHELL_PORT", "10024"))
STATUS_TOKEN = os.environ.get("SODEX_ADMIN_STATUS_TOKEN", "status-secret")
CONTROL_TOKEN = os.environ.get("SODEX_ADMIN_CONTROL_TOKEN", "control-secret")
BASE_READY_MARKERS = (
    "AUDIT listener_ready kind=admin port=10023",
    "AUDIT listener_ready kind=http port=8080",
)
DEBUG_READY_MARKER = f"AUDIT listener_ready kind=debug_shell port={GUEST_DEBUG_SHELL_PORT}"
FAILURE_MARKERS = ("PF:", "PageFault", "General Protection Exception")


def dump_file(path: pathlib.Path) -> None:
    if not path.exists():
        return
    data = path.read_text(errors="replace")
    if data:
        print(data, end="")


def read_text(path: pathlib.Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(errors="replace")


def query_monitor(sock_path: pathlib.Path) -> str:
    if not sock_path.exists():
        return ""

    chunks: list[bytes] = []
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.5)
            sock.connect(str(sock_path))
            time.sleep(0.1)
            for command in ("info registers", "info pic", "quit"):
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


def assert_no_guest_failure(serial_log: pathlib.Path, qemu_log: pathlib.Path) -> None:
    serial_text = read_text(serial_log)
    qemu_text = read_text(qemu_log)

    for marker in FAILURE_MARKERS:
        if marker in serial_text or marker in qemu_text:
            raise AssertionError(f"guest failure marker detected: {marker}")


def wait_until_ready(deadline: float, serial_log: pathlib.Path,
                     qemu_log: pathlib.Path) -> None:
    while time.time() < deadline:
        serial_text = read_text(serial_log)
        assert_no_guest_failure(serial_log, qemu_log)
        if (all(marker in serial_text for marker in BASE_READY_MARKERS) and
                DEBUG_READY_MARKER in serial_text):
            return
        time.sleep(0.5)
    raise AssertionError("debug shell did not become ready in time")


def wait_for_close_events(deadline: float, serial_log: pathlib.Path,
                          qemu_log: pathlib.Path, expected_count: int) -> None:
    while time.time() < deadline:
        assert_no_guest_failure(serial_log, qemu_log)
        if read_text(serial_log).count("debug_shell_close") >= expected_count:
            return
        time.sleep(0.2)
    raise AssertionError("debug shell close audit did not appear in time")


def recv_until(sock: socket.socket, needle: bytes, timeout: float) -> bytes:
    deadline = time.time() + timeout
    chunks: list[bytes] = []
    while time.time() < deadline:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            break
        chunks.append(data)
        joined = b"".join(chunks)
        if needle in joined:
            return joined
    raise AssertionError(f"expected {needle!r} in stream, got {b''.join(chunks)!r}")


def request_debug_shell(preface: str, needle: bytes, timeout: float = 2.0) -> bytes:
    with socket.create_connection(("127.0.0.1", HOST_DEBUG_SHELL_PORT), timeout=2.0) as sock:
        sock.settimeout(0.5)
        sock.sendall(preface.encode("ascii"))
        return recv_until(sock, needle, timeout)


def open_authenticated_shell() -> tuple[socket.socket, bytes]:
    sock = socket.create_connection(("127.0.0.1", HOST_DEBUG_SHELL_PORT), timeout=2.0)
    sock.settimeout(0.5)
    sock.sendall(f"TOKEN {CONTROL_TOKEN}\n".encode("ascii"))
    data = recv_until(sock, b"OK shell\n", 5.0)
    if b"OK shell\n" not in data:
        sock.close()
        raise AssertionError(f"missing shell upgrade marker: {data!r}")
    return sock, data


def reconnect_authenticated_shell(deadline: float) -> tuple[socket.socket, bytes]:
    last_error = ""
    while time.time() < deadline:
        try:
            return open_authenticated_shell()
        except Exception as exc:  # noqa: BLE001
            last_error = str(exc)
            time.sleep(0.2)
    raise AssertionError(f"failed to reconnect debug shell: {last_error}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_debug_shell_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    logdir.mkdir(parents=True, exist_ok=True)

    serial_log = logdir / "debug_shell_serial.log"
    qemu_log = logdir / "debug_shell_qemu_debug.log"
    monitor_log = logdir / "debug_shell_monitor.log"
    monitor_sock = logdir / "debug_shell_monitor.sock"

    for path in (serial_log, qemu_log, monitor_log, monitor_sock):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    qemu_args = [
        qemu_bin,
        "-drive",
        f"file={fsboot},format=raw,if=ide",
        "-m",
        str(qemu_memory_mb),
        "-nographic",
        "-monitor",
        f"unix:{monitor_sock},server,nowait",
        "-serial",
        f"file:{serial_log}",
        "-D",
        str(qemu_log),
        "-netdev",
        "user,id=net0,"
        f"hostfwd=tcp:127.0.0.1:{HOST_HTTP_PORT}-10.0.2.15:8080,"
        f"hostfwd=tcp:127.0.0.1:{HOST_ADMIN_PORT}-10.0.2.15:10023,"
        f"hostfwd=tcp:127.0.0.1:{HOST_DEBUG_SHELL_PORT}-10.0.2.15:{GUEST_DEBUG_SHELL_PORT}",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_proc = subprocess.Popen(
        qemu_args,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    try:
        deadline = time.time() + timeout
        wait_until_ready(deadline, serial_log, qemu_log)

        data = request_debug_shell("STATUS\n", b"ERR invalid_preface\n")
        if b"ERR invalid_preface\n" not in data:
            raise AssertionError(f"unexpected invalid preface response: {data!r}")
        wait_for_close_events(deadline, serial_log, qemu_log, 1)

        data = request_debug_shell(f"TOKEN {STATUS_TOKEN}\n", b"ERR unauthorized\n")
        if b"ERR unauthorized\n" not in data:
            raise AssertionError(f"unexpected unauthorized response: {data!r}")
        wait_for_close_events(deadline, serial_log, qemu_log, 2)

        shell_sock, banner = open_authenticated_shell()
        try:
            if b"OK shell\n" not in banner:
                raise AssertionError(f"missing shell upgrade marker: {banner!r}")
        finally:
            shell_sock.close()

        wait_for_close_events(deadline, serial_log, qemu_log, 3)
        shell_sock, banner = reconnect_authenticated_shell(time.time() + 5.0)
        try:
            if b"OK shell\n" not in banner:
                raise AssertionError(f"reconnect missing upgrade marker: {banner!r}")
        finally:
            shell_sock.close()

        serial_text = read_text(serial_log)
        if "debug_shell_start" not in serial_text:
            raise AssertionError("serial log missing debug_shell_start audit")
        if serial_text.count("debug_shell_close") < 3:
            raise AssertionError("serial log missing debug_shell_close audit")

        assert_no_guest_failure(serial_log, qemu_log)
        print("=== DEBUG SHELL SMOKE DONE ===")
        dump_file(serial_log)
        print(f"Logs: {serial_log}, {qemu_log}, {monitor_log}")
        return 0
    except Exception:
        monitor_log.write_text(query_monitor(monitor_sock), errors="replace")
        dump_file(serial_log)
        dump_file(qemu_log)
        raise
    finally:
        qemu_proc.terminate()
        try:
            qemu_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            qemu_proc.kill()
            qemu_proc.wait()


if __name__ == "__main__":
    raise SystemExit(main())
