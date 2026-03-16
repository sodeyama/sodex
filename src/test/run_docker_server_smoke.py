#!/usr/bin/env python3
"""Docker 上の headless server runtime smoke を実行する。"""

from __future__ import annotations

import os
import pathlib
import shutil
import socket
import subprocess
import sys
import time

DEFAULT_TIMEOUT = 300
READY_MARKER = "AUDIT server_runtime_ready"
FAILURE_MARKERS = ("PF:", "PageFault", "General Protection Exception")
HOST_HTTP_PORT = int(os.environ.get("SODEX_HOST_HTTP_PORT", "18080"))
HOST_ADMIN_PORT = int(os.environ.get("SODEX_HOST_ADMIN_PORT", "10023"))
STATUS_TOKEN = os.environ.get("SODEX_ADMIN_STATUS_TOKEN", "status-secret")
CONTROL_TOKEN = os.environ.get("SODEX_ADMIN_CONTROL_TOKEN", "control-secret")
EXPECTED_CONFIG_ERRORS = int(os.environ.get("SODEX_EXPECT_CONFIG_ERRORS", "0") or "0")


def dump_file(path: pathlib.Path) -> None:
    if not path.exists():
        return
    data = path.read_text(errors="replace")
    if data:
        print(data, end="")


def write_text(path: pathlib.Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def run_capture(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, text=True, capture_output=True, check=False)


def container_logs(container_name: str) -> str:
    result = run_capture("docker", "logs", container_name)
    if result.returncode != 0:
        return ""
    return (result.stdout or "") + (result.stderr or "")


def container_status(container_name: str) -> str:
    result = run_capture(
        "docker",
        "inspect",
        "-f",
        "{{.State.Status}}",
        container_name,
    )
    if result.returncode != 0:
        return "missing"
    return result.stdout.strip()


def load_ready_log(container_log: pathlib.Path, guest_log_dir: pathlib.Path) -> str:
    serial_log = guest_log_dir / "serial.log"
    if serial_log.exists():
        serial_text = serial_log.read_text(errors="replace")
        if READY_MARKER in serial_text:
            return serial_text

    if container_log.exists():
        return container_log.read_text(errors="replace")

    return ""


def assert_no_failure_markers(container_log: pathlib.Path,
                              guest_log_dir: pathlib.Path) -> None:
    texts = [container_log.read_text(errors="replace") if container_log.exists() else ""]
    serial_log = guest_log_dir / "serial.log"
    if serial_log.exists():
        texts.append(serial_log.read_text(errors="replace"))
    qemu_log = guest_log_dir / "qemu_debug.log"
    if qemu_log.exists():
        texts.append(qemu_log.read_text(errors="replace"))

    for marker in FAILURE_MARKERS:
        for text in texts:
            if marker in text:
                raise AssertionError(f"failure marker detected: {marker}")


def wait_until_ready(container_name: str, deadline: float,
                     container_log: pathlib.Path,
                     guest_log_dir: pathlib.Path) -> None:
    while time.time() < deadline:
        logs = container_logs(container_name)
        write_text(container_log, logs)
        assert_no_failure_markers(container_log, guest_log_dir)

        if READY_MARKER in logs:
            return

        serial_log = guest_log_dir / "serial.log"
        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")
            if READY_MARKER in serial_text:
                return

        if logs and "=== headless server mode with hostfwd ===" in logs:
            try:
                head, body = send_http("GET", "/healthz")
                if "200 " in head and body == "ok\n":
                    return
            except OSError:
                pass

            try:
                if "OK PONG" in send_admin("PING\n"):
                    return
            except OSError:
                pass

        status = container_status(container_name)
        if status not in ("created", "running"):
            raise AssertionError(f"container exited before ready: {status}")

        time.sleep(1.0)

    raise AssertionError("docker server runtime did not become ready in time")


def assert_ready_state(container_log: pathlib.Path, guest_log_dir: pathlib.Path) -> None:
    logs = load_ready_log(container_log, guest_log_dir)
    if READY_MARKER not in logs:
        token = status_access_token()
        if token is not None:
            logs = send_admin(f"TOKEN {token} LOG TAIL 8\n")
        else:
            return
    assert_ready_fields(logs)


def send_http(method: str, path: str, token: str | None = None) -> tuple[str, str]:
    headers = [
        f"{method} {path} HTTP/1.1",
        "Host: 127.0.0.1",
        "Connection: close",
    ]
    if token:
        headers.append(f"Authorization: Bearer {token}")
    request = ("\r\n".join(headers) + "\r\n\r\n").encode("ascii")

    with socket.create_connection(("127.0.0.1", HOST_HTTP_PORT), timeout=5.0) as sock:
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
    with socket.create_connection(("127.0.0.1", HOST_ADMIN_PORT), timeout=5.0) as sock:
        sock.sendall(line.encode("ascii"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    return b"".join(chunks).decode(errors="replace")


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


def parse_ready_fields(logs: str) -> dict[str, str]:
    for line in reversed(logs.splitlines()):
        if READY_MARKER in line:
            fields: dict[str, str] = {}
            for field in line.split():
                if "=" not in field:
                    continue
                key, value = field.split("=", 1)
                fields[key] = value
            return fields
    raise AssertionError("ready marker line was not found")


def assert_ready_fields(logs: str) -> None:
    fields = parse_ready_fields(logs)
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

    admin = send_admin(f"TOKEN {token} LOG TAIL 8\n")
    assert_contains(admin, "server_runtime_ready", "admin log tail")


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
        print("usage: run_docker_server_smoke.py <image> <logdir>", file=sys.stderr)
        return 2

    image = sys.argv[1]
    logdir = pathlib.Path(sys.argv[2]).resolve()
    guest_log_dir = logdir / "docker-guest-log"
    container_log = logdir / "docker-container.log"
    container_name = f"sodex-server-runtime-smoke-{os.getpid()}"
    timeout = int(os.environ.get("SODEX_DOCKER_TIMEOUT", DEFAULT_TIMEOUT))

    if guest_log_dir.exists():
        shutil.rmtree(guest_log_dir)
    guest_log_dir.mkdir(parents=True, exist_ok=True)
    if container_log.exists():
        container_log.unlink()

    docker_args = [
        "docker",
        "run",
        "--rm",
        "-d",
        "--name",
        container_name,
        "-p",
        f"{HOST_HTTP_PORT}:18080",
        "-p",
        f"{HOST_ADMIN_PORT}:10023",
        "-v",
        f"{guest_log_dir}:/var/log/sodex",
    ]

    for key in ("SODEX_ADMIN_STATUS_TOKEN", "SODEX_ADMIN_CONTROL_TOKEN",
                "SODEX_ADMIN_ALLOW_IP", "SODEX_ADMIN_CONFIG_EXTRA",
                "SODEX_QEMU_ACCEL", "SODEX_QEMU_MEM_MB"):
        value = os.environ.get(key)
        if value:
            docker_args.extend(["-e", f"{key}={value}"])

    docker_args.append(image)

    start = subprocess.run(docker_args, text=True, capture_output=True, check=False)
    if start.returncode != 0:
        sys.stdout.write(start.stdout)
        sys.stderr.write(start.stderr)
        return start.returncode

    try:
        deadline = time.time() + timeout
        wait_until_ready(container_name, deadline, container_log, guest_log_dir)
        logs = container_logs(container_name)
        write_text(container_log, logs)
        assert_ready_state(container_log, guest_log_dir)

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

        logs = container_logs(container_name)
        write_text(container_log, logs)
        assert_no_failure_markers(container_log, guest_log_dir)

        print("=== DOCKER SERVER RUNTIME SMOKE DONE ===")
        print(f"Artifacts: {container_log}, {guest_log_dir}")
        return 0
    finally:
        logs = container_logs(container_name)
        if logs:
            write_text(container_log, logs)
        subprocess.run(
            ["docker", "rm", "-f", container_name],
            text=True,
            capture_output=True,
            check=False,
        )
        dump_file(container_log)


if __name__ == "__main__":
    raise SystemExit(main())
