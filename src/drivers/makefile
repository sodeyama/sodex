include ../makefile.inc

OBJS = $(subst .c,.o,$(wildcard *.c))

all : ${OBJS}

%.o: %.c
	${CC} -c ${CFLAGS} -I ${INCLUDE} -I . -o $@ $<

.PHONY: clean
clean:
	rm ${OBJS}