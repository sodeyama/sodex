# makefile.inc - Project-wide build configuration
# Single source of truth for paths, toolchain, and flags.

# Project root (auto-detect from this file's location)
BASEDIR := $(dir $(lastword $(MAKEFILE_LIST)))
BASEDIR := $(BASEDIR:/=)

SRCDIR    = ${BASEDIR}/src
INCLUDE   = ${SRCDIR}/include
TOOLDIR   = ${SRCDIR}/tools
INITDIR   = ${SRCDIR}/init

USRDIR    = ${SRCDIR}/usr
LIBDIR    = ${USRDIR}/lib
LIBCDIR   = ${LIBDIR}/libc
DRIVERDIR = ${SRCDIR}/drivers
KLIBDIR   = ${SRCDIR}/lib
NETDIR    = ${SRCDIR}/net

# Build output directories (all artifacts go here)
BUILDDIR  = ${BASEDIR}/build
OBJDIR    = ${BUILDDIR}/obj
BIN       = ${BUILDDIR}/bin
LIST      = ${BUILDDIR}/list
LOGDIR    = ${BUILDDIR}/log

# Toolchain (cross-compiler for i486 kernel)
# GNU make の built-in 変数よりこちらを優先しつつ、
# command line の `make CC=...` はそのまま上書きできるようにする。
AS      = /opt/homebrew/bin/x86_64-elf-as
CC      = /opt/homebrew/bin/x86_64-elf-gcc
CXX     = /usr/bin/g++
LD      = /opt/homebrew/bin/x86_64-elf-ld
MAKE    = /usr/bin/make
AR      = /opt/homebrew/bin/x86_64-elf-ar

# Compiler and linker flags
CFLAGS  = -m32 -g -O0 -nostdlib -fno-exceptions -ffreestanding -fno-builtin -fno-pie -fno-pic -Wall -Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -fno-stack-protector -fno-common -mno-mmx -mno-sse -mno-sse2
ASFLAGS = --32

LFLAGS  = -m elf_i386 -T ${BUILDDIR}/boot.ld -Map ${LIST}/boot.map --oformat binary --allow-multiple-definition
BOOTA_LFLAGS = -m elf_i386 -Ttext 0x0 --oformat binary -nostdlib
BOOTM_LFLAGS = -m elf_i386 -Ttext 0x200 --oformat binary -nostdlib
PTEST_LFLAGS = -m elf_i386 -nostdlib
