#!/usr/bin/env python3
"""term 上の vi / agent 実行導線を QEMU で smoke する。"""

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

    def send_ctrl(self, key: str, delay: float = 0.04) -> None:
        self.command(f"sendkey ctrl-{key}", pause=0.05)
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
        "?": "shift-slash",
        ".": "dot",
        ">": "shift-dot",
        ",": "comma",
        "<": "shift-comma",
        "-": "minus",
        "_": "shift-minus",
        ":": "shift-semicolon",
        ";": "semicolon",
        "$": "shift-4",
        "\"": "shift-apostrophe",
        "'": "apostrophe",
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


def wait_for_metric_count(serial_log: pathlib.Path, marker: str,
                          minimum: int, timeout: float) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_log.exists():
            matches = [
                line for line in serial_log.read_text(errors="replace").splitlines()
                if marker in line
            ]
            if len(matches) >= minimum:
                return matches[-1]
        time.sleep(0.2)
    raise TimeoutError(f"metric count not reached: {marker} >= {minimum}")


def parse_metric_fields(line: str) -> dict[str, str]:
    result: dict[str, str] = {}

    for part in line.strip().split():
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        result[key] = value
    return result


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
            serial_text = serial_log.read_text(errors="replace")
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


def inode_blocks(image: bytes, ino: int) -> list[int]:
    inode = inode_bytes(image, ino)
    return [struct.unpack_from("<I", inode, 40 + index * 4)[0] for index in range(12)]


def read_file(image: bytes, ino: int) -> bytes:
    size = inode_size(image, ino)
    remaining = size
    chunks: list[bytes] = []

    for block in inode_blocks(image, ino):
        if block == 0 or remaining <= 0:
            break
        take = min(BLOCK_SIZE, remaining)
        start = block * BLOCK_SIZE
        chunks.append(image[start:start + take])
        remaining -= take
    return b"".join(chunks)


def read_dir_entries(image: bytes, ino: int) -> dict[str, tuple[int, int]]:
    result: dict[str, tuple[int, int]] = {}
    remaining = inode_size(image, ino)

    for block in inode_blocks(image, ino):
        offset = 0

        if block == 0 or remaining <= 0:
            break
        data = image[block * BLOCK_SIZE:(block + 1) * BLOCK_SIZE]
        limit = min(BLOCK_SIZE, remaining)
        while offset + 8 <= limit:
            inode_num = struct.unpack_from("<I", data, offset)[0]
            rec_len = struct.unpack_from("<H", data, offset + 4)[0]
            name_len = data[offset + 6]
            file_type = data[offset + 7]
            if inode_num == 0 or rec_len == 0:
                break
            name = data[offset + 8:offset + 8 + name_len].decode("ascii", errors="ignore")
            result[name] = (inode_num, file_type)
            offset += rec_len
        remaining -= BLOCK_SIZE

    return result


def read_user_file(image: bytes, name: str) -> str:
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
        raise AssertionError(f"/home/user/{name} was not created")
    return read_file(image, file_entry[0]).decode("ascii", errors="replace")


def try_read_user_file(fsboot: pathlib.Path, name: str) -> str | None:
    try:
        return read_user_file(fsboot.read_bytes(), name)
    except (AssertionError, FileNotFoundError):
        return None


def wait_for_user_file(fsboot: pathlib.Path, name: str, timeout: float,
                       expected: str | None = None) -> str:
    deadline = time.time() + timeout

    while time.time() < deadline:
        text = try_read_user_file(fsboot, name)
        if text is not None:
            if expected is None or text == expected:
                return text
        time.sleep(0.2)

    if expected is None:
        raise TimeoutError(f"user file not found: {name}")
    raise TimeoutError(f"user file did not match: {name}")


