#!/usr/bin/env python3
"""cat の複数ファイル連結を QEMU で smoke する。"""

from __future__ import annotations

import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2
DEFAULT_TIMEOUT = 45


class QemuMonitor:
    def __init__(self, sock_path: pathlib.Path) -> None:
        deadline = time.time() + 60.0

        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(1.0)
        while True:
            try:
                self.sock.connect(str(sock_path))
                break
            except OSError:
                if time.time() >= deadline:
                    raise
                time.sleep(0.1)
        time.sleep(0.2)
        try:
            self.sock.recv(4096)
        except OSError:
            pass

    def command(self, text: str, pause: float = 0.15) -> str:
        self.sock.sendall((text + "\n").encode("ascii"))
        time.sleep(pause)
        self.sock.settimeout(0.05)
        chunks: list[bytes] = []
        while True:
            try:
                chunk = self.sock.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            chunks.append(chunk)
            if len(chunk) < 4096:
                break
        self.sock.settimeout(1.0)
        return b"".join(chunks).decode(errors="replace")

    def close(self) -> None:
        self.sock.close()


def wait_for_path(path: pathlib.Path, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {path}")


def read_serial_text(serial_log: pathlib.Path) -> str:
    if not serial_log.exists():
        return ""
    return serial_log.read_text(errors="replace")


def wait_for_serial_marker(serial_log: pathlib.Path, marker: str,
                           timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if marker in read_serial_text(serial_log):
            return
        time.sleep(0.2)
    raise TimeoutError(f"serial marker not found: {marker}")


def inode_bytes(image: bytes, ino: int) -> bytes:
    start = P_INODE_BLOCK + (ino - 1) * INODE_SIZE
    return image[start:start + INODE_SIZE]


def inode_size(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 4)[0]


def inode_block0(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 40)[0]


def read_file(image: bytes, ino: int) -> bytes:
    size = inode_size(image, ino)
    block0 = inode_block0(image, ino)
    start = block0 * BLOCK_SIZE
    return image[start:start + size]


def read_dir_entries(image: bytes, ino: int) -> dict[str, tuple[int, int]]:
    block = inode_block0(image, ino)
    data = image[block * BLOCK_SIZE:(block + 1) * BLOCK_SIZE]
    offset = 0
    result: dict[str, tuple[int, int]] = {}
    while offset + 8 <= len(data):
        inode_num = struct.unpack_from("<I", data, offset)[0]
        rec_len = struct.unpack_from("<H", data, offset + 4)[0]
        name_len = data[offset + 6]
        file_type = data[offset + 7]
        if inode_num == 0 or rec_len == 0:
            break
        name = data[offset + 8:offset + 8 + name_len].decode("ascii", errors="ignore")
        result[name] = (inode_num, file_type)
        offset += rec_len
    return result


def read_user_file(fsboot: pathlib.Path, name: str) -> str:
    image = fsboot.read_bytes()
    root_entries = read_dir_entries(image, SODEX_ROOT_INO)
    home_entry = root_entries.get("home")
    if home_entry is None:
        raise AssertionError("/home was not found")
    home_entries = read_dir_entries(image, home_entry[0])
    user_entry = home_entries.get("user")
    if user_entry is None:
        raise AssertionError("/home/user was not found")
    user_entries = read_dir_entries(image, user_entry[0])
    file_entry = user_entries.get(name)
    if file_entry is None:
        raise AssertionError(f"{name} was not created")
    return read_file(image, file_entry[0]).decode("ascii", errors="replace")


def assert_cat_state(fsboot: pathlib.Path) -> None:
    combined_text = read_user_file(fsboot, "o")
    if combined_text != "a\nb\nc\n":
        raise AssertionError(f"o mismatch: {combined_text!r}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_cat_multifile_smoke.py <fsboot> <logdir>",
              file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = logdir / f"cat_multifile_monitor_{os.getpid()}.sock"
    serial_log = logdir / f"cat_multifile_serial_{os.getpid()}.log"
    qemu_log = logdir / f"cat_multifile_qemu_{os.getpid()}.log"
    smoke_fsboot = logdir / f"cat_multifile_fsboot_{os.getpid()}.bin"

    for path in (monitor_sock, serial_log, qemu_log, smoke_fsboot):
        if path.exists():
            path.unlink()

    shutil.copyfile(fsboot, smoke_fsboot)

    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_memory_mb = get_qemu_memory_mb()
    qemu_args = [
        qemu_bin,
        "-drive", f"file={smoke_fsboot},format=raw,if=ide",
        "-m", str(qemu_memory_mb),
        "-display", "none",
        "-no-reboot",
        "-serial", f"file:{serial_log}",
        "-monitor", f"unix:{monitor_sock},server,nowait",
        "-D", str(qemu_log),
        "-netdev", "user,id=net0",
        "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu = subprocess.Popen(
        qemu_args,
        cwd=repo_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    monitor: QemuMonitor | None = None
    try:
        wait_for_path(monitor_sock, 10)
        monitor = QemuMonitor(monitor_sock)
        wait_for_serial_marker(serial_log, "AUDIT cat_multifile_done", timeout)
        time.sleep(0.6)
        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_cat_state(smoke_fsboot)
        serial_text = read_serial_text(serial_log)
        if "PF:" in serial_text or "PageFault" in serial_text:
            raise AssertionError("page fault was detected during cat smoke")

        print("=== CAT MULTIFILE QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}")
        return 0
    except Exception as exc:
        print(f"cat multifile smoke failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if monitor is not None:
            monitor.close()
        if qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()


if __name__ == "__main__":
    raise SystemExit(main())
