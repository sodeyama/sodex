#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
RUN_ROOT="${SODEX_CONTAINER_BUILD_ROOT:-/tmp/sodex-server-runtime-build}"
LOG_DIR="${SODEX_LOG_DIR:-/var/log/sodex}"
OVERLAY_DIR="${RUN_ROOT}/build/docker-rootfs-overlay"

rm -rf "${RUN_ROOT}"
mkdir -p "$(dirname "${RUN_ROOT}")" "${LOG_DIR}"
cp -a "${REPO_ROOT}" "${RUN_ROOT}"

python3 "${RUN_ROOT}/src/test/write_server_runtime_overlay.py" "$OVERLAY_DIR"

${MAKE:-/usr/bin/make} -C "${RUN_ROOT}/src" SODEX_ROOTFS_OVERLAY="$OVERLAY_DIR" all

exec env SODEX_LOG_DIR="${LOG_DIR}" "${RUN_ROOT}/bin/start.sh" server-headless
