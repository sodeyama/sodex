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
# GNU make の built-in 既定値だけを上書きし、
# command line / 環境変数からの指定はそのまま尊重する。
ifeq ($(origin AS), default)
AS = /opt/homebrew/bin/x86_64-elf-as
endif
ifeq ($(origin AS), undefined)
AS = /opt/homebrew/bin/x86_64-elf-as
endif

ifeq ($(origin CC), default)
CC = /opt/homebrew/bin/x86_64-elf-gcc
endif
ifeq ($(origin CC), undefined)
CC = /opt/homebrew/bin/x86_64-elf-gcc
endif

ifeq ($(origin CXX), default)
CXX = /usr/bin/g++
endif
ifeq ($(origin CXX), undefined)
CXX = /usr/bin/g++
endif

ifeq ($(origin LD), default)
LD = /opt/homebrew/bin/x86_64-elf-ld
endif
ifeq ($(origin LD), undefined)
LD = /opt/homebrew/bin/x86_64-elf-ld
endif

ifeq ($(origin MAKE), default)
MAKE = /usr/bin/make
endif
ifeq ($(origin MAKE), undefined)
MAKE = /usr/bin/make
endif

ifeq ($(origin AR), default)
AR = /opt/homebrew/bin/x86_64-elf-ar
endif
ifeq ($(origin AR), undefined)
AR = /opt/homebrew/bin/x86_64-elf-ar
endif

# Compiler and linker flags
CFLAGS  = -m32 -g -O0 -nostdlib -fno-exceptions -ffreestanding -fno-builtin -fno-pie -fno-pic -Wall -Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -fno-stack-protector -fno-common -mno-mmx -mno-sse -mno-sse2
ASFLAGS = --32

LFLAGS  = -m elf_i386 -T ${BUILDDIR}/boot.ld -Map ${LIST}/boot.map --oformat binary --allow-multiple-definition
BOOTA_LFLAGS = -m elf_i386 -Ttext 0x0 --oformat binary -nostdlib
BOOTM_LFLAGS = -m elf_i386 -Ttext 0x200 --oformat binary -nostdlib
PTEST_LFLAGS = -m elf_i386 -nostdlib
