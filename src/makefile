include makefile.inc

DEVICE = USB_DEVICE
#DEVICE = FDC_DEVICE

VPATH = . ${DRIVERDIR}

KERNEL  = ${BIN}/kernel.bin
ACIENT  = ${BIN}/boota.bin
MIDDLE  = ${BIN}/bootm.bin
FSBOOT  = ${BIN}/fsboot.bin
FSBOOT_PROFILE = ${BIN}/fsboot.profile
INIT    = ${INITDIR}/bin/ptest
INIT2   = ${INITDIR}/bin/ptest2

# Tools
KMKFS   = ${BUILDDIR}/tools/kmkfs
GETSIZE = ${BUILDDIR}/tools/getsize
UDEV_GOTHIC_TTF = ${BASEDIR}/third_party/fonts/UDEVGothic-Regular.ttf
FONT8X16_HEADER = ${INCLUDE}/font8x16_data.h
FONT16X16_HEADER = ${INCLUDE}/font16x16_data.h
FONT_HEADERS = ${FONT8X16_HEADER} ${FONT16X16_HEADER}
FONT16X16_BASE_RANGES = 3000-303f,3040-309f,30a0-30ff,ff01-ff60,ffe0-ffe6
FONT_NARROW_CELL_WIDTH = 12
FONT_NARROW_CELL_HEIGHT = 24
FONT_WIDE_CELL_WIDTH = 24
FONT_WIDE_CELL_HEIGHT = 24
FONT_NARROW_SIZE = 19
FONT_WIDE_SIZE = 21
FONT_SUPERSAMPLE = 3
IME_DICT_SOURCE = ${BUILDDIR}/tools/ime_dictionary_source.tsv
IME_DICT_MANUAL_SOURCE = ${TOOLDIR}/ime_dictionary_manual.tsv
IME_DICT_SOURCE_TOOL = ${TOOLDIR}/build_ime_dictionary_source.py
IME_DICT_MOZC_DIR = ${BASEDIR}/third_party/dictionaries/mozc/dictionary_oss
IME_DICT_MOZC_SOURCES = $(wildcard ${IME_DICT_MOZC_DIR}/dictionary*.txt)
IME_DICT_TOOL = ${TOOLDIR}/mkimeblob.py
IME_DICT_BLOB = ${USRDIR}/bin/ime_dictionary.blob

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
SODEX_QEMU_MEM_MB ?= 1024
SODEX_RAM_CAP_MB ?= 0
SODEX_ROOTFS_OVERLAY ?= ${SRCDIR}/rootfs-overlay
SODEX_ADMIN_STATUS_TOKEN ?= status-secret
SODEX_ADMIN_CONTROL_TOKEN ?= control-secret
SODEX_ADMIN_ALLOW_IP ?= 10.0.2.2
SODEX_ADMIN_CONFIG_EXTRA ?=
SODEX_EXPECT_CONFIG_ERRORS ?= 0
SODEX_DEBUG_SHELL_PORT ?= 10024
SODEX_HOST_DEBUG_SHELL_PORT ?= 10024
SODEX_SSH_PORT ?= 10022
SODEX_SSH_PASSWORD ?= root-secret
SODEX_SSH_SIGNER_PORT ?= 0
SODEX_SSH_HOSTKEY_ED25519_SEED ?= 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
SODEX_SSH_RNG_SEED ?= ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100
MEMORY_SCALE_CFLAGS =
ROOTFS_OVERLAY_ARG = $(if $(strip $(SODEX_ROOTFS_OVERLAY)),$(SODEX_ROOTFS_OVERLAY))
BOOT_KERNEL_MAX_SECTS = 1240

ifneq ($(strip $(SODEX_RAM_CAP_MB)),0)
MEMORY_SCALE_CFLAGS += -DSODEX_RAM_CAP_MB=$(SODEX_RAM_CAP_MB)
endif

