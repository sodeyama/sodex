#!/usr/bin/env python3
"""agent-term profile と @ route を QEMU で smoke する。"""

from __future__ import annotations

import os
import pathlib
import socket
import struct
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 60
BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2


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

    def command(self, text: str, pause: float = 0.08) -> str:
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

    def send_key(self, key: str, delay: float = 0.06) -> None:
        self.command(f"sendkey {key}", pause=0.05)
        time.sleep(delay)

    def send_text(self, text: str) -> None:
        for ch in text:
            self.send_key(qemu_key_for_char(ch))
        time.sleep(0.2)

    def close(self) -> None:
        self.sock.close()


def qemu_key_for_char(ch: str) -> str:
    keymap = {
        " ": "spc",
        "\n": "ret",
        "/": "slash",
        ".": "dot",
        ">": "shift-dot",
        "-": "minus",
        "_": "shift-minus",
        ":": "shift-semicolon",
        ";": "semicolon",
        "@": "shift-2",
    }
    if ch in keymap:
        return keymap[ch]
    if "A" <= ch <= "Z":
        return f"shift-{ch.lower()}"
    return ch


def tail_text(path: pathlib.Path, limit: int = 400) -> str:
    if not path.exists():
        return ""
    text = path.read_text(errors="replace")
    if len(text) <= limit:
        return text
    return text[-limit:]


def wait_for_path(path: pathlib.Path, timeout: float,
                  process: subprocess.Popen[bytes] | None = None,
                  error_log: pathlib.Path | None = None) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        if process is not None and process.poll() is not None:
            detail = ""
            if error_log is not None:
                stderr_text = tail_text(error_log)
                if stderr_text:
                    detail = f": {stderr_text}"
            raise RuntimeError(
                f"qemu exited before {path} appeared (code={process.returncode}){detail}"
            )
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {path}")


def wait_for_serial_text(serial_log: pathlib.Path, text: str, timeout: float) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")
            if text in serial_text:
                return serial_text
        time.sleep(0.2)
    raise TimeoutError(f"serial text not found: {text}")


def wait_for_serial_count(serial_log: pathlib.Path, text: str,
                          minimum: int, timeout: float) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")
            if serial_text.count(text) >= minimum:
                return serial_text
        time.sleep(0.2)
    raise TimeoutError(f"serial text count not reached: {text} >= {minimum}")


def serial_count(serial_log: pathlib.Path, text: str) -> int:
    if not serial_log.exists():
        return 0
    return serial_log.read_text(errors="replace").count(text)


def inode_bytes(image: bytes, ino: int) -> bytes:
    start = P_INODE_BLOCK + (ino - 1) * INODE_SIZE
    return image[start:start + INODE_SIZE]


def inode_size(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 4)[0]


def inode_blocks(image: bytes, ino: int) -> list[int]:
    inode = inode_bytes(image, ino)
    return [struct.unpack_from("<I", inode, 40 + index * 4)[0] for index in range(12)]


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


def lookup_inode(image: bytes, path: str) -> int:
    current = SODEX_ROOT_INO

    if path == "/":
        return current
    for part in [segment for segment in path.split("/") if segment]:
        entries = read_dir_entries(image, current)
        if part not in entries:
            raise AssertionError(f"path not found in image: {path}")
        current = entries[part][0]
    return current


def read_file_by_path(image: bytes, path: str) -> str:
    return read_file(image, lookup_inode(image, path)).decode("ascii", errors="replace")


