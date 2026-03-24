#!/usr/bin/env python3
"""agent-term 欠落時の init fallback を QEMU で smoke する。"""

from __future__ import annotations

import os
import pathlib
import shutil
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


def tail_text(path: pathlib.Path, limit: int = 400) -> str:
    if not path.exists():
        return ""
    text = path.read_text(errors="replace")
    if len(text) <= limit:
        return text
    return text[-limit:]


def wait_for_serial_text(serial_log: pathlib.Path, text: str, timeout: float,
                         process: subprocess.Popen[bytes],
                         error_log: pathlib.Path) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if serial_log.exists():
            serial_text = serial_log.read_text(errors="replace")
            if text in serial_text:
                return serial_text
        if process.poll() is not None:
            detail = tail_text(error_log)
            raise RuntimeError(
                f"qemu exited before serial text appeared (code={process.returncode}): {detail}"
            )
        time.sleep(0.2)
    raise TimeoutError(f"serial text not found: {text}")


def inode_bytes(image: bytes, ino: int) -> bytes:
    start = P_INODE_BLOCK + (ino - 1) * INODE_SIZE
    return image[start:start + INODE_SIZE]


def inode_size(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 4)[0]


def inode_blocks(image: bytes, ino: int) -> list[int]:
    inode = inode_bytes(image, ino)
    return [struct.unpack_from("<I", inode, 40 + index * 4)[0] for index in range(12)]


def lookup_inode(image: bytes, path: str) -> int:
    current = SODEX_ROOT_INO

    if path == "/":
        return current

    for part in [segment for segment in path.split("/") if segment]:
        remaining = inode_size(image, current)
        found = 0

        for block in inode_blocks(image, current):
          offset = 0

          if block == 0 or remaining <= 0:
              break
          block_start = block * BLOCK_SIZE
          limit = min(BLOCK_SIZE, remaining)
          while offset + 8 <= limit:
              inode_num = struct.unpack_from("<I", image, block_start + offset)[0]
              rec_len = struct.unpack_from("<H", image, block_start + offset + 4)[0]
              name_len = image[block_start + offset + 6]
              if inode_num == 0 or rec_len == 0:
                  break
              name = image[block_start + offset + 8:
                           block_start + offset + 8 + name_len].decode("ascii", errors="ignore")
              if name == part:
                  found = inode_num
                  break
              offset += rec_len
          if found != 0:
              break
          remaining -= BLOCK_SIZE
        if found == 0:
            raise AssertionError(f"path not found in image: {path}")
        current = found

    return current


def rename_dir_entry(image: bytearray, dir_path: str, old_name: str, new_name: str) -> None:
    dir_ino = lookup_inode(bytes(image), dir_path)
    remaining = inode_size(bytes(image), dir_ino)

    if len(old_name) != len(new_name):
        raise AssertionError("replacement name must keep the same length")

    for block in inode_blocks(bytes(image), dir_ino):
        offset = 0

        if block == 0 or remaining <= 0:
            break
        block_start = block * BLOCK_SIZE
        limit = min(BLOCK_SIZE, remaining)
        while offset + 8 <= limit:
            inode_num = struct.unpack_from("<I", image, block_start + offset)[0]
            rec_len = struct.unpack_from("<H", image, block_start + offset + 4)[0]
            name_len = image[block_start + offset + 6]
            if inode_num == 0 or rec_len == 0:
                break
            name_start = block_start + offset + 8
            name = image[name_start:name_start + name_len].decode("ascii", errors="ignore")
            if name == old_name:
                image[name_start:name_start + name_len] = new_name.encode("ascii")
                return
            offset += rec_len
        remaining -= BLOCK_SIZE

    raise AssertionError(f"directory entry not found: {dir_path}/{old_name}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_agent_fallback_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_memory_mb = get_qemu_memory_mb()
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")

    logdir.mkdir(parents=True, exist_ok=True)
    mutated_fsboot = logdir / f"agent_fallback_fsboot_{os.getpid()}.bin"
    serial_log = logdir / f"agent_fallback_serial_{os.getpid()}.log"
    qemu_log = logdir / f"agent_fallback_qemu_{os.getpid()}.log"
    qemu_stderr_log = logdir / f"agent_fallback_qemu_stderr_{os.getpid()}.log"

    shutil.copyfile(fsboot, mutated_fsboot)
    image = bytearray(mutated_fsboot.read_bytes())
    rename_dir_entry(image, "/usr/bin", "agent-term", "agent_term")
    mutated_fsboot.write_bytes(image)

    for path in (serial_log, qemu_log, qemu_stderr_log):
        if path.exists():
            path.unlink()

    qemu_args = [
        qemu_bin,
        "-drive",
        f"file={mutated_fsboot},format=raw,if=ide",
        "-m",
        str(qemu_memory_mb),
        "-display",
        "none",
        "-no-reboot",
        "-serial",
        f"file:{serial_log}",
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
        try:
            wait_for_serial_text(serial_log, "AUDIT init_boot_profile_terminal=1", timeout,
                                 qemu, qemu_stderr_log)
            wait_for_serial_text(serial_log, "AUDIT init_spawn_failed=/usr/bin/agent-term", timeout,
                                 qemu, qemu_stderr_log)
            wait_for_serial_text(serial_log, "AUDIT init_spawn_fallback_try=/usr/bin/term", timeout,
                                 qemu, qemu_stderr_log)
            wait_for_serial_text(serial_log, "AUDIT init_spawned=/usr/bin/term", timeout,
                                 qemu, qemu_stderr_log)
            print("=== AGENT FALLBACK QEMU SMOKE DONE ===")
            print(f"Artifacts: {serial_log}, {qemu_log}, {qemu_stderr_log}, {mutated_fsboot}")
            return 0
        except Exception as exc:
            print(f"agent fallback smoke failed: {exc}", file=sys.stderr)
            return 1
        finally:
            if qemu.poll() is None:
                qemu.terminate()
                try:
                    qemu.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    qemu.kill()
                    qemu.wait()


if __name__ == "__main__":
    raise SystemExit(main())
