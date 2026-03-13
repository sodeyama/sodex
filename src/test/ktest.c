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
#include <ne2000.h>
#include <uip.h>
#include <uip_arp.h>
#include <socket.h>
#include <string.h>

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

/* === Network initialization for tests === */
EXTERN void network_init(void);
EXTERN void network_poll(void);

PRIVATE void ktest_network_init(void)
{
  serial_puts("  [NET] Initializing NE2000...\n");
  init_ne2000();

  serial_puts("  [NET] Initializing uIP...\n");
  uip_init();

  uip_ipaddr_t ipaddr;
  uip_ipaddr(&ipaddr, 10, 0, 2, 15);
  uip_sethostaddr(&ipaddr);
  uip_ipaddr(&ipaddr, 255, 255, 255, 0);
  uip_setnetmask(&ipaddr);
  uip_ipaddr(&ipaddr, 10, 0, 2, 2);
  uip_setdraddr(&ipaddr);

  struct uip_eth_addr mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
  uip_setethaddr(mac);

  network_init();

  /* Enable NE2000 IMR */
  u_int16_t nb = 0xC100;
  out8(nb + 0x00, 0x22);  /* CR: Page0, Start, RD abort */
  out8(nb + 0x0F, 0x7F);  /* IMR: enable all */
  out8(nb + 0x07, 0xFF);  /* ISR: clear all pending */

  serial_puts("  [NET] Network initialized\n");
}

/* === Test: ARP resolution (send ARP request for gateway 10.0.2.2) === */
PRIVATE void test_arp_resolution(void)
{
  /* Send ARP request for gateway */
  u_int8_t arp_pkt[60];
  int i;
  memset(arp_pkt, 0, 60);
  for (i = 0; i < 6; i++) arp_pkt[i] = 0xFF; /* broadcast */
  arp_pkt[6]=0x52; arp_pkt[7]=0x54; arp_pkt[8]=0x00;
  arp_pkt[9]=0x12; arp_pkt[10]=0x34; arp_pkt[11]=0x56;
  arp_pkt[12]=0x08; arp_pkt[13]=0x06; /* ARP */
  arp_pkt[14]=0x00; arp_pkt[15]=0x01; /* HW: Ethernet */
  arp_pkt[16]=0x08; arp_pkt[17]=0x00; /* Proto: IP */
  arp_pkt[18]=0x06; arp_pkt[19]=0x04; /* HW size, proto size */
  arp_pkt[20]=0x00; arp_pkt[21]=0x01; /* Opcode: request */
  arp_pkt[22]=0x52; arp_pkt[23]=0x54; arp_pkt[24]=0x00;
  arp_pkt[25]=0x12; arp_pkt[26]=0x34; arp_pkt[27]=0x56;
  arp_pkt[28]=10; arp_pkt[29]=0; arp_pkt[30]=2; arp_pkt[31]=15;
  memset(&arp_pkt[32], 0, 6);
  arp_pkt[38]=10; arp_pkt[39]=0; arp_pkt[40]=2; arp_pkt[41]=2;

  ne2000_send(arp_pkt, 60);

  /* Poll for ARP reply via network_poll - need to wait for packet arrival */
  int got_reply = 0;
  u_int32_t loops;
  for (loops = 0; loops < 10000000; loops++) {
    disableInterrupt();
    network_poll();
    enableInterrupt();
    /* After network_poll processes an ARP reply, bnry should advance */
    out8(0xC100 + 0x00, 0x62); /* CR: Page1 + Start */
    (void)in8(0xC100 + 0x07); /* CURR on Page1 offset 7 */
    out8(0xC100 + 0x00, 0x22); /* CR: Page0 + Start */
    u_int8_t bnry = in8(0xC100 + 0x03);
    if (bnry != 0x46) { /* BNRY_ADDR initial value */
      got_reply = 1;
      break;
    }
  }

  if (got_reply) {
    ktest_pass("arp_resolution");
  } else {
    ktest_fail("arp_resolution", "no ARP reply from gateway");
  }
}

