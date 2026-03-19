#!/usr/bin/env python3
"""shell の fd redirection と pager を QEMU で smoke する。"""

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
DEFAULT_TIMEOUT = 60


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

    def send_key(self, key: str, delay: float = 0.005) -> None:
        self.command(f"sendkey {key}", pause=0.005)
        time.sleep(delay)

    def send_ctrl(self, key: str, delay: float = 0.005) -> None:
        self.command(f"sendkey ctrl-{key}", pause=0.005)
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
        "\"": "shift-apostrophe",
        "&": "shift-7",
        "'": "apostrophe",
        "(": "shift-9",
        ")": "shift-0",
        "+": "shift-equal",
        ",": "comma",
        "-": "minus",
        ".": "dot",
        "/": "slash",
        ":": "shift-semicolon",
        ";": "semicolon",
        "<": "shift-comma",
        "=": "equal",
        ">": "shift-dot",
        "?": "shift-slash",
        "\\": "backslash",
        "|": "shift-backslash",
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


def count_serial_marker(serial_log: pathlib.Path, marker: str) -> int:
    return read_serial_text(serial_log).count(marker)


def wait_for_serial_marker_count(serial_log: pathlib.Path, marker: str,
                                 expected: int, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if count_serial_marker(serial_log, marker) >= expected:
            return
        time.sleep(0.2)
    raise TimeoutError(f"serial marker count not reached: {marker} >= {expected}")


def wait_for_metric(serial_log: pathlib.Path, point: str, timeout: float) -> str:
    deadline = time.time() + timeout
    marker = f"TERM_METRIC point={point}"
    while time.time() < deadline:
        for line in read_serial_text(serial_log).splitlines():
            if marker in line:
                return line
        time.sleep(0.2)
    raise TimeoutError(f"metric not found: {point}")


def latest_render_top(serial_log: pathlib.Path, program: str) -> int | None:
    prefix = f"AUDIT {program}_render top="
    last: int | None = None
    for line in read_serial_text(serial_log).splitlines():
        if prefix not in line:
            continue
        try:
            top_text = line.split(prefix, 1)[1].split(" ", 1)[0]
            last = int(top_text)
        except (IndexError, ValueError):
            continue
    return last


def wait_for_render_top(serial_log: pathlib.Path, program: str,
                        previous_count: int,
                        predicate, timeout: float) -> int:
    deadline = time.time() + timeout
    prefix = f"AUDIT {program}_render top="
    while time.time() < deadline:
        text = read_serial_text(serial_log)
        lines = [line for line in text.splitlines() if prefix in line]
        if len(lines) > previous_count:
            top = latest_render_top(serial_log, program)
            if top is not None and predicate(top):
                return len(lines)
        time.sleep(0.2)
    raise TimeoutError(f"render marker not reached: {program}")


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


def assert_guest_state(fsboot: pathlib.Path) -> None:
    merged_text = read_user_file(fsboot, "merged.log")
    stdout_text = read_user_file(fsboot, "stdout.log")
    err_text = read_user_file(fsboot, "err.log")
    pager_text = read_user_file(fsboot, "pager.txt")

    if merged_text != "out\ncat: open failed missing.txt\n":
        raise AssertionError(f"merged.log mismatch: {merged_text!r}")
    if stdout_text != "out\n":
        raise AssertionError(f"stdout.log mismatch: {stdout_text!r}")
    if err_text != "routed\n":
        raise AssertionError(f"err.log mismatch: {err_text!r}")
    if "l01\n" not in pager_text or "l80\n" not in pager_text:
        raise AssertionError("pager.txt does not contain expected lines")


def send_and_wait(monitor: QemuMonitor, text: str, delay: float = 1.2) -> None:
    monitor.send_text(text)
    monitor.send_key("ret")
    time.sleep(delay)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_shell_redirection_pager_smoke.py <fsboot> <logdir>",
              file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = logdir / f"shell_redir_pager_monitor_{os.getpid()}.sock"
    serial_log = logdir / f"shell_redir_pager_serial_{os.getpid()}.log"
    qemu_log = logdir / f"shell_redir_pager_qemu_{os.getpid()}.log"
    smoke_fsboot = logdir / f"shell_redir_pager_fsboot_{os.getpid()}.bin"

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

        send_and_wait(monitor,
                      "sh -c \"echo out; cat missing.txt\" > merged.log 2>&1")
        send_and_wait(monitor,
                      "sh -c \"echo out; cat missing.txt\" 2>&1 > stdout.log")
        send_and_wait(monitor, "echo routed 2> err.log 1>&2")

        for start in range(1, 81, 5):
            lines = "; ".join(
                f"echo l{value:02d}"
                for value in range(start, min(start + 5, 81))
            )
            redirect = ">" if start == 1 else ">>"
            send_and_wait(monitor, f"sh -c \"{lines}\" {redirect} pager.txt")

        more_count = count_serial_marker(serial_log, "AUDIT more_render")
        monitor.send_text("more pager.txt")
        monitor.send_key("ret")
        more_count = wait_for_render_top(serial_log, "more", more_count,
                                         lambda top: top == 0, timeout)
        monitor.send_key("spc")
        more_count = wait_for_render_top(serial_log, "more", more_count,
                                         lambda top: top > 0, timeout)
        monitor.send_text("b")
        more_count = wait_for_render_top(serial_log, "more", more_count,
                                         lambda top: top == 0, timeout)
        monitor.send_text("q")
        time.sleep(0.8)

        less_count = count_serial_marker(serial_log, "AUDIT less_render")
        monitor.send_text("less pager.txt")
        monitor.send_key("ret")
        less_count = wait_for_render_top(serial_log, "less", less_count,
                                         lambda top: top == 0, timeout)
        monitor.send_text("G")
        less_count = wait_for_render_top(serial_log, "less", less_count,
                                         lambda top: top > 0, timeout)
        monitor.send_text("g")
        less_count = wait_for_render_top(serial_log, "less", less_count,
                                         lambda top: top == 0, timeout)
        monitor.send_text("/l60")
        monitor.send_key("ret")
        less_count = wait_for_render_top(serial_log, "less", less_count,
                                         lambda top: top > 0, timeout)
        monitor.send_text("q")
        time.sleep(0.8)

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        assert_guest_state(smoke_fsboot)
        serial_text = read_serial_text(serial_log)
        if "PF:" in serial_text or "PageFault" in serial_text:
            raise AssertionError("page fault was detected during smoke")

        print("=== SHELL REDIRECTION/PAGER QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}")
        return 0
    except Exception as exc:
        print(f"shell redirection/pager smoke failed: {exc}", file=sys.stderr)
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
