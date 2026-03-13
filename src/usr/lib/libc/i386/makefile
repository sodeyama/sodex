include ../../../../makefile.inc
include ../../../makefile.inc

SUB_OBJDIR = ${USR_OBJDIR}/lib/libc/i386
$(shell mkdir -p $(SUB_OBJDIR))

SSRCS = $(wildcard *.S)
OBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .S,.o,$(SSRCS)))

all: ${OBJS}
	$(AR) rcs $(LIBAOBJ) ${OBJS}

$(SUB_OBJDIR)/%.o : %.S
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(LIBAOBJ) ${OBJS}
