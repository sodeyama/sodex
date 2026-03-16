#!/usr/bin/env python3
"""job control の最小動作を QEMU で確認する。"""

from __future__ import annotations

import os
import pathlib
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
    dump_file,
    read_text,
    reserve_host_port,
    run_expect,
    wait_for_ssh_banner,
    wait_until_ready,
)

DEFAULT_TIMEOUT = 45


def ssh_job_control_session(host_ssh_port: int, password: str) -> str:
    script = f"""
set timeout {SSH_EXPECT_TIMEOUT}
log_user 1
spawn ssh -tt -F /dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p {host_ssh_port} root@127.0.0.1
expect -re {{[Pp]assword:}}
send "{password}\\r"
expect -re {{sodex .*> }}
send "sleep 2 &\\r"
expect -re {{\\[[0-9]+\\] [0-9]+}}
expect -re {{sodex .*> }}
send "jobs\\r"
expect -re {{\\[[0-9]+\\] [0-9]+ sleep 2}}
expect -re {{sodex .*> }}
send "bg %1\\r"
expect -re {{sodex .*> }}
send "echo $?\\r"
expect -re {{\\n0\\r?\\n}}
expect -re {{sodex .*> }}
send "fg %1\\r"
expect -re {{sodex .*> }}
send "echo $?\\r"
expect -re {{\\n0\\r?\\n}}
expect -re {{sodex .*> }}
send "jobs\\r"
expect {{
  -re {{\\[[0-9]+\\] [0-9]+ sleep 2}} {{ exit 125 }}
  -re {{sodex .*> }} {{ puts "JOB_CONTROL_OK" }}
  timeout {{ exit 124 }}
}}
send "exit\\r"
expect eof
"""
    return run_expect(script, timeout=SSH_EXPECT_TIMEOUT)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_job_control_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    logdir.mkdir(parents=True, exist_ok=True)

    serial_log = logdir / "job_control_serial.log"
    qemu_log = logdir / "job_control_qemu_debug.log"
    qemu_stderr_log = logdir / "job_control_qemu_stderr.log"

    for path in (serial_log, qemu_log, qemu_stderr_log):
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
        wait_until_ready(time.time() + timeout,
                         serial_log, qemu_log, qemu_proc, qemu_stderr_log)
        wait_for_ssh_banner(time.time() + timeout, host_ssh_port)
        output = ssh_job_control_session(host_ssh_port, SSH_PASSWORD)
        assert_contains(output, "JOB_CONTROL_OK", "job control")
        assert_no_guest_failure(serial_log, qemu_log)
        print("=== JOB CONTROL SMOKE DONE ===")
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
