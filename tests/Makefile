CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -I. -Imocks \
	-fno-builtin -D_FORTIFY_SOURCE=0 \
	-Wno-incompatible-library-redeclaration

SRC = ../src
FONT_HEADERS = $(SRC)/include/font_default.h $(SRC)/include/font8x16_data.h $(SRC)/include/font16x16_data.h
IME_HEADERS = $(SRC)/usr/include/ime.h $(SRC)/usr/include/ime_conversion.h \
	$(SRC)/usr/include/ime_dict_blob.h $(SRC)/usr/include/ime_dictionary.h \
	$(SRC)/usr/include/utf8.h

TESTS = test_lib test_memb test_memory test_memory_layout test_vga test_key test_tty test_terminal_surface test_cell_renderer test_font_default test_vt_parser test_vt_fixtures test_ext3fs test_clock test_ne2000 test_ne2000_irq test_kernel_net test_netpoll test_socket test_admin_parser test_http_server_parser test_debug_shell_parser test_server_runtime_shared test_ssh_crypto test_ssh_runtime_policy test_vi_buffer test_utf8 test_wcwidth test_ime_romaji test_ime_dict_blob test_ime_cache test_ime_dictionary test_ime_conversion test_eshell_parser

IME_FIXTURE = ime_dictionary_fixture.blob
IME_FIXTURE_SOURCE = ime_dictionary_fixture_source.tsv
IME_DICT_MANUAL_SOURCE = $(SRC)/tools/ime_dictionary_manual.tsv
IME_DICT_SOURCE_TOOL = $(SRC)/tools/build_ime_dictionary_source.py
IME_DICT_MOZC_DIR = ../third_party/dictionaries/mozc/dictionary_oss
IME_DICT_MOZC_SOURCES = $(wildcard $(IME_DICT_MOZC_DIR)/dictionary*.txt)

.PHONY: all clean test derive_ssh_keypair ssh_signer

all: $(TESTS)

test: $(TESTS)
	@echo "=== Running all tests ==="
	@failed=0; \
	for t in $(TESTS); do \
		echo ""; \
		echo "--- $$t ---"; \
		./$$t || failed=1; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "=== ALL TEST SUITES PASSED ==="; \
	else \
		echo "=== SOME TESTS FAILED ==="; \
		exit 1; \
	fi

# String/math functions test
test_lib: test_lib.c test_lib_string.o test_lib_math.o
	$(CC) $(CFLAGS) -o $@ test_lib.c test_lib_string.o test_lib_math.o

test_lib_string.o: $(SRC)/lib/string.c
	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $@ $<

test_lib_math.o: $(SRC)/lib/lib.c
	$(CC) $(CFLAGS) -DTEST_BUILD \
		-DPUBLIC= -Dinline= \
		-Dpow=sodex_pow -Dlogn=sodex_logn \
		-include mocks/mock_kernel.h \
		-c -o $@ $<

# Memory block allocation test
test_memb: test_memb.c test_memb_memb.o test_memb_string.o
	$(CC) $(CFLAGS) -o $@ test_memb.c test_memb_memb.o test_memb_string.o

test_memb_memb.o: $(SRC)/lib/memb.c
	$(CC) $(CFLAGS) -DTEST_BUILD \
		-I$(SRC)/include \
		-include mocks/mock_kernel.h \
		-c -o $@ $<

test_memb_string.o: $(SRC)/lib/string.c
	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $@ $<

# Memory management (kalloc/kfree) test
test_memory: test_memory.c test_memory_memory.o test_memory_string.o
	$(CC) $(CFLAGS) -o $@ test_memory.c test_memory_memory.o test_memory_string.o

test_memory_memory.o: $(SRC)/memory.c
	$(CC) $(CFLAGS) -DTEST_BUILD -DMEMDEBUG=1 \
		-I$(SRC)/include \
		-Imocks \
		-c -o $@ $<

test_memory_string.o: $(SRC)/lib/string.c
	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $@ $<

# Memory layout policy test
test_memory_layout: test_memory_layout.c test_memory_layout_layout.o test_memory_layout_string.o
	$(CC) $(CFLAGS) -I$(SRC)/include -o $@ test_memory_layout.c test_memory_layout_layout.o test_memory_layout_string.o

