#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
OVERLAY_DIR="${REPO_ROOT}/build/docker-rootfs-overlay"

mkdir -p "${REPO_ROOT}/build/log"

python3 "${REPO_ROOT}/src/test/write_server_runtime_overlay.py" "$OVERLAY_DIR"

${MAKE:-/usr/bin/make} -C "${REPO_ROOT}/src" SODEX_ROOTFS_OVERLAY="$OVERLAY_DIR" all

exec "${REPO_ROOT}/bin/start.sh" server-headless
