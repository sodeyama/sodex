include ../makefile.inc
include makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}
$(shell mkdir -p $(SUB_OBJDIR) ${USRDIR}/bin)

IDLE   = ${USRDIR}/bin/idle
INIT   = ${USRDIR}/bin/init
PROC3  = ${USRDIR}/bin/proc3

all: $(SUB_OBJDIR)/idle.o $(SUB_OBJDIR)/init.o $(SUB_OBJDIR)/proc3.o
	cd lib; make
	cd command; make
	${LD} ${LFLAGS} $(SUB_OBJDIR)/idle.o -o ${IDLE} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/init.o -o ${INIT} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/proc3.o -o ${PROC3} ${STATIC_LIBS}

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	cd lib; make clean
	cd command; make clean
	rm -f $(SUB_OBJDIR)/*.o
