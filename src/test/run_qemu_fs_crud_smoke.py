#!/usr/bin/env python3
"""fs CRUD の QEMU smoke を行い、fsboot を直接検査する。"""

from __future__ import annotations

import json
import os
import pathlib
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


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    index = 0

    def next_token() -> bytes:
      nonlocal index
      while index < len(data) and chr(data[index]).isspace():
          index += 1
      if data[index:index + 1] == b"#":
          while index < len(data) and data[index] != 0x0A:
              index += 1
          return next_token()
      start = index
      while index < len(data) and not chr(data[index]).isspace():
          index += 1
      return data[start:index]

    magic = next_token()
    if magic != b"P6":
        raise ValueError(f"unsupported PPM: {magic!r}")
    width = int(next_token())
    height = int(next_token())
    max_value = int(next_token())
    if max_value != 255:
        raise ValueError(f"unsupported max value: {max_value}")
    while index < len(data) and chr(data[index]).isspace():
        index += 1
    return width, height, data[index:]


def crop_matches(ppm_path: pathlib.Path, reference: dict[str, int | str]) -> bool:
    width, _height, pixels = read_ppm(ppm_path)
    x = int(reference["x"])
    y = int(reference["y"])
    crop_width = int(reference["width"])
    crop_height = int(reference["height"])
    expected = reference["sha256"]
    row_stride = width * 3
    import hashlib

    crop = bytearray()
    for row in range(y, y + crop_height):
        start = row * row_stride + x * 3
        crop.extend(pixels[start:start + crop_width * 3])
    return hashlib.sha256(crop).hexdigest() == expected


class QemuMonitor:
    def __init__(self, sock_path: pathlib.Path) -> None:
        deadline = time.time() + 15.0

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
        return b"".join(chunks).decode(errors="replace")

    def send_key(self, key: str, delay: float = 0.04) -> None:
        self.command(f"sendkey {key}", pause=0.05)
        time.sleep(delay)

    def send_text(self, text: str) -> None:
        keymap = {
            " ": "spc",
            "/": "slash",
            ".": "dot",
            "-": "minus",
            "\n": "ret",
        }
        for ch in text:
            key = keymap.get(ch, ch)
            self.send_key(key)

    def close(self) -> None:
        self.sock.close()


def wait_for_path(path: pathlib.Path, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {path}")


def wait_for_prompt(monitor: QemuMonitor, ppm_path: pathlib.Path,
                    reference: dict[str, int | str], timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.3)
        if crop_matches(ppm_path, reference):
            return
        time.sleep(0.2)
    raise TimeoutError("prompt screenshot did not match reference")


def wait_for_terminal_ready(monitor: QemuMonitor, ppm_path: pathlib.Path,
                            reference: dict[str, int | str],
                            serial_log: pathlib.Path, timeout: float) -> None:
    deadline = time.time() + timeout
    serial_ready_at: float | None = None

    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.3)
        if crop_matches(ppm_path, reference):
            return

        if serial_log.exists():
            serial_text = serial_log.read_text(errors="ignore")
            if "AUDIT eshell_ready" in serial_text:
                if serial_ready_at is None:
                    serial_ready_at = time.time()
                elif time.time() - serial_ready_at >= 1.0:
                    return

        time.sleep(0.2)

    raise TimeoutError("terminal ready was not observed")


def inode_bytes(image: bytes, ino: int) -> bytes:
    start = P_INODE_BLOCK + (ino - 1) * INODE_SIZE
    return image[start:start + INODE_SIZE]


def inode_size(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 4)[0]


def inode_block0(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 40)[0]


def read_dir_entries(image: bytes, ino: int) -> dict[str, tuple[int, int]]:
    block = inode_block0(image, ino)
    if block == 0:
        return {}
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


def lookup_path(image: bytes, path: str) -> tuple[int, int] | None:
    current = SODEX_ROOT_INO
    if path == "/":
        return current, 2
    for part in [p for p in path.split("/") if p]:
        entries = read_dir_entries(image, current)
        if part not in entries:
            return None
        current, file_type = entries[part]
    return current, file_type


def assert_fs_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()

    keep = lookup_path(image, "/keep")
    if keep is None or keep[1] != 2:
        raise AssertionError("/keep was not created")
    if lookup_path(image, "/gone") is not None:
        raise AssertionError("/gone should have been removed")
    if lookup_path(image, "/keep/a.txt") is not None:
        raise AssertionError("/keep/a.txt should have been renamed")

    note = lookup_path(image, "/keep/b.txt")
    if note is None or note[1] != 1:
        raise AssertionError("/keep/b.txt was not created")
    if inode_size(image, note[0]) != 0:
        raise AssertionError("/keep/b.txt is not empty")

    cd_abs = lookup_path(image, "/etc/cd-abs-smoke.txt")
    if cd_abs is None or cd_abs[1] != 1:
        raise AssertionError("/etc/cd-abs-smoke.txt was not created")
    if lookup_path(image, "/keep/etc/cd-abs-smoke.txt") is not None:
        raise AssertionError("absolute cd resolved to /keep/etc")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_fs_crud_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = logdir / f"fs_crud_monitor_{os.getpid()}.sock"
    serial_log = logdir / f"fs_crud_serial_{os.getpid()}.log"
    qemu_log = logdir / f"fs_crud_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"fs_crud_prompt_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm):
        if path.exists():
            path.unlink()

    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_memory_mb = get_qemu_memory_mb()
    qemu_args = [
        qemu_bin,
        "-drive", f"file={fsboot},format=raw,if=ide",
        "-m", str(qemu_memory_mb),
        "-display", "none",
        "-no-reboot",
        "-serial", f"file:{serial_log}",
        "-monitor", f"unix:{monitor_sock},server,nowait",
        "-D", str(qemu_log),
        "-netdev", "user,id=net0",
        "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    commands = [
        "mkdir keep\n",
        "mkdir keep/etc\n",
        "touch keep/a.txt\n",
        "mv keep/a.txt keep/b.txt\n",
        "mkdir gone\n",
        "touch gone/tmp.txt\n",
        "rm gone/tmp.txt\n",
        "rmdir gone\n",
        "cd keep\n",
        "cd /etc\n",
        "touch cd-abs-smoke.txt\n",
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
        wait_for_terminal_ready(monitor, prompt_ppm, reference, serial_log,
                                timeout)

        for command in commands:
            monitor.send_text(command)
            time.sleep(0.8)

        time.sleep(1.0)
        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_fs_state(fsboot)

        print("=== FS CRUD QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {prompt_ppm}")
        return 0
    except Exception as exc:
        print(f"fs CRUD smoke failed: {exc}", file=sys.stderr)
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
