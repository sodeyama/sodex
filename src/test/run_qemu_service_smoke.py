#!/usr/bin/env python3
"""service sshd の status/restart を QEMU で確認する。"""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time
from typing import TextIO

from qemu_config import get_qemu_memory_mb
from run_qemu_ssh_smoke import (
    SSH_EXPECT_TIMEOUT,
    SSH_PASSWORD,
    assert_contains,
    assert_no_guest_failure,
    assert_qemu_running,
    dump_file,
    read_text,
    reserve_host_port,
    run_expect,
    wait_for_ssh_banner,
    wait_until_ready,
)

DEFAULT_TIMEOUT = 45


def ssh_status_session(host_ssh_port: int, password: str) -> str:
    script = f"""
set timeout {SSH_EXPECT_TIMEOUT}
log_user 1
spawn ssh -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "{password}\\r"
expect -re {{sodex .*> }}
send "service sshd status\\r"
expect -re {{sodex .*> }}
send "echo $?\\r"
expect -re {{\\n0\\r?\\n}}
expect -re {{sodex .*> }}
send "ps\\r"
expect "sshd"
expect -re {{sodex .*> }}
send "exit\\r"
expect eof
puts "SERVICE_STATUS_OK"
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def ssh_restart_session(host_ssh_port: int, password: str) -> str:
    script = f"""
set timeout {SSH_EXPECT_TIMEOUT}
log_user 1
spawn ssh -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "{password}\\r"
expect -re {{sodex .*> }}
send "service sshd restart\\r"
expect {{
  eof {{ puts "SERVICE_RESTART_OK" }}
  timeout {{ exit 124 }}
}}
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def ssh_reconnect_status(host_ssh_port: int, password: str) -> str:
    script = f"""
set timeout {SSH_EXPECT_TIMEOUT}
log_user 1
spawn ssh -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "{password}\\r"
expect -re {{sodex .*> }}
send "service sshd status\\r"
expect -re {{sodex .*> }}
send "echo $?\\r"
expect -re {{\\n0\\r?\\n}}
expect -re {{sodex .*> }}
send "exit\\r"
expect eof
puts "SERVICE_RECONNECT_OK"
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_service_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    logdir.mkdir(parents=True, exist_ok=True)

    serial_log = logdir / "service_serial.log"
    qemu_log = logdir / "service_qemu_debug.log"
    monitor_sock = logdir / "service_monitor.sock"
    qemu_stderr_log = logdir / "service_qemu_stderr.log"

    for path in (serial_log, qemu_log, monitor_sock, qemu_stderr_log):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()
    host_ssh_port, reserved_sock = reserve_host_port()

    qemu_cmd = [
        "script",
        "-q",
        "/dev/null",
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
        f"hostfwd=tcp:127.0.0.1:{host_ssh_port}-10.0.2.15:{os.environ.get('SODEX_SSH_PORT', '10022')}",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_proc = None
    qemu_stderr_fp: TextIO | None = None
    try:
        if reserved_sock is not None:
            reserved_sock.close()
            reserved_sock = None
        qemu_stderr_fp = qemu_stderr_log.open("w", encoding="utf-8")
        qemu_proc = subprocess.Popen(
            qemu_cmd,
            stdout=subprocess.DEVNULL,
            stderr=qemu_stderr_fp,
        )
        deadline = time.time() + timeout
        wait_until_ready(deadline, serial_log, qemu_log, qemu_proc, qemu_stderr_log)
        wait_for_ssh_banner(deadline, host_ssh_port)

        output = ssh_status_session(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "SERVICE_STATUS_OK", "service status")

        output = ssh_restart_session(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "SERVICE_RESTART_OK", "service restart")

        deadline = time.time() + timeout
        wait_for_ssh_banner(deadline, host_ssh_port)
        output = ssh_reconnect_status(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "SERVICE_RECONNECT_OK", "service reconnect")

        serial_text = read_text(serial_log)
        if "AUDIT sshd_service restart" not in serial_text:
            raise AssertionError("serial log missing restart audit")
        assert_no_guest_failure(serial_log, qemu_log)
        print("=== SERVICE SMOKE DONE ===")
        dump_file(serial_log)
        return 0
    except Exception:
        dump_file(serial_log)
        dump_file(qemu_log)
        dump_file(qemu_stderr_log)
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


if __name__ == "__main__":
    raise SystemExit(main())
