#!/usr/bin/env python3
"""terminal の scrollback 復帰を QEMU で smoke する。"""

from __future__ import annotations

import hashlib
import os
import pathlib
import socket
import subprocess
import sys
import time

from qemu_config import get_qemu_memory_mb

DEFAULT_TIMEOUT = 45
PROMPT_SCROLL_ENTERS = 64
ROUNDTRIP_PAGE_COUNT = 2


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


def image_sha256(ppm_path: pathlib.Path) -> str:
    width, height, pixels = read_ppm(ppm_path)
    digest = hashlib.sha256()
    digest.update(str(width).encode("ascii"))
    digest.update(b"x")
    digest.update(str(height).encode("ascii"))
    digest.update(b":")
    digest.update(pixels)
    return digest.hexdigest()


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

    def send_enter(self, count: int, delay: float = 0.03) -> None:
        for _ in range(count):
            self.send_key("ret", delay=delay)

    def close(self) -> None:
        self.sock.close()


def qemu_key_for_char(ch: str) -> str:
    keymap = {
        " ": "spc",
        "\n": "ret",
        "/": "slash",
        ".": "dot",
        "-": "minus",
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


def wait_for_serial_text(serial_log: pathlib.Path, text: str, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")
            if text in serial_text:
                return
        time.sleep(0.2)
    raise TimeoutError(f"serial text not found: {text}")


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


def wait_for_term_metric(serial_log: pathlib.Path, point: str, timeout: float) -> str:
    deadline = time.time() + timeout
    marker = f"TERM_METRIC point={point}"
    rescue_marker = "AUDIT init_enter_rescue"
    term_marker = "AUDIT term_main_loop_enter"

    while time.time() < deadline:
        if serial_log.exists():
            text = serial_log.read_text(errors="replace")
            if marker in text:
                for line in text.splitlines():
                    if marker in line:
                        return line
            if rescue_marker in text and term_marker not in text:
                raise AssertionError("term が起動する前に rescue shell へフォールバックしました")
        time.sleep(0.2)
    raise TimeoutError(f"metric not found: {point}")


def wait_for_idle(delay: float) -> None:
    deadline = time.time() + delay
    while time.time() < deadline:
        time.sleep(0.05)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_terminal_scrollback_smoke.py <fsboot> <logdir>",
              file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    monitor_sock = logdir / f"term_scrollback_monitor_{os.getpid()}.sock"
    serial_log = logdir / f"term_scrollback_serial_{os.getpid()}.log"
    qemu_log = logdir / f"term_scrollback_qemu_{os.getpid()}.log"
    baseline_ppm = logdir / f"term_scrollback_baseline_{os.getpid()}.ppm"
    scrolled_ppm = logdir / f"term_scrollback_scrolled_{os.getpid()}.ppm"
    restored_ppm = logdir / f"term_scrollback_restored_{os.getpid()}.ppm"

    for path in (monitor_sock, serial_log, qemu_log,
                 baseline_ppm, scrolled_ppm, restored_ppm):
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
        wait_for_term_metric(serial_log, "full_redraw", timeout)
        wait_for_serial_text(serial_log, "AUDIT eshell_ready", timeout)
        wait_for_idle(2.0)

        monitor.send_enter(PROMPT_SCROLL_ENTERS)
        wait_for_idle(1.2)

        for command in ("ls\n", "ps\n", "pwd\n"):
            monitor.send_text(command)
            wait_for_idle(1.0)

        monitor.command(f"screendump {baseline_ppm}", pause=0.3)
        baseline_digest = image_sha256(baseline_ppm)

        for _ in range(ROUNDTRIP_PAGE_COUNT):
            monitor.send_key("pgup", delay=0.08)
        wait_for_idle(0.5)
        monitor.command(f"screendump {scrolled_ppm}", pause=0.3)
        scrolled_digest = image_sha256(scrolled_ppm)
        if scrolled_digest == baseline_digest:
            raise AssertionError("PageUp did not change the terminal viewport")

        for _ in range(ROUNDTRIP_PAGE_COUNT):
            monitor.send_key("pgdn", delay=0.08)
        wait_for_idle(0.6)
        monitor.command(f"screendump {restored_ppm}", pause=0.3)
        restored_digest = image_sha256(restored_ppm)
        if restored_digest != baseline_digest:
            raise AssertionError("scrollback roundtrip did not restore the bottom screen")

        serial_text = serial_log.read_text(errors="replace")
        if "PF:" in serial_text or "PageFault" in serial_text:
            raise AssertionError("page fault was detected during scrollback smoke")

        print("=== TERMINAL SCROLLBACK QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {baseline_ppm}, "
              f"{scrolled_ppm}, {restored_ppm}")
        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        return 0
    except Exception as exc:
        print(f"terminal scrollback smoke failed: {exc}", file=sys.stderr)
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
