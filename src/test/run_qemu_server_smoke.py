#!/usr/bin/env python3
"""server runtime の QEMU smoke を実行する。"""

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
STATUS_TOKEN = os.environ.get("SODEX_ADMIN_STATUS_TOKEN", "status-secret")
CONTROL_TOKEN = os.environ.get("SODEX_ADMIN_CONTROL_TOKEN", "control-secret")
READY_MARKER = "AUDIT server_runtime_ready"
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


def send_http(method: str, path: str, token: str | None = None) -> tuple[str, str]:
    headers = [
        f"{method} {path} HTTP/1.1",
        "Host: 127.0.0.1",
        "Connection: close",
    ]
    if token:
        headers.append(f"Authorization: Bearer {token}")
    request = ("\r\n".join(headers) + "\r\n\r\n").encode("ascii")

    with socket.create_connection(("127.0.0.1", HOST_HTTP_PORT), timeout=2.0) as sock:
        sock.sendall(request)
        chunks: list[bytes] = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)

    raw = b"".join(chunks).decode(errors="replace")
    head, _, body = raw.partition("\r\n\r\n")
    return head, body


def send_admin(line: str) -> str:
    with socket.create_connection(("127.0.0.1", HOST_ADMIN_PORT), timeout=2.0) as sock:
        sock.sendall(line.encode("ascii"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
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
        assert_no_guest_failure(serial_log, qemu_log)
        if READY_MARKER in read_text(serial_log):
            return
        time.sleep(0.5)
    raise AssertionError("server runtime did not become ready in time")


def assert_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: expected {needle!r} in {text!r}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_server_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    logdir.mkdir(parents=True, exist_ok=True)

    serial_log = logdir / "server_serial.log"
    qemu_log = logdir / "server_qemu_debug.log"
    monitor_log = logdir / "server_monitor.log"
    monitor_sock = logdir / "server_monitor.sock"

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
        f"hostfwd=tcp:127.0.0.1:{HOST_ADMIN_PORT}-10.0.2.15:10023",
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

        head, body = send_http("GET", "/healthz")
        assert_contains(head, "200 OK", "health head")
        if body != "ok\n":
            raise AssertionError(f"health body mismatch: {body!r}")

        head, body = send_http("GET", "/status", STATUS_TOKEN)
        assert_contains(head, "200 OK", "status head")
        assert_contains(body, '"agent":"stopped"', "status body")

        head, body = send_http("POST", "/agent/start", CONTROL_TOKEN)
        assert_contains(head, "200 OK", "start head")
        assert_contains(body, '"agent":"running"', "start body")

        head, body = send_http("GET", "/status", STATUS_TOKEN)
        assert_contains(body, '"agent":"running"', "status running body")

        head, body = send_http("POST", "/agent/stop", CONTROL_TOKEN)
        assert_contains(head, "200 OK", "stop head")
        assert_contains(body, '"agent":"stopped"', "stop body")

        admin = send_admin("PING\n")
        assert_contains(admin, "OK PONG", "admin ping")

        admin = send_admin("STATUS\n")
        assert_contains(admin, "ERR unauthorized", "admin unauthorized")

        admin = send_admin(f"TOKEN {STATUS_TOKEN} STATUS\n")
        assert_contains(admin, "OK agent=stopped", "admin status")

        admin = send_admin(f"TOKEN {CONTROL_TOKEN} AGENT START\n")
        assert_contains(admin, "OK agent=running", "admin start")

        admin = send_admin("NOPE\n")
        assert_contains(admin, "ERR invalid_command", "admin invalid")

        admin = send_admin(f"TOKEN {STATUS_TOKEN} LOG TAIL 32\n")
        assert_contains(admin, "agent_start", "admin log tail")
        assert_contains(admin, "server_runtime_ready", "admin ready log tail")

        assert_no_guest_failure(serial_log, qemu_log)

        print("=== SERVER RUNTIME SMOKE DONE ===")
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
