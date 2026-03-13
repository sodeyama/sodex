include ../../makefile.inc
include ../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/lib
$(shell mkdir -p $(SUB_OBJDIR))

OBJS = $(SUB_OBJDIR)/crt0.o

all: ${OBJS}
	cd libc; make

$(SUB_OBJDIR)/%.o: %.S
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	cd libc; make clean
	rm -f ${OBJS}
