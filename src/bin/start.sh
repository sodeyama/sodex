#!/bin/sh
# Sodex OS - QEMU launcher for macOS
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
qemu-system-i386 -drive file="$SCRIPT_DIR/fsboot.bin",format=raw,if=ide -m 128
