#!/bin/sh
# Sodex OS - QEMU launcher for macOS
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$SCRIPT_DIR/../.."
BUILD_BIN="$REPO_ROOT/build/bin"
LOG_DIR="$REPO_ROOT/build/log"
mkdir -p "$LOG_DIR"
qemu-system-i386 \
    -drive file="$BUILD_BIN/fsboot.bin",format=raw,if=ide \
    -m 128 \
    -serial file:"$LOG_DIR/serial.log" \
    -d int,cpu_reset -D "$LOG_DIR/qemu_debug.log"
