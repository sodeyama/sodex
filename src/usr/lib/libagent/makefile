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

$(SUB_OBJDIR)/%.o : %.c
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(LIBAGENT) ${OBJS}
