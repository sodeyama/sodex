USR_INCLUDE = ${USRDIR}/include

USR_OBJDIR  = ${OBJDIR}/usr
CRTOBJ      = ${USR_OBJDIR}/lib/crt0.o
STDLIB      = ${USR_OBJDIR}/lib/libc/stdlib.a
LIBAOBJ     = ${USR_OBJDIR}/lib/libc/libc.a
LIBAGENT    = ${USR_OBJDIR}/lib/libagent/libagent.a
LIBBEARSSL  = ${USR_OBJDIR}/lib/bearssl/libbearssl.a

STATIC_LIBS = ${CRTOBJ} ${LIBAGENT} ${LIBBEARSSL} ${STDLIB} ${LIBAOBJ}

CFLAGS  = -m32 -march=i486 -nostdlib -ffreestanding -fno-builtin -fno-pie -fno-pic -Wall -Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -fno-stack-protector -mno-mmx -mno-sse -mno-sse2
ASFLAGS = --32
LFLAGS  = -m elf_i386 -nostdlib
