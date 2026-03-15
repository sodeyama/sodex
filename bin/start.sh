#!/bin/sh

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
BUILD_BIN="$REPO_ROOT/build/bin"
LOG_DIR="$REPO_ROOT/build/log"
QEMU_MEM_MB="${SODEX_QEMU_MEM_MB:-512}"
mkdir -p "$LOG_DIR"

COMMON_OPTS="-drive file=$BUILD_BIN/fsboot.bin,format=raw,if=ide \
    -m $QEMU_MEM_MB \
    -serial file:$LOG_DIR/serial.log \
    -d int,cpu_reset -D $LOG_DIR/qemu_debug.log \
    -monitor unix:$LOG_DIR/monitor.sock,server,nowait"

NIC_OPTS="-device ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0"
QEMU_CMD="${QEMU_CMD:-qemu-system-i386}"

case "${1:-user}" in
  server)
    echo "=== user net mode with hostfwd ==="
    echo "host 127.0.0.1:18080 -> guest 10.0.2.15:8080"
    echo "host 127.0.0.1:10023 -> guest 10.0.2.15:10023"
    echo ""
    "$QEMU_CMD" \
        $COMMON_OPTS \
        -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-10.0.2.15:8080,hostfwd=tcp:127.0.0.1:10023-10.0.2.15:10023 \
        $NIC_OPTS \
        -display cocoa
    ;;
  net)
    echo "=== vmnet-shared mode (sudo required) ==="
    echo "Subnet: 10.0.2.0/24, Gateway: 10.0.2.1, Guest: 10.0.2.15"
    echo ""
    echo "After boot, ping from another terminal:"
    echo "  ping 10.0.2.15"
    echo ""
    sudo "$QEMU_CMD" \
        $COMMON_OPTS \
        -netdev vmnet-shared,id=net0,start-address=10.0.2.1,end-address=10.0.2.254,subnet-mask=255.255.255.0 \
        $NIC_OPTS \
        -display cocoa
    ;;
  *)
    echo "=== user net mode ==="
    echo "Note: host cannot ping guest in this mode"
    echo ""
    "$QEMU_CMD" \
        $COMMON_OPTS \
        -netdev user,id=net0 \
        $NIC_OPTS \
        -display cocoa
    ;;
esac
