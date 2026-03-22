#!/usr/bin/env python3
"""shell の alias/history/展開を boot 時 script で QEMU smoke する。"""

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
    "AUDIT rcS_usability_begin",
    "AUDIT usability_smoke_script_begin",
    "AUDIT usability_eshell_status=0",
    "AUDIT usability_smoke_script_done",
    "AUDIT rcS_usability_status=0",
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
    raise AssertionError("shell usability markers did not become ready in time")


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


def read_user_entries(image: bytes) -> dict[str, tuple[int, int]]:
    root_entries = read_dir_entries(image, SODEX_ROOT_INO)
    home_entry = root_entries.get("home")
    if home_entry is None:
        raise AssertionError("/home was not found")

    home_entries = read_dir_entries(image, home_entry[0])
    user_entry = home_entries.get("user")
    if user_entry is None:
        raise AssertionError("/home/user was not found")

    return read_dir_entries(image, user_entry[0])


def read_user_file(fsboot: pathlib.Path, name: str) -> str:
    image = fsboot.read_bytes()
    user_entries = read_user_entries(image)
    file_entry = user_entries.get(name)
    if file_entry is None:
        raise AssertionError(f"{name} was not created")
    return read_file(image, file_entry[0]).decode("ascii", errors="replace")


def assert_guest_state(fsboot: pathlib.Path) -> None:
    alias_text = read_user_file(fsboot, "alias.txt")
    type_alias_text = read_user_file(fsboot, "type_alias.txt")
    type_builtin_text = read_user_file(fsboot, "type_builtin.txt")
    command_ls_text = read_user_file(fsboot, "command_ls.txt")
    home_text = read_user_file(fsboot, "home.txt")
    glob_text = read_user_file(fsboot, "glob.txt")
    history_replay_text = read_user_file(fsboot, "history_replay.txt")
    history_prefix_text = read_user_file(fsboot, "history_prefix.txt")
    history_list_text = read_user_file(fsboot, "history_list.txt")
    eshell_alias_text = read_user_file(fsboot, "eshell_alias.txt")
    eshell_command_text = read_user_file(fsboot, "eshell_command_alias.txt")
    eshell_home_text = read_user_file(fsboot, "eshell_home.txt")
    eshell_glob_text = read_user_file(fsboot, "eshell_glob.txt")

    if alias_text != "alias_ok\n":
        raise AssertionError(f"alias.txt mismatch: {alias_text!r}")
    if type_alias_text != "hi is alias for echo alias_ok\n":
        raise AssertionError(f"type_alias.txt mismatch: {type_alias_text!r}")
    if type_builtin_text != "echo is shell builtin\n":
        raise AssertionError(f"type_builtin.txt mismatch: {type_builtin_text!r}")
    if command_ls_text != "/usr/bin/ls\n":
        raise AssertionError(f"command_ls.txt mismatch: {command_ls_text!r}")
    if home_text != "/home/user\n":
        raise AssertionError(f"home.txt mismatch: {home_text!r}")
    if glob_text != "/home/user/globdir/a.txt /home/user/globdir/b.txt\n":
        raise AssertionError(f"glob.txt mismatch: {glob_text!r}")
    if history_replay_text != "hist\nhist\n":
        raise AssertionError(f"history_replay.txt mismatch: {history_replay_text!r}")
    if history_prefix_text != "prefix\nprefix\n":
        raise AssertionError(f"history_prefix.txt mismatch: {history_prefix_text!r}")
    if "echo hist >> /home/user/history_replay.txt" not in history_list_text:
        raise AssertionError(f"history_list.txt missing replay entry: {history_list_text!r}")
    if "echo prefix >> /home/user/history_prefix.txt" not in history_list_text:
        raise AssertionError(f"history_list.txt missing prefix entry: {history_list_text!r}")
    if eshell_alias_text != "eshell_alias_ok\n":
        raise AssertionError(f"eshell_alias.txt mismatch: {eshell_alias_text!r}")
    if eshell_command_text != "alias greet='echo eshell_alias_ok'\n":
        raise AssertionError(f"eshell_command_alias.txt mismatch: {eshell_command_text!r}")
    if eshell_home_text != "/home/user\n":
        raise AssertionError(f"eshell_home.txt mismatch: {eshell_home_text!r}")
    if eshell_glob_text != "/home/user/globdir/a.txt /home/user/globdir/b.txt\n":
        raise AssertionError(f"eshell_glob.txt mismatch: {eshell_glob_text!r}")


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def build_temp_rootfs(repo_root: pathlib.Path, logdir: pathlib.Path) -> pathlib.Path:
    overlay_src = repo_root / "src" / "rootfs-overlay"
    overlay_dir = logdir / "shell_usability_rootfs_overlay"
    temp_fsboot = logdir / "shell_usability_fsboot.bin"
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
        overlay_dir / "home" / "user" / "usability_smoke.sh",
        """echo AUDIT usability_smoke_script_begin
mkdir /home/user/globdir
echo a > /home/user/globdir/a.txt
echo b > /home/user/globdir/b.txt
echo h > /home/user/globdir/.hidden.txt
alias hi='echo alias_ok'
hi > /home/user/alias.txt
type hi > /home/user/type_alias.txt
type echo > /home/user/type_builtin.txt
command -v ls > /home/user/command_ls.txt
echo ~ > /home/user/home.txt
echo /home/user/globdir/*.txt > /home/user/glob.txt
unalias hi
/usr/bin/eshell < /home/user/usability_eshell_input.txt
eshell_status=$?
echo AUDIT usability_eshell_status=$eshell_status
if [ "$eshell_status" != "0" ]; then
  exit $eshell_status
fi
echo AUDIT usability_smoke_script_done
""",
    )
    write_text(
        overlay_dir / "home" / "user" / "usability_eshell_input.txt",
        """echo hist >> /home/user/history_replay.txt
!!
echo prefix >> /home/user/history_prefix.txt
!echo prefix
history > /home/user/history_list.txt
alias greet='echo eshell_alias_ok'
greet > /home/user/eshell_alias.txt
command -v greet > /home/user/eshell_command_alias.txt
echo ~ > /home/user/eshell_home.txt
echo /home/user/globdir/*.txt > /home/user/eshell_glob.txt
""",
    )
    write_text(
        overlay_dir / "etc" / "init.d" / "rcS",
        """echo AUDIT rcS_usability_begin
/usr/bin/sh /home/user/usability_smoke.sh
status=$?
echo AUDIT rcS_usability_status=$status
echo AUDIT rcS_done
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
    )
    return temp_fsboot


def launch_qemu(repo_root: pathlib.Path, fsboot: pathlib.Path, logdir: pathlib.Path
                ) -> tuple[subprocess.Popen[bytes], pathlib.Path, pathlib.Path, pathlib.Path, TextIO]:
    serial_log = logdir / "shell_usability_serial.log"
    qemu_log = logdir / "shell_usability_qemu_debug.log"
    qemu_stderr_log = logdir / "shell_usability_qemu_stderr.log"
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))
    qemu_bin = os.environ.get("QEMU", "qemu-system-i386")
    mem_mb = get_qemu_memory_mb()
    qemu_cmd = [
        "script",
        "-q",
        "/dev/null",
        qemu_bin,
        "-drive",
        f"file={fsboot},format=raw,if=ide",
        "-m",
        str(mem_mb),
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

    for path in (serial_log, qemu_log, qemu_stderr_log):
        if path.exists():
            path.unlink()

    qemu_stderr_fp = qemu_stderr_log.open("w", encoding="utf-8")
    qemu_proc = subprocess.Popen(
        qemu_cmd,
        cwd=repo_root,
        stdout=subprocess.DEVNULL,
        stderr=qemu_stderr_fp,
    )
    return qemu_proc, serial_log, qemu_log, qemu_stderr_log, qemu_stderr_fp


def shutdown_qemu(qemu_proc: subprocess.Popen[bytes]) -> None:
    if qemu_proc.poll() is not None:
        return
    qemu_proc.terminate()
    try:
        qemu_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu_proc.kill()
        qemu_proc.wait(timeout=5)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: run_qemu_shell_usability_smoke.py <fsboot.bin> <logdir>")
        return 2

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    logdir = pathlib.Path(argv[2]).resolve()
    logdir.mkdir(parents=True, exist_ok=True)
    temp_fsboot = build_temp_rootfs(repo_root, logdir)
    qemu_proc = None
    qemu_stderr_fp: TextIO | None = None
    timeout = int(os.environ.get("SODEX_QEMU_TIMEOUT", DEFAULT_TIMEOUT))

    try:
        qemu_proc, serial_log, qemu_log, qemu_stderr_log, qemu_stderr_fp = launch_qemu(
            repo_root, temp_fsboot, logdir
        )
        deadline = time.time() + timeout
        wait_until_ready(deadline, serial_log, qemu_log, qemu_proc, qemu_stderr_log)
        assert_no_guest_failure(serial_log, qemu_log)
        assert_guest_state(temp_fsboot)
    except Exception:
        if qemu_proc is not None:
            shutdown_qemu(qemu_proc)
        if qemu_stderr_fp is not None:
            qemu_stderr_fp.close()
        dump_file(logdir / "shell_usability_serial.log")
        dump_file(logdir / "shell_usability_qemu_debug.log")
        dump_file(logdir / "shell_usability_qemu_stderr.log")
        raise

    if qemu_proc is not None:
        shutdown_qemu(qemu_proc)
    if qemu_stderr_fp is not None:
        qemu_stderr_fp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
