#!/usr/bin/env python3
"""server runtime 用の rootfs overlay を生成する。"""

from __future__ import annotations

import os
import pathlib
import shutil
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: write_server_runtime_overlay.py <overlay_dir>", file=sys.stderr)
        return 2

    overlay_dir = pathlib.Path(sys.argv[1]).resolve()
    etc_dir = overlay_dir / "etc"
    config_path = etc_dir / "sodex-admin.conf"

    status_token = os.environ.get("SODEX_ADMIN_STATUS_TOKEN", "status-secret")
    control_token = os.environ.get("SODEX_ADMIN_CONTROL_TOKEN", "control-secret")
    allow_ip = os.environ.get("SODEX_ADMIN_ALLOW_IP", "10.0.2.2")
    debug_shell_port = os.environ.get("SODEX_DEBUG_SHELL_PORT", "")

    if overlay_dir.exists():
        shutil.rmtree(overlay_dir)
    etc_dir.mkdir(parents=True, exist_ok=True)

    lines = [
        f"status_token={status_token}",
        f"control_token={control_token}",
        f"allow_ip={allow_ip}",
    ]
    if debug_shell_port and debug_shell_port != "0":
        lines.append(f"debug_shell_port={debug_shell_port}")
    lines.append("")

    config_path.write_text("\n".join(lines), encoding="ascii")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
