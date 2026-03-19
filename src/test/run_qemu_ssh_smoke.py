#!/usr/bin/env python3
"""SSH server の QEMU smoke を実行する。"""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time
from typing import TextIO

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 45
DEFAULT_EXPECT_TIMEOUT = 120
GUEST_SSH_PORT = int(os.environ.get("SODEX_SSH_PORT", "10022"))
SSH_PASSWORD = os.environ.get("SODEX_SSH_PASSWORD", "root-secret")
SSH_SIGNER_PORT = int(os.environ.get("SODEX_SSH_SIGNER_PORT", "0"))
SSH_EXPECT_TIMEOUT = int(
    os.environ.get("SODEX_SSH_EXPECT_TIMEOUT", str(DEFAULT_EXPECT_TIMEOUT))
)
READY_MARKER = f"AUDIT listener_ready kind=ssh port={GUEST_SSH_PORT}"
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


def normalize_output(data: str | bytes | None) -> str:
    if data is None:
        return ""
    if isinstance(data, bytes):
        return data.decode(errors="replace")
    return data


def reserve_host_port() -> tuple[int, socket.socket | None]:
    raw = os.environ.get("SODEX_HOST_SSH_PORT", "").strip()
    if raw not in ("", "0"):
        return int(raw), None

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sock.listen(1)
    return int(sock.getsockname()[1]), sock


def read_overlay_secret(logdir: pathlib.Path) -> str:
    config_path = logdir / "ssh-rootfs-overlay" / "etc" / "sodex-admin.conf"

    if not config_path.exists():
        return ""

    for line in config_path.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.startswith("ssh_hostkey_ed25519_secret="):
            return line.split("=", 1)[1].strip()
    return ""


def ensure_ssh_signer(repo_root: pathlib.Path) -> pathlib.Path:
    signer_path = repo_root / "tests" / "ssh_signer"

    if signer_path.exists():
        return signer_path

    subprocess.run(
        ["make", "-C", str(repo_root / "tests"), "ssh_signer"],
        check=True,
    )
    return signer_path


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


def assert_qemu_running(qemu_proc: subprocess.Popen[bytes],
                        qemu_stderr_log: pathlib.Path) -> None:
    if qemu_proc.poll() is None:
        return

    stderr_text = read_text(qemu_stderr_log)
    raise AssertionError(
        f"qemu exited early with code {qemu_proc.returncode}:\n{stderr_text}"
    )


def wait_until_ready(deadline: float, serial_log: pathlib.Path,
                     qemu_log: pathlib.Path,
                     qemu_proc: subprocess.Popen[bytes],
                     qemu_stderr_log: pathlib.Path) -> None:
    while time.time() < deadline:
        assert_qemu_running(qemu_proc, qemu_stderr_log)
        assert_no_guest_failure(serial_log, qemu_log)
        if READY_MARKER in read_text(serial_log):
            return
        time.sleep(0.5)
    raise AssertionError("ssh server did not become ready in time")


def wait_for_ssh_banner(deadline: float, host_ssh_port: int) -> None:
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", host_ssh_port), timeout=1.0) as sock:
                sock.settimeout(1.0)
                data = sock.recv(128)
        except OSError:
            time.sleep(0.5)
            continue

        if data.startswith(b"SSH-2.0-"):
            return
        time.sleep(0.5)

    raise AssertionError("ssh banner did not become reachable in time")


