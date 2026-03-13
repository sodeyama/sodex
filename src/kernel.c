/*
 *  @File        kernel.c
 *  @Brief       kernel main
 *  
 *  @Author      Sodex
 *  @Revision    0.0.3
 *  @License     BSD License
 *  @Date        create: 2007/04/19
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <sodex/list.h>
#include <kernel.h>
#include <vga.h>
#include <descriptor.h>
#include <io.h>
#include <memory.h>
#include <lib.h>
#include <dma.h>
#include <floppy.h>
#include <process.h>
#include <rs232c.h>
#include <ext3fs.h>
#include <fs.h>
#include <page.h>
#include <syscall.h>
#include <ne2000.h>
#include <pci.h>
#include <uhci-hcd.h>
#include <mstorage.h>
#include <scsi.h>
#include <ata.h>
#include <uip.h>
#include <uip_arp.h>
#ifdef KTEST_BUILD
PUBLIC void run_kernel_tests(void);
#endif

EXTERN void network_init(void);
EXTERN void network_poll(void);

PRIVATE void mem_test();
PRIVATE void fdc_test();
PRIVATE void fs_test();
PRIVATE void dlist_test();
PRIVATE void serial_test();
PRIVATE void ext3_test();
PRIVATE void syscall_test();

PRIVATE void dbg_serial_init(void)
{
  out8(0x3F8 + 1, 0x00);
  out8(0x3F8 + 3, 0x80);
  out8(0x3F8 + 0, 0x01);
  out8(0x3F8 + 1, 0x00);
  out8(0x3F8 + 3, 0x03);
  out8(0x3F8 + 2, 0xC7);
  out8(0x3F8 + 4, 0x0B);
}

PRIVATE void dbg_serial_putc(char c)
{
  while (!(in8(0x3F8 + 5) & 0x20));
  out8(0x3F8, c);
}

PRIVATE void dbg_serial_puts(const char *s)
{
  while (*s) {
    if (*s == '\n') dbg_serial_putc('\r');
    dbg_serial_putc(*s++);
  }
}

PUBLIC void start_kernel()
{
  // KERNEL setting
  init_screen();
  dbg_serial_init();
  dbg_serial_puts("SERIAL: kernel started\n");

  _kputs(" BOOTLOADER: Kernel was loaded\n");
  _kputs("\n");
  _kputs(" KERNEL: SETUP 32bit Protected mode\n");
  _kputs(" KERNEL: SETUP SCREEN\n");
  init_setupgdt();
  _kputs(" KERNEL: SETUP GDT\n");
  init_setupidthandlers();
  _kputs(" KERNEL: SETUP IDT HANDLERS\n");
  init_setupidt();
  _kputs(" KERNEL: SETUP IDT\n");
  init_pit();
  _kputs(" KERNEL: SETUP PIT\n");
  init_mem();
  _kputs(" KERNEL: SETUP KERNEL MEMORY\n");

#ifdef KTEST_BUILD
  run_kernel_tests();
  /* not reached */
#endif

  init_key();
  _kputs(" KERNEL: SETUP KEY\n");

  init_dma();
  _kputs(" KERNEL: SETUP DMA\n");
#ifdef FDC_DEVICE
  init_fdc();
  _kputs(" KERNEL: SETUP FDC\n");
#endif
  init_paging();
  _kputs(" KERNEL: SETUP PAGING\n");
  _kputs(" KERNEL: SETUP PCI\n");
  init_pci();
#ifdef USB_DEVICE
  ata_init();
  rawdev.raw_read = ata_read;
  rawdev.raw_write = ata_write;
  _kputs(" KERNEL: SETUP ATA\n");