# === Build directory creation (done upfront via shell) ===
$(shell mkdir -p $(OBJDIR) $(BIN) $(LIST) $(LOGDIR) $(BUILDDIR)/tools $(OBJDIR)/test $(INITDIR)/bin)

# === Main targets ===

.PHONY: all usr-build regen-font8x16 regen-font16x16 regen-fonts
.PHONY: kernel-subdirs drivers-build lib-build net-build display-build tty-build
all : ${FONT_HEADERS} usr-build ${IME_DICT_BLOB} kernel-subdirs ${KERNEL} ${ACIENT} ${MIDDLE} tools
	@if [ ! -e ${FSBOOT} ]; then touch ${FSBOOT}; fi
	$(MAKE) ptest
	${KMKFS} ${ACIENT} ${MIDDLE} ${KERNEL} ${FSBOOT} ${INIT} ${INIT2} ${ROOTFS_OVERLAY_ARG}
	@printf 'rootfs_overlay=%s\n' "${ROOTFS_OVERLAY_ARG}" > ${FSBOOT_PROFILE}

regen-font8x16: ${FONT8X16_HEADER}

regen-font16x16: ${FONT16X16_HEADER}

regen-fonts: regen-font8x16 regen-font16x16

usr-build: ${FONT_HEADERS}
	cd usr; make

${FONT8X16_HEADER}: ${TOOLDIR}/mkfontpack.py ${UDEV_GOTHIC_TTF}
	python3 ${TOOLDIR}/mkfontpack.py \
		--font-path ${UDEV_GOTHIC_TTF} \
		--cell-width ${FONT_NARROW_CELL_WIDTH} \
		--cell-height ${FONT_NARROW_CELL_HEIGHT} \
		--font-size ${FONT_NARROW_SIZE} \
		--supersample ${FONT_SUPERSAMPLE} \
		--threshold 88 \
		--output-header $@ \
		--source-name UDEVGothic-Regular.ttf

${FONT16X16_HEADER}: ${TOOLDIR}/mkfontpack.py ${UDEV_GOTHIC_TTF} ${IME_DICT_SOURCE}
	python3 ${TOOLDIR}/mkfontpack.py \
		--font-path ${UDEV_GOTHIC_TTF} \
		--output-header $@ \
		--cell-width ${FONT_WIDE_CELL_WIDTH} \
		--cell-height ${FONT_WIDE_CELL_HEIGHT} \
		--font-size ${FONT_WIDE_SIZE} \
		--supersample ${FONT_SUPERSAMPLE} \
		--threshold 88 \
		--codepoint-ranges ${FONT16X16_BASE_RANGES} \
		--text-source ${IME_DICT_SOURCE} \
		--symbol-name font16x16_glyphs \
		--source-name UDEVGothic-Regular.ttf

${IME_DICT_SOURCE}: ${IME_DICT_SOURCE_TOOL} ${IME_DICT_MANUAL_SOURCE} ${IME_DICT_MOZC_SOURCES}
	python3 ${IME_DICT_SOURCE_TOOL} \
		--manual ${IME_DICT_MANUAL_SOURCE} \
		--mozc-dir ${IME_DICT_MOZC_DIR} \
		--max-cost 6000 \
		--output $@

${IME_DICT_BLOB}: ${IME_DICT_SOURCE} ${IME_DICT_TOOL}
	mkdir -p ${USRDIR}/bin
	python3 ${IME_DICT_TOOL} --input ${IME_DICT_SOURCE} --output $@

kernel-subdirs: drivers-build lib-build net-build display-build tty-build

drivers-build:
	cd drivers; make

lib-build:
	cd lib; make

net-build:
	cd net; make

display-build:
	cd display; make

tty-build:
	cd tty; make

