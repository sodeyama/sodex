#!/usr/bin/env python3
"""日本語 IME の shell / vi 導線を QEMU で smoke する。"""

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

from qemu_config import get_qemu_memory_mb

BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2
DEFAULT_TIMEOUT = 45
IME_FILE_TEXT = "首相 感じ ABC".encode("utf-8")
SENTENCE_FILE_TEXT = "彼女は東京に行く".encode("utf-8")
SHELL_FILENAME = "漢字"
BLOB_SHELL_FILENAME = "首相"
LATIN_FILENAME = "latin.txt"
SENTENCE_FILENAME = "sentence.txt"


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
        deadline = time.time() + 20.0

        self.sock: socket.socket | None = None
        while True:
            sock: socket.socket | None = None
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.settimeout(1.0)
                sock.connect(str(sock_path))
                self.sock = sock
                break
            except OSError:
                try:
                    if sock is not None:
                        sock.close()
                except Exception:
                    pass
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

    def send_ctrl_space(self) -> None:
        self.send_key("ctrl-spc")

    def send_hankaku_zenkaku(self) -> None:
        self.send_key("grave_accent")

    def send_henkan(self) -> None:
        self.send_key("henkan")

    def send_muhenkan(self) -> None:
        self.send_key("muhenkan")

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
            if "A" <= ch <= "Z":
                self.send_key(f"shift-{ch.lower()}")
                continue
            self.send_key(keymap.get(ch, ch))

    def close(self) -> None:
        if self.sock is not None:
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


def wait_for_term_ready(serial_log: pathlib.Path, monitor: QemuMonitor,
                        timeout: float) -> None:
    deadline = time.time() + timeout
    launched_from_rescue = False

    while time.time() < deadline:
        if serial_log.exists():
            text = serial_log.read_text(errors="replace")
            term_pos = text.rfind("AUDIT term_mode=framebuffer")

            if ("AUDIT init_enter_rescue" in text and
                    "AUDIT eshell_ready" in text and
                    not launched_from_rescue and
                    term_pos < 0):
                monitor.send_text("/usr/bin/term\n")
                launched_from_rescue = True
            if term_pos >= 0 and "AUDIT eshell_ready" in text[term_pos:]:
                return
        time.sleep(0.2)

    raise TimeoutError("term shell did not become ready")


def wait_for_prompt(monitor: QemuMonitor, ppm_path: pathlib.Path,
                    reference: dict[str, int | str], timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.3)
        if crop_matches(ppm_path, reference):
            return
        time.sleep(0.2)
    raise TimeoutError("prompt screenshot did not match reference")


def wait_for_prompt_best_effort(monitor: QemuMonitor, ppm_path: pathlib.Path,
                                reference: dict[str, int | str],
                                timeout: float) -> None:
    try:
        wait_for_prompt(monitor, ppm_path, reference, min(timeout, 3.0))
    except TimeoutError:
        time.sleep(1.0)


def wait_for_screen(monitor: QemuMonitor, ppm_path: pathlib.Path,
                    reference: dict[str, int | str], timeout: float,
                    what: str) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.3)
        if crop_matches(ppm_path, reference):
            return
        time.sleep(0.2)
    raise TimeoutError(f"{what} screenshot did not match reference")


def wait_for_screen_best_effort(monitor: QemuMonitor, ppm_path: pathlib.Path,
                                reference: dict[str, int | str], timeout: float,
                                what: str) -> None:
    try:
        wait_for_screen(monitor, ppm_path, reference, min(timeout, 3.0), what)
    except TimeoutError:
        time.sleep(1.0)


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
        name = data[offset + 8:offset + 8 + name_len].decode("utf-8", errors="replace")
        result[name] = (inode_num, file_type)
        offset += rec_len
    return result


