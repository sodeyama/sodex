#!/usr/bin/env python3
"""server runtime 用の rootfs overlay を生成する。"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys


def derive_ssh_hostkey(seed_hex: str) -> tuple[str, str]:
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    tool_path = repo_root / "tests" / "derive_ssh_keypair"

    if not tool_path.exists():
      subprocess.run(
          ["make", "-C", str(repo_root / "tests"), "derive_ssh_keypair"],
          check=True,
      )

    proc = subprocess.run(
        [str(tool_path), seed_hex],
        check=True,
        capture_output=True,
        text=True,
    )
    public_hex = ""
    secret_hex = ""
    for line in proc.stdout.splitlines():
        if line.startswith("public="):
            public_hex = line.split("=", 1)[1].strip()
        if line.startswith("secret="):
            secret_hex = line.split("=", 1)[1].strip()
    if not public_hex or not secret_hex:
        raise RuntimeError("derive_ssh_keypair output is incomplete")
    return public_hex, secret_hex


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
    ssh_port = os.environ.get("SODEX_SSH_PORT", "")
    ssh_password = os.environ.get("SODEX_SSH_PASSWORD", "")
    ssh_signer_port = os.environ.get("SODEX_SSH_SIGNER_PORT", "")
    ssh_hostkey_seed = os.environ.get("SODEX_SSH_HOSTKEY_ED25519_SEED", "")
    ssh_hostkey_public = os.environ.get("SODEX_SSH_HOSTKEY_ED25519_PUBLIC", "")
    ssh_hostkey_secret = os.environ.get("SODEX_SSH_HOSTKEY_ED25519_SECRET", "")
    ssh_rng_seed = os.environ.get("SODEX_SSH_RNG_SEED", "")
    config_extra = os.environ.get("SODEX_ADMIN_CONFIG_EXTRA", "")

    if ssh_hostkey_seed and (not ssh_hostkey_public or not ssh_hostkey_secret):
        ssh_hostkey_public, ssh_hostkey_secret = derive_ssh_hostkey(ssh_hostkey_seed)

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
    if ssh_port and ssh_port != "0":
        lines.append(f"ssh_port={ssh_port}")
    if ssh_password:
        lines.append(f"ssh_password={ssh_password}")
    if ssh_signer_port and ssh_signer_port != "0":
        lines.append(f"ssh_signer_port={ssh_signer_port}")
    if ssh_hostkey_seed:
        lines.append(f"ssh_hostkey_ed25519_seed={ssh_hostkey_seed}")
    if ssh_hostkey_public:
        lines.append(f"ssh_hostkey_ed25519_public={ssh_hostkey_public}")
    if ssh_hostkey_secret:
        lines.append(f"ssh_hostkey_ed25519_secret={ssh_hostkey_secret}")
    if ssh_rng_seed:
        lines.append(f"ssh_rng_seed={ssh_rng_seed}")
    if config_extra:
        lines.extend(config_extra.splitlines())
    lines.append("")

    config_path.write_text("\n".join(lines), encoding="ascii")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
