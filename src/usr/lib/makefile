include ../../makefile.inc
include ../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/lib
$(shell mkdir -p $(SUB_OBJDIR))

OBJS = $(SUB_OBJDIR)/crt0.o

all: ${OBJS}
	cd libc; make
	cd bearssl; make
	cd libagent; make
	cd libsx; make

$(SUB_OBJDIR)/%.o: %.S
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	cd libc; make clean
	cd bearssl; make clean
	cd libagent; make clean
	cd libsx; make clean
	rm -f ${OBJS}
