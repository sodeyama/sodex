include ../makefile.inc

SRCS = $(wildcard *.c)
SUB_OBJDIR = $(OBJDIR)/drivers
OBJS = $(addprefix $(SUB_OBJDIR)/,$(subst .c,.o,$(SRCS)))

$(shell mkdir -p $(SUB_OBJDIR))

all : ${OBJS}

$(SUB_OBJDIR)/%.o: %.c
	${CC} -c ${CFLAGS} -I ${INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJS)