#endif
  init_ext3fs();
  _kputs(" KERNEL: SETUP EXT3FS\n");

  init_syscall();
  _kputs(" KERNEL: SETUP SYSTEMCALL\n");
  dbg_serial_puts("SERIAL: before init_ne2000\n");

  init_ne2000();
  _kputs(" KERNEL: SETUP NE2000\n");
  dbg_serial_puts("SERIAL: after init_ne2000\n");

  /* Immediate NE2000 register dump right after init */
  {
    EXTERN int io_base;
    u_int16_t ne_base = 0xC100;
    _kprintf("io_base=%x ne_base=%x\n", io_base, ne_base);
    /* Write IMR directly and verify via ISR test */
    out8(ne_base + O_CR, CR_PAGE0|CR_STA|CR_RD_STOP);
    out8(ne_base + O_IMR, IMR_ALL);
    u_int8_t cr_test = in8(ne_base + 0x00);

    /* Write a known pattern to PIC and read back */
    u_int8_t pic1_before = in8(0x21);
    out8(0x21, 0xAA);
    u_int8_t pic1_after = in8(0x21);
    out8(0x21, pic1_before); /* restore */

    dbg_serial_puts("TEST CR=");
    for (int b=7; b>=0; b--) dbg_serial_putc((cr_test & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" PIC1 before=");
    for (int b=7; b>=0; b--) dbg_serial_putc((pic1_before & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" after_AA=");
    for (int b=7; b>=0; b--) dbg_serial_putc((pic1_after & (1<<b)) ? '1' : '0');
    dbg_serial_puts("\n");
    u_int8_t cr0 = in8(ne_base + 0x00);
    u_int8_t isr0 = in8(ne_base + 0x07);
    _kprintf("NE2K-POST: CR=%x ISR=%x IMR=NA\n", cr0, isr0);
    u_int8_t pic1 = in8(0x21);
    u_int8_t pic2 = in8(0xA1);
    _kprintf("PIC-POST: m=%x s=%x\n", pic1, pic2);
    /* binary to serial */
    dbg_serial_puts("POST CR=");
    for (int b=7; b>=0; b--) dbg_serial_putc((cr0 & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" ISR=");
    for (int b=7; b>=0; b--) dbg_serial_putc((isr0 & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" IMR=QEMU-N/A");
    dbg_serial_puts(" PICm=");
    for (int b=7; b>=0; b--) dbg_serial_putc((pic1 & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" PICs=");
    for (int b=7; b>=0; b--) dbg_serial_putc((pic2 & (1<<b)) ? '1' : '0');
    dbg_serial_puts("\n");
  }

  /* uIP TCP/IP stack initialization */
  dbg_serial_puts("SERIAL: before uip_init\n");
  uip_init();
  {
    uip_ipaddr_t ipaddr;
    uip_ipaddr(&ipaddr, 10, 0, 2, 15);
    uip_sethostaddr(&ipaddr);
    uip_ipaddr(&ipaddr, 255, 255, 255, 0);
    uip_setnetmask(&ipaddr);
    uip_ipaddr(&ipaddr, 10, 0, 2, 1);
    uip_setdraddr(&ipaddr);
  }
  {
    struct uip_eth_addr mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    uip_setethaddr(mac);
  }
  _kputs(" KERNEL: SETUP uIP\n");

  network_init();
  _kputs(" KERNEL: SETUP NETWORK\n");

  /* Test NE2000 TX/RX before process scheduler takes over */
  {
    /* Force IMR to enable all interrupts */
    {
      u_int16_t nb = 0xC100;
      out8(nb + O_CR, CR_PAGE0|CR_STA|CR_RD_STOP);
      out8(nb + O_IMR, 0x7F);
      out8(nb + O_ISR, 0xFF);  /* Clear all pending */
    }
    dbg_serial_puts("SENDING ARP REQUEST\n");
    u_int8_t arp_pkt[42];
    int i;
    for (i=0; i<6; i++) arp_pkt[i] = 0xFF;
    arp_pkt[6]=0x52; arp_pkt[7]=0x54; arp_pkt[8]=0x00;
    arp_pkt[9]=0x12; arp_pkt[10]=0x34; arp_pkt[11]=0x56;
    arp_pkt[12]=0x08; arp_pkt[13]=0x06;
    arp_pkt[14]=0x00; arp_pkt[15]=0x01;
    arp_pkt[16]=0x08; arp_pkt[17]=0x00;
    arp_pkt[18]=0x06; arp_pkt[19]=0x04;
    arp_pkt[20]=0x00; arp_pkt[21]=0x01;
    arp_pkt[22]=0x52; arp_pkt[23]=0x54; arp_pkt[24]=0x00;
    arp_pkt[25]=0x12; arp_pkt[26]=0x34; arp_pkt[27]=0x56;
    arp_pkt[28]=10; arp_pkt[29]=0; arp_pkt[30]=2; arp_pkt[31]=15;
    for (i=32; i<38; i++) arp_pkt[i] = 0x00;
    arp_pkt[38]=10; arp_pkt[39]=0; arp_pkt[40]=2; arp_pkt[41]=2;
    /* Pad to minimum Ethernet frame size (60 bytes) */
    u_int8_t frame[60];
    memset(frame, 0, 60);
    memcpy(frame, arp_pkt, 42);
    ne2000_send(frame, 60);
    dbg_serial_puts("ARP SENT\n");

    /* Check CR and ISR immediately after send */
    u_int16_t ne_base = 0xC100;
    {
      u_int8_t cr_after = in8(ne_base + 0x00);
      u_int8_t isr_after = in8(ne_base + 0x07);
      dbg_serial_puts("AFTER-TX CR=");
      for (int b=7; b>=0; b--) dbg_serial_putc((cr_after & (1<<b)) ? '1' : '0');
      dbg_serial_puts(" ISR=");
      for (int b=7; b>=0; b--) dbg_serial_putc((isr_after & (1<<b)) ? '1' : '0');
      dbg_serial_puts("\n");
    }

    /* Poll for PTX or PRX */
    u_int32_t loops;
    for (loops = 0; loops < 50000000; loops++) {
      u_int8_t isr = in8(ne_base + 0x07);
      if (isr) {
        dbg_serial_puts("ISR=");
        for (int b=7; b>=0; b--) dbg_serial_putc((isr & (1<<b)) ? '1' : '0');
        dbg_serial_puts("\n");
        out8(ne_base + 0x07, isr);
        if (isr & 0x01) {
          dbg_serial_puts("GOT PRX!\n");
          ne2000_rx_pending = 1;
          network_poll();
        }
        if (isr & 0x02) {
          dbg_serial_puts("GOT PTX\n");
        }
        if (isr & 0x04) dbg_serial_puts("RXE\n");
        if (isr & 0x08) dbg_serial_puts("TXE\n");
        break;
      }
    }
    if (loops == 50000000) {
      dbg_serial_puts("NO ISR after 50M polls\n");
      u_int8_t tsr = in8(ne_base + 0x04);
      dbg_serial_puts("TSR=");
      for (int b=7; b>=0; b--) dbg_serial_putc((tsr & (1<<b)) ? '1' : '0');
      dbg_serial_puts("\n");
    }
    /* Check if IRQ handler was called */
    {
      EXTERN volatile u_int32_t ne2k_irq_count;
      EXTERN volatile u_int8_t ne2k_last_isr;
      dbg_serial_puts("IRQ count=");
      /* Print count as decimal */
      {
        u_int32_t n = ne2k_irq_count;
        if (n == 0) { dbg_serial_putc('0'); }
        else {
          char d[10]; int di = 0;
          while (n) { d[di++] = '0' + (n % 10); n /= 10; }
          while (di--) dbg_serial_putc(d[di]);
        }
      }
      dbg_serial_puts(" last_isr=");
      for (int b=7; b>=0; b--) dbg_serial_putc((ne2k_last_isr & (1<<b)) ? '1' : '0');
      dbg_serial_puts("\n");
    }
  }

  init_process();
  _kputs(" KERNEL: SETUP PROCESS\n");
  _kputs(" KERNEL: SETUP SIGNAL\n");

  _kputs("\n");

  {
    /* NE2000 register debug dump (once) */
    u_int16_t ne_base = 0xC100;
    u_int8_t cr = in8(ne_base + 0x00);
    u_int8_t isr = in8(ne_base + 0x07);
    _kprintf("NE2K: CR=%x ISR=%x IMR=NA\n", cr, isr);

    /* PIC mask debug */
    u_int8_t pic1 = in8(0x21);
    u_int8_t pic2 = in8(0xA1);
    _kprintf("PIC: master=%x slave=%x\n", pic1, pic2);

    /* Output to serial as binary bit patterns */
    dbg_serial_puts("NE2K CR=");
    for (int b=7; b>=0; b--) dbg_serial_putc((cr & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" ISR=");
    for (int b=7; b>=0; b--) dbg_serial_putc((isr & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" IMR=QEMU-N/A");
    dbg_serial_puts("\nPIC m=");
    for (int b=7; b>=0; b--) dbg_serial_putc((pic1 & (1<<b)) ? '1' : '0');
    dbg_serial_puts(" s=");
    for (int b=7; b>=0; b--) dbg_serial_putc((pic2 & (1<<b)) ? '1' : '0');
    dbg_serial_puts("\n");
  }

  for(;;) {
    network_poll();
  }
}

PRIVATE void syscall_test()
{
  // system call test
  ext3_ls(rootdir);
  fs_stdio_open(&gtask);
  char buf[32];
  int fd = ext3_open("/ptest", O_RDWR, 0);
  lseek(fd, 0x100, SEEK_SET);
  ext3_read(fd, buf, 32);

  int size = 16;
  int fd_stdout = STDOUT_FILENO;
  asm __volatile__ (
                    " movl %0, %%ebx    \n\t"
                    " movl %1, %%ecx    \n\t"
                    " movl %2, %%edx    \n\t"
                    " movl $0x4, %%eax  \n\t"
                    " int $0x80"
                    :: "r" (fd_stdout), "r" (buf), "r" (size));
}

PRIVATE void ext3_test()
{
  int fd = ext3_open("/hoge", O_CREAT|O_RDWR, 0);
  char buf[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  ext3_write(fd, buf, 16);
  ext3_ls(rootdir);
  ext3_mkdir("/usr", 0);
  ext3_ls(rootdir);
  lseek(fd, 4, SEEK_SET);
  char read[16];
  ext3_read(fd, read, 16);
  int i;
  for (i=0; i<16; i++)
    _kprintf("%x ", read[i]);
  _kputc('\n');
}

/*
PRIVATE void mem_test()
{
  void *new1, *new2, *new3, *new4;
  new1 = kalloc(512);
  if (new1 == NULL)
    _kputs("kalloc error\n");
  _kprint_mem();
  
  new2 = kalloc(256);
  if (new2 == NULL)
    _kputs("kalloc error\n");
  _kprint_mem();

  new3 = kalloc(256);
  if (new3 == NULL)
    _kputs("kalloc error\n");
  _kprint_mem();

  new4 = kalloc(1024);
  if (new4 == NULL)
    _kputs("kalloc error\n");
  _kprint_mem();

  kfree(new2);
  _kprint_mem();

  kfree(new3);
  _kprint_mem();

  kfree(new4);
  _kprint_mem();
}

PRIVATE void fdc_test()
{
  //fdc_rowread(0, 0, 1); // head, track, sector
  char buf[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
  fdc_rowwrite(buf, 0, 8, 9); // buf, head, track, sector
  fdc_rowread(0, 8, 9);
}

PRIVATE void fs_test()
{
  //ext3_ls(rootdir);
  _kputs("file open\n");
  int fd = ext3_open("/bootm.bin", 0, 0);
  if (fd == FS_OPEN_FAIL) {
    _kprintf("ext3_open error\n");
  }
  _kprintf("filename:%s\n", gtask.fs_fd[fd]->f_dentry->d_name);

  char* buf = (char*)kalloc(64);
  memset(buf, 0, 64);

  _kputs("file read\n");
  ext3_read(fd, buf, 64);

  _kputs("hexdump\n");
  int i;
  for (i = 0; i < 64; i++) {
    _kprintb(buf[i]);
    _kputc(' ');
    if ((i+1)%8 == 0) _kputc('\n');
  }
  kfree(buf);
}

PRIVATE void dlist_test()
{
  struct hoge {
    int a;
    struct dlist_set dlist;
  };
  struct hoge h1, h2, h3;
  init_dlist_set(&h1.dlist);
  init_dlist_set(&h2.dlist);
  init_dlist_set(&h3.dlist);
  h1.a = 1;
  h2.a = 2;
  h3.a = 3;
  dlist_insert_before(&(h2.dlist), &(h1.dlist));
  dlist_insert_before(&(h3.dlist), &(h1.dlist));

  struct dlist_set* p;
  dlist_for_each(p, &(h1.dlist)) {
    struct hoge* hoge = dlist_entry(p, struct hoge, dlist);
    _kprintf("member a:%x\n", hoge->a);
  }
}

PRIVATE void serial_test()
{
  u_int8_t x = 0x33;
  com1_printf("hogehoge = %x\r\n", x);
}
*/
