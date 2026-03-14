#!/usr/bin/env python3
"""vi による保存導線を QEMU で smoke する。"""

from __future__ import annotations

import json
import os
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time
import hashlib

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
    row_stride = width * 3
    crop = bytearray()
    for row in range(y, y + crop_height):
        start = row * row_stride + x * 3
        crop.extend(pixels[start:start + crop_width * 3])
    return hashlib.sha256(crop).hexdigest() == reference["sha256"]


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
            ":": "shift-semicolon",
            ">": "shift-dot",
            "\n": "ret",
        }
        for ch in text:
            self.send_key(keymap.get(ch, ch))

    def close(self) -> None:
        self.sock.close()


def wait_for_path(path: pathlib.Path, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {path}")


def wait_for_metric(serial_log: pathlib.Path, point: str, timeout: float) -> str:
    deadline = time.time() + timeout
    marker = f"TERM_METRIC point={point}"
    while time.time() < deadline:
        if serial_log.exists():
            text = serial_log.read_text(errors="replace")
            for line in text.splitlines():
                if marker in line:
                    return line
        time.sleep(0.2)
    raise TimeoutError(f"metric not found: {point}")


def wait_for_prompt(monitor: QemuMonitor, ppm_path: pathlib.Path,
                    reference: dict[str, int | str], timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.3)
        if crop_matches(ppm_path, reference):
            return
        time.sleep(0.2)
    raise TimeoutError("prompt screenshot did not match reference")


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


def assert_vi_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()
    root_entries = read_dir_entries(image, SODEX_ROOT_INO)
    memo_entry = root_entries.get("memo.txt")
    shell_entry = root_entries.get("aftervi.txt")
    if memo_entry is None:
        raise AssertionError("memo.txt was not created")
    if shell_entry is None:
        raise AssertionError("aftervi.txt was not created after returning to shell")

    content = read_file(image, memo_entry[0]).decode("ascii", errors="ignore")
    if content != "first second\nfinal":
        raise AssertionError(f"memo.txt content mismatch: {content!r}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_vi_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = pathlib.Path(tempfile.gettempdir()) / f"sdx_v_{os.getpid()}.sock"
    serial_log = logdir / f"vi_serial_{os.getpid()}.log"
    qemu_log = logdir / f"vi_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"vi_prompt_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm):
        if path.exists():
            path.unlink()

    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_args = [
        qemu_bin,
        "-drive", f"file={fsboot},format=raw,if=ide",
        "-m", "128",
        "-display", "none",
        "-no-reboot",
        "-serial", f"file:{serial_log}",
        "-monitor", f"unix:{monitor_sock},server,nowait",
        "-D", str(qemu_log),
        "-device", "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
        "-netdev", "user,id=net0",
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
        wait_for_metric(serial_log, "full_redraw", timeout)
        wait_for_prompt(monitor, prompt_ppm, reference, timeout)

        monitor.send_text("vi memo.txt\n")
        time.sleep(1.0)
        monitor.send_text("ifirst second")
        time.sleep(0.5)
        monitor.send_key("esc")
        time.sleep(0.3)
        monitor.send_text("0dw")
        time.sleep(0.3)
        monitor.send_text("ifirst ")
        time.sleep(0.4)
        monitor.send_key("esc")
        time.sleep(0.3)
        monitor.send_text("exa")
        time.sleep(0.2)
        monitor.send_text("d")
        time.sleep(0.2)
        monitor.send_key("esc")
        time.sleep(0.3)
        monitor.send_text("ofinal")
        time.sleep(0.4)
        monitor.send_key("esc")
        time.sleep(0.3)
        monitor.send_text("ofiller")
        time.sleep(0.4)
        monitor.send_key("esc")
        time.sleep(0.3)
        monitor.send_text("ddgg")
        time.sleep(0.5)
        monitor.send_text(":wq\n")
        wait_for_prompt(monitor, prompt_ppm, reference, timeout)
        monitor.send_text("touch aftervi.txt\n")
        time.sleep(1.0)

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_vi_state(fsboot)

        print("=== VI QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}")
        return 0
    except Exception as exc:
        print(f"vi smoke failed: {exc}", file=sys.stderr)
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
