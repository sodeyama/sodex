include makefile.inc

DEVICE = USB_DEVICE
#DEVICE = FDC_DEVICE

VPATH = . ${DRIVERDIR}

KERNEL  = ${BIN}/kernel.bin
ACIENT  = ${BIN}/boota.bin
MIDDLE  = ${BIN}/bootm.bin
FSBOOT  = ${BIN}/fsboot.bin
INIT    = ${INITDIR}/bin/ptest
INIT2   = ${INITDIR}/bin/ptest2

# Tools
KMKFS   = ${BUILDDIR}/tools/kmkfs
GETSIZE = ${BUILDDIR}/tools/getsize
UDEV_GOTHIC_TTF = ${BASEDIR}/third_party/fonts/UDEVGothic-Regular.ttf
UDEV_GOTHIC_UTF8_RANGES = 3000-303f,3040-309f,30a0-30ff,ff01-ff60,ffe0-ffe6,5909,5b57,611b,611f,63db,65e5,672c,6f22,85cd,8a9e

# Test process
PTEST   = ${INITDIR}/bin/ptest
PTEST2  = ${INITDIR}/bin/ptest2

# Kernel objects (C sources -> build/obj/*.o)
SRCS = $(wildcard *.c)
OBJS = $(addprefix $(OBJDIR)/,$(subst .c,.o,$(SRCS)))

