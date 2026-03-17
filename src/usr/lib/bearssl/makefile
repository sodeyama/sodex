include ../../../makefile.inc
include ../../makefile.inc

BEARSSL_DIR = $(shell pwd)
BEARSSL_INC = $(BEARSSL_DIR)/inc
BEARSSL_SRC = $(BEARSSL_DIR)/src

SUB_OBJDIR = ${USR_OBJDIR}/lib/bearssl
$(shell mkdir -p $(SUB_OBJDIR))

# Find all .c files recursively
CSRCS = $(shell find $(BEARSSL_SRC) -name '*.c')
COBJS = $(patsubst $(BEARSSL_SRC)/%.c,$(SUB_OBJDIR)/%.o,$(CSRCS))

LIBBEARSSL = $(SUB_OBJDIR)/libbearssl.a

# BearSSL needs access to its own inc/ and src/inner.h
# compat/ provides string.h, limits.h stubs that don't conflict with stdint.h
BEARSSL_CFLAGS = $(CFLAGS) -I$(BEARSSL_DIR)/compat -I$(BEARSSL_INC) -I$(BEARSSL_SRC) \
	-Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
	-Wno-missing-field-initializers -Wno-unused-variable -Wno-cast-align

all: $(COBJS)
	$(AR) rcs $(LIBBEARSSL) $(COBJS)

$(SUB_OBJDIR)/%.o: $(BEARSSL_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(BEARSSL_CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -rf $(SUB_OBJDIR)
