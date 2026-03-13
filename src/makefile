include makefile.inc

DEVICE = USB_DEVICE
#DEVICE = FDC_DEVICE

VPATH = . ${DRIVERDIR}

#PTEST_LFLAGS = -Ttext 0x0 -Tdata 0x100 --oformat binary -nostdlib

KERNEL  = ${BIN}/kernel.bin
ACIENT  = ${BIN}/boota.bin
MIDDLE  = ${BIN}/bootm.bin
FSBOOT  = ${BIN}/fsboot.bin
INIT	= ${INITDIR}/bin/ptest
INIT2	= ${INITDIR}/bin/ptest2

#for tools
KMKFS	= ${TOOLDIR}/bin/kmkfs
GETSIZE = ${TOOLDIR}/bin/getsize

#for test process
PTEST	= ${INITDIR}/bin/ptest
PTEST2	= ${INITDIR}/bin/ptest2

OBJS = $(subst .c,.o,$(wildcard *.c))
#OBJS = $(subst .c,.o,$(SRCS))

DRIVER_OBJS = ${DRIVERDIR}/*.o
KLIB_OBJS	= ${KLIBDIR}/*.o
NET_OBJS	= ${NETDIR}/*.o

ifeq (${DEVICE}, USB_DEVICE)
FIRST_BOOT_FILE = bootusb.S
else
FIRST_BOOT_FILE = bootacient.S
endif

ASMSRC  = startup.S		\
		  sys_core.S    \
		  ihandlers.S	\
		  page_asm.S

ASMOBJS = $(ASMSRC:.S=.o)

HEADERS = $(wildcard *.h)

.PHONY: all
all : ${KERNEL} ${ACIENT} ${MIDDLE}
	make tools
	@if [ ! -e ${FSBOOT} ];   \
	  then touch ${FSBOOT};   \
	fi
	make ptest
	${KMKFS} ${ACIENT} ${MIDDLE} ${KERNEL} ${FSBOOT} ${INIT} ${INIT2}

#${KERNEL} : ${ACIENT} ${MIDDLE} ${OBJS} ${ASMOBJS} boot.ld
#	${LD} ${LFLAGS} ${ACIENT} ${MIDDLE} ${OBJS} ${ASMOBJS} -o $@
${KERNEL} : ${OBJS} ${ASMOBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} boot.ld
#	cd drivers; make
#	cd lib; make
#	cd net; make
#	cd usr; make
	${LD} ${LFLAGS} ${OBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${ASMOBJS} -o $@

${DRIVER_OBJS} :
	cd drivers; make

${KLIB_OBJS} :
	cd lib; make

${NET_OBJS} :
	cd net; make

KSIZE=`${GETSIZE} ${KERNEL}`
${ACIENT} : ${FIRST_BOOT_FILE} ${KERNEL}
	echo ${KSIZE}
	${AS} ${ASFLAGS} --defsym KERNEL_SECTS=${KSIZE} ${FIRST_BOOT_FILE} -o boota.o -a > ${LIST}/boota.lst
	${LD} ${BOOTA_LFLAGS} boota.o -o $@

${MIDDLE} : bootmiddle.S
	${AS} ${ASFLAGS} bootmiddle.S -I ${INCLUDE} -o bootmiddle.o -a > ${LIST}/bootm.lst
	${LD} ${BOOTM_LFLAGS} bootmiddle.o -o $@

boot.ld: boot.ld.S
	$(CC) -E -P boot.ld.S -I $(INCLUDE) -I . -o boot.ld 

%.o: %.S
	${AS} ${ASFLAGS} -I $(INCLUDE) -I . -o $@ $<

EXTRA_CFLAGS ?=

%.o: %.c ${INCLUDE}/*.h ${INCLUDE}/sodex/*.h
	$(CC) -D ${DEVICE}=DEFINED -c ${CFLAGS} ${EXTRA_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

remake:
	make clean; make

.PHONY: tools
tools: ${KMKFS} ${GETSIZE}

${KMKFS} : ${TOOLDIR}/kmkfs.cpp
	$(C++) -D PATH_USRBIN=\"${BASEDIR}/src/usr/bin/\" -o $@ ${TOOLDIR}/kmkfs.cpp
	chmod 755 $@

${GETSIZE} : ${TOOLDIR}/getsize.c
	/usr/bin/gcc -o $@ ${TOOLDIR}/getsize.c
	chmod 755 $@


.PHONY: ptest
ptest : ${PTEST} ${PTEST2}
${PTEST} : ${INITDIR}/ptest.S
	$(AS) ${ASFLAGS} -o ptest.o ${INITDIR}/ptest.S
	$(LD) ${PTEST_LFLAGS} -o $@ ptest.o
${PTEST2} : ${INITDIR}/ptest2.S
	$(AS) ${ASFLAGS} -o ptest2.o ${INITDIR}/ptest2.S
	$(LD) ${PTEST_LFLAGS} -o $@ ptest2.o

.PHONY: usb
usb:
	${AS} ${ASFLAGS} bootusb.S -o bootusb.o
	${LD} -m elf_i386 -Ttext 0x0 -o bootusb bootusb.o --oformat binary


TESTDIR = ${SRCDIR}/test
KTEST_OBJS = ${TESTDIR}/ktest.o

# Build and run kernel integration tests in QEMU
# Usage: make test-qemu
.PHONY: test-qemu ktest-build
test-qemu: ktest-build
	@echo "=== Running kernel integration tests in QEMU ==="
	@(qemu-system-i386 \
		-drive file=$(FSBOOT),format=raw \
		-nographic \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-no-reboot \
		-monitor none \
		2>/dev/null &); \
	QPID=$$!; sleep 10; kill $$QPID 2>/dev/null; wait $$QPID 2>/dev/null; \
	echo ""; \
	echo "=== KERNEL INTEGRATION TESTS DONE ==="

# Compile ktest.o with KTEST_BUILD
${TESTDIR}/ktest.o: ${TESTDIR}/ktest.c
	$(CC) -c ${CFLAGS} ${EXTRA_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

# Build kernel with ktest linked in (kernel.c also needs KTEST_BUILD)
ktest-build:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="-DKTEST_BUILD" ktest-link

ktest-link: ${OBJS} ${ASMOBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} $(KTEST_OBJS) boot.ld
	${LD} ${LFLAGS} ${OBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${ASMOBJS} $(KTEST_OBJS) -o ${KERNEL}
	$(MAKE) tools
	$(MAKE) ${ACIENT} ${MIDDLE}
	@if [ ! -e ${FSBOOT} ];   \
	  then touch ${FSBOOT};   \
	fi
	$(MAKE) ptest
	${KMKFS} ${ACIENT} ${MIDDLE} ${KERNEL} ${FSBOOT} ${INIT} ${INIT2}

.PHONY: clean
clean :
	cd usr; make clean
	cd drivers; make clean
	cd lib; make clean
	cd net; make clean
	rm -f ${OBJS} ${ASMOBJS} ${KERNEL} ${ACIENT} ${MIDDLE} ${KMKFS} ${FSBOOT} ${TESTDIR}/*.o
