sodex   = /Users/sodeyama/git/sodex
BASEDIR	= $(sodex)
SRCDIR  = ${BASEDIR}/src
BIN     = ${SRCDIR}/bin
LIST    = ${BASEDIR}/list
INCLUDE = $(SRCDIR)/include
TOOLDIR = ${SRCDIR}/tools
INITDIR = ${SRCDIR}/init

USRDIR  = ${SRCDIR}/usr
LIBDIR	= ${USRDIR}/lib
LIBCDIR = ${LIBDIR}/libc
DRIVERDIR = ${SRCDIR}/drivers
KLIBDIR   = ${SRCDIR}/lib
NETDIR    = ${SRCDIR}/net

AS      = /opt/homebrew/bin/x86_64-elf-as
#CC      = /usr/local/bin/gcc
#CC      = /usr/bin/gcc
CC      = /opt/homebrew/bin/x86_64-elf-gcc
C++		= /usr/bin/g++
#LD      = /usr/bin/ld
LD      = /opt/homebrew/bin/x86_64-elf-ld
MAKE	= /usr/bin/make
AR      = /opt/homebrew/bin/x86_64-elf-ar

#CFLAGS  = -nostdlib -fno-exceptions -ffreestanding -fno-builtin -Wall -fno-stack-protector
#CFLAGS  = -g -O0 -nostdlib -fno-exceptions -ffreestanding -fno-builtin -Wall
CFLAGS  = -m32 -g -O0 -nostdlib -fno-exceptions -ffreestanding -fno-builtin -Wall -Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -fno-stack-protector -fno-common
ASFLAGS = --32
#LFLAGS  = -T boot.ld -Map ${LIST}/boot.map --oformat binary
LFLAGS  = -m elf_i386 -T boot.ld -Map ${LIST}/boot.map --oformat binary --allow-multiple-definition
#LFLAGS  = -Ttext 0x1000 -Map ${LIST}/boot.map --oformat binary
BOOTA_LFLAGS = -m elf_i386 -Ttext 0x0 --oformat binary -nostdlib
BOOTM_LFLAGS = -m elf_i386 -Ttext 0x200 --oformat binary -nostdlib
PTEST_LFLAGS = -m elf_i386 -nostdlib