def assert_ime_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()
    root_entries = read_dir_entries(image, SODEX_ROOT_INO)
    ime_entry = root_entries.get("ime.txt")
    marker_entry = root_entries.get("afterime.txt")
    shell_entry = root_entries.get(SHELL_FILENAME)
    blob_shell_entry = root_entries.get(BLOB_SHELL_FILENAME)
    latin_entry = root_entries.get(LATIN_FILENAME)
    sentence_entry = root_entries.get(SENTENCE_FILENAME)

    if ime_entry is None:
        raise AssertionError("ime.txt was not created")
    if marker_entry is None:
        raise AssertionError("afterime.txt was not created after returning to shell")
    if shell_entry is None:
        raise AssertionError(f"{SHELL_FILENAME!r} was not created from shell input")
    if blob_shell_entry is None:
        raise AssertionError(f"{BLOB_SHELL_FILENAME!r} was not created from blob dictionary input")
    if latin_entry is None:
        raise AssertionError(f"{LATIN_FILENAME!r} was not created after returning to latin mode")
    if sentence_entry is None:
        raise AssertionError(f"{SENTENCE_FILENAME!r} was not created from sentence conversion")

    content = read_file(image, ime_entry[0])
    if content != IME_FILE_TEXT:
        raise AssertionError(f"ime.txt content mismatch: {content!r}")
    sentence_content = read_file(image, sentence_entry[0])
    if sentence_content != SENTENCE_FILE_TEXT:
        raise AssertionError(f"sentence.txt content mismatch: {sentence_content!r}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_ime_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())
    ime_reference = json.loads((repo_root / "src/test/data/term_ime_line_reference.json").read_text())
    hira_overlay_reference = json.loads((repo_root / "src/test/data/term_ime_hira_overlay_reference.json").read_text())
    latin_overlay_reference = json.loads((repo_root / "src/test/data/term_ime_latin_overlay_reference.json").read_text())
    conversion_overlay_reference = json.loads((repo_root / "src/test/data/term_ime_conversion_overlay_reference.json").read_text())

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = pathlib.Path(tempfile.gettempdir()) / f"sdx_i_{os.getpid()}.sock"
    serial_log = logdir / f"ime_serial_{os.getpid()}.log"
    qemu_log = logdir / f"ime_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"ime_prompt_{os.getpid()}.ppm"
    ime_ppm = logdir / f"ime_line_{os.getpid()}.ppm"
    overlay_ppm = logdir / f"ime_overlay_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm, ime_ppm, overlay_ppm):
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
        wait_for_term_ready(serial_log, monitor, timeout)
        wait_for_prompt_best_effort(monitor, prompt_ppm, reference, timeout)

        monitor.send_text("touch ")
        monitor.send_hankaku_zenkaku()
        wait_for_screen_best_effort(monitor, overlay_ppm, hira_overlay_reference, timeout, "hira overlay")
        monitor.send_text("aiueo")
        monitor.send_key("backspace")
        monitor.send_key("backspace")
        wait_for_screen_best_effort(monitor, ime_ppm, ime_reference, timeout, "ime line")
        monitor.send_hankaku_zenkaku()
        wait_for_screen_best_effort(monitor, overlay_ppm, latin_overlay_reference, timeout, "latin overlay")
        monitor.send_text("\n")
        time.sleep(1.0)
        wait_for_prompt_best_effort(monitor, prompt_ppm, reference, timeout)

        monitor.send_text("touch ")
        monitor.send_hankaku_zenkaku()
        monitor.send_text("kanji")
        monitor.send_key("spc")
        wait_for_screen_best_effort(monitor, overlay_ppm, conversion_overlay_reference, timeout, "conversion overlay")
        monitor.send_key("ret")
        monitor.send_muhenkan()
        monitor.send_text("\n")
        time.sleep(1.0)
        wait_for_prompt_best_effort(monitor, prompt_ppm, reference, timeout)
        monitor.send_text(f"touch {LATIN_FILENAME}\n")
        time.sleep(1.0)
        wait_for_prompt_best_effort(monitor, prompt_ppm, reference, timeout)

        monitor.send_text("touch ")
        monitor.send_hankaku_zenkaku()
        monitor.send_text("shushou")
        monitor.send_key("spc")
        monitor.send_key("ret")
        monitor.send_muhenkan()
        monitor.send_text("\n")
        time.sleep(1.0)
        wait_for_prompt_best_effort(monitor, prompt_ppm, reference, timeout)

        monitor.send_text("vi ime.txt\n")
        time.sleep(1.0)
        monitor.send_text("i")
        monitor.send_henkan()
        monitor.send_text("shushou")
        monitor.send_key("spc")
        monitor.send_key("ret")
        monitor.send_muhenkan()
        monitor.send_text(" ")
        monitor.send_henkan()
        monitor.send_text("kanji")
        monitor.send_key("spc")
        monitor.send_key("right")
        monitor.send_key("ret")
        monitor.send_muhenkan()
        monitor.send_text(" ABC")
        time.sleep(0.5)
        monitor.send_key("esc")
        time.sleep(0.5)
        monitor.send_text(":wq\n")
        time.sleep(1.0)
        monitor.send_text(f"vi {SENTENCE_FILENAME}\n")
        time.sleep(1.0)
        monitor.send_text("i")
        monitor.send_henkan()
        monitor.send_text("kanojohatoukyouniiku")
        monitor.send_key("spc")
        monitor.send_key("right")
        monitor.send_key("right")
        monitor.send_key("ret")
        monitor.send_muhenkan()
        time.sleep(0.5)
        monitor.send_key("esc")
        time.sleep(0.5)
        monitor.send_text(":wq\n")
        time.sleep(1.0)
        monitor.send_text("touch afterime.txt\n")
        time.sleep(1.0)

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_ime_state(fsboot)

        print("=== IME QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {prompt_ppm}, {ime_ppm}, {overlay_ppm}")
        return 0
    except Exception as exc:
        print(f"ime smoke failed: {exc}", file=sys.stderr)
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
