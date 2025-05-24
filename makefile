include makefile.inc

DEVICE = USB_DEVICE
#DEVICE = FDC_DEVICE

VPATH = . ${DRIVERDIR}

#PTEST_LFLAGS = -Ttext 0x0 -Tdata 0x100 --oformat binary -nostdlib
PTEST_LFLAGS = -nostdlib

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
	cd usr; make
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
	${AS} --defsym KERNEL_SECTS=${KSIZE} ${FIRST_BOOT_FILE} -o boota.o -a > ${LIST}/boota.lst
	${LD} ${BOOTA_LFLAGS} boota.o -o $@

${MIDDLE} : bootmiddle.S
	${AS} bootmiddle.S -I ${INCLUDE} -o bootmiddle.o -a > ${LIST}/bootm.lst
	${LD} ${BOOTM_LFLAGS} bootmiddle.o -o $@

boot.ld: boot.ld.S
	$(CC) -E -P boot.ld.S -I $(INCLUDE) -I . -o boot.ld 

%.o: %.S
	${AS} ${ASFLAGS} -I $(INCLUDE) -I . -o $@ $<

%.o: %.c ${INCLUDE}/*.h ${INCLUDE}/sodex/*.h
	$(CC) -D ${DEVICE}=DEFINED -c ${CFLAGS} -I ${INCLUDE} -I . -o $@ $<

remake:
	make clean; make

.PHONY: tools
tools: ${KMKFS} ${GETSIZE}

${KMKFS} : ${TOOLDIR}/kmkfs.cpp
	$(C++) -D PATH_USRBIN=\"${BASEDIR}/src/usr/bin/\" -o $@ ${TOOLDIR}/kmkfs.cpp
	chmod 755 $@

${GETSIZE} : ${TOOLDIR}/getsize.c
	$(CC) -o $@ ${TOOLDIR}/getsize.c
	chmod 755 $@


.PHONY: ptest
ptest : ${PTEST} ${PTEST2}
${PTEST} : ${INITDIR}/ptest.S
	$(AS) -o ptest.o ${INITDIR}/ptest.S
	$(LD) ${PTEST_LFLAGS} -o $@ ptest.o
${PTEST2} : ${INITDIR}/ptest2.S
	$(AS) -o ptest2.o ${INITDIR}/ptest2.S
	$(LD) ${PTEST_LFLAGS} -o $@ ptest2.o

.PHONY: usb
usb:
	${AS} bootusb.S -o bootusb.o
	${LD} -Ttext 0x0 -o bootusb bootusb.o --oformat binary


.PHONY: clean
clean :
	cd usr; make clean
	cd drivers; make clean
	cd lib; make clean
	cd net; make clean
	rm -f ${OBJS} ${ASMOBJS} ${KERNEL} ${ACIENT} ${MIDDLE} ${KMKFS} ${FSBOOT}
