#!/usr/bin/env python3
"""ext3 large file の QEMU smoke を行う。"""

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
FIXTURE_BLOCK_COUNT = 1280
FIXTURE_NAME = "largefile.bin"
FIXTURE_REPORT = "/large_report.txt"


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
        for ch in text:
            self.send_key(qemu_key_for_char(ch))

    def close(self) -> None:
        self.sock.close()


def qemu_key_for_char(ch: str) -> str:
    keymap = {
        " ": "spc",
        "\n": "ret",
        "/": "slash",
        ".": "dot",
        ",": "comma",
        "-": "minus",
        "_": "shift-minus",
        ":": "shift-semicolon",
    }
    if ch in keymap:
        return keymap[ch]
    if "A" <= ch <= "Z":
        return f"shift-{ch.lower()}"
    return ch


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
    file_type = 2
    if path == "/":
        return current, file_type
    for part in [p for p in path.split("/") if p]:
        entries = read_dir_entries(image, current)
        if part not in entries:
            return None
        current, file_type = entries[part]
    return current, file_type


def read_small_file(image: bytes, path: str) -> str:
    entry = lookup_path(image, path)
    if entry is None:
        raise AssertionError(f"{path} was not created")
    ino, file_type = entry
    if file_type != 1:
        raise AssertionError(f"{path} is not a regular file")
    size = inode_size(image, ino)
    block0 = inode_block0(image, ino)
    data = image[block0 * BLOCK_SIZE:block0 * BLOCK_SIZE + size]
    return data.decode("ascii", errors="replace")


def make_fixture(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as fp:
        for block_index in range(FIXTURE_BLOCK_COUNT):
            block = bytearray(BLOCK_SIZE)
            prefix = f"BLK:{block_index:08x}".encode("ascii")
            block[:len(prefix)] = prefix
            fp.write(block)


def assert_large_file_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()
    report = read_small_file(image, FIXTURE_REPORT)
    if "status=ok" not in report:
        raise AssertionError(f"large file report was not ok: {report!r}")
    if "size=0x00500000" not in report:
        raise AssertionError(f"unexpected large file size report: {report!r}")
    if "block=0x000004ff" not in report:
        raise AssertionError(f"unexpected last block report: {report!r}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_qemu_ext3_large_file_smoke.py <logdir>", file=sys.stderr)
        return 2

    logdir = pathlib.Path(sys.argv[1]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    fsboot = repo_root / "build/bin/fsboot.bin"
    fixture_path = repo_root / "src/usr/bin" / FIXTURE_NAME
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = logdir / f"ext3_large_monitor_{os.getpid()}.sock"
    serial_log = logdir / f"ext3_large_serial_{os.getpid()}.log"
    qemu_log = logdir / f"ext3_large_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"ext3_large_prompt_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm):
        if path.exists():
            path.unlink()

    make_fixture(fixture_path)
    qemu = None
    monitor: QemuMonitor | None = None
    try:
        subprocess.run(["make", "-C", "src", "all"], cwd=repo_root, check=True)

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

        qemu = subprocess.Popen(
            qemu_args,
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        wait_for_path(monitor_sock, 10)
        monitor = QemuMonitor(monitor_sock)
        wait_for_prompt(monitor, prompt_ppm, reference, timeout)
        monitor.send_text("/usr/bin/fslarge\n")
        time.sleep(3.0)
        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)

        assert_large_file_state(fsboot)
        serial_text = serial_log.read_text(errors="replace")
        if "PF:" in serial_text or "PageFault" in serial_text:
            raise AssertionError("page fault was detected during ext3 large file smoke")

        print("=== EXT3 LARGE FILE QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {prompt_ppm}")
        return 0
    except Exception as exc:
        print(f"ext3 large file smoke failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if monitor is not None:
            monitor.close()
        if qemu is not None and qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()
        try:
            fixture_path.unlink()
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