${KERNEL} : kernel-subdirs ${FONT_HEADERS} ${OBJS} ${ASMOBJS} ${BUILDDIR}/boot.ld
	${LD} ${LFLAGS} ${OBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${DISPLAY_OBJS} ${TTY_OBJS} ${ASMOBJS} -o $@

# === Boot loader ===

KSIZE=`${GETSIZE} ${KERNEL}`
${ACIENT} : ${FIRST_BOOT_FILE} ${KERNEL} ${GETSIZE}
	@ksize=`${GETSIZE} ${KERNEL}`; \
	if [ "$$ksize" -gt "${BOOT_KERNEL_MAX_SECTS}" ]; then \
		echo "kernel.bin is too large for the current boot loader ($$ksize > ${BOOT_KERNEL_MAX_SECTS} sectors)"; \
		exit 1; \
	fi
	echo ${KSIZE}
	${AS} ${ASFLAGS} --defsym KERNEL_SECTS=${KSIZE} ${FIRST_BOOT_FILE} -o $(OBJDIR)/boota.o -a > ${LIST}/boota.lst
	${LD} ${BOOTA_LFLAGS} $(OBJDIR)/boota.o -o $@

${MIDDLE} : bootmiddle.S
	${AS} ${ASFLAGS} bootmiddle.S -I ${INCLUDE} -o $(OBJDIR)/bootmiddle.o -a > ${LIST}/bootm.lst
	${LD} ${BOOTM_LFLAGS} -Ttext 0x400 $(OBJDIR)/bootmiddle.o -o $@

# === Linker script preprocessing ===

${BUILDDIR}/boot.ld: boot.ld.S
	$(CC) -E -P boot.ld.S -I $(INCLUDE) -I . \
		-DSTARTUP_OBJ="\"$(OBJDIR)/startup.o\"" \
		-DKERNEL_OBJ="\"$(OBJDIR)/kernel.o\"" \
		-DVGA_OBJ="\"$(OBJDIR)/vga.o\"" \
		-o $@

# === Pattern rules (C and assembly -> build/obj/) ===

$(OBJDIR)/%.o: %.c ${FONT_HEADERS}
	$(CC) -D ${DEVICE}=DEFINED -c ${CFLAGS} ${EXTRA_CFLAGS} ${MEMORY_SCALE_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

$(OBJDIR)/%.o: %.S
	${AS} ${ASFLAGS} -I $(INCLUDE) -I . -o $@ $<

# === Build tools ===

.PHONY: tools
tools: ${KMKFS} ${GETSIZE}

${KMKFS} : ${TOOLDIR}/kmkfs.cpp
	$(CXX) -D PATH_USRBIN=\"${BASEDIR}/src/usr/bin/\" -o $@ ${TOOLDIR}/kmkfs.cpp
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

.PHONY: test-qemu test-qemu-memory test-qemu-user-memory test-qemu-term test-qemu-term-scrollback test-qemu-fs test-qemu-shell-io test-qemu-shell-programmable test-qemu-shell-usability test-qemu-cat-multifile test-qemu-shell-redirection-pager test-qemu-shell-completion test-qemu-vi test-qemu-ime test-qemu-ext3-large test-qemu-server test-qemu-debug-shell test-qemu-ssh test-qemu-init-rc test-qemu-init-failure test-qemu-service test-qemu-service-contract test-qemu-jobs test-qemu-curl-https test-qemu-unix-text-tools test-agent-bringup test-claude-smoke test-agent-full ktest-build
TERM_ROOTFS_OVERLAY = $(LOGDIR)/term-rootfs-overlay
TERM_FSBOOT = $(LOGDIR)/term-fsboot.bin
TERM_FSBOOT_PROFILE = $(LOGDIR)/term-fsboot.profile
TERM_SCROLLBACK_ROOTFS_OVERLAY = $(LOGDIR)/term-scrollback-rootfs-overlay
TERM_SCROLLBACK_FSBOOT = $(LOGDIR)/term-scrollback-fsboot.bin
TERM_SCROLLBACK_FSBOOT_PROFILE = $(LOGDIR)/term-scrollback-fsboot.profile

test-qemu: ktest-build
	@echo "=== Running kernel integration tests in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_ktest.py $(FSBOOT) $(LOGDIR)

test-qemu-memory: ktest-build
	@echo "=== Running memory scaling smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_memory_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-user-memory:
	@$(MAKE) clean
	@$(MAKE) all
	@echo "=== Running user memory smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_user_memory_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-term:
	@python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(TERM_ROOTFS_OVERLAY)
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(TERM_ROOTFS_OVERLAY) FSBOOT=$(TERM_FSBOOT) FSBOOT_PROFILE=$(TERM_FSBOOT_PROFILE) all
	@echo "=== Running rich terminal smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_terminal_smoke.py $(TERM_FSBOOT) $(LOGDIR)

test-qemu-term-scrollback:
	@python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(TERM_SCROLLBACK_ROOTFS_OVERLAY)
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(TERM_SCROLLBACK_ROOTFS_OVERLAY) FSBOOT=$(TERM_SCROLLBACK_FSBOOT) FSBOOT_PROFILE=$(TERM_SCROLLBACK_FSBOOT_PROFILE) all
	@echo "=== Running terminal scrollback smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_terminal_scrollback_smoke.py $(TERM_SCROLLBACK_FSBOOT) $(LOGDIR)

test-qemu-fs: all
	@echo "=== Running fs CRUD smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_fs_crud_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-ext3-large:
	@echo "=== Running ext3 large file smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_ext3_large_file_smoke.py $(LOGDIR)

test-qemu-shell-io: all
	@echo "=== Running shell IO smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_shell_io_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-shell-programmable: all
	@echo "=== Running programmable shell smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_shell_programmable_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-shell-usability: all
	@echo "=== Running shell usability smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_shell_usability_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-cat-multifile:
	@rm -rf $(LOGDIR)/cat-multifile-overlay
	@mkdir -p $(LOGDIR)/cat-multifile-overlay
	@cp -R $(BASEDIR)/src/rootfs-overlay/. $(LOGDIR)/cat-multifile-overlay/
	@mkdir -p $(LOGDIR)/cat-multifile-overlay/etc/init.d
	@cp $(BASEDIR)/src/test/data/cat_multifile_rcS.sh $(LOGDIR)/cat-multifile-overlay/etc/init.d/rcS.cat
	@printf 'initdefault:rescue\nsysinit:/etc/init.d/rcS.cat\nrespawn:rescue:/usr/bin/sh\n' > $(LOGDIR)/cat-multifile-overlay/etc/inittab
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/cat-multifile-overlay all
	@echo "=== Running cat multifile smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_cat_multifile_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-shell-redirection-pager: all
	@echo "=== Running shell redirection/pager smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_shell_redirection_pager_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-unix-text-tools:
	@rm -rf $(LOGDIR)/unix-text-tools-overlay
	@mkdir -p $(LOGDIR)/unix-text-tools-overlay/etc/init.d
	@cp -R $(BASEDIR)/src/rootfs-overlay/. $(LOGDIR)/unix-text-tools-overlay/
	@cp $(BASEDIR)/src/test/data/unix_text_tools_rcS.sh $(LOGDIR)/unix-text-tools-overlay/etc/init.d/rcS.unix-text
	@cp $(BASEDIR)/src/test/data/unix_text_tools_rcS_extra.sh $(LOGDIR)/unix-text-tools-overlay/etc/init.d/rcS.unix-text-extra
	@printf 'initdefault:rescue\nsysinit:/etc/init.d/rcS.unix-text\nrespawn:rescue:/usr/bin/sh\n' > $(LOGDIR)/unix-text-tools-overlay/etc/inittab
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/unix-text-tools-overlay all
	@echo "=== Running unix text tools smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_unix_text_tools_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-shell-completion:
	@$(MAKE) clean
	@python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/shell-completion-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/shell-completion-rootfs-overlay all
	@echo "=== Running shell completion smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_shell_completion_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-vi: all
	@echo "=== Running vi smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_vi_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-ime: all
	@echo "=== Running IME smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_ime_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-server:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_ADMIN_CONFIG_EXTRA=$(SODEX_ADMIN_CONFIG_EXTRA) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/server-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/server-rootfs-overlay all
	@echo "=== Running server runtime smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_EXPECT_CONFIG_ERRORS=$(SODEX_EXPECT_CONFIG_ERRORS) python3 $(BASEDIR)/src/test/run_qemu_server_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-debug-shell:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_DEBUG_SHELL_PORT=$(SODEX_DEBUG_SHELL_PORT) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/debug-shell-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/debug-shell-rootfs-overlay all
	@echo "=== Running debug shell smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_DEBUG_SHELL_PORT=$(SODEX_DEBUG_SHELL_PORT) SODEX_HOST_DEBUG_SHELL_PORT=$(SODEX_HOST_DEBUG_SHELL_PORT) python3 $(BASEDIR)/src/test/run_qemu_debug_shell_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-ssh:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_INITDEFAULT_RUNLEVEL=server-headless SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) SODEX_SSH_SIGNER_PORT=$(SODEX_SSH_SIGNER_PORT) SODEX_SSH_HOSTKEY_ED25519_SEED=$(SODEX_SSH_HOSTKEY_ED25519_SEED) SODEX_SSH_RNG_SEED=$(SODEX_SSH_RNG_SEED) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/ssh-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/ssh-rootfs-overlay all
	@echo "=== Running SSH smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_HOST_SSH_PORT=$(SODEX_HOST_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) SODEX_SSH_SIGNER_PORT=$(SODEX_SSH_SIGNER_PORT) python3 $(BASEDIR)/src/test/run_qemu_ssh_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-init-rc:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_INITDEFAULT_RUNLEVEL=server-headless SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) SODEX_SSH_SIGNER_PORT=$(SODEX_SSH_SIGNER_PORT) SODEX_SSH_HOSTKEY_ED25519_SEED=$(SODEX_SSH_HOSTKEY_ED25519_SEED) SODEX_SSH_RNG_SEED=$(SODEX_SSH_RNG_SEED) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/init-rc-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/init-rc-rootfs-overlay all
	@echo "=== Running init rc smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) SODEX_SSH_PORT=$(SODEX_SSH_PORT) python3 $(BASEDIR)/src/test/run_qemu_init_rc_smoke.py $(FSBOOT) $(LOGDIR)
	@$(MAKE) test-qemu-init-failure

test-qemu-init-failure:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_INITDEFAULT_RUNLEVEL=server-headless SODEX_INIT_SYSINIT=/etc/init.d/rcS.fail python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/init-failure-rootfs-overlay
	@mkdir -p $(LOGDIR)/init-failure-rootfs-overlay/etc/init.d
	@cp $(BASEDIR)/src/test/data/init_rc_fail.sh $(LOGDIR)/init-failure-rootfs-overlay/etc/init.d/rcS.fail
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/init-failure-rootfs-overlay all
	@echo "=== Running init failure smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_init_failure_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-service:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_INITDEFAULT_RUNLEVEL=server-headless SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) SODEX_SSH_SIGNER_PORT=$(SODEX_SSH_SIGNER_PORT) SODEX_SSH_HOSTKEY_ED25519_SEED=$(SODEX_SSH_HOSTKEY_ED25519_SEED) SODEX_SSH_RNG_SEED=$(SODEX_SSH_RNG_SEED) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/service-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/service-rootfs-overlay all
	@echo "=== Running service smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_HOST_SSH_PORT=$(SODEX_HOST_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) python3 $(BASEDIR)/src/test/run_qemu_service_smoke.py $(FSBOOT) $(LOGDIR)
	@$(MAKE) test-qemu-service-contract

test-qemu-service-contract:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_INITDEFAULT_RUNLEVEL=server-headless SODEX_INIT_SYSINIT=/etc/init.d/rcS.contract SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) SODEX_SSH_SIGNER_PORT=$(SODEX_SSH_SIGNER_PORT) SODEX_SSH_HOSTKEY_ED25519_SEED=$(SODEX_SSH_HOSTKEY_ED25519_SEED) SODEX_SSH_RNG_SEED=$(SODEX_SSH_RNG_SEED) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/service-contract-rootfs-overlay
	@mkdir -p $(LOGDIR)/service-contract-rootfs-overlay/etc/init.d
	@cp $(BASEDIR)/src/test/data/service_contract.sh $(LOGDIR)/service-contract-rootfs-overlay/etc/init.d/rcS.contract
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/service-contract-rootfs-overlay all
	@echo "=== Running service contract smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_service_contract_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-jobs:
	@$(MAKE) clean
	@SODEX_ADMIN_STATUS_TOKEN=$(SODEX_ADMIN_STATUS_TOKEN) SODEX_ADMIN_CONTROL_TOKEN=$(SODEX_ADMIN_CONTROL_TOKEN) SODEX_ADMIN_ALLOW_IP=$(SODEX_ADMIN_ALLOW_IP) SODEX_INITDEFAULT_RUNLEVEL=server-headless SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) SODEX_SSH_SIGNER_PORT=$(SODEX_SSH_SIGNER_PORT) SODEX_SSH_HOSTKEY_ED25519_SEED=$(SODEX_SSH_HOSTKEY_ED25519_SEED) SODEX_SSH_RNG_SEED=$(SODEX_SSH_RNG_SEED) python3 $(BASEDIR)/src/test/write_server_runtime_overlay.py $(LOGDIR)/jobs-rootfs-overlay
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/jobs-rootfs-overlay all
	@echo "=== Running job control smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) SODEX_SSH_PORT=$(SODEX_SSH_PORT) SODEX_HOST_SSH_PORT=$(SODEX_HOST_SSH_PORT) SODEX_SSH_PASSWORD=$(SODEX_SSH_PASSWORD) python3 $(BASEDIR)/src/test/run_qemu_job_control_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-curl-https:
	@$(MAKE) clean
	@mkdir -p $(LOGDIR)/curl-https-overlay/etc/init.d
	@printf '#!/bin/sh\ncurl https://www.biztex.co.jp -o /tmp/biztex.html\n' > $(LOGDIR)/curl-https-overlay/etc/init.d/rcS
	@printf 'console:rescue:/usr/bin/sh\nsysinit:/etc/init.d/rcS\n' > $(LOGDIR)/curl-https-overlay/etc/inittab
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/curl-https-overlay all
	@echo "=== Running curl HTTPS smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_curl_https_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-websearch:
	@$(MAKE) clean
	@mkdir -p $(LOGDIR)/websearch-overlay/etc/init.d
	@printf '#!/bin/sh\nwebsearch hello world\n' > $(LOGDIR)/websearch-overlay/etc/init.d/rcS
	@printf 'console:rescue:/usr/bin/sh\nsysinit:/etc/init.d/rcS\n' > $(LOGDIR)/websearch-overlay/etc/inittab
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/websearch-overlay all
	@echo "=== Running websearch smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_websearch_smoke.py $(FSBOOT) $(LOGDIR)

