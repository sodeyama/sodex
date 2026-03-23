#!/usr/bin/env python3
"""sxi の最小 bring-up を QEMU 上で確認する。"""

from __future__ import annotations

import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from typing import TextIO

from qemu_config import get_qemu_memory_mb

BLOCK_SIZE = 4096
INODE_SIZE = 128
P_INODE_BLOCK = 16384
SODEX_ROOT_INO = 2
DEFAULT_TIMEOUT = 60
HOST_SERVER_PORT = 18081
GUEST_SERVER_PORT = 18082
FAILURE_MARKERS = ("PF:", "PageFault", "General Protection Exception")
READY_MARKERS = (
    "AUDIT sxi_smoke_begin",
    "AUDIT sxi_bad_status=2",
    "AUDIT sxi_hello_check_status=0",
    "AUDIT sxi_import_check_status=0",
    "AUDIT sxi_stdlib_check_status=0",
    "AUDIT sxi_stdin_check_status=0",
    "AUDIT sxi_grep_check_status=0",
    "AUDIT sxi_interop_check_status=0",
    "AUDIT sxi_spawn_check_status=0",
    "AUDIT sxi_pipe_check_status=0",
    "AUDIT sxi_fork_check_status=0",
    "AUDIT sxi_bytes_check_status=0",
    "AUDIT sxi_list_map_check_status=0",
    "AUDIT sxi_literal_check_status=0",
    "AUDIT sxi_net_client_check_status=0",
    "AUDIT sxi_net_server_check_status=0",
    "AUDIT sxi_checks_status=0",
    "AUDIT sxi_hello_run_status=0",
    "AUDIT sxi_operators_run_status=0",
    "AUDIT sxi_while_run_status=0",
    "AUDIT sxi_for_run_status=0",
    "AUDIT sxi_break_run_status=0",
    "AUDIT sxi_scope_run_status=0",
    "AUDIT sxi_recursive_run_status=0",
    "AUDIT sxi_import_run_status=0",
    "AUDIT sxi_stdlib_run_status=0",
    "AUDIT sxi_json_run_status=0",
    "AUDIT sxi_copy_run_status=0",
    "AUDIT sxi_proc_run_status=0",
    "AUDIT sxi_stdin_run_status=0",
    "AUDIT sxi_grep_run_status=0",
    "AUDIT sxi_interop_run_status=0",
    "AUDIT sxi_spawn_run_status=0",
    "AUDIT sxi_pipe_run_status=0",
    "AUDIT sxi_fork_run_status=0",
    "AUDIT sxi_bytes_run_status=0",
    "AUDIT sxi_list_map_run_status=0",
    "AUDIT sxi_literal_run_status=0",
    "AUDIT sxi_net_client_run_status=0",
    "AUDIT sxi_net_server_run_status=0",
    "AUDIT sxi_inline_status=0",
    "AUDIT sxi_smoke_done",
    "AUDIT sxi_runs_status=0",
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
    raise AssertionError("sxi smoke markers did not become ready in time")


def stop_qemu(qemu_proc: subprocess.Popen[bytes] | None) -> None:
    if qemu_proc is None:
        return
    qemu_proc.terminate()
    try:
        qemu_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu_proc.kill()
        qemu_proc.wait()


def inode_bytes(image: bytes, ino: int) -> bytes:
    start = P_INODE_BLOCK + (ino - 1) * INODE_SIZE
    return image[start:start + INODE_SIZE]


def inode_size(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 4)[0]


def inode_block0(image: bytes, ino: int) -> int:
    return struct.unpack_from("<I", inode_bytes(image, ino), 40)[0]


def inode_block(image: bytes, ino: int, index: int) -> int:
    if index < 0:
        return 0
    return struct.unpack_from("<I", inode_bytes(image, ino), 40 + index * 4)[0]


def read_file(image: bytes, ino: int) -> bytes:
    size = inode_size(image, ino)
    block0 = inode_block0(image, ino)
    start = block0 * BLOCK_SIZE
    return image[start:start + size]


def read_dir_entries(image: bytes, ino: int) -> dict[str, tuple[int, int]]:
    size = inode_size(image, ino)
    data = bytearray()
    block_index = 0
    offset = 0
    result: dict[str, tuple[int, int]] = {}

    while len(data) < size and block_index < 12:
        block = inode_block(image, ino, block_index)
        if block == 0:
            break
        data.extend(image[block * BLOCK_SIZE:(block + 1) * BLOCK_SIZE])
        block_index += 1

    while offset + 8 <= len(data) and offset < size:
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
    expected_outputs = {
        "/home/user/sxi_hello_out.txt": "Hello, sodex sx\n",
        "/home/user/sxi_operators_out.txt": "22\ntrue\n",
        "/home/user/sxi_while_out.txt": "10\n",
        "/home/user/sxi_for_out.txt": "15\n",
        "/home/user/sxi_break_out.txt": "9\n",
        "/home/user/sxi_scope_out.txt": "inner\nouter\n",
        "/home/user/sxi_recursive_out.txt": "21\n",
        "/home/user/sxi_import_out.txt": "IMPORT_OK\n",
        "/home/user/sxi_stdlib_out.txt": "STDLIB-OK\n",
        "/home/user/sxi_json_out.txt": "sodex-json\ntrue\n7\ntrue\n",
        "/home/user/sxi_copy_out.txt": "true\nsample-from-rootfs\n\nCOPIED_BY_SX\n",
        "/home/user/sxi_proc_out.txt": "sample-from-rootfs\ntrue\n",
        "/home/user/sxi_stdin_out.txt": "stdin-head|stdin-tail\n",
        "/home/user/sxi_grep_out.txt": "alpha-one\nalpha-three\n",
        "/home/user/sxi_interop_out.txt":
            "3\n/home/user/sx-examples/argv_fs_time.sx\nbeta\ntrue\ntrue\ntrue\nalpha\ntrue\n",
        "/home/user/sxi_spawn_out.txt": "5\nfalse\nsample-from-rootfs\n",
        "/home/user/sxi_pipe_out.txt": "PIPE_OK\ntrue\n",
        "/home/user/sxi_fork_out.txt": "7\nFORK_OK\n",
        "/home/user/sxi_bytes_out.txt":
            "false\n8\nBYTES_OK\nfalse\nfs.read_text failed\nBYTES_OK\nMANUAL_ERR\ntrue\nsample-from-rootfs\n",
        "/home/user/sxi_list_map_out.txt": "3\ngamma\n3\nalpha\n2\nfalse\n",
        "/home/user/sxi_literal_out.txt": "alpha\n3\ntrue\n",
        "/home/user/sxi_net_client_out.txt": "true\nHOST_REPLY\n",
        "/home/user/sxi_net_server_out.txt": "true\nHOST_TO_GUEST\n",
        "/home/user/sxi_inline.txt": "INLINE_OK\n",
    }
    removed_paths = (
        "/tmp/sx-spawn.txt",
        "/tmp/sx-fork.txt",
        "/tmp/sx-interop",
        "/tmp/sx-bytes.bin",
    )

    for path, expected in expected_outputs.items():
        actual = read_path_text(fsboot, path)
        if actual != expected:
            raise AssertionError(f"{path} mismatch: {actual!r}")

    copied_text = read_path_text(fsboot, "/tmp/sx-copy.txt")
    if copied_text != "sample-from-rootfs\n\nCOPIED_BY_SX":
        raise AssertionError(f"/tmp/sx-copy.txt mismatch: {copied_text!r}")

    image = fsboot.read_bytes()
    for path in removed_paths:
        if lookup_path(image, path) is not None:
            raise AssertionError(f"{path} should have been removed")


def write_text(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def run_host_server(errors: list[BaseException]) -> None:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", HOST_SERVER_PORT))
            server.listen(1)
            server.settimeout(DEFAULT_TIMEOUT)
            conn, _addr = server.accept()
            with conn:
                payload = conn.recv(256).decode("ascii", errors="replace")
                if payload != "SX_CLIENT":
                    raise AssertionError(f"guest client payload mismatch: {payload!r}")
                conn.sendall(b"HOST_REPLY")
                time.sleep(0.5)
    except BaseException as exc:  # pragma: no cover - smoke helper
        errors.append(exc)


def drive_guest_server(errors: list[BaseException]) -> None:
    deadline = time.time() + DEFAULT_TIMEOUT

    try:
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", GUEST_SERVER_PORT), timeout=1.0) as conn:
                    conn.sendall(b"HOST_TO_GUEST")
                    payload = conn.recv(256).decode("ascii", errors="replace")
                    if payload != "SX_SERVER":
                        raise AssertionError(f"guest server payload mismatch: {payload!r}")
                    time.sleep(0.5)
                    return
            except OSError:
                time.sleep(0.2)
        raise AssertionError("guest server did not become reachable in time")
    except BaseException as exc:  # pragma: no cover - smoke helper
        errors.append(exc)


def build_temp_rootfs(repo_root: pathlib.Path, logdir: pathlib.Path) -> pathlib.Path:
    overlay_src = repo_root / "src" / "rootfs-overlay"
    overlay_dir = logdir / "sxi_rootfs_overlay"
    temp_fsboot = logdir / "sxi_fsboot.bin"
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
        overlay_dir / "home" / "user" / "sxi_smoke_checks.sh",
        """/usr/bin/sxi --check /home/user/bad.sx
echo AUDIT sxi_bad_status=$?
/usr/bin/sxi --check /home/user/sx-examples/hello.sx
echo AUDIT sxi_hello_check_status=$?
/usr/bin/sxi --check /home/user/import_main.sx
echo AUDIT sxi_import_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/stdlib_import.sx
echo AUDIT sxi_stdlib_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/stdin_echo.sx
echo AUDIT sxi_stdin_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/grep_lite.sx alpha
echo AUDIT sxi_grep_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/argv_fs_time.sx alpha beta
echo AUDIT sxi_interop_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/spawn_wait.sx
echo AUDIT sxi_spawn_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/pipe_roundtrip.sx
echo AUDIT sxi_pipe_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/fork_wait.sx
echo AUDIT sxi_fork_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/env_bytes_result.sx
echo AUDIT sxi_bytes_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/list_map.sx
echo AUDIT sxi_list_map_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/literal_branching.sx
echo AUDIT sxi_literal_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/net_client.sx
echo AUDIT sxi_net_client_check_status=$?
/usr/bin/sxi --check /home/user/sx-examples/net_server.sx
echo AUDIT sxi_net_server_check_status=$?
exit 0
""",
    )
    write_text(
        overlay_dir / "home" / "user" / "sxi_smoke_runs.sh",
        """/usr/bin/sxi /home/user/sx-examples/hello.sx > /home/user/sxi_hello_out.txt
echo AUDIT sxi_hello_run_status=$?
/usr/bin/sxi /home/user/sx-examples/operators.sx > /home/user/sxi_operators_out.txt
echo AUDIT sxi_operators_run_status=$?
/usr/bin/sxi /home/user/sx-examples/while_counter.sx > /home/user/sxi_while_out.txt
echo AUDIT sxi_while_run_status=$?
/usr/bin/sxi /home/user/sx-examples/for_counter.sx > /home/user/sxi_for_out.txt
echo AUDIT sxi_for_run_status=$?
/usr/bin/sxi /home/user/sx-examples/break_continue.sx > /home/user/sxi_break_out.txt
echo AUDIT sxi_break_run_status=$?
/usr/bin/sxi /home/user/sx-examples/scope_blocks.sx > /home/user/sxi_scope_out.txt
echo AUDIT sxi_scope_run_status=$?
/usr/bin/sxi /home/user/sx-examples/recursive_sum.sx > /home/user/sxi_recursive_out.txt
echo AUDIT sxi_recursive_run_status=$?
/usr/bin/sxi /home/user/sx-examples/import_main.sx > /home/user/sxi_import_out.txt
echo AUDIT sxi_import_run_status=$?
/usr/bin/sxi /home/user/sx-examples/stdlib_import.sx > /home/user/sxi_stdlib_out.txt
echo AUDIT sxi_stdlib_run_status=$?
/usr/bin/sxi /home/user/sx-examples/json_report.sx > /home/user/sxi_json_out.txt
echo AUDIT sxi_json_run_status=$?
/usr/bin/sxi /home/user/sx-examples/copy_file.sx > /home/user/sxi_copy_out.txt
echo AUDIT sxi_copy_run_status=$?
/usr/bin/sxi /home/user/sx-examples/proc_capture.sx > /home/user/sxi_proc_out.txt
echo AUDIT sxi_proc_run_status=$?
/usr/bin/sxi /home/user/sx-examples/stdin_echo.sx < /home/user/sx-examples/stdin_source.txt > /home/user/sxi_stdin_out.txt
echo AUDIT sxi_stdin_run_status=$?
/usr/bin/sxi /home/user/sx-examples/grep_lite.sx alpha < /home/user/sx-examples/grep_source.txt > /home/user/sxi_grep_out.txt
echo AUDIT sxi_grep_run_status=$?
/usr/bin/sxi /home/user/sx-examples/argv_fs_time.sx alpha beta > /home/user/sxi_interop_out.txt
echo AUDIT sxi_interop_run_status=$?
/usr/bin/sxi /home/user/sx-examples/spawn_wait.sx > /home/user/sxi_spawn_out.txt
echo AUDIT sxi_spawn_run_status=$?
/usr/bin/sxi /home/user/sx-examples/pipe_roundtrip.sx > /home/user/sxi_pipe_out.txt
echo AUDIT sxi_pipe_run_status=$?
/usr/bin/sxi /home/user/sx-examples/fork_wait.sx > /home/user/sxi_fork_out.txt
echo AUDIT sxi_fork_run_status=$?
/usr/bin/sxi /home/user/sx-examples/env_bytes_result.sx > /home/user/sxi_bytes_out.txt
echo AUDIT sxi_bytes_run_status=$?
/usr/bin/sxi /home/user/sx-examples/list_map.sx > /home/user/sxi_list_map_out.txt
echo AUDIT sxi_list_map_run_status=$?
/usr/bin/sxi /home/user/sx-examples/literal_branching.sx > /home/user/sxi_literal_out.txt
echo AUDIT sxi_literal_run_status=$?
/usr/bin/sxi /home/user/sx-examples/net_client.sx > /home/user/sxi_net_client_out.txt
echo AUDIT sxi_net_client_run_status=$?
/usr/bin/sxi /home/user/sx-examples/net_server.sx > /home/user/sxi_net_server_out.txt
echo AUDIT sxi_net_server_run_status=$?
/usr/bin/sxi -e 'io.println("INLINE_OK");' > /home/user/sxi_inline.txt
echo AUDIT sxi_inline_status=$?
echo AUDIT sxi_smoke_done
exit 0
""",
    )
    write_text(
        overlay_dir / "etc" / "init.d" / "rcS",
        """echo AUDIT sxi_smoke_begin
/usr/bin/sh /home/user/sxi_smoke_checks.sh
echo AUDIT sxi_checks_status=$?
/usr/bin/sh /home/user/sxi_smoke_runs.sh
echo AUDIT sxi_runs_status=$?
exit 0
""",
    )
    write_text(
        overlay_dir / "home" / "user" / "bad.sx",
        """io.println(missing);
""",
    )
    write_text(
        overlay_dir / "home" / "user" / "import_main.sx",
        """import "sx-examples/import_lib.sx";
io.println(imported_name());
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
        cwd=repo_root / "src",
        check=True,
    )
    return temp_fsboot


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_sxi_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    _fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    logdir.mkdir(parents=True, exist_ok=True)
    temp_fsboot = build_temp_rootfs(repo_root, logdir)

    serial_log = logdir / "sxi_serial.log"
    qemu_log = logdir / "sxi_qemu_debug.log"
    qemu_stderr_log = logdir / "sxi_qemu_stderr.log"

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
        f"user,id=net0,hostfwd=tcp::{GUEST_SERVER_PORT}-:{GUEST_SERVER_PORT}",
        "-device",
        "ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0",
    ]

    qemu_proc = None
    qemu_stderr_fp: TextIO | None = None
    host_server_errors: list[BaseException] = []
    host_client_errors: list[BaseException] = []
    host_server_thread = threading.Thread(
        target=run_host_server, args=(host_server_errors,), daemon=True
    )
    host_client_thread = threading.Thread(
        target=drive_guest_server, args=(host_client_errors,), daemon=True
    )
    try:
        host_server_thread.start()
        qemu_stderr_fp = qemu_stderr_log.open("w", encoding="utf-8")
        qemu_proc = subprocess.Popen(
            qemu_cmd,
            cwd=repo_root,
            stdout=subprocess.DEVNULL,
            stderr=qemu_stderr_fp,
        )
        host_client_thread.start()
        wait_until_ready(time.time() + timeout, serial_log, qemu_log,
                         qemu_proc, qemu_stderr_log)
        host_server_thread.join(timeout=5)
        host_client_thread.join(timeout=5)
        if host_server_thread.is_alive():
            raise AssertionError("host server helper did not finish")
        if host_client_thread.is_alive():
            raise AssertionError("host client helper did not finish")
        if host_server_errors:
            raise host_server_errors[0]
        if host_client_errors:
            raise host_client_errors[0]
        stop_qemu(qemu_proc)
        qemu_proc = None
        assert_guest_state(temp_fsboot)
        print("=== SXI QEMU SMOKE DONE ===")
        print(f"Artifacts: {serial_log}, {qemu_log}, {temp_fsboot}")
        return 0
    except Exception:
        dump_file(serial_log)
        dump_file(qemu_log)
        dump_file(qemu_stderr_log)
        raise
    finally:
        if qemu_proc is not None:
            stop_qemu(qemu_proc)
        if qemu_stderr_fp is not None:
            qemu_stderr_fp.close()


if __name__ == "__main__":
    raise SystemExit(main())