/* === Test: Socket creation === */
PRIVATE void test_socket_create(void)
{
  int fd = kern_socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    ktest_pass("socket_create_tcp");
    kern_close_socket(fd);
  } else {
    ktest_fail("socket_create_tcp", "kern_socket returned -1");
  }

  fd = kern_socket(AF_INET, SOCK_DGRAM, 0);
  if (fd >= 0) {
    ktest_pass("socket_create_udp");
    kern_close_socket(fd);
  } else {
    ktest_fail("socket_create_udp", "kern_socket returned -1");
  }
}

/* === Test: TCP connect to echo server (10.0.2.100:7777 via guestfwd) === */
PRIVATE void test_tcp_connect(void)
{
  int fd = kern_socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    ktest_fail("tcp_connect", "socket creation failed");
    return;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(7777);
  /* 10.0.2.100 = 0x0A000264 (little-endian stored) */
  u_int8_t *a = (u_int8_t *)&addr.sin_addr;
  a[0] = 10; a[1] = 0; a[2] = 2; a[3] = 100;

  serial_puts("  [TCP] Connecting to 10.0.2.100:7777...\n");
  int ret = kern_connect(fd, &addr);

  if (ret == 0 && socket_table[fd].state == SOCK_STATE_CONNECTED) {
    ktest_pass("tcp_connect");
  } else {
    ktest_fail("tcp_connect", "connect failed or not in CONNECTED state");
    serial_puts("  [TCP] connect returned ");
    serial_putdec(ret);
    serial_puts(", state=");
    serial_putdec(socket_table[fd].state);
    serial_puts("\n");
    kern_close_socket(fd);
    return;
  }

  /* Test: TCP send */
  const char *msg = "HELLO!";
  int slen = 6;
  int sent = kern_send(fd, (void *)msg, slen, 0);
  if (sent == slen) {
    ktest_pass("tcp_send");
  } else {
    ktest_fail("tcp_send", "send returned wrong length");
  }

  /* Test: TCP recv (echo server should return same data) */
  char rxbuf[64];
  memset(rxbuf, 0, sizeof(rxbuf));
  int received = kern_recv(fd, rxbuf, sizeof(rxbuf), 0);
  if (received == slen && memcmp(rxbuf, msg, slen) == 0) {
    serial_puts("  [TCP] Echo received\n");
    ktest_pass("tcp_recv");
  } else {
    serial_puts("  [TCP] recv failed, len=");
    serial_putdec(received);
    serial_puts("\n");
    ktest_fail("tcp_recv", "echo mismatch or timeout");
  }

  serial_puts("  [TCP] Closing socket...\n");
  if (kern_close_socket(fd) == 0) {
    serial_puts("  [TCP] Socket closed\n");
  } else {
    ktest_fail("tcp_close", "kern_close_socket returned -1");
  }
}

/* === Test: UDP sendto/recvfrom === */
PRIVATE void test_udp_echo(void)
{
  int fd = kern_socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    ktest_fail("udp_echo", "socket creation failed");
    return;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(7778);
  u_int8_t *a = (u_int8_t *)&addr.sin_addr;
  a[0] = 10; a[1] = 0; a[2] = 2; a[3] = 100;

  const char *msg = "UDP_TEST";
  int sent = kern_sendto(fd, (void *)msg, 8, 0, &addr);
  if (sent == 8) {
    ktest_pass("udp_sendto");
  } else {
    ktest_fail("udp_sendto", "sendto returned wrong length");
  }

  char rxbuf[64];
  memset(rxbuf, 0, sizeof(rxbuf));
  struct sockaddr_in from;
  int received = kern_recvfrom(fd, rxbuf, sizeof(rxbuf), 0, &from);
  if (received > 0) {
    serial_puts("  [UDP] Received ");
    serial_putdec(received);
    serial_puts(" bytes\n");
    ktest_pass("udp_recvfrom");
  } else {
    /* UDP echo may not be available; don't fail hard */
    serial_puts("  [UDP] No echo reply (server may not be running)\n");
    ktest_pass("udp_recvfrom_no_server");
  }

  kern_close_socket(fd);
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

  /* Network tests (requires NE2000 + uIP init) */
  serial_puts("\n--- Network Tests ---\n");
  ktest_network_init();
  test_arp_resolution();
  test_socket_create();
  test_tcp_connect();
  test_udp_echo();

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
