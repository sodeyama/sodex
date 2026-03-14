#!/usr/bin/env python3
"""Rich terminal の QEMU smoke と screenshot 比較を行う。"""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import socket
import subprocess
import sys
import time

DEFAULT_TIMEOUT = 45
PROMPT_ROWS = 48
LONG_OUTPUT_ENTERS = 64


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


def crop_sha256(ppm_path: pathlib.Path, x: int, y: int, width: int, height: int) -> str:
    image_width, image_height, pixels = read_ppm(ppm_path)
    if x + width > image_width or y + height > image_height:
        raise ValueError("crop is outside image bounds")
    row_stride = image_width * 3
    crop = bytearray()
    for row in range(y, y + height):
        start = row * row_stride + x * 3
        crop.extend(pixels[start:start + width * 3])
    return hashlib.sha256(crop).hexdigest()


class QemuMonitor:
    def __init__(self, sock_path: pathlib.Path) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(1.0)
        self.sock.connect(str(sock_path))
        time.sleep(0.2)
        try:
            self.sock.recv(4096)
        except OSError:
            pass

    def command(self, text: str, pause: float = 0.2) -> str:
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

    def send_enter(self, count: int, delay: float = 0.03) -> None:
        for _ in range(count):
            self.command("sendkey ret", pause=0.05)
            time.sleep(delay)

    def close(self) -> None:
        self.sock.close()


def wait_for_path(path: pathlib.Path, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for {path}")


def wait_for_prompt(monitor: QemuMonitor, ppm_path: pathlib.Path, reference: dict[str, int | str],
                    timeout: float) -> float:
    deadline = time.time() + timeout
    while time.time() < deadline:
        monitor.command(f"screendump {ppm_path}", pause=0.4)
        digest = crop_sha256(ppm_path,
                             int(reference["x"]),
                             int(reference["y"]),
                             int(reference["width"]),
                             int(reference["height"]))
        if digest == reference["sha256"]:
            return time.time()
        time.sleep(0.2)
    raise TimeoutError("prompt screenshot did not match reference")


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


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_terminal_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    reference = json.loads((repo_root / "src/test/data/term_prompt_reference.json").read_text())

    logdir.mkdir(parents=True, exist_ok=True)
    serial_log = logdir / "term_smoke_serial.log"
    qemu_log = logdir / "term_smoke_qemu.log"
    monitor_sock = logdir / "term_smoke_monitor.sock"
    boot_ppm = logdir / "term_smoke_boot.ppm"
    scroll_ppm = logdir / "term_smoke_scroll.ppm"
    long_ppm = logdir / "term_smoke_long.ppm"

    for path in (serial_log, qemu_log, monitor_sock, boot_ppm, scroll_ppm, long_ppm):
        if path.exists():
            path.unlink()

    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_args = [
        qemu_bin,
        "-drive",
        f"file={fsboot},format=raw,if=ide",
        "-m",
        "128",
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

    start = time.time()
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

        prompt_seen = wait_for_prompt(monitor, boot_ppm, reference, timeout)
        full_line = wait_for_metric(serial_log, "full_redraw", 5)

        monitor.send_enter(PROMPT_ROWS)
        scroll_line = wait_for_metric(serial_log, "scroll", 5)
        monitor.command(f"screendump {scroll_ppm}", pause=0.4)

        monitor.send_enter(LONG_OUTPUT_ENTERS)
        long_line = wait_for_metric(serial_log, "long_output", 5)
        monitor.command(f"screendump {long_ppm}", pause=0.4)

        print("")
        print("=== RICH TERMINAL QEMU SMOKE DONE ===")
        print(f"boot_prompt_ms={int((prompt_seen - start) * 1000)}")
        print(full_line)
        print(scroll_line)
        print(long_line)
        print(f"Artifacts: {boot_ppm}, {scroll_ppm}, {long_ppm}, {serial_log}, {qemu_log}")

        monitor.command("quit", pause=0.1)
        qemu.wait(timeout=5)
        return 0
    except Exception as exc:
        print(f"rich terminal smoke failed: {exc}", file=sys.stderr)
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