def agent_hash_path(path: str) -> int:
    value = 5381

    for ch in path.encode("utf-8"):
        value = ((value << 5) + value) ^ ch
        value &= 0xFFFFFFFF
    return value


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_agent_fusion_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    serial_log = logdir / f"agent_fusion_serial_{os.getpid()}.log"
    qemu_log = logdir / f"agent_fusion_qemu_{os.getpid()}.log"
    qemu_stderr_log = logdir / f"agent_fusion_qemu_stderr_{os.getpid()}.log"
    monitor_sock = logdir / f"agent_fusion_monitor_{os.getpid()}.sock"

    for path in (serial_log, qemu_log, qemu_stderr_log, monitor_sock):
      if path.exists():
        path.unlink()

    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_memory_mb = get_qemu_memory_mb()
    qemu_args = [
        qemu_bin,
        "-drive",
        f"file={fsboot},format=raw,if=ide",
        "-m",
        str(qemu_memory_mb),
        "-display",
        "none",
        "-no-reboot",
        "-serial",
        f"file:{serial_log}",
        "-monitor",
        f"unix:{monitor_sock},server,nowait",
        "-D",
        str(qemu_log),
        "-netdev",
        "user,id=net0",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    with qemu_stderr_log.open("wb") as stderr_file:
        qemu = subprocess.Popen(
            qemu_args,
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=stderr_file,
        )
        monitor: QemuMonitor | None = None

        try:
            wait_for_path(monitor_sock, 15, process=qemu, error_log=qemu_stderr_log)
            monitor = QemuMonitor(monitor_sock)
            wait_for_serial_text(serial_log, "AUDIT term_agent_fusion_enabled", timeout)
            wait_for_serial_text(serial_log, "AUDIT eshell_agent_fusion_enabled", timeout)
            time.sleep(1.0)

            route_done_count = serial_count(serial_log, "AUDIT eshell_agent_route_done")
            monitor.send_text("@memory add fusion-note\n")
            wait_for_serial_count(serial_log, "AUDIT eshell_agent_route_done",
                                  route_done_count + 1, timeout)
            time.sleep(1.0)
            monitor.send_text("/status\n")
            wait_for_serial_count(serial_log, "AUDIT eshell_fusion_status", 1, timeout)
            time.sleep(1.0)
            auto_apply_count = serial_count(serial_log,
                                            "AUDIT eshell_recovery_auto_apply=")
            monitor.send_text("ecoh fused > /home/user/typo_echo.txt\n")
            wait_for_serial_count(serial_log, "AUDIT eshell_recovery_auto_apply=",
                                  auto_apply_count + 1, timeout)
            auto_apply_count = serial_count(serial_log,
                                            "AUDIT eshell_recovery_auto_apply=")
            monitor.send_text("cd /hme/user\n")
            wait_for_serial_count(serial_log, "AUDIT eshell_recovery_auto_apply=",
                                  auto_apply_count + 1, timeout)
            monitor.send_text("echo recovered > /home/user/recovery_path.txt\n")
            time.sleep(0.5)

            print("=== AGENT FUSION QEMU SMOKE DONE ===")
            print(f"Artifacts: {serial_log}, {qemu_log}, {qemu_stderr_log}")
            monitor.command("quit", pause=0.1)
            qemu.wait(timeout=5)
            image = fsboot.read_bytes()
            workspace_path = f"/var/agent/memory/{agent_hash_path('/home/user'):08x}.md"
            session_index = read_file_by_path(image, "/var/agent/sessions/index.txt")
            workspace_text = read_file_by_path(image, workspace_path)
            typo_echo_text = read_file_by_path(image, "/home/user/typo_echo.txt")
            recovery_path_text = read_file_by_path(image, "/home/user/recovery_path.txt")
            session_ids = [line for line in session_index.splitlines() if line]
            if len(session_ids) < 1:
                raise AssertionError(f"session index count mismatch: {session_ids!r}")
            if "fusion-note" not in workspace_text:
                raise AssertionError("workspace memory file does not contain fusion-note")
            if typo_echo_text != "fused\n":
                raise AssertionError(f"typo_echo.txt mismatch: {typo_echo_text!r}")
            if recovery_path_text != "recovered\n":
                raise AssertionError(f"recovery_path.txt mismatch: {recovery_path_text!r}")
            return 0
        except Exception as exc:
            print(f"agent fusion smoke failed: {exc}", file=sys.stderr)
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
