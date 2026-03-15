#!/usr/bin/env python3
"""memory scaling の QEMU matrix を回す。"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys

DEFAULT_MATRIX = (128, 256, 512, 1024)
LAYOUT_RE = re.compile(
    r"MEMORY_LAYOUT detected_mb=(?P<detected>\d+) "
    r"source=(?P<source>\S+) "
    r"cap_mb=(?P<cap>\d+) "
    r"effective_mb=(?P<effective>\d+) "
    r"direct_map_mb=(?P<direct>\d+) "
    r"kernel_heap_base=(?P<heap_base>0x[0-9a-f]+) "
    r"kernel_heap_size=(?P<heap_size>0x[0-9a-f]+) "
    r"process_pool_base=(?P<pool_base>0x[0-9a-f]+) "
    r"process_pool_size=(?P<pool_size>0x[0-9a-f]+) "
    r"pde_end=(?P<pde_end>\d+)"
)


def parse_matrix() -> list[int]:
    raw = os.environ.get("SODEX_QEMU_MATRIX_MB", "").strip()
    if raw == "":
        return list(DEFAULT_MATRIX)
    return [int(part.strip()) for part in raw.split(",") if part.strip()]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_qemu_memory_smoke.py <fsboot> <logdir>", file=sys.stderr)
        return 2

    fsboot = pathlib.Path(sys.argv[1]).resolve()
    logdir = pathlib.Path(sys.argv[2]).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    runner = repo_root / "src/test/run_qemu_ktest.py"
    matrix = parse_matrix()

    for qemu_mem_mb in matrix:
        env = os.environ.copy()
        env["SODEX_QEMU_MEM_MB"] = str(qemu_mem_mb)

        result = subprocess.run(
            [sys.executable, str(runner), str(fsboot), str(logdir)],
            cwd=repo_root,
            env=env,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            return result.returncode

        match = LAYOUT_RE.search(result.stdout)
        if match is None:
            print(f"memory layout line not found for {qemu_mem_mb}MB", file=sys.stderr)
            sys.stdout.write(result.stdout)
            return 1

        cap_mb = int(match.group("cap"))
        effective_mb = int(match.group("effective"))
        direct_map_mb = int(match.group("direct"))
        detected_mb = int(match.group("detected"))
        pde_end = int(match.group("pde_end"))
        expected_ceiling_mb = qemu_mem_mb if cap_mb == 0 else min(qemu_mem_mb, cap_mb)
        expected_pde_end = 768 + (effective_mb // 4)

        if detected_mb > expected_ceiling_mb or expected_ceiling_mb - detected_mb >= 8:
            print(
                f"detected RAM is outside expected range for {qemu_mem_mb}MB: detected={detected_mb} ceiling={expected_ceiling_mb}",
                file=sys.stderr,
            )
            sys.stdout.write(result.stdout)
            return 1
        if effective_mb > detected_mb or detected_mb - effective_mb >= 8:
            print(
                f"effective RAM is outside detected range for {qemu_mem_mb}MB: detected={detected_mb} effective={effective_mb}",
                file=sys.stderr,
            )
            sys.stdout.write(result.stdout)
            return 1
        if direct_map_mb != effective_mb:
            print(
                f"direct map mismatch for {qemu_mem_mb}MB: effective={effective_mb} direct={direct_map_mb}",
                file=sys.stderr,
            )
            sys.stdout.write(result.stdout)
            return 1
        if pde_end != expected_pde_end:
            print(
                f"pde mismatch for {qemu_mem_mb}MB: pde_end={pde_end} expected={expected_pde_end}",
                file=sys.stderr,
            )
            sys.stdout.write(result.stdout)
            return 1

        print(
            f"memory_matrix qemu_mb={qemu_mem_mb} detected_mb={detected_mb} effective_mb={effective_mb} pde_end={pde_end}"
        )

    print("=== MEMORY SCALING QEMU SMOKE DONE ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
