include ../../makefile.inc
include ../makefile.inc

SRCS	= crt0.S
OBJS	= $(SRCS:.S=.o)

all: ${OBJS} 
	cd libc; make

%.o: %.S
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	cd libc; make clean
	rm -f ${OBJS}