test-qemu-webfetch:
	@$(MAKE) clean
	@mkdir -p $(LOGDIR)/webfetch-overlay/etc/init.d
	@printf '#!/bin/sh\nwebfetch http://127.0.0.1:18081/article\n' > $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'echo AUDIT init_webfetch_article $$?\n' >> $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'webfetch -m 120 http://127.0.0.1:18081/long\n' >> $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'echo AUDIT init_webfetch_long $$?\n' >> $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'webfetch http://denied.invalid/article\n' >> $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'echo AUDIT init_webfetch_deny $$?\n' >> $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'echo AUDIT init_webfetch_smoke_done\n' >> $(LOGDIR)/webfetch-overlay/etc/init.d/rcS
	@printf 'console:rescue:/usr/bin/sh\nsysinit:/etc/init.d/rcS\n' > $(LOGDIR)/webfetch-overlay/etc/inittab
	@$(MAKE) SODEX_ROOTFS_OVERLAY=$(LOGDIR)/webfetch-overlay all
	@echo "=== Running webfetch smoke in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/src/test/run_qemu_webfetch_smoke.py $(FSBOOT) $(LOGDIR)

$(OBJDIR)/test/ktest.o: ${TESTDIR}/ktest.c
	$(CC) -c ${CFLAGS} ${EXTRA_CFLAGS} -I ${INCLUDE} -I . -o $@ $<

