#!/bin/sh
# Sodex OS - QEMU launcher for macOS
#
# Usage:
#   ./start.sh          # user net mode (default)
#   ./start.sh net       # vmnet-shared mode (requires sudo, enables ping from host)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$SCRIPT_DIR/../.."
BUILD_BIN="$REPO_ROOT/build/bin"
LOG_DIR="$REPO_ROOT/build/log"
mkdir -p "$LOG_DIR"

COMMON_OPTS="-drive file=$BUILD_BIN/fsboot.bin,format=raw,if=ide \
    -m 128 \
    -serial file:$LOG_DIR/serial.log \
    -d int,cpu_reset -D $LOG_DIR/qemu_debug.log \
    -monitor unix:$LOG_DIR/monitor.sock,server,nowait"

NIC_OPTS="-device ne2k_isa,irq=11,iobase=0xc100,mac=52:54:00:12:34:56,netdev=net0"

case "${1:-user}" in
  net)
    echo "=== vmnet-shared mode (sudo required) ==="
    echo "Subnet: 10.0.2.0/24, Gateway: 10.0.2.1, Guest: 10.0.2.15"
    echo ""
    echo "After boot, ping from another terminal:"
    echo "  ping 10.0.2.15"
    echo ""
    sudo qemu-system-i386 \
        $COMMON_OPTS \
        -netdev vmnet-shared,id=net0,start-address=10.0.2.1,end-address=10.0.2.254,subnet-mask=255.255.255.0 \
        $NIC_OPTS \
        -display cocoa
    ;;
  *)
    echo "=== user net mode ==="
    echo "Note: host cannot ping guest in this mode"
    echo ""
    qemu-system-i386 \
        $COMMON_OPTS \
        -netdev user,id=net0 \
        $NIC_OPTS \
        -display cocoa
    ;;
esac
