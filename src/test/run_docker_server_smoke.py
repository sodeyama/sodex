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

DEFAULT_TIMEOUT = 120
READY_MARKER = "AUDIT server_runtime_ready"
FAILURE_MARKERS = ("PF:", "PageFault", "General Protection Exception")
HOST_HTTP_PORT = int(os.environ.get("SODEX_HOST_HTTP_PORT", "18080"))
HOST_ADMIN_PORT = int(os.environ.get("SODEX_HOST_ADMIN_PORT", "10023"))
STATUS_TOKEN = os.environ.get("SODEX_ADMIN_STATUS_TOKEN", "status-secret")
CONTROL_TOKEN = os.environ.get("SODEX_ADMIN_CONTROL_TOKEN", "control-secret")


def dump_file(path: pathlib.Path) -> None:
    if not path.exists():
        return
    data = path.read_text(errors="replace")
    if data:
        print(data, end="")


def run_capture(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, text=True, capture_output=True, check=False)


def container_logs(container_name: str) -> str:
    result = run_capture("docker", "logs", container_name)
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


def assert_no_failure_markers(container_log: pathlib.Path,
                              guest_log_dir: pathlib.Path) -> None:
    texts = [container_log.read_text(errors="replace") if container_log.exists() else ""]
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
        container_log.write_text(logs, encoding="utf-8")
        assert_no_failure_markers(container_log, guest_log_dir)

        if READY_MARKER in logs:
            return

        status = container_status(container_name)
        if status not in ("created", "running"):
            raise AssertionError(f"container exited before ready: {status}")

        time.sleep(1.0)

    raise AssertionError("docker server runtime did not become ready in time")


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
                "SODEX_ADMIN_ALLOW_IP", "SODEX_QEMU_ACCEL", "SODEX_QEMU_MEM_MB"):
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

        head, body = send_http("POST", "/agent/stop", CONTROL_TOKEN)
        assert_contains(head, "200 OK", "stop head")
        assert_contains(body, '"agent":"stopped"', "stop body")

        admin = send_admin("PING\n")
        assert_contains(admin, "OK PONG", "admin ping")

        admin = send_admin(f"TOKEN {STATUS_TOKEN} STATUS\n")
        assert_contains(admin, "OK agent=stopped", "admin status")

        admin = send_admin(f"TOKEN {STATUS_TOKEN} LOG TAIL 8\n")
        assert_contains(admin, "server_runtime_ready", "admin log tail")

        logs = container_logs(container_name)
        container_log.write_text(logs, encoding="utf-8")
        assert_no_failure_markers(container_log, guest_log_dir)

        print("=== DOCKER SERVER RUNTIME SMOKE DONE ===")
        print(f"Artifacts: {container_log}, {guest_log_dir}")
        return 0
    finally:
        logs = container_logs(container_name)
        if logs:
            container_log.write_text(logs, encoding="utf-8")
        subprocess.run(
            ["docker", "rm", "-f", container_name],
            text=True,
            capture_output=True,
            check=False,
        )
        dump_file(container_log)


if __name__ == "__main__":
    raise SystemExit(main())