ktest-build:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="-DKTEST_BUILD" ktest-link

ktest-link: kernel-subdirs ${OBJS} ${ASMOBJS} $(KTEST_OBJS) ${BUILDDIR}/boot.ld
	${LD} ${LFLAGS} ${OBJS} ${DRIVER_OBJS} ${KLIB_OBJS} ${NET_OBJS} ${DISPLAY_OBJS} ${TTY_OBJS} ${ASMOBJS} $(KTEST_OBJS) -o ${KERNEL}
	$(MAKE) usr-build
	$(MAKE) tools
	$(MAKE) ${MIDDLE}
	@ksize=`${GETSIZE} ${KERNEL}`; \
	if [ "$$ksize" -gt "${BOOT_KERNEL_MAX_SECTS}" ]; then \
		echo "kernel.bin is too large ($$ksize > ${BOOT_KERNEL_MAX_SECTS} sectors)"; \
		exit 1; \
	fi
	echo `${GETSIZE} ${KERNEL}`
	${AS} ${ASFLAGS} --defsym KERNEL_SECTS=`${GETSIZE} ${KERNEL}` ${FIRST_BOOT_FILE} -o $(OBJDIR)/boota.o -a > ${LIST}/boota.lst
	${LD} ${BOOTA_LFLAGS} $(OBJDIR)/boota.o -o ${ACIENT}
	@if [ ! -e ${FSBOOT} ]; then touch ${FSBOOT}; fi
	$(MAKE) ptest
	${KMKFS} ${ACIENT} ${MIDDLE} ${KERNEL} ${FSBOOT} ${INIT} ${INIT2} ${ROOTFS_OVERLAY_ARG}

