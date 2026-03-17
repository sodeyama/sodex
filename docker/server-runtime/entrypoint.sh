#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
RUN_ROOT="${SODEX_CONTAINER_BUILD_ROOT:-/tmp/sodex-server-runtime-build}"
LOG_DIR="${SODEX_LOG_DIR:-/var/log/sodex}"
OVERLAY_DIR="${RUN_ROOT}/build/docker-rootfs-overlay"
TARGET_UID=""
TARGET_GID=""

run_as_target() {
  if [ "$TARGET_UID" = "0" ] && [ "$TARGET_GID" = "0" ]; then
    "$@"
    return
  fi
  setpriv --reuid "$TARGET_UID" --regid "$TARGET_GID" --clear-groups "$@"
}

exec_as_target() {
  if [ "$TARGET_UID" = "0" ] && [ "$TARGET_GID" = "0" ]; then
    exec "$@"
  fi
  exec setpriv --reuid "$TARGET_UID" --regid "$TARGET_GID" --clear-groups "$@"
}

if [ -n "${SODEX_CONTAINER_UID:-}" ] || [ -n "${SODEX_CONTAINER_GID:-}" ]; then
  : "${SODEX_CONTAINER_UID:?SODEX_CONTAINER_UID が必要です}"
  : "${SODEX_CONTAINER_GID:?SODEX_CONTAINER_GID が必要です}"
  TARGET_UID="${SODEX_CONTAINER_UID}"
  TARGET_GID="${SODEX_CONTAINER_GID}"
fi

rm -rf "${RUN_ROOT}"
mkdir -p "$(dirname "${RUN_ROOT}")" "${LOG_DIR}"

if [ -z "$TARGET_UID" ]; then
  TARGET_UID="$(stat -c '%u' "${LOG_DIR}")"
  TARGET_GID="$(stat -c '%g' "${LOG_DIR}")"
fi

cp -a "${REPO_ROOT}" "${RUN_ROOT}"

if [ "$TARGET_UID" != "0" ] || [ "$TARGET_GID" != "0" ]; then
  chown -R "${TARGET_UID}:${TARGET_GID}" "${RUN_ROOT}"
  if ! run_as_target test -w "${LOG_DIR}"; then
    chown -R "${TARGET_UID}:${TARGET_GID}" "${LOG_DIR}"
  fi
fi

run_as_target python3 "${RUN_ROOT}/src/test/write_server_runtime_overlay.py" "$OVERLAY_DIR"

run_as_target env HOME="${RUN_ROOT}" \
  ${MAKE:-/usr/bin/make} -C "${RUN_ROOT}/src" SODEX_ROOTFS_OVERLAY="$OVERLAY_DIR" all

exec_as_target env HOME="${RUN_ROOT}" SODEX_LOG_DIR="${LOG_DIR}" \
  "${RUN_ROOT}/bin/start.sh" server-headless
