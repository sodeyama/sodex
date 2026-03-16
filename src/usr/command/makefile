include ../../makefile.inc
include ../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/command
$(shell mkdir -p $(SUB_OBJDIR))

CSRCS = $(wildcard *.c)
COBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(CSRCS)))
OBJS  = ${COBJS}
SSHD_EXTRA_OBJS = \
	$(SUB_OBJDIR)/sshd_server.o \
	$(SUB_OBJDIR)/sshd_packet_core.o \
	$(SUB_OBJDIR)/sshd_auth_core.o \
	$(SUB_OBJDIR)/sshd_channel_core.o \
	$(SUB_OBJDIR)/sshd_ssh_crypto.o \
	$(SUB_OBJDIR)/sshd_tweetnacl.o \
	$(SUB_OBJDIR)/sshd_sha256.o \
	$(SUB_OBJDIR)/sshd_aes.o
SSHD_CFLAGS = $(filter-out -O0,$(CFLAGS)) -O2 -march=i486 -msoft-float -mno-mmx -mno-sse -mno-sse2

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
PING    = ${USRDIR}/bin/ping
TERM    = ${USRDIR}/bin/term
SSHD    = ${USRDIR}/bin/sshd
TOUCH   = ${USRDIR}/bin/touch
MKDIR   = ${USRDIR}/bin/mkdir
RM      = ${USRDIR}/bin/rm
RMDIR   = ${USRDIR}/bin/rmdir
MV      = ${USRDIR}/bin/mv
VI      = ${USRDIR}/bin/vi
UTF8DEMO = ${USRDIR}/bin/utf8demo
MEMGROW = ${USRDIR}/bin/memgrow
FSLARGE = ${USRDIR}/bin/fslarge

all: ${OBJS} ${SSHD_EXTRA_OBJS} ${STATIC_LIBS}
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
	${LD} ${LFLAGS} $(SUB_OBJDIR)/ping.o -o ${PING} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/term.o -o ${TERM} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/sshd.o ${SSHD_EXTRA_OBJS} -o ${SSHD} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/touch.o -o ${TOUCH} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/mkdir.o -o ${MKDIR} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/rm.o -o ${RM} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/rmdir.o -o ${RMDIR} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/mv.o -o ${MV} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/vi.o -o ${VI} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/utf8demo.o -o ${UTF8DEMO} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/memgrow.o -o ${MEMGROW} ${STATIC_LIBS}
	${LD} ${LFLAGS} $(SUB_OBJDIR)/fslarge.o -o ${FSLARGE} ${STATIC_LIBS}

$(SUB_OBJDIR)/sshd.o: sshd.c ../../include/ssh_server.h
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_server.o: ../../net/ssh_server.c ../../include/ssh_server.h ../../include/ssh_crypto.h ../../include/ssh_packet_core.h ../../include/ssh_auth_core.h ../../include/ssh_channel_core.h
	${CC} -DUSERLAND_SSHD_BUILD=1 -c ${SSHD_CFLAGS} -I ${INCLUDE} -idirafter ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_packet_core.o: ../../lib/ssh_packet_core.c ../../include/ssh_packet_core.h
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -idirafter ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_auth_core.o: ../../lib/ssh_auth_core.c ../../include/ssh_auth_core.h ../../include/ssh_packet_core.h
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -idirafter ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_channel_core.o: ../../lib/ssh_channel_core.c ../../include/ssh_channel_core.h ../../include/ssh_packet_core.h
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -idirafter ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_ssh_crypto.o: ../../lib/ssh_crypto.c
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_tweetnacl.o: ../../lib/tweetnacl.c
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_sha256.o: ../../lib/sha256.c
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/sshd_aes.o: ../../lib/aes.c
	${CC} -c ${SSHD_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS)