test_memory_layout_layout.o: $(SRC)/memory_layout.c
	$(CC) $(CFLAGS) -DTEST_BUILD \
		-I$(SRC)/include \
		-Imocks \
		-include mocks/mock_memory_deps.h \
		-c -o $@ $<

test_memory_layout_string.o: $(SRC)/lib/string.c
	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $@ $<

# VGA _kvsnprintf format test
test_vga: test_vga.c test_vga_vga.o test_vga_console.o test_vga_vga_text_backend.o
	$(CC) $(CFLAGS) -o $@ test_vga.c test_vga_vga.o test_vga_console.o test_vga_vga_text_backend.o

test_vga_vga.o: $(SRC)/vga.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_vga_console.o: $(SRC)/display/console.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_vga_vga_text_backend.o: $(SRC)/display/vga_text_backend.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

# Keyboard event/input adapter tests
test_key: test_key.c test_key_key.o
	$(CC) $(CFLAGS) -o $@ test_key.c test_key_key.o

test_key_key.o: $(SRC)/key.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

# TTY / PTY tests
test_tty: test_tty.c test_tty_tty.o
	$(CC) $(CFLAGS) -o $@ test_tty.c test_tty_tty.o

test_tty_tty.o: $(SRC)/tty/tty.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

# Terminal surface tests
test_terminal_surface: test_terminal_surface.c test_terminal_surface_surface.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_terminal_surface.c test_terminal_surface_surface.o

test_terminal_surface_surface.o: $(SRC)/usr/lib/libc/terminal_surface.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# Cell renderer tests
test_cell_renderer: test_cell_renderer.c test_cell_renderer_renderer.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_cell_renderer.c test_cell_renderer_renderer.o