# Sub-module objects
DRIVER_OBJS = $(OBJDIR)/drivers/*.o
KLIB_OBJS   = $(OBJDIR)/lib/*.o
NET_OBJS    = $(OBJDIR)/net/*.o
DISPLAY_OBJS = $(OBJDIR)/display/*.o
TTY_OBJS    = $(OBJDIR)/tty/*.o

# Test objects
TESTDIR     = ${SRCDIR}/test
KTEST_OBJS  = $(OBJDIR)/test/ktest.o

ifeq (${DEVICE}, USB_DEVICE)
FIRST_BOOT_FILE = bootusb.S
else
FIRST_BOOT_FILE = bootacient.S
endif

ASMSRC  = startup.S    \
          sys_core.S    \
          ihandlers.S   \
          page_asm.S

ASMOBJS = $(addprefix $(OBJDIR)/,$(ASMSRC:.S=.o))

EXTRA_CFLAGS ?=
SODEX_QEMU_MEM_MB ?= 512
SODEX_RAM_CAP_MB ?= 0
MEMORY_SCALE_CFLAGS =

ifneq ($(strip $(SODEX_RAM_CAP_MB)),0)
MEMORY_SCALE_CFLAGS += -DSODEX_RAM_CAP_MB=$(SODEX_RAM_CAP_MB)
endif

# === Build directory creation (done upfront via shell) ===
$(shell mkdir -p $(OBJDIR) $(BIN) $(LIST) $(LOGDIR) $(BUILDDIR)/tools $(OBJDIR)/test $(INITDIR)/bin)

# === Main targets ===

.PHONY: all usr-build regen-font8x16 regen-font16x16 regen-fonts
all : usr-build ${KERNEL} ${ACIENT} ${MIDDLE} tools
	@if [ ! -e ${FSBOOT} ]; then touch ${FSBOOT}; fi
	$(MAKE) ptest
	${KMKFS} ${ACIENT} ${MIDDLE} ${KERNEL} ${FSBOOT} ${INIT} ${INIT2}

regen-font8x16:
	python3 ${TOOLDIR}/mkfontpack.py \
		--font-path ${UDEV_GOTHIC_TTF} \
		--font-size 13 \
		--output-header ${INCLUDE}/font8x16_data.h \
		--source-name UDEVGothic-Regular.ttf

regen-font16x16:
	python3 ${TOOLDIR}/mkfontpack.py \
		--font-path ${UDEV_GOTHIC_TTF} \
		--output-header ${INCLUDE}/font16x16_data.h \
		--cell-width 16 \
		--cell-height 16 \
		--font-size 14 \
		--codepoint-ranges ${UDEV_GOTHIC_UTF8_RANGES} \
		--symbol-name font16x16_glyphs \
		--source-name UDEVGothic-Regular.ttf

regen-fonts: regen-font8x16 regen-font16x16

usr-build:
	cd usr; make

${KERNEL} : ${OBJS} ${ASMOBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${DISPLAY_OBJS} ${TTY_OBJS} ${BUILDDIR}/boot.ld
	${LD} ${LFLAGS} ${OBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${DISPLAY_OBJS} ${TTY_OBJS} ${ASMOBJS} -o $@

${DRIVER_OBJS} :
	cd drivers; make

${KLIB_OBJS} :
	cd lib; make

${NET_OBJS} :
	cd net; make

${DISPLAY_OBJS} :
	cd display; make

${TTY_OBJS} :
	cd tty; make

# === Boot loader ===

KSIZE=`${GETSIZE} ${KERNEL}`
${ACIENT} : ${FIRST_BOOT_FILE} ${KERNEL} ${GETSIZE}
	echo ${KSIZE}
	${AS} ${ASFLAGS} --defsym KERNEL_SECTS=${KSIZE} ${FIRST_BOOT_FILE} -o $(OBJDIR)/boota.o -a > ${LIST}/boota.lst
	${LD} ${BOOTA_LFLAGS} $(OBJDIR)/boota.o -o $@

${MIDDLE} : bootmiddle.S
	${AS} ${ASFLAGS} bootmiddle.S -I ${INCLUDE} -o $(OBJDIR)/bootmiddle.o -a > ${LIST}/bootm.lst
	${LD} ${BOOTM_LFLAGS} $(OBJDIR)/bootmiddle.o -o $@

# === Linker script preprocessing ===

${BUILDDIR}/boot.ld: boot.ld.S
	$(CC) -E -P boot.ld.S -I $(INCLUDE) -I . \
		-DSTARTUP_OBJ="\"$(OBJDIR)/startup.o\"" \
		-DKERNEL_OBJ="\"$(OBJDIR)/kernel.o\"" \
		-DVGA_OBJ="\"$(OBJDIR)/vga.o\"" \
		-o $@

# === Pattern rules (C and assembly -> build/obj/) ===

$(OBJDIR)/%.o: %.c
	$(CC) -D ${DEVICE}=DEFINED -c ${CFLAGS} ${EXTRA_CFLAGS} ${MEMORY_SCALE_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

$(OBJDIR)/%.o: %.S
	${AS} ${ASFLAGS} -I $(INCLUDE) -I . -o $@ $<

# === Build tools ===

.PHONY: tools
tools: ${KMKFS} ${GETSIZE}

${KMKFS} : ${TOOLDIR}/kmkfs.cpp
	$(C++) -D PATH_USRBIN=\"${BASEDIR}/src/usr/bin/\" -o $@ ${TOOLDIR}/kmkfs.cpp
	chmod 755 $@

${GETSIZE} : ${TOOLDIR}/getsize.c
	/usr/bin/gcc -o $@ ${TOOLDIR}/getsize.c
	chmod 755 $@

# === Test processes ===

.PHONY: ptest
ptest : ${PTEST} ${PTEST2}

${PTEST} : ${INITDIR}/ptest.S
	$(AS) ${ASFLAGS} -o $(OBJDIR)/ptest.o ${INITDIR}/ptest.S
	$(LD) ${PTEST_LFLAGS} -o $@ $(OBJDIR)/ptest.o

${PTEST2} : ${INITDIR}/ptest2.S
	$(AS) ${ASFLAGS} -o $(OBJDIR)/ptest2.o ${INITDIR}/ptest2.S
	$(LD) ${PTEST_LFLAGS} -o $@ $(OBJDIR)/ptest2.o

# === QEMU integration tests ===

.PHONY: test-qemu test-qemu-memory test-qemu-term test-qemu-fs test-qemu-shell-io test-qemu-vi test-qemu-ime ktest-build
test-qemu: ktest-build
	@echo "=== Running kernel integration tests in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_ktest.py $(FSBOOT) $(LOGDIR)

test-qemu-memory: ktest-build
	@echo "=== Running memory scaling smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_memory_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-term: all
	@echo "=== Running rich terminal smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_terminal_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-fs: all
	@echo "=== Running fs CRUD smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_fs_crud_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-shell-io: all
	@echo "=== Running shell IO smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_shell_io_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-vi: all
	@echo "=== Running vi smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_vi_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-ime: all
	@echo "=== Running IME smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_ime_smoke.py $(FSBOOT) $(LOGDIR)

$(OBJDIR)/test/ktest.o: ${TESTDIR}/ktest.c
	$(CC) -c ${CFLAGS} ${EXTRA_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

ktest-build:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="-DKTEST_BUILD" ktest-link

ktest-link: ${OBJS} ${ASMOBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${DISPLAY_OBJS} ${TTY_OBJS} $(KTEST_OBJS) ${BUILDDIR}/boot.ld
	${LD} ${LFLAGS} ${OBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${DISPLAY_OBJS} ${TTY_OBJS} ${ASMOBJS} $(KTEST_OBJS) -o ${KERNEL}
	$(MAKE) usr-build
	$(MAKE) tools
	$(MAKE) ${ACIENT} ${MIDDLE}
	@if [ ! -e ${FSBOOT} ]; then touch ${FSBOOT}; fi
	$(MAKE) ptest
	${KMKFS} ${ACIENT} ${MIDDLE} ${KERNEL} ${FSBOOT} ${INIT} ${INIT2}

# === Misc ===

.PHONY: usb
usb:
	${AS} ${ASFLAGS} bootusb.S -o $(OBJDIR)/bootusb.o
	${LD} -m elf_i386 -Ttext 0x0 -o bootusb $(OBJDIR)/bootusb.o --oformat binary

remake:
	$(MAKE) clean; $(MAKE)

# === Clean ===

.PHONY: clean
clean :
	cd usr; make clean
	cd drivers; make clean
	cd lib; make clean
	cd net; make clean
	cd display; make clean
	cd tty; make clean
	rm -rf ${BUILDDIR}