def run_expect(script: str, timeout: int = 20) -> str:
    try:
        proc = subprocess.run(
            ["script", "-q", "/dev/null", "expect", "-c", script],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        output = (normalize_output(exc.stdout) + normalize_output(exc.stderr)).replace(
            "\x08", ""
        ).replace("\x04", "")
        raise AssertionError(f"expect timed out({timeout}s):\n{output}") from exc
    output = (proc.stdout + proc.stderr).replace("\x08", "").replace("\x04", "")
    if proc.returncode != 0:
        raise AssertionError(f"expect failed({proc.returncode}):\n{output}")
    return output

def ssh_success_session(host_ssh_port: int, password: str) -> str:
    script = f"""
set timeout 30
log_user 1
spawn ssh -vv -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "{password}\\r"
expect "shell request accepted on channel 0"
expect -re {{sodex .*> }}
send "exit\\r"
expect {{
  eof {{ }}
  timeout {{ exit 124 }}
}}
puts "SSH_SESSION_OK"
exit 0
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def ssh_wrong_password(host_ssh_port: int) -> str:
    script = f"""
set timeout {SSH_EXPECT_TIMEOUT}
log_user 1
spawn ssh -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o NumberOfPasswordPrompts=1 -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "wrong-secret\\r"
expect {{
  "Permission denied" {{ puts "SSH_BADPASS_OK" }}
  eof {{ puts "SSH_BADPASS_OK" }}
  timeout {{ exit 124 }}
}}
expect eof
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def ssh_tab_completion_session(host_ssh_port: int, password: str) -> str:
    script = f"""
set timeout {SSH_EXPECT_TIMEOUT}
log_user 1
spawn ssh -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "{password}\\r"
expect -re {{sodex .*> }}
send "echo SSH_TAB_OK > ssh_unique_target.txt\\r"
expect -re {{sodex .*> }}
send "cat ssh_uni\\t\\r"
expect {{
  "SSH_TAB_OK" {{ }}
  "cat: open failed ssh_uni" {{ exit 125 }}
  timeout {{ exit 124 }}
}}
expect -re {{sodex .*> }}
send "rm ssh_unique_target.txt\\r"
expect -re {{sodex .*> }}
send "exit\\r"
expect eof
puts "SSH_TAB_COMPLETION_OK"
exit 0
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def assert_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: expected {needle!r} in {text!r}")


def parse_metric_fields(line: str) -> dict[str, str]:
    result: dict[str, str] = {}

    for part in line.strip().split():
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        result[key] = value
    return result


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_ssh_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    logdir.mkdir(parents=True, exist_ok=True)

    serial_log = logdir / "ssh_serial.log"
    qemu_log = logdir / "ssh_qemu_debug.log"
    monitor_log = logdir / "ssh_monitor.log"
    monitor_sock = logdir / "ssh_monitor.sock"
    qemu_stderr_log = logdir / "ssh_qemu_stderr.log"
    signer_stdout_log = logdir / "ssh_signer_stdout.log"
    signer_stderr_log = logdir / "ssh_signer_stderr.log"

    for path in (
        serial_log,
        qemu_log,
        monitor_log,
        monitor_sock,
        qemu_stderr_log,
        signer_stdout_log,
        signer_stderr_log,
    ):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()
    host_ssh_port, reserved_sock = reserve_host_port()

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
        f"hostfwd=tcp:127.0.0.1:{host_ssh_port}-10.0.2.15:{GUEST_SSH_PORT}",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_cmd = ["script", "-q", "/dev/null", qemu_bin] + qemu_args[1:]

    qemu_proc = None
    signer_proc = None
    qemu_stderr_fp: TextIO | None = None
    signer_stdout_fp: TextIO | None = None
    signer_stderr_fp: TextIO | None = None

    try:
        if SSH_SIGNER_PORT > 0:
            signer_secret = read_overlay_secret(logdir)
            if not signer_secret:
                raise AssertionError("ssh signer secret is missing in overlay config")
            signer_path = ensure_ssh_signer(repo_root)
            signer_stdout_fp = signer_stdout_log.open("w", encoding="utf-8")
            signer_stderr_fp = signer_stderr_log.open("w", encoding="utf-8")
            signer_proc = subprocess.Popen(
                [str(signer_path), str(SSH_SIGNER_PORT), signer_secret],
                stdout=signer_stdout_fp,
                stderr=signer_stderr_fp,
                text=True,
            )
            time.sleep(0.3)
            if signer_proc.poll() is not None:
                raise AssertionError(
                    f"ssh signer exited early with code {signer_proc.returncode}"
                )

        qemu_stderr_fp = qemu_stderr_log.open("w", encoding="utf-8")
        if reserved_sock is not None:
            reserved_sock.close()
            reserved_sock = None
        qemu_proc = subprocess.Popen(
            qemu_cmd,
            stdout=subprocess.DEVNULL,
            stderr=qemu_stderr_fp,
        )
        deadline = time.time() + timeout
        wait_until_ready(deadline, serial_log, qemu_log, qemu_proc, qemu_stderr_log)
        output = ssh_success_session(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "SSH_SESSION_OK", "ssh success")
        output = ssh_tab_completion_session(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "SSH_TAB_COMPLETION_OK", "ssh tab completion")
        output = ssh_wrong_password(host_ssh_port)
        assert_contains(output, "SSH_BADPASS_OK", "ssh bad password")

        output = ssh_success_session(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "SSH_SESSION_OK", "ssh reconnect")

        serial_text = read_text(serial_log)
        if serial_text.count("ssh_auth_success") < 2:
            raise AssertionError("serial log missing ssh_auth_success audit")
        if serial_text.count("ssh_session_start") < 2:
            raise AssertionError("serial log missing ssh_session_start audit")
        if "ssh_auth_failure" not in serial_text:
            raise AssertionError("serial log missing ssh_auth_failure audit")
        if "reason=protocol_error" in serial_text:
            raise AssertionError("serial log contains protocol_error close")
        ssh_metric_lines = [
            line for line in serial_text.splitlines()
            if "SSH_METRIC " in line
        ]
        if len(ssh_metric_lines) < 2:
            raise AssertionError("serial log missing SSH_METRIC lines")
        for line in ssh_metric_lines:
            fields = parse_metric_fields(line)
            for key in (
                "pty_read_bytes",
                "channel_data_avg_len",
                "packets_per_frame",
                "ticks_per_frame",
            ):
                if key not in fields or not fields[key].isdigit():
                    raise AssertionError(f"invalid ssh metric line: {line!r}")
        if not any(
            int(parse_metric_fields(line)["pty_read_bytes"]) > 0
            for line in ssh_metric_lines
        ):
            raise AssertionError("serial log missing non-empty SSH_METRIC reads")

        assert_no_guest_failure(serial_log, qemu_log)
        print("=== SSH SMOKE DONE ===")
        print(f"SSH host port: {host_ssh_port}")
        dump_file(serial_log)
        print(f"Logs: {serial_log}, {qemu_log}, {monitor_log}, {qemu_stderr_log}")
        return 0
    except Exception:
        monitor_log.write_text(query_monitor(monitor_sock), errors="replace")
        dump_file(serial_log)
        dump_file(qemu_log)
        dump_file(qemu_stderr_log)
        dump_file(signer_stdout_log)
        dump_file(signer_stderr_log)
        raise
    finally:
        if reserved_sock is not None:
            reserved_sock.close()
        if qemu_proc is not None:
            qemu_proc.terminate()
            try:
                qemu_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu_proc.kill()
                qemu_proc.wait()
        if qemu_stderr_fp is not None:
            qemu_stderr_fp.close()
        if signer_proc is not None:
            signer_proc.terminate()
            try:
                signer_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                signer_proc.kill()
                signer_proc.wait()
        if signer_stdout_fp is not None:
            signer_stdout_fp.close()
        if signer_stderr_fp is not None:
            signer_stderr_fp.close()


if __name__ == "__main__":
    raise SystemExit(main())
