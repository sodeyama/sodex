include ../makefile.inc
include makefile.inc

IDLE   = ${USRDIR}/bin/idle
INIT   = ${USRDIR}/bin/init
PROC3  = ${USRDIR}/bin/proc3

all: idle.o init.o proc3.o
	cd lib; make
	cd command; make
	${LD} ${LFLAGS} idle.o -o ${IDLE} ${STATIC_LIBS}
	${LD} ${LFLAGS} init.o -o ${INIT} ${STATIC_LIBS}
	${LD} ${LFLAGS} proc3.o -o ${PROC3} ${STATIC_LIBS}

%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	cd lib; make clean
	cd command; make clean
	rm -f $(wildcard *.o)
#	rm ${IDLE} ${INIT} ${PROC3} ${STATIC_LIBS}
