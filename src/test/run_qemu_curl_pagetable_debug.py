#!/usr/bin/env python3
"""Diagnostic tool: dump page table and memory around curl's TLS buffers.

This is NOT a pass/fail test. It starts QEMU, waits for the curl TLS
handshake, then queries the QEMU monitor for page-table and memory info
and writes everything to build/log/curl_pagetable_debug.log.
"""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 90


def _send_monitor_command(sock: socket.socket, command: str) -> str:
    """Send a command to the QEMU monitor and collect the response."""
    sock.sendall((command + "\n").encode("ascii"))
    time.sleep(0.3)
    chunks: list[bytes] = []
    while True:
        try:
            data = sock.recv(8192)
        except OSError:
            break
        if not data:
            break
        chunks.append(data)
        if len(data) < 8192:
            break
    return b"".join(chunks).decode(errors="replace")


def _query_monitor(sock_path: pathlib.Path, commands: list[str]) -> str:
    """Connect to QEMU monitor socket and run a list of commands."""
    if not sock_path.exists():
        return "(monitor socket not found)\n"

    output_parts: list[str] = []
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(2.0)
            sock.connect(str(sock_path))
            # consume the banner
            time.sleep(0.2)
            try:
                sock.recv(4096)
            except OSError:
                pass

            for cmd in commands:
                output_parts.append(f">>> {cmd}\n")
                result = _send_monitor_command(sock, cmd)
                output_parts.append(result)
                output_parts.append("\n")

            # quit gracefully
            try:
                sock.sendall(b"quit\n")
            except OSError:
                pass
    except OSError as exc:
        output_parts.append(f"monitor query failed: {exc}\n")

    return "".join(output_parts)


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: run_qemu_curl_pagetable_debug.py <fsboot> <logdir>",
            file=sys.stderr,
        )
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    serial_log = logdir / "curl_pt_debug_serial.log"
    qemu_log = logdir / "curl_pt_debug_qemu.log"
    debug_log = logdir / "curl_pagetable_debug.log"
    monitor_sock = logdir / "curl_pt_debug_monitor.sock"

    for path in (serial_log, qemu_log, debug_log, monitor_sock):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()

    qemu_args = [
        qemu_bin,
        "-drive", f"file={fsboot},format=raw,if=ide",
        "-m", str(qemu_memory_mb),
        "-nographic",
        "-no-reboot",
        "-monitor", f"unix:{monitor_sock},server,nowait",
        "-serial", f"file:{serial_log}",
        "-D", str(qemu_log),
        "-netdev", "user,id=net0",
        "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    print(f"Starting QEMU for page-table debug (timeout={timeout}s) ...")
    qemu_proc = subprocess.Popen(
        qemu_args,
        cwd=repo_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    timed_out = False
    handshake_seen = False
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if qemu_proc.poll() is not None:
                print("QEMU exited early.")
                break
            if serial_log.exists():
                text = serial_log.read_text(errors="replace")
                if "[TLS] handshake OK" in text:
                    handshake_seen = True
                    print("Detected TLS handshake OK — querying monitor ...")
                    # Give a brief moment for curl to proceed a bit
                    time.sleep(1.0)
                    break
            time.sleep(0.5)
        else:
            timed_out = True

        # --- Query the QEMU monitor ---
        monitor_commands = [
            "info registers",
            "info tlb",
            "xp /32bx 0x08075000",
            "xp /32bx 0x08079000",
            # Dump around the specific address range 0x08072000-0x0807A000
            "xp /8wx 0x08072000",
            "xp /8wx 0x08074000",
            "xp /8wx 0x08076000",
            "xp /8wx 0x08078000",
            "xp /8wx 0x0807A000",
        ]

        monitor_output = _query_monitor(monitor_sock, monitor_commands)

        # --- Collect serial log ---
        serial_text = ""
        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")

        # --- Write the combined debug log ---
        with debug_log.open("w") as f:
            f.write("=" * 72 + "\n")
            f.write("  CURL PAGE TABLE DEBUG DUMP\n")
            f.write(f"  Time: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"  Handshake seen: {handshake_seen}\n")
            f.write(f"  Timed out: {timed_out}\n")
            f.write("=" * 72 + "\n\n")

            f.write("-" * 40 + "\n")
            f.write("  QEMU MONITOR OUTPUT\n")
            f.write("-" * 40 + "\n")
            f.write(monitor_output)
            f.write("\n")

            f.write("-" * 40 + "\n")
            f.write("  SERIAL LOG (filtered TLS/curl lines)\n")
            f.write("-" * 40 + "\n")
            for line in serial_text.splitlines():
                if any(kw in line for kw in (
                    "TLS", "curl", "init_rc", "SERIAL", "NEWDATA",
                    "RECVAPP", "err=", "iobuf", "page", "brk",
                    "sbrk", "mmap", "alloc",
                )):
                    f.write(line + "\n")
            f.write("\n")

            f.write("-" * 40 + "\n")
            f.write("  FULL SERIAL LOG\n")
            f.write("-" * 40 + "\n")
            f.write(serial_text)

        print(f"\nDebug dump written to: {debug_log}")
        print(f"Serial log: {serial_log}")
        print(f"QEMU debug log: {qemu_log}")

        if timed_out:
            print(f"WARNING: QEMU timed out after {timeout}s (handshake not seen)")

    finally:
        qemu_proc.terminate()
        try:
            qemu_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            qemu_proc.kill()
            qemu_proc.wait()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
