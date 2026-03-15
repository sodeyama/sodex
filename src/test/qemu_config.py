#!/usr/bin/env python3
"""QEMU 共通設定。"""

from __future__ import annotations

import os

DEFAULT_QEMU_MEM_MB = 512


def get_qemu_memory_mb() -> int:
    raw = os.environ.get("SODEX_QEMU_MEM_MB", "").strip()
    if raw == "":
        return DEFAULT_QEMU_MEM_MB

    value = int(raw)
    if value <= 0:
        raise ValueError("SODEX_QEMU_MEM_MB must be positive")
    return value
