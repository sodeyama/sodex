include ../../../../makefile.inc
include ../../../makefile.inc

OBJS = $(subst .S,.o,$(wildcard *.S)) 

all: ${OBJS}
	$(AR) rcs $(LIBAOBJ) ${OBJS} 

%.o : %.S
	$(CC) -c ${CFLAGS} -I ${USR_INCLUDE} -I . -o $@ $<


.PHONY: clean
clean:
	rm -f $(LIBA) ${OBJS}