test-agent-bringup:
	@echo "=== Running agent bringup test (Phase A) in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/tests/run_agent_bringup.py

test-claude-smoke:
	@echo "=== Running Claude smoke test (Phase C) in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/tests/run_claude_smoke.py

test-agent-full:
	@echo "=== Running Agent Integration Test (Plan 18) in QEMU ==="
	@SODEX_QEMU_MEM_MB=$(SODEX_QEMU_MEM_MB) python3 $(BASEDIR)/tests/run_agent_integration.py

# === API key injection ===

ENVFILE = $(BASEDIR)/.env.local
CLAUDE_CONF = $(BASEDIR)/src/rootfs-overlay/etc/claude.conf

.PHONY: inject-api-key
inject-api-key:
	@if [ ! -f "$(ENVFILE)" ]; then \
		echo "ERROR: $(ENVFILE) not found."; \
		echo "Create it with: echo 'ANTHROPIC_API_KEY=sk-ant-...' > .env.local"; \
		exit 1; \
	fi
	@API_KEY=$$(grep '^ANTHROPIC_API_KEY=' "$(ENVFILE)" | head -1 | cut -d= -f2-); \
	if [ -z "$$API_KEY" ]; then \
		echo "ERROR: ANTHROPIC_API_KEY not found in $(ENVFILE)"; \
		exit 1; \
	fi; \
	echo "$$API_KEY" > "$(CLAUDE_CONF)"; \
	echo "API key injected into rootfs overlay ($(CLAUDE_CONF))"

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
	rm -f ${IME_DICT_BLOB}
	rm -rf ${BUILDDIR}
