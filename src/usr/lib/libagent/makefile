include ../../../makefile.inc
include ../../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/lib/libagent
$(shell mkdir -p $(SUB_OBJDIR))

CSRCS = $(wildcard *.c)
COBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(CSRCS)))
OBJS  = ${COBJS}

LIBAGENT = ${USR_OBJDIR}/lib/libagent/libagent.a

all: ${OBJS}
	$(AR) rcs $(LIBAGENT) ${OBJS}

BEARSSL_DIR = $(USRDIR)/lib/bearssl
BEARSSL_INC = $(BEARSSL_DIR)/inc
BEARSSL_COMPAT = $(BEARSSL_DIR)/compat
BEARSSL_SRC = $(BEARSSL_DIR)/src

# tls_client.c needs BearSSL compat headers BEFORE usr/include to avoid type conflicts
$(SUB_OBJDIR)/tls_client.o : tls_client.c
	$(CC) -c ${CFLAGS} -I $(BEARSSL_COMPAT) -I $(BEARSSL_INC) -I $(BEARSSL_SRC) -I ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/%.o : %.c
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I $(BEARSSL_INC) -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(LIBAGENT) ${OBJS}
