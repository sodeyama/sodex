include ../../../makefile.inc
include ../../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/lib/libsx
$(shell mkdir -p $(SUB_OBJDIR))

CSRCS = $(wildcard *.c)
COBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(CSRCS)))

LIBSX = $(SUB_OBJDIR)/libsx.a

all: ${COBJS}
	$(AR) rcs $(LIBSX) ${COBJS}

$(SUB_OBJDIR)/%.o: %.c
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(LIBSX) ${COBJS}