def assert_vi_state(fsboot: pathlib.Path) -> None:
    image = fsboot.read_bytes()
    command_vi_text = read_user_file(image, "term_command_vi.txt")
    command_agent_text = read_user_file(image, "term_command_agent.txt")
    agent_sessions_text = read_user_file(image, "agent_sessions.txt")
    previ_text = read_user_file(image, "previ.txt")
    content = read_user_file(image, "memo.txt")
    aftervi_text = read_user_file(image, "aftervi.txt")

    if command_vi_text != "/usr/bin/vi\n":
        raise AssertionError(f"term_command_vi.txt mismatch: {command_vi_text!r}")
    if command_agent_text != "/usr/bin/agent\n":
        raise AssertionError(f"term_command_agent.txt mismatch: {command_agent_text!r}")
    if agent_sessions_text != "No sessions.\n":
        raise AssertionError(f"agent_sessions.txt mismatch: {agent_sessions_text!r}")
    if previ_text != "":
        raise AssertionError(f"previ.txt mismatch: {previ_text!r}")
    if content != "alpha beta\ngamma delta\nomega":
        raise AssertionError(f"memo.txt content mismatch: {content!r}")
    if aftervi_text != "":
        raise AssertionError(f"aftervi.txt mismatch: {aftervi_text!r}")


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def build_temp_rootfs(repo_root: pathlib.Path, logdir: pathlib.Path) -> pathlib.Path:
    overlay_src = repo_root / "src" / "rootfs-overlay"
    overlay_dir = logdir / "vi_rootfs_overlay"
    temp_fsboot = logdir / "vi_fsboot.bin"
    kmkfs = repo_root / "build" / "tools" / "kmkfs"
    boota = repo_root / "build" / "bin" / "boota.bin"
    bootm = repo_root / "build" / "bin" / "bootm.bin"
    kernel = repo_root / "build" / "bin" / "kernel.bin"
    init = repo_root / "src" / "init" / "bin" / "ptest"
    init2 = repo_root / "src" / "init" / "bin" / "ptest2"

    if overlay_dir.exists():
        subprocess.run(["rm", "-rf", str(overlay_dir)], check=True)
    subprocess.run(["cp", "-R", str(overlay_src), str(overlay_dir)], check=True)
    if temp_fsboot.exists():
        temp_fsboot.unlink()

    write_text(
        overlay_dir / "home" / "user" / "term_lookup.sh",
        """command -v vi > /home/user/term_command_vi.txt
command -v agent > /home/user/term_command_agent.txt
agent sessions > /home/user/agent_sessions.txt
touch /home/user/term_lookup.done
""",
    )

    subprocess.run(
        [
            str(kmkfs),
            str(boota),
            str(bootm),
            str(kernel),
            str(temp_fsboot),
            str(init),
            str(init2),
            str(overlay_dir),
        ],
        check=True,
        cwd=repo_root / "src",
    )
    return temp_fsboot


def send_command_and_wait_for_file(monitor: QemuMonitor,
                                   text: str,
                                   fsboot: pathlib.Path,
                                   name: str,
                                   timeout: float,
                                   expected: str | None = None,
                                   delay: float = 0.2) -> str:
    monitor.send_text(text)
    monitor.send_key("ret")
    time.sleep(delay)
    return wait_for_user_file(fsboot, name, timeout, expected)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_vi_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    _ = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())

    logdir.mkdir(parents=True, exist_ok=True)
    smoke_fsboot = build_temp_rootfs(repo_root, logdir)
    monitor_sock = pathlib.Path(tempfile.gettempdir()) / f"sdx_v_{os.getpid()}.sock"
    serial_log = logdir / f"vi_serial_{os.getpid()}.log"
    qemu_log = logdir / f"vi_qemu_{os.getpid()}.log"
    prompt_ppm = logdir / f"vi_prompt_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log, prompt_ppm):
        if path.exists():
            path.unlink()

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
        wait_for_serial_marker(serial_log, "AUDIT eshell_ready", timeout)
        wait_for_terminal_ready(monitor, prompt_ppm, reference, serial_log,
                                timeout)

        send_command_and_wait_for_file(
            monitor,
            "sh /home/user/term_lookup.sh",
            smoke_fsboot,
            "term_lookup.done",
            timeout,
            "",
            0.4,
        )
        send_command_and_wait_for_file(
            monitor,
            "touch /home/user/previ.txt",
            smoke_fsboot,
            "previ.txt",
            timeout,
            "",
            0.4,
        )

        monitor.send_text("vi /home/user/memo.txt\n")
        time.sleep(1.0)
        monitor.send_text("ialpha beta\ngamma delta\nomega")
        time.sleep(0.6)
        monitor.send_key("esc")
        time.sleep(0.5)
        monitor.send_text(":wq\n")
        wait_for_user_file(smoke_fsboot, "memo.txt", timeout,
                           "alpha beta\ngamma delta\nomega")
        send_command_and_wait_for_file(
            monitor,
            "touch /home/user/aftervi.txt",
            smoke_fsboot,
            "aftervi.txt",
            timeout,
            "",
            0.4,
        )

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_vi_state(smoke_fsboot)
        serial_text = serial_log.read_text(errors="replace")
        if "PF:" in serial_text or "PageFault" in serial_text:
            raise AssertionError("page fault was detected during vi smoke")

        print("=== VI QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {smoke_fsboot}")
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
