#!/usr/bin/env python3
"""shell の長い top-level script list を QEMU で確認する。"""

from __future__ import annotations

import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time
from typing import TextIO

from qemu_config import get_qemu_memory_mb

BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2
DEFAULT_TIMEOUT = 45
FAILURE_MARKERS = ("PF:", "PageFault", "General Protection Exception")
READY_MARKERS = (
    "AUDIT rcS_long_begin",
    "AUDIT long_list_begin",
    "AUDIT long_list_done",
    "AUDIT long_list_status=0",
    "AUDIT init_rc_done ok",
)


def dump_file(path: pathlib.Path) -> None:
    if not path.exists():
        return
    data = path.read_text(errors="replace")
    if data:
        print(data, end="")


def read_text(path: pathlib.Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(errors="replace")


def assert_no_guest_failure(serial_log: pathlib.Path, qemu_log: pathlib.Path) -> None:
    serial_text = read_text(serial_log)
    qemu_text = read_text(qemu_log)

    for marker in FAILURE_MARKERS:
        if marker in serial_text or marker in qemu_text:
            raise AssertionError(f"guest failure marker detected: {marker}")


def assert_qemu_running(qemu_proc: subprocess.Popen[bytes],
                        qemu_stderr_log: pathlib.Path) -> None:
    if qemu_proc.poll() is None:
        return

    stderr_text = read_text(qemu_stderr_log)
    raise AssertionError(
        f"qemu exited early with code {qemu_proc.returncode}:\n{stderr_text}"
    )


def wait_until_ready(deadline: float, serial_log: pathlib.Path,
                     qemu_log: pathlib.Path,
                     qemu_proc: subprocess.Popen[bytes],
                     qemu_stderr_log: pathlib.Path) -> None:
    while time.time() < deadline:
        assert_qemu_running(qemu_proc, qemu_stderr_log)
        assert_no_guest_failure(serial_log, qemu_log)
        serial_text = read_text(serial_log)
        if all(marker in serial_text for marker in READY_MARKERS):
            return
        time.sleep(0.5)
    raise AssertionError("shell long-list markers did not become ready in time")


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


def lookup_path(image: bytes, path: str) -> tuple[int, int] | None:
    current = SODEX_ROOT_INO

    if path == "/":
        return current, 2
    for part in [p for p in path.split("/") if p]:
        entries = read_dir_entries(image, current)
        if part not in entries:
            return None
        current, file_type = entries[part]
    return current, file_type


def read_path_text(fsboot: pathlib.Path, path: str) -> str:
    image = fsboot.read_bytes()
    entry = lookup_path(image, path)

    if entry is None or entry[1] != 1:
        raise AssertionError(f"{path} was not created")
    return read_file(image, entry[0]).decode("ascii", errors="replace")


def assert_guest_state(fsboot: pathlib.Path) -> None:
    long_list_text = read_path_text(fsboot, "/home/user/long_list.txt")

    if long_list_text != "59\n":
        raise AssertionError(f"/home/user/long_list.txt mismatch: {long_list_text!r}")


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def build_long_list_script(count: int) -> str:
    lines = ["echo AUDIT long_list_begin"]

    for i in range(count):
        lines.append(f"LONG_RESULT={i}")
    lines.append("echo $LONG_RESULT > /home/user/long_list.txt")
    lines.append("echo AUDIT long_list_done")
    return "\n".join(lines) + "\n"


def build_temp_rootfs(repo_root: pathlib.Path, logdir: pathlib.Path) -> pathlib.Path:
    overlay_src = repo_root / "src" / "rootfs-overlay"
    overlay_dir = logdir / "shell_long_list_rootfs_overlay"
    temp_fsboot = logdir / "shell_long_list_fsboot.bin"
    kmkfs = repo_root / "build" / "tools" / "kmkfs"
    boota = repo_root / "build" / "bin" / "boota.bin"
    bootm = repo_root / "build" / "bin" / "bootm.bin"
    kernel = repo_root / "build" / "bin" / "kernel.bin"
    init = repo_root / "src" / "init" / "bin" / "ptest"
    init2 = repo_root / "src" / "init" / "bin" / "ptest2"

    if overlay_dir.exists():
        shutil.rmtree(overlay_dir)
    shutil.copytree(overlay_src, overlay_dir)
    if temp_fsboot.exists():
        temp_fsboot.unlink()

    write_text(
        overlay_dir / "home" / "user" / "long_list_smoke.sh",
        build_long_list_script(60),
    )
    write_text(
        overlay_dir / "etc" / "init.d" / "rcS",
        """echo AUDIT rcS_long_begin
/usr/bin/sh /home/user/long_list_smoke.sh
status=$?
echo AUDIT long_list_status=$status
exit $status
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


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_shell_long_list_smoke.py <fsboot> <logdir>",
              file=sys.stderr)
        return 2

    _ = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    temp_fsboot = build_temp_rootfs(repo_root, logdir)

    serial_log = logdir / "shell_long_list_serial.log"
    qemu_log = logdir / "shell_long_list_qemu_debug.log"
    qemu_stderr_log = logdir / "shell_long_list_qemu_stderr.log"

    for path in (serial_log, qemu_log, qemu_stderr_log):
        if path.exists():
            path.unlink()

    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    qemu_memory_mb = get_qemu_memory_mb()
    qemu_cmd = [
        "script",
        "-q",
        "/dev/null",
        qemu_bin,
        "-drive",
        f"file={temp_fsboot},format=raw,if=ide",
        "-m",
        str(qemu_memory_mb),
        "-nographic",
        "-serial",
        f"file:{serial_log}",
        "-D",
        str(qemu_log),
        "-netdev",
        "user,id=net0",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_proc = None
    qemu_stderr_fp: TextIO | None = None
    try:
        qemu_stderr_fp = qemu_stderr_log.open("w", encoding="utf-8")
        qemu_proc = subprocess.Popen(
            qemu_cmd,
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=qemu_stderr_fp,
        )
        wait_until_ready(time.time() + timeout, serial_log, qemu_log,
                         qemu_proc, qemu_stderr_log)
        assert_guest_state(temp_fsboot)
        print("=== SHELL LONG LIST QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {temp_fsboot}")
        return 0
    except Exception:
        dump_file(serial_log)
        dump_file(qemu_log)
        dump_file(qemu_stderr_log)
        raise
    finally:
        if qemu_proc is not None:
            qemu_proc.terminate()
            try:
                qemu_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu_proc.kill()
                qemu_proc.wait()
        if qemu_stderr_fp is not None:
            qemu_stderr_fp.close()


if __name__ == "__main__":
    raise SystemExit(main())
