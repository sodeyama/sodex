#!/bin/sh

print_usage() {
  echo "usage: $0 [user|server|server-headless|net] [--ssh|--no-ssh] [--ssh-host-port=PORT] [--ssh-guest-port=PORT]"
}

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
BUILD_BIN="${SODEX_BUILD_BIN:-$BUILD_ROOT/bin}"
LOG_DIR="${SODEX_LOG_DIR:-$BUILD_ROOT/log}"
QEMU_MEM_MB="${SODEX_QEMU_MEM_MB:-1024}"
HOST_BIND_ADDR="${SODEX_HOST_BIND_ADDR:-127.0.0.1}"
HOST_HTTP_PORT="${SODEX_HOST_HTTP_PORT:-18080}"
HOST_ADMIN_PORT="${SODEX_HOST_ADMIN_PORT:-10023}"
HOST_SSH_PORT="${SODEX_HOST_SSH_PORT:-10022}"
GUEST_SSH_PORT="${SODEX_SSH_PORT:-10022}"
QEMU_ACCEL="${SODEX_QEMU_ACCEL:-}"
QEMU_DEBUG_FLAGS="${SODEX_QEMU_DEBUG_FLAGS-int,cpu_reset}"
QEMU_SERIAL_MODE="${SODEX_QEMU_SERIAL_MODE:-stdio}"
MODE="user"
SSH_SELECTION="auto"
mkdir -p "$LOG_DIR"

while [ $# -gt 0 ]; do
  case "$1" in
    user|server|server-headless|net)
      MODE="$1"
      ;;
    --ssh)
      SSH_SELECTION="on"
      ;;
    --no-ssh)
      SSH_SELECTION="off"
      ;;
    --ssh-host-port=*)
      HOST_SSH_PORT="${1#*=}"
      SSH_SELECTION="on"
      ;;
    --ssh-guest-port=*)
      GUEST_SSH_PORT="${1#*=}"
      SSH_SELECTION="on"
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      echo "不明な引数: $1" >&2
      print_usage >&2
      exit 1
      ;;
  esac
  shift
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
  if ! is_valid_port "$HOST_SSH_PORT"; then
    echo "不正な host SSH ポートです: $HOST_SSH_PORT" >&2
    exit 1
  fi
  if ! is_valid_port "$GUEST_SSH_PORT"; then
    echo "不正な guest SSH ポートです: $GUEST_SSH_PORT" >&2
    exit 1
  fi
fi

# Check Claude API key availability
CLAUDE_CONF="$REPO_ROOT/src/rootfs-overlay/etc/claude.conf"
if [ -f "$CLAUDE_CONF" ]; then
  echo "[Claude] API key loaded (ask command available)"
elif [ -f "$REPO_ROOT/.env.local" ]; then
  echo "[Claude] .env.local found but not injected. Run: cd src && make inject-api-key"
fi

COMMON_OPTS="-drive file=$BUILD_BIN/fsboot.bin,format=raw,if=ide \
    -m $QEMU_MEM_MB \
    -monitor unix:$LOG_DIR/monitor.sock,server,nowait"

COMMON_SERIAL_FILE_OPTS="-serial file:$LOG_DIR/serial.log"
HEADLESS_SERIAL_OPTS="-serial stdio"
COMMON_ACCEL_OPTS=""
COMMON_DEBUG_OPTS=""
if [ -n "$QEMU_ACCEL" ]; then
  COMMON_ACCEL_OPTS="-accel $QEMU_ACCEL"
fi
if [ -n "$QEMU_DEBUG_FLAGS" ]; then
  COMMON_DEBUG_OPTS="-d $QEMU_DEBUG_FLAGS -D $LOG_DIR/qemu_debug.log"
fi
if [ "$QEMU_SERIAL_MODE" = "file" ]; then
  HEADLESS_SERIAL_OPTS="-serial file:$LOG_DIR/serial.log"
fi

NIC_OPTS="-device ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0"
QEMU_CMD="${QEMU_CMD:-qemu-system-i386}"
SSH_HOSTFWD_OPTS=""
if [ "$ENABLE_SSH" -eq 1 ]; then
  SSH_HOSTFWD_OPTS=",hostfwd=tcp:$HOST_BIND_ADDR:$HOST_SSH_PORT-10.0.2.15:$GUEST_SSH_PORT"
