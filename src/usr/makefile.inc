USR_INCLUDE = ${USRDIR}/include

USR_OBJDIR  = ${OBJDIR}/usr
CRTOBJ      = ${USR_OBJDIR}/lib/crt0.o
STDLIB      = ${USR_OBJDIR}/lib/libc/stdlib.a
LIBAOBJ     = ${USR_OBJDIR}/lib/libc/libc.a

STATIC_LIBS = ${CRTOBJ} ${STDLIB} ${LIBAOBJ}

CFLAGS  = -m32 -nostdlib -ffreestanding -fno-builtin -fno-pie -fno-pic -Wall -Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -fno-stack-protector
ASFLAGS = --32
LFLAGS  = -m elf_i386 -nostdlib
