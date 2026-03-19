#!/usr/bin/env python3
"""shell の tab completion を QEMU で smoke する。"""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time

from qemu_config import get_qemu_memory_mb

BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2
DEFAULT_TIMEOUT = 45
OVERLAY_CROP = {
    "x": 780,
    "y": 0,
    "width": 480,
    "height": 24,
}


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


def crop_sha256(ppm_path: pathlib.Path, reference: dict[str, int | str]) -> str:
    image_width, _image_height, pixels = read_ppm(ppm_path)
    x = int(reference["x"])
    y = int(reference["y"])
    width = int(reference["width"])
    height = int(reference["height"])
    row_stride = image_width * 3
    crop = bytearray()

    for row in range(y, y + height):
        start = row * row_stride + x * 3
        crop.extend(pixels[start:start + width * 3])
    return hashlib.sha256(crop).hexdigest()


def crop_matches(ppm_path: pathlib.Path, reference: dict[str, int | str]) -> bool:
    return crop_sha256(ppm_path, reference) == reference["sha256"]


class QemuMonitor:
    def __init__(self, sock_path: pathlib.Path) -> None:
        deadline = time.time() + 20.0

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

    def send_command(self, text: str, settle: float = 1.2) -> None:
        self.send_text(text)
        self.send_key("ret")
        time.sleep(settle)

    def close(self) -> None:
        self.sock.close()


def qemu_key_for_char(ch: str) -> str:
    keymap = {
        " ": "spc",
        "\n": "ret",
        "/": "slash",
        "?": "shift-slash",
        ".": "dot",
        ">": "shift-dot",
        ",": "comma",
        "<": "shift-comma",
        "-": "minus",
        "_": "shift-minus",
        "=": "equal",
        "+": "shift-equal",
        "|": "shift-backslash",
        "\\": "backslash",
        "\"": "shift-apostrophe",
        "'": "apostrophe",
        ":": "shift-semicolon",
        ";": "semicolon",
        "(": "shift-9",
        ")": "shift-0",
    }
    if ch in keymap:
        return keymap[ch]
    if "A" <= ch <= "Z":
        return f"shift-{ch.lower()}"
    return ch