fi
QEMU_DISPLAY="${QEMU_DISPLAY:-cocoa,zoom-to-fit=off}"

case "$MODE" in
  server)
    echo "=== user net mode with hostfwd ==="
    echo "host $HOST_BIND_ADDR:$HOST_HTTP_PORT -> guest 10.0.2.15:8080"
    echo "host $HOST_BIND_ADDR:$HOST_ADMIN_PORT -> guest 10.0.2.15:10023"
    if [ "$ENABLE_SSH" -eq 1 ]; then
      echo "host $HOST_BIND_ADDR:$HOST_SSH_PORT -> guest 10.0.2.15:$GUEST_SSH_PORT"
    fi
    echo ""
    "$QEMU_CMD" \
        $COMMON_OPTS \
        $COMMON_SERIAL_FILE_OPTS \
        $COMMON_DEBUG_OPTS \
        $COMMON_ACCEL_OPTS \
        -netdev user,id=net0,hostfwd=tcp:$HOST_BIND_ADDR:$HOST_HTTP_PORT-10.0.2.15:8080,hostfwd=tcp:$HOST_BIND_ADDR:$HOST_ADMIN_PORT-10.0.2.15:10023$SSH_HOSTFWD_OPTS \
        $NIC_OPTS \
        -display "$QEMU_DISPLAY"
    ;;
  server-headless)
    echo "=== headless server mode with hostfwd ==="
    echo "host $HOST_BIND_ADDR:$HOST_HTTP_PORT -> guest 10.0.2.15:8080"
    echo "host $HOST_BIND_ADDR:$HOST_ADMIN_PORT -> guest 10.0.2.15:10023"
    if [ "$ENABLE_SSH" -eq 1 ]; then
      echo "host $HOST_BIND_ADDR:$HOST_SSH_PORT -> guest 10.0.2.15:$GUEST_SSH_PORT"
    fi
    echo "serial: stdio"
    echo ""
    "$QEMU_CMD" \
        -drive file=$BUILD_BIN/fsboot.bin,format=raw,if=ide \
        -m "$QEMU_MEM_MB" \
        $HEADLESS_SERIAL_OPTS \
        -monitor unix:$LOG_DIR/monitor.sock,server,nowait \
        $COMMON_DEBUG_OPTS \
        $COMMON_ACCEL_OPTS \
        -netdev user,id=net0,hostfwd=tcp:$HOST_BIND_ADDR:$HOST_HTTP_PORT-10.0.2.15:8080,hostfwd=tcp:$HOST_BIND_ADDR:$HOST_ADMIN_PORT-10.0.2.15:10023$SSH_HOSTFWD_OPTS \
        $NIC_OPTS \
        -display none
    ;;
  net)
    echo "=== vmnet-shared mode (sudo required) ==="
    echo "Subnet: 10.0.2.0/24, Gateway: 10.0.2.1, Guest: 10.0.2.15"
    if [ "$ENABLE_SSH" -eq 1 ]; then
      echo "SSH は guest 10.0.2.15:$GUEST_SSH_PORT へ直接接続してください"
    fi
    echo ""
    echo "After boot, ping from another terminal:"
    echo "  ping 10.0.2.15"
    echo ""
    sudo "$QEMU_CMD" \
        $COMMON_OPTS \
        $COMMON_SERIAL_FILE_OPTS \
        $COMMON_DEBUG_OPTS \
        $COMMON_ACCEL_OPTS \
        -netdev vmnet-shared,id=net0,start-address=10.0.2.1,end-address=10.0.2.254,subnet-mask=255.255.255.0 \
        $NIC_OPTS \
        -display "$QEMU_DISPLAY"
    ;;
  *)
    echo "=== user net mode ==="
    if [ "$ENABLE_SSH" -eq 1 ]; then
      echo "host $HOST_BIND_ADDR:$HOST_SSH_PORT -> guest 10.0.2.15:$GUEST_SSH_PORT"
    else
      echo "Note: host cannot ping guest in this mode"
    fi
    echo ""
    "$QEMU_CMD" \
        $COMMON_OPTS \
        $COMMON_SERIAL_FILE_OPTS \
        $COMMON_DEBUG_OPTS \
        $COMMON_ACCEL_OPTS \
        -netdev user,id=net0$SSH_HOSTFWD_OPTS \
        $NIC_OPTS \
        -display "$QEMU_DISPLAY"
    ;;
esac
