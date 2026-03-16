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
EXPECTED_CONFIG_ERRORS = int(os.environ.get("SODEX_EXPECT_CONFIG_ERRORS", "0") or "0")
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


def assert_status_code(head: str, expected: int, label: str) -> None:
    assert_contains(head, f"{expected} ", label)


def assert_retry_after(head: str, expected_seconds: int, label: str) -> None:
    assert_contains(head, f"Retry-After: {expected_seconds}", label)


def status_access_token() -> str | None:
    if STATUS_TOKEN:
        return STATUS_TOKEN
    if CONTROL_TOKEN:
        return CONTROL_TOKEN
    return None


def parse_ready_fields(serial_text: str) -> dict[str, str]:
    for line in reversed(serial_text.splitlines()):
        if READY_MARKER in line:
            fields: dict[str, str] = {}
            for field in line.split():
                if "=" not in field:
                    continue
                key, value = field.split("=", 1)
                fields[key] = value
            return fields
    raise AssertionError("ready marker line was not found")


def assert_ready_fields(serial_text: str) -> None:
    fields = parse_ready_fields(serial_text)
    expected_stok = "on" if STATUS_TOKEN else "off"
    expected_ctok = "on" if CONTROL_TOKEN else "off"

    if fields.get("stok") != expected_stok:
        raise AssertionError(f"ready stok mismatch: {fields!r}")
    if fields.get("ctok") != expected_ctok:
        raise AssertionError(f"ready ctok mismatch: {fields!r}")
    if int(fields.get("cfgerr", "-1")) != EXPECTED_CONFIG_ERRORS:
        raise AssertionError(f"ready cfgerr mismatch: {fields!r}")


def assert_http_status_behavior() -> None:
    token = status_access_token()

    if token is None:
        head, body = send_http("GET", "/status")
        assert_status_code(head, 403, "status forbidden head")
        assert_contains(body, "forbidden", "status forbidden body")
        return

    head, body = send_http("GET", "/status", token)
    assert_status_code(head, 200, "status head")
    assert_contains(body, '"agent":"stopped"', "status body")


def assert_http_control_behavior() -> None:
    token = status_access_token()

    if not CONTROL_TOKEN:
        head, body = send_http("POST", "/agent/start")
        assert_status_code(head, 403, "start forbidden head")
        assert_contains(body, "forbidden", "start forbidden body")
        return

    head, body = send_http("POST", "/agent/start", CONTROL_TOKEN)
    assert_status_code(head, 200, "start head")
    assert_contains(body, '"agent":"running"', "start body")

    if token is not None:
        head, body = send_http("GET", "/status", token)
        assert_status_code(head, 200, "status running head")
        assert_contains(body, '"agent":"running"', "status running body")

    head, body = send_http("POST", "/agent/stop", CONTROL_TOKEN)
    assert_status_code(head, 200, "stop head")
    assert_contains(body, '"agent":"stopped"', "stop body")


def assert_admin_status_behavior() -> None:
    token = status_access_token()

    admin = send_admin("PING\n")
    assert_contains(admin, "OK PONG", "admin ping")

    if token is None:
        admin = send_admin("STATUS\n")
        assert_contains(admin, "ERR unauthorized", "admin unauthorized")
        return

    admin = send_admin(f"TOKEN {token} STATUS\n")
    assert_contains(admin, "OK agent=stopped", "admin status")

    admin = send_admin("NOPE\n")
    assert_contains(admin, "ERR invalid_command", "admin invalid")

    admin = send_admin(f"TOKEN {token} LOG TAIL 32\n")
    assert_contains(admin, "server_runtime_ready", "admin ready log tail")


def assert_http_throttle_behavior() -> None:
    for _ in range(3):
        head, body = send_http("GET", "/status", "wrong-secret")
        assert_status_code(head, 403, "http pre-throttle head")
        assert_contains(body, "forbidden", "http pre-throttle body")

    head, body = send_http("GET", "/status", "wrong-secret")
    assert_status_code(head, 429, "http throttle head")
    assert_retry_after(head, 1, "http throttle retry-after")
    assert_contains(body, "throttled retry=1", "http throttle body")


def assert_admin_throttle_behavior() -> None:
    for _ in range(3):
        admin = send_admin("TOKEN wrong-secret STATUS\n")
        assert_contains(admin, "ERR unauthorized", "admin pre-throttle")

    admin = send_admin("TOKEN wrong-secret STATUS\n")
    assert_contains(admin, "ERR throttled retry=1", "admin throttle")


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
        assert_ready_fields(read_text(serial_log))

        head, body = send_http("GET", "/healthz")
        assert_status_code(head, 200, "health head")
        if body != "ok\n":
            raise AssertionError(f"health body mismatch: {body!r}")

        assert_http_status_behavior()
        assert_http_control_behavior()
        assert_admin_status_behavior()
        assert_http_throttle_behavior()
        time.sleep(1.1)

        head, body = send_http("GET", "/healthz")
        assert_status_code(head, 200, "health recovery head")
        if body != "ok\n":
            raise AssertionError(f"health recovery body mismatch: {body!r}")

        assert_admin_throttle_behavior()
        time.sleep(1.1)

        admin = send_admin("PING\n")
        assert_contains(admin, "OK PONG", "admin recovery ping")

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