test_cell_renderer_renderer.o: $(SRC)/usr/lib/libc/cell_renderer.c $(FONT_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# Shared default font lookup tests
test_font_default: test_font_default.c $(FONT_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_font_default.c

# VT parser tests
test_vt_parser: test_vt_parser.c test_vt_parser_surface.o test_vt_parser_parser.o test_vt_parser_utf8.o test_vt_parser_wcwidth.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_vt_parser.c test_vt_parser_surface.o test_vt_parser_parser.o test_vt_parser_utf8.o test_vt_parser_wcwidth.o

test_vt_parser_surface.o: $(SRC)/usr/lib/libc/terminal_surface.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_vt_parser_parser.o: $(SRC)/usr/lib/libc/vt_parser.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_vt_parser_utf8.o: $(SRC)/usr/lib/libc/utf8.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_vt_parser_wcwidth.o: $(SRC)/usr/lib/libc/wcwidth.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# VT fixture tests
test_vt_fixtures: test_vt_fixtures.c test_vt_parser_surface.o test_vt_parser_parser.o test_vt_parser_utf8.o test_vt_parser_wcwidth.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_vt_fixtures.c test_vt_parser_surface.o test_vt_parser_parser.o test_vt_parser_utf8.o test_vt_parser_wcwidth.o

# ext3fs bitmap/parse tests (standalone, no ext3fs.c dependency)
test_ext3fs: test_ext3fs.c
	$(CC) $(CFLAGS) -o $@ test_ext3fs.c

# clock_time / timer tests (Plan 01)
test_clock: test_clock.c test_clock_clockarch.o test_clock_timer.o
	$(CC) $(CFLAGS) -o $@ test_clock.c test_clock_clockarch.o test_clock_timer.o

test_clock_clockarch.o: $(SRC)/net/clock-arch.c
	$(CC) $(CFLAGS) -DTEST_BUILD \
		-DCLOCK_CONF_SECOND=100 \
		-I$(SRC)/include \
		-include mocks/mock_kernel.h \
		-c -o $@ $<

test_clock_timer.o: $(SRC)/net/timer.c
	$(CC) $(CFLAGS) -DTEST_BUILD \
		-DCLOCK_CONF_SECOND=100 \
		-I$(SRC)/include \
		-include mocks/mock_kernel.h \
		-c -o $@ $<

# NE2000 receive logic tests (Plan 02) - pure logic, no I/O deps
test_ne2000: test_ne2000.c
	$(CC) $(CFLAGS) -o $@ test_ne2000.c

# NE2000 IRQ handler logic tests (Plan 03) - pure logic
test_ne2000_irq: test_ne2000_irq.c
	$(CC) $(CFLAGS) -o $@ test_ne2000_irq.c

# Kernel network integration tests (Plan 04) - config validation
test_kernel_net: test_kernel_net.c
	$(CC) $(CFLAGS) -o $@ test_kernel_net.c

# Network polling logic tests (Plan 05)
test_netpoll: test_netpoll.c
	$(CC) $(CFLAGS) -o $@ test_netpoll.c

# Socket ring buffer, inet functions, checksum tests (Phase 9)
test_socket: test_socket.c
	$(CC) $(CFLAGS) -o $@ test_socket.c

# Admin text protocol parser/auth tests
test_admin_parser: test_admin_parser.c test_admin_parser_admin.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ test_admin_parser.c test_admin_parser_admin.o

test_admin_parser_admin.o: $(SRC)/net/admin_server.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

# HTTP parser/mapper tests
test_http_server_parser: test_http_server_parser.c test_http_server_parser_http.o test_http_server_parser_admin.o test_server_audit.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ test_http_server_parser.c test_http_server_parser_http.o test_http_server_parser_admin.o test_server_audit.o

test_http_server_parser_http.o: $(SRC)/net/http_server.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_http_server_parser_admin.o: $(SRC)/net/admin_server.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_server_audit.o: $(SRC)/net/server_audit.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_server_runtime_config.o: $(SRC)/net/server_runtime_config.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

# Debug shell parser/config tests
test_debug_shell_parser: test_debug_shell_parser.c test_debug_shell_parser_debug_shell.o test_debug_shell_parser_admin.o test_server_runtime_config.o test_server_audit.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ test_debug_shell_parser.c test_debug_shell_parser_debug_shell.o test_debug_shell_parser_admin.o test_server_runtime_config.o test_server_audit.o

test_debug_shell_parser_debug_shell.o: $(SRC)/net/debug_shell_server.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_debug_shell_parser_admin.o: $(SRC)/net/admin_server.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_server_runtime_shared: test_server_runtime_shared.c test_admin_parser_admin.o test_server_runtime_config.o test_server_audit.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ test_server_runtime_shared.c test_admin_parser_admin.o test_server_runtime_config.o test_server_audit.o

# SSH crypto primitives tests
test_ssh_crypto: test_ssh_crypto.c test_ssh_crypto_crypto.o test_ssh_crypto_tweetnacl.o test_ssh_crypto_sha256.o test_ssh_crypto_aes.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ test_ssh_crypto.c test_ssh_crypto_crypto.o test_ssh_crypto_tweetnacl.o test_ssh_crypto_sha256.o test_ssh_crypto_aes.o

test_ssh_crypto_crypto.o: $(SRC)/lib/ssh_crypto.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_ssh_crypto_tweetnacl.o: $(SRC)/lib/tweetnacl.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_ssh_crypto_sha256.o: $(SRC)/lib/sha256.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_ssh_crypto_aes.o: $(SRC)/lib/aes.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -c -o $@ $<

test_ssh_runtime_policy: test_ssh_runtime_policy.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ test_ssh_runtime_policy.c

derive_ssh_keypair: derive_ssh_keypair.c test_ssh_crypto_crypto.o test_ssh_crypto_tweetnacl.o test_ssh_crypto_sha256.o test_ssh_crypto_aes.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ derive_ssh_keypair.c test_ssh_crypto_crypto.o test_ssh_crypto_tweetnacl.o test_ssh_crypto_sha256.o test_ssh_crypto_aes.o

ssh_signer: ssh_signer.c test_ssh_crypto_crypto.o test_ssh_crypto_tweetnacl.o test_ssh_crypto_sha256.o test_ssh_crypto_aes.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/include -o $@ ssh_signer.c test_ssh_crypto_crypto.o test_ssh_crypto_tweetnacl.o test_ssh_crypto_sha256.o test_ssh_crypto_aes.o

# vi buffer tests
test_vi_buffer: test_vi_buffer.c test_vi_buffer_vi_buffer.o test_vi_buffer_utf8.o test_vi_buffer_wcwidth.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_vi_buffer.c test_vi_buffer_vi_buffer.o test_vi_buffer_utf8.o test_vi_buffer_wcwidth.o

test_vi_buffer_vi_buffer.o: $(SRC)/usr/lib/libc/vi_buffer.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_vi_buffer_utf8.o: $(SRC)/usr/lib/libc/utf8.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_vi_buffer_wcwidth.o: $(SRC)/usr/lib/libc/wcwidth.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# UTF-8 logic tests
test_utf8: test_utf8.c test_utf8_utf8.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_utf8.c test_utf8_utf8.o

test_utf8_utf8.o: $(SRC)/usr/lib/libc/utf8.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# Width calculation tests
test_wcwidth: test_wcwidth.c test_wcwidth_wcwidth.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_wcwidth.c test_wcwidth_wcwidth.o

test_wcwidth_wcwidth.o: $(SRC)/usr/lib/libc/wcwidth.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# IME romaji conversion tests
test_ime_romaji: test_ime_romaji.c test_ime_romaji_ime.o test_ime_romaji_utf8.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_ime_romaji.c test_ime_romaji_ime.o test_ime_romaji_utf8.o

test_ime_romaji_ime.o: $(SRC)/usr/lib/libc/ime_romaji.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_ime_romaji_utf8.o: $(SRC)/usr/lib/libc/utf8.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

$(IME_FIXTURE_SOURCE): $(IME_DICT_SOURCE_TOOL) $(IME_DICT_MANUAL_SOURCE) $(IME_DICT_MOZC_SOURCES)
	python3 $(IME_DICT_SOURCE_TOOL) \
		--manual $(IME_DICT_MANUAL_SOURCE) \
		--mozc-dir $(IME_DICT_MOZC_DIR) \
		--max-cost 6000 \
		--output $@

$(IME_FIXTURE): $(SRC)/tools/mkimeblob.py $(IME_FIXTURE_SOURCE)
	python3 $(SRC)/tools/mkimeblob.py --input $(IME_FIXTURE_SOURCE) --output $@

# IME blob lookup tests
test_ime_dict_blob: test_ime_dict_blob.c test_ime_dict_blob_blob.o $(IME_FIXTURE)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_ime_dict_blob.c test_ime_dict_blob_blob.o

test_ime_dict_blob_blob.o: $(SRC)/usr/lib/libc/ime_dict_blob.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# IME cache tests
test_ime_cache: test_ime_cache.c test_ime_cache_blob.o $(IME_FIXTURE)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_ime_cache.c test_ime_cache_blob.o

test_ime_cache_blob.o: $(SRC)/usr/lib/libc/ime_dict_blob.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# IME dictionary tests
test_ime_dictionary: test_ime_dictionary.c test_ime_dictionary_dict.o test_ime_dictionary_blob.o $(IME_FIXTURE)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_ime_dictionary.c test_ime_dictionary_dict.o test_ime_dictionary_blob.o

test_ime_dictionary_dict.o: $(SRC)/usr/lib/libc/ime_dictionary.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_ime_dictionary_blob.o: $(SRC)/usr/lib/libc/ime_dict_blob.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# IME conversion tests
test_ime_conversion: test_ime_conversion.c test_ime_conversion_ime.o test_ime_conversion_conv.o test_ime_conversion_dict.o test_ime_conversion_blob.o test_ime_conversion_utf8.o $(IME_FIXTURE)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_ime_conversion.c test_ime_conversion_ime.o test_ime_conversion_conv.o test_ime_conversion_dict.o test_ime_conversion_blob.o test_ime_conversion_utf8.o

test_ime_conversion_ime.o: $(SRC)/usr/lib/libc/ime_romaji.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_ime_conversion_conv.o: $(SRC)/usr/lib/libc/ime_conversion.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_ime_conversion_dict.o: $(SRC)/usr/lib/libc/ime_dictionary.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_ime_conversion_blob.o: $(SRC)/usr/lib/libc/ime_dict_blob.c $(IME_HEADERS)
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

test_ime_conversion_utf8.o: $(SRC)/usr/lib/libc/utf8.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

# eshell parser tests
test_eshell_parser: test_eshell_parser.c test_eshell_parser_parser.o
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -o $@ test_eshell_parser.c test_eshell_parser_parser.o

test_eshell_parser_parser.o: $(SRC)/usr/lib/libc/eshell_parser.c
	$(CC) $(CFLAGS) -DTEST_BUILD -idirafter $(SRC)/usr/include -c -o $@ $<

clean:
	rm -f $(TESTS) $(IME_FIXTURE) $(IME_FIXTURE_SOURCE) derive_ssh_keypair ssh_signer *.o
