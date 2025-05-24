include ../../makefile.inc
include ../makefile.inc

COBJS = $(subst .c,.o,$(wildcard *.c))
CMDS  = $(subst .c,,$(wildcard *.c))
OBJS  = ${COBJS}

ESHELL	= ${USRDIR}/bin/eshell
LS  	= ${USRDIR}/bin/ls
CAT 	= ${USRDIR}/bin/cat
PS		= ${USRDIR}/bin/ps
PWD		= ${USRDIR}/bin/pwd
CD		= ${USRDIR}/bin/cd
TEST	= ${USRDIR}/bin/test
TEST2	= ${USRDIR}/bin/test2
TEST3	= ${USRDIR}/bin/test3
KILL	= ${USRDIR}/bin/kill
REGEX	= ${USRDIR}/bin/regex
SEND	= ${USRDIR}/bin/send

all: ${OBJS} ${STATIC_LIBS}
	${LD} ${LFLAGS} eshell.o -o ${ESHELL} ${STATIC_LIBS}
	${LD} ${LFLAGS} ls.o -o ${LS} ${STATIC_LIBS}
	${LD} ${LFLAGS} cat.o -o ${CAT} ${STATIC_LIBS}
	${LD} ${LFLAGS} ps.o -o ${PS} ${STATIC_LIBS}
	${LD} ${LFLAGS} pwd.o -o ${PWD} ${STATIC_LIBS}
	${LD} ${LFLAGS} cd.o -o ${CD} ${STATIC_LIBS}
	${LD} ${LFLAGS} test.o -o ${TEST} ${STATIC_LIBS}
	${LD} ${LFLAGS} test2.o -o ${TEST2} ${STATIC_LIBS}
	${LD} ${LFLAGS} test3.o -o ${TEST3} ${STATIC_LIBS}
	${LD} ${LFLAGS} kill.o -o ${KILL} ${STATIC_LIBS}
	${LD} ${LFLAGS} regex.o -o ${REGEX} ${STATIC_LIBS}
	${LD} ${LFLAGS} send.o -o ${SEND} ${STATIC_LIBS}

%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

hoge:
	echo ${CMDS}


.PHONY: clean
clean:
	rm -f $(wildcard *.o)
#	rm ${ESHELL} ${LS} ${CAT} ${PS} ${PWD} ${CD} ${TEST} ${TEST2} ${TEST3} ${KILL} ${WRITE} ${REGEX}
