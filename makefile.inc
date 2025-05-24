
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

AS      = /usr/bin/as
#CC      = /usr/local/bin/gcc
CC      = /usr/bin/gcc
C++		= /usr/bin/g++
LD      = /usr/bin/ld
MAKE	= /usr/bin/make
AR      = /usr/bin/ar

#CFLAGS  = -nostdlib -fno-exceptions -ffreestanding -fno-builtin -Wall -fno-stack-protector
CFLAGS  = -g -O0 -nostdlib -fno-exceptions -ffreestanding -fno-builtin -Wall
ASFLAGS = 
LFLAGS  = -T boot.ld -Map ${LIST}/boot.map --oformat binary
#LFLAGS  = -Ttext 0x1000 -Map ${LIST}/boot.map --oformat binary
BOOTA_LFLAGS = -Ttext 0x0 --oformat binary -nostdlib
BOOTM_LFLAGS = -Ttext 0x200 --oformat binary -nostdlib
