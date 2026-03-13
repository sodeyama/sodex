
USR_INCLUDE = ${USRDIR}/include

CRTOBJ	= ${LIBDIR}/crt0.o
STDLIB  = ${LIBCDIR}/stdlib.a
LIBAOBJ = ${LIBCDIR}/libc.a

STATIC_LIBS = ${CRTOBJ} ${STDLIB} ${LIBAOBJ}

CFLAGS = -m32 -nostdlib -ffreestanding -fno-builtin -Wall -Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -fno-stack-protector
ASFLAGS = --32
LFLAGS = -m elf_i386 -nostdlib