def wait_for_path(path: pathlib.Path, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {path}")


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


def wait_for_serial_marker(serial_log: pathlib.Path, marker: str,
                           timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_log.exists():
            text = serial_log.read_text(errors="replace")
            if marker in text:
                return
        time.sleep(0.2)
    raise TimeoutError(f"serial marker not found: {marker}")


def count_serial_marker(serial_log: pathlib.Path, marker: str) -> int:
    if not serial_log.exists():
        return 0
    return serial_log.read_text(errors="replace").count(marker)


def wait_for_serial_marker_count(serial_log: pathlib.Path, marker: str,
                                 expected: int, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if count_serial_marker(serial_log, marker) >= expected:
            return
        time.sleep(0.2)
    raise TimeoutError(f"serial marker count not reached: {marker} >= {expected}")


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


def assert_completion_state(fsboot: pathlib.Path) -> None:
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
    unique_entry = user_entries.get("unique.txt")
    unique_copy_entry = user_entries.get("unique_copy.txt")
    dir_entry = user_entries.get("dir_only")
    dir_copy_entry = user_entries.get("dir_copy.txt")
    empty_case_entry = user_entries.get("empty_case")

    if unique_entry is None:
        raise AssertionError("unique.txt was not created")
    if unique_copy_entry is None:
        raise AssertionError("unique_copy.txt was not created via unique completion")
    if dir_entry is None:
        raise AssertionError("dir_only was not created")
    if dir_copy_entry is None:
        raise AssertionError("dir_copy.txt was not created via directory completion")
    if empty_case_entry is None:
        raise AssertionError("empty_case was not created")

    dir_entries = read_dir_entries(image, dir_entry[0])
    inner_entry = dir_entries.get("inner.txt")
    empty_case_entries = read_dir_entries(image, empty_case_entry[0])
    if inner_entry is None:
        raise AssertionError("dir_only/inner.txt was not created")
    if "delete_me.txt" in empty_case_entries:
        raise AssertionError("delete_me.txt was not removed via empty token completion")

    unique_text = read_file(image, unique_entry[0]).decode("ascii", errors="ignore")
    unique_copy_text = read_file(image, unique_copy_entry[0]).decode("ascii", errors="ignore")
    inner_text = read_file(image, inner_entry[0]).decode("ascii", errors="ignore")
    dir_copy_text = read_file(image, dir_copy_entry[0]).decode("ascii", errors="ignore")

    if unique_text != unique_copy_text:
        raise AssertionError("unique_copy.txt content does not match unique.txt")
    if inner_text != dir_copy_text:
        raise AssertionError("dir_copy.txt content does not match dir_only/inner.txt")
    if "seed.txt" not in unique_text:
        raise AssertionError("unique.txt does not contain expected ls output")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_shell_completion_smoke.py <fsboot> <logdir>",
              file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    capture_only = os.environ.get("SODEX_CAPTURE_COMPLETION_SHA") == "1"
    overlay_reference = OVERLAY_CROP
    if not capture_only:
        overlay_reference = json.loads(
            (repo_root / "src/test/data/term_completion_overlay_reference.json").read_text()
        )

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = pathlib.Path(tempfile.gettempdir()) / f"sdx_completion_{os.getpid()}.sock"
    serial_log = logdir / f"shell_completion_serial_{os.getpid()}.log"
    qemu_log = logdir / f"shell_completion_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"shell_completion_prompt_{os.getpid()}.ppm"
    overlay_ppm = logdir / f"shell_completion_overlay_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm, overlay_ppm):
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
        try:
            wait_for_metric(serial_log, "full_redraw", 5)
        except TimeoutError:
            ready_count = count_serial_marker(serial_log, "AUDIT eshell_ready")
            wait_for_serial_marker(serial_log, "AUDIT eshell_ready", timeout)
            monitor.send_text("term")
            time.sleep(0.2)
            monitor.send_key("ret")
            wait_for_metric(serial_log, "full_redraw", timeout)
            wait_for_serial_marker_count(serial_log, "AUDIT eshell_ready",
                                         ready_count + 1, timeout)
        else:
            wait_for_serial_marker(serial_log, "AUDIT eshell_ready", timeout)
        time.sleep(1.0)
        monitor.command(f"screendump {prompt_ppm}", pause=0.3)

        monitor.send_command("touch seed.txt")
        monitor.send_command("ls > unique.txt")
        monitor.send_command("mkdir dir_only")
        monitor.send_command("ls > dir_only/inner.txt")
        monitor.send_command("touch pair_apple.txt")
        monitor.send_command("touch pair_apricot.txt")
        monitor.send_command("mkdir empty_case")
        monitor.send_command("touch empty_case/delete_me.txt")

        monitor.send_text("cat uni")
        monitor.send_key("tab")
        monitor.send_command("> unique_copy.txt")

        monitor.send_text("cat dir_o")
        monitor.send_key("tab")
        monitor.send_command("inner.txt > dir_copy.txt")

        monitor.send_text("cat pair_")
        monitor.send_key("tab")
        time.sleep(0.2)
        monitor.send_key("tab")
        if capture_only:
            monitor.command(f"screendump {overlay_ppm}", pause=0.3)
            digest = crop_sha256(overlay_ppm, overlay_reference)
            print(json.dumps({
                "x": overlay_reference["x"],
                "y": overlay_reference["y"],
                "width": overlay_reference["width"],
                "height": overlay_reference["height"],
                "sha256": digest,
            }, indent=2))
        else:
            wait_for_screen(monitor, overlay_ppm, overlay_reference,
                            timeout, "completion overlay")
        monitor.send_key("esc")
        monitor.send_key("ret")
        time.sleep(1.0)

        monitor.send_command("cd empty_case")
        monitor.send_text("rm ")
        monitor.send_key("tab")
        monitor.send_key("ret")
        time.sleep(1.0)
        monitor.send_command("cd ..")

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)

        if not capture_only:
            assert_completion_state(fsboot)
            serial_text = serial_log.read_text(errors="replace")
            if "PF:" in serial_text or "PageFault" in serial_text:
                raise AssertionError(
                    "page fault was detected during shell completion smoke"
                )
            print("=== SHELL COMPLETION QEMU SMOKE DONE ===")
            print(f"Artifacts: {serial_log}, {qemu_log}, {prompt_ppm}, {overlay_ppm}")
        return 0
    except Exception as exc:
        print(f"shell completion smoke failed: {exc}", file=sys.stderr)
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
