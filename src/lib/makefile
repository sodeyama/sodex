include ../makefile.inc

SRCS = $(wildcard *.c)
SUB_OBJDIR = $(OBJDIR)/lib
OBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(SRCS)))

$(shell mkdir -p $(SUB_OBJDIR))

all : ${OBJS}

$(SUB_OBJDIR)/tweetnacl.o: tweetnacl.c
	${CC} -c $(filter-out -O0,$(CFLAGS)) -O2 -I ${INCLUDE} -I . -o $@ $<

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS)
