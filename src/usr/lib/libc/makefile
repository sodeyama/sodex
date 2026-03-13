include ../../../makefile.inc
include ../../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/lib/libc
$(shell mkdir -p $(SUB_OBJDIR))

SSRCS = $(wildcard *.S)
CSRCS = $(wildcard *.c)
SOBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .S,.o,$(SSRCS)))
COBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(CSRCS)))

OBJS = ${SOBJS} ${COBJS}

all: ${OBJS}
	cd i386; make
	$(AR) rcs $(STDLIB) ${OBJS}

$(SUB_OBJDIR)/%.o : %.S
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/%.o : %.c
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	cd i386; make clean
	rm -f $(STDLIB) ${OBJS}
