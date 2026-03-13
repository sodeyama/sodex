/*
 *  @File        ktest.c
 *  @Brief       Kernel integration test runner (runs inside QEMU)
 *
 *  Uses COM1 serial port for output and isa-debug-exit for QEMU shutdown.
 *  Build with KTEST_BUILD defined to enable.
 */

#include <kernel.h>
#include <io.h>
#include <memory.h>

/* Serial port (COM1) */
#define SERIAL_PORT       0x3F8
#define SERIAL_LSR_OFFSET 5
#define SERIAL_LSR_THRE   0x20  /* Transmitter Holding Register Empty */

/* QEMU ISA debug exit device */
#define QEMU_EXIT_PORT    0xF4
#define QEMU_EXIT_SUCCESS 0x00  /* exit code = (0 << 1) | 1 = 1 */
#define QEMU_EXIT_FAILURE 0x01  /* exit code = (1 << 1) | 1 = 3 */

PRIVATE int ktest_passed = 0;
PRIVATE int ktest_failed = 0;

PRIVATE void serial_init(void)
{
  out8(SERIAL_PORT + 1, 0x00);  /* Disable interrupts */
  out8(SERIAL_PORT + 3, 0x80);  /* Enable DLAB */
  out8(SERIAL_PORT + 0, 0x01);  /* Baud rate divisor lo (115200) */
  out8(SERIAL_PORT + 1, 0x00);  /* Baud rate divisor hi */
  out8(SERIAL_PORT + 3, 0x03);  /* 8N1 */
  out8(SERIAL_PORT + 2, 0xC7);  /* Enable FIFO */
  out8(SERIAL_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */
}

PRIVATE void serial_putc(char c)
{
  while (!(in8(SERIAL_PORT + SERIAL_LSR_OFFSET) & SERIAL_LSR_THRE));
  out8(SERIAL_PORT, c);
}

PRIVATE void serial_puts(const char *s)
{
  while (*s) {
    if (*s == '\n')
      serial_putc('\r');
    serial_putc(*s++);
  }
}

PRIVATE void serial_putdec(int val)
{
  char buf[12];
  int len = 0;
  unsigned int uval;
  if (val < 0) {
    serial_putc('-');
    uval = (unsigned int)(-val);
  } else {
    uval = (unsigned int)val;
  }
  if (uval == 0) {
    serial_putc('0');
    return;
  }
  while (uval > 0) {
    buf[len++] = '0' + (uval % 10);
    uval /= 10;
  }
  int i;
  for (i = len - 1; i >= 0; i--)
    serial_putc(buf[i]);
}

PRIVATE void ktest_pass(const char *name)
{
  serial_puts("  [PASS] ");
  serial_puts(name);
  serial_puts("\n");
  ktest_passed++;
}

PRIVATE void ktest_fail(const char *name, const char *reason)
{
  serial_puts("  [FAIL] ");
  serial_puts(name);
  serial_puts(": ");
  serial_puts(reason);
  serial_puts("\n");
  ktest_failed++;
}

/* === Test: kalloc/kfree basic === */
PRIVATE void test_memory_kalloc_kfree(void)
{
  void *p1 = kalloc(1024);
  void *p2 = kalloc(2048);

  if (p1 && p2 && p1 != p2) {
    ktest_pass("memory_kalloc_basic");
  } else {
    ktest_fail("memory_kalloc_basic", "kalloc returned NULL or same pointer");
  }

  kfree(p1);
  kfree(p2);

  void *p3 = kalloc(1024);
  if (p3) {
    ktest_pass("memory_realloc_after_free");
  } else {
    ktest_fail("memory_realloc_after_free", "kalloc after free returned NULL");
  }
  kfree(p3);
}

/* === Test: aalloc aligned allocation === */
PRIVATE void test_memory_aalloc(void)
{
  void *p = aalloc(256, 12);  /* 4KB aligned */
  if (p && ((u_int32_t)p & 0xFFF) == 0) {
    ktest_pass("memory_aalloc_4k_aligned");
  } else {
    ktest_fail("memory_aalloc_4k_aligned", "not 4KB aligned");
  }
  if (p) afree(p);
}

/* === Test: kalloc many small blocks === */
PRIVATE void test_memory_many_allocs(void)
{
  void *ptrs[32];
  int i;
  int ok = 1;
  for (i = 0; i < 32; i++) {
    ptrs[i] = kalloc(64);
    if (!ptrs[i]) { ok = 0; break; }
  }
  if (ok) {
    ktest_pass("memory_many_small_allocs");
  } else {
    ktest_fail("memory_many_small_allocs", "kalloc returned NULL");
  }
  for (i = 0; i < 32; i++) {
    if (ptrs[i]) kfree(ptrs[i]);
  }
}

/* === Test: GDT is loaded (SGDT should return non-zero base) === */
PRIVATE void test_gdt_loaded(void)
{
  u_int8_t gdtr[6];
  asm volatile("sgdt %0" : "=m"(gdtr));
  u_int32_t base = *(u_int32_t*)(gdtr + 2);
  u_int16_t limit = *(u_int16_t*)(gdtr);

  if (limit > 0 && base != 0) {
    ktest_pass("gdt_loaded");
  } else {
    ktest_fail("gdt_loaded", "GDT base or limit is zero");
  }
}

/* === Test: IDT is loaded === */
PRIVATE void test_idt_loaded(void)
{
  u_int8_t idtr[6];
  asm volatile("sidt %0" : "=m"(idtr));
  u_int32_t base = *(u_int32_t*)(idtr + 2);
  u_int16_t limit = *(u_int16_t*)(idtr);

  if (limit > 0 && base != 0) {
    ktest_pass("idt_loaded");
  } else {
    ktest_fail("idt_loaded", "IDT base or limit is zero");
  }
}

/* === Test: Paging is enabled (CR0 bit 31) === */
PRIVATE void test_paging_enabled(void)
{
  u_int32_t cr0;
  asm volatile("movl %%cr0, %0" : "=r"(cr0));
  if (cr0 & (1 << 31)) {
    ktest_pass("paging_enabled");
  } else {
    ktest_fail("paging_enabled", "CR0.PG bit not set");
  }
}

/* === Test: Interrupts are enabled (EFLAGS.IF) === */
PRIVATE void test_interrupts_enabled(void)
{
  u_int32_t eflags;
  asm volatile("pushfl; popl %0" : "=r"(eflags));
  if (eflags & (1 << 9)) {
    ktest_pass("interrupts_enabled");
  } else {
    ktest_fail("interrupts_enabled", "EFLAGS.IF not set");
  }
}

/* === Test: Kernel is in higher half (address >= 0xC0000000) === */
PRIVATE void test_higher_half(void)
{
  u_int32_t addr = (u_int32_t)&ktest_passed;
  if (addr >= 0xC0000000) {
    ktest_pass("higher_half_mapping");
  } else {
    ktest_fail("higher_half_mapping", "kernel not in higher half");
  }
}

/* === Main test runner === */
PUBLIC void run_kernel_tests(void)
{
  serial_init();
  serial_puts("=== Kernel Integration Tests ===\n");

  /* CPU/descriptor tests */
  test_gdt_loaded();
  test_idt_loaded();
  test_paging_enabled();
  test_interrupts_enabled();
  test_higher_half();

  /* Memory management tests */
  test_memory_kalloc_kfree();
  test_memory_aalloc();
  test_memory_many_allocs();

  /* Summary */
  serial_puts("\n--- Results: ");
  serial_putdec(ktest_passed);
  serial_puts("/");
  serial_putdec(ktest_passed + ktest_failed);
  serial_puts(" passed ---\n");

  if (ktest_failed == 0) {
    serial_puts("=== ALL KERNEL TESTS PASSED ===\n");
    out8(QEMU_EXIT_PORT, QEMU_EXIT_SUCCESS);
  } else {
    serial_puts("=== SOME KERNEL TESTS FAILED ===\n");
    out8(QEMU_EXIT_PORT, QEMU_EXIT_FAILURE);
  }

  /* If isa-debug-exit is not available, halt */
  disableInterrupt();
  for(;;);
}
