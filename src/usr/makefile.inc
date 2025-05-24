
USR_INCLUDE = ${USRDIR}/include

CRTOBJ	= ${LIBDIR}/crt0.o
STDLIB  = ${LIBCDIR}/stdlib.a
LIBAOBJ = ${LIBCDIR}/libc.a

STATIC_LIBS = ${CRTOBJ} ${STDLIB} ${LIBAOBJ}

CFLAGS = -nostdlib -Wall
LFLAGS = -nostdlib