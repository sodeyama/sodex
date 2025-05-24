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

PRIVATE void mem_test();
PRIVATE void fdc_test();
PRIVATE void fs_test();
PRIVATE void dlist_test();
PRIVATE void serial_test();
PRIVATE void ext3_test();
PRIVATE void syscall_test();

PUBLIC void start_kernel()
{
  // KERNEL setting
  init_screen();

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
  scsi_init();
  _kputs(" KERNEL: SETUP SCSI EMU\n");
  init_uhci();
  _kputs(" KERNEL: SETUP UHCI\n");
#endif
  init_ext3fs();
  _kputs(" KERNEL: SETUP EXT3FS\n");

  init_syscall();
  _kputs(" KERNEL: SETUP SYSTEMCALL\n");
  //init_ne2000();
  //_kputs(" KERNEL: SETUP NE2000\n");
  init_process();
  _kputs(" KERNEL: SETUP PROCESS\n");
  _kputs(" KERNEL: SETUP SIGNAL\n");

  _kputs("\n");


  /*
  char *buf = kalloc(512);
  memset(buf, 0, 512);
  scsi_write(3253, 1, buf);
  //scsi_read(10, 4, buf);
  int i;
  for (i = 0; i < 16; i++) {
    _kprintb(buf[i]);
  }
  */

  for(;;);
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
