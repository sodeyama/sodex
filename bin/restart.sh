#!/bin/sh

is_valid_port() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac

  [ "$1" -ge 1 ] && [ "$1" -le 65535 ]
}

find_repo_root() {
  dir="$1"

  while [ "$dir" != "/" ]; do
    if [ -f "$dir/makefile" ] && [ -f "$dir/makefile.inc" ] && [ -d "$dir/src" ]; then
      printf '%s\n' "$dir"
      return 0
    fi
    dir=$(dirname "$dir")
  done

  return 1
}

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(find_repo_root "$SCRIPT_DIR")" || {
  echo "リポジトリルートが見つかりません: $SCRIPT_DIR" >&2
  exit 1
}
BUILD_ROOT="${SODEX_BUILD_ROOT:-$REPO_ROOT/build}"
LOG_DIR="${SODEX_LOG_DIR:-$BUILD_ROOT/log}"
SSH_OVERLAY_DIR="${SODEX_SSH_OVERLAY_DIR:-}"
MODE="user"
SSH_SELECTION="auto"
SSH_GUEST_PORT="${SODEX_SSH_PORT:-10022}"

select_ssh_overlay_dir() {
  if [ -n "$SSH_OVERLAY_DIR" ]; then
    printf '%s\n' "$SSH_OVERLAY_DIR"
    return 0
  fi

  case "$MODE" in
    server|server-headless)
      printf '%s\n' "$LOG_DIR/ssh-rootfs-overlay-server-headless"
      ;;
    *)
      printf '%s\n' "$LOG_DIR/ssh-rootfs-overlay-user"
      ;;
  esac
}

for arg in "$@"; do
  case "$arg" in
    user|server|server-headless|net)
      MODE="$arg"
      ;;
    --ssh|--ssh-host-port=*)
      SSH_SELECTION="on"
      ;;
    --ssh-guest-port=*)
      SSH_SELECTION="on"
      SSH_GUEST_PORT="${arg#*=}"
      ;;
    --no-ssh)
      SSH_SELECTION="off"
      ;;
  esac
done

case "$SSH_SELECTION" in
  on)
    ENABLE_SSH=1
    ;;
  off)
    ENABLE_SSH=0
    ;;
  *)
    case "$MODE" in
      server|server-headless)
        ENABLE_SSH=1
        ;;
      *)
        ENABLE_SSH=0
        ;;
    esac
    ;;
esac

if [ "$ENABLE_SSH" -eq 1 ]; then
  SSH_OVERLAY_DIR="$(select_ssh_overlay_dir)"
  if ! is_valid_port "$SSH_GUEST_PORT"; then
    echo "不正な guest SSH ポートです: $SSH_GUEST_PORT" >&2
    exit 1
  fi

  : "${SODEX_ADMIN_ALLOW_IP:=10.0.2.2}"
  : "${SODEX_SSH_PASSWORD:=root-secret}"
  : "${SODEX_SSH_SIGNER_PORT:=0}"
  : "${SODEX_SSH_HOSTKEY_ED25519_SEED:=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff}"
  : "${SODEX_SSH_RNG_SEED:=ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100}"
  SODEX_SSH_PORT="$SSH_GUEST_PORT"
  export SODEX_ADMIN_ALLOW_IP
  export SODEX_SSH_PORT
  export SODEX_SSH_PASSWORD
  export SODEX_SSH_SIGNER_PORT
  export SODEX_SSH_HOSTKEY_ED25519_SEED
  export SODEX_SSH_RNG_SEED
  case "$MODE" in
    server|server-headless)
      export SODEX_INITDEFAULT_RUNLEVEL=server-headless
      ;;
    *)
      export SODEX_INITDEFAULT_RUNLEVEL=user
      ;;
  esac

  make -C "$REPO_ROOT" clean || exit 1
  mkdir -p "$LOG_DIR" || exit 1
  # Inject Claude API key if .env.local exists
  if [ -f "$REPO_ROOT/.env.local" ]; then
    make -C "$REPO_ROOT/src" inject-api-key || true
  fi
  python3 "$REPO_ROOT/src/test/write_server_runtime_overlay.py" "$SSH_OVERLAY_DIR" || exit 1
  SODEX_ROOTFS_OVERLAY="$SSH_OVERLAY_DIR" make -C "$REPO_ROOT" || exit 1
  exec "$REPO_ROOT/bin/start.sh" "$@"
fi

make -C "$REPO_ROOT" clean || exit 1
# Inject Claude API key if .env.local exists
if [ -f "$REPO_ROOT/.env.local" ]; then
  make -C "$REPO_ROOT/src" inject-api-key || true
fi
make -C "$REPO_ROOT" || exit 1
exec "$REPO_ROOT/bin/start.sh" "$@"
