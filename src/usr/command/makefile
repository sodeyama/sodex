include ../../makefile.inc
include ../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/command
$(shell mkdir -p $(SUB_OBJDIR))

CSRCS = $(wildcard *.c)
COBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(CSRCS)))
OBJS  = ${COBJS}

ESHELL  = ${USRDIR}/bin/eshell
LS      = ${USRDIR}/bin/ls
CAT     = ${USRDIR}/bin/cat
PS      = ${USRDIR}/bin/ps
PWD     = ${USRDIR}/bin/pwd
CD      = ${USRDIR}/bin/cd
TEST    = ${USRDIR}/bin/test
TEST2   = ${USRDIR}/bin/test2
TEST3   = ${USRDIR}/bin/test3
KILL    = ${USRDIR}/bin/kill
REGEX   = ${USRDIR}/bin/regex
SEND    = ${USRDIR}/bin/send

all: ${OBJS} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/eshell.o -o ${ESHELL} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/ls.o -o ${LS} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/cat.o -o ${CAT} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/ps.o -o ${PS} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/pwd.o -o ${PWD} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/cd.o -o ${CD} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/test.o -o ${TEST} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/test2.o -o ${TEST2} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/test3.o -o ${TEST3} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/kill.o -o ${KILL} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/regex.o -o ${REGEX} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/send.o -o ${SEND} ${STATIC_LIBS}

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS)
