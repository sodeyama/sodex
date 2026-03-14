#!/usr/bin/env python3
"""UTF-8 表示と vi 保存導線を QEMU で smoke する。"""

from __future__ import annotations

import json
import os
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time

BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2
DEFAULT_TIMEOUT = 45
CELL_WIDTH = 8
CELL_HEIGHT = 16
FG_PIXEL = (0xAA, 0xAA, 0xAA)
BG_PIXEL = (0x00, 0x00, 0x00)
DEMO_TEXT = "日本語 あいうえお ABC\n".encode("utf-8")
GLYPH_MATCH_THRESHOLD = 210


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
    import hashlib

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


def load_wide_glyphs(header_path: pathlib.Path) -> dict[int, list[int]]:
    glyphs: dict[int, list[int]] = {}
    pattern = re.compile(r"\{0x([0-9a-f]+), \{([^}]*)\}\},")

    for codepoint_hex, rows_text in pattern.findall(header_path.read_text()):
        rows = [int(value.strip(), 16) for value in rows_text.split(",")]
        glyphs[int(codepoint_hex, 16)] = rows
    return glyphs


def pixel_at(pixels: bytes, image_width: int, x: int, y: int) -> tuple[int, int, int]:
    start = (y * image_width + x) * 3
    return pixels[start], pixels[start + 1], pixels[start + 2]


def glyph_score(pixels: bytes, image_width: int, image_height: int,
                x0: int, y0: int, rows: list[int]) -> int:
    if x0 < 0 or y0 < 0:
        return -1
    if x0 + 16 > image_width or y0 + 16 > image_height:
        return -1

    score = 0
    for y, row_bits in enumerate(rows):
        for x in range(16):
            expected = FG_PIXEL if (row_bits & (1 << (15 - x))) != 0 else BG_PIXEL
            if pixel_at(pixels, image_width, x0 + x, y0 + y) == expected:
                score += 1
    return score


def has_glyph_sequence(ppm_path: pathlib.Path, glyph_rows: list[list[int]]) -> bool:
    image_width, image_height, pixels = read_ppm(ppm_path)
    sequence_width = len(glyph_rows) * 16

    for y in range(0, image_height - CELL_HEIGHT + 1, CELL_HEIGHT):
        for x in range(0, image_width - sequence_width + 1, CELL_WIDTH):
            ok = True
            for index, rows in enumerate(glyph_rows):
                if glyph_score(pixels, image_width, image_height,
                               x + index * 16, y, rows) < GLYPH_MATCH_THRESHOLD:
                    ok = False
                    break
            if ok:
                return True
    return False


def wait_for_utf8_output(monitor: QemuMonitor, ppm_path: pathlib.Path,
                         kanji_rows: list[list[int]],
                         hira_rows: list[list[int]],
                         timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.3)
        if ppm_path.exists():
            if has_glyph_sequence(ppm_path, kanji_rows) and has_glyph_sequence(ppm_path, hira_rows):
                return
        time.sleep(0.2)
    raise TimeoutError("utf8 glyph sequence not found in screenshot")


def assert_utf8_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()
    root_entries = read_dir_entries(image, SODEX_ROOT_INO)
    utf8_entry = root_entries.get("utf8.txt")
    marker_entry = root_entries.get("afterutf8.txt")
    if utf8_entry is None:
        raise AssertionError("utf8.txt was not created")
    if marker_entry is None:
        raise AssertionError("afterutf8.txt was not created after returning to shell")

    content = read_file(image, utf8_entry[0])
    expected = b"z" + DEMO_TEXT
    if content != expected:
        raise AssertionError(f"utf8.txt content mismatch: {content!r}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_utf8_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())
    wide_glyphs = load_wide_glyphs(repo_root / "src/include/font16x16_data.h")
    kanji_rows = [wide_glyphs[0x65E5], wide_glyphs[0x672C], wide_glyphs[0x8A9E]]
    hira_rows = [wide_glyphs[0x3042], wide_glyphs[0x3044], wide_glyphs[0x3046],
                 wide_glyphs[0x3048], wide_glyphs[0x304A]]

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = pathlib.Path(tempfile.gettempdir()) / f"sdx_u8_{os.getpid()}.sock"
    serial_log = logdir / f"utf8_serial_{os.getpid()}.log"
    qemu_log = logdir / f"utf8_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"utf8_prompt_{os.getpid()}.ppm"
    utf8_ppm = logdir / f"utf8_output_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm, utf8_ppm):
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

        monitor.send_text("utf8demo > utf8.txt\n")
        time.sleep(1.0)
        monitor.send_text("cat utf8.txt\n")
        wait_for_utf8_output(monitor, utf8_ppm, kanji_rows, hira_rows, timeout)

        monitor.send_text("vi utf8.txt\n")
        time.sleep(1.0)
        monitor.send_text("iz")
        time.sleep(0.5)
        monitor.send_key("esc")
        time.sleep(0.5)
        monitor.send_text(":wq\n")
        time.sleep(1.0)
        monitor.send_text("touch afterutf8.txt\n")
        time.sleep(1.0)

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_utf8_state(fsboot)

        print("=== UTF8 QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {utf8_ppm}")
        return 0
    except Exception as exc:
        print(f"utf8 smoke failed: {exc}", file=sys.stderr)
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
