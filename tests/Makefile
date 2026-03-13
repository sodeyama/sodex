CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -I. -Imocks \
	-fno-builtin -D_FORTIFY_SOURCE=0 \
	-Wno-incompatible-library-redeclaration

SRC = ../src

TESTS = test_lib test_memb test_memory test_vga test_ext3fs test_clock test_ne2000 test_ne2000_irq test_kernel_net test_netpoll

.PHONY: all clean test

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

# VGA _kvsnprintf format test
test_vga: test_vga.c test_vga_vga.o
	$(CC) $(CFLAGS) -o $@ test_vga.c test_vga_vga.o

test_vga_vga.o: $(SRC)/vga.c
	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $@ $<

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

clean:
	rm -f $(TESTS) *.o
