include ../../../makefile.inc
include ../../makefile.inc

SOBJS = $(subst .S,.o,$(wildcard *.S)) 
COBJS = $(subst .c,.o,$(wildcard *.c))

OBJS = ${SOBJS} ${COBJS}

all: ${OBJS} ${COBJS} ${CPPOBJS}
	cd i386; make
	$(AR) rcs $(STDLIB) ${OBJS}

%.o : %.S
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<

%.o : %.c
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<


.PHONY: clean
clean:
	cd i386; make clean
	rm -f $(STDLIB) ${OBJS}