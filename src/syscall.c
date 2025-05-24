/*
 *  @File syscall.c
 *  @Brief system call functions
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/07/09  update: 2007/07/09
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <syscall.h>
#include <ihandlers.h>
#include <descriptor.h>
#include <vga.h>
#include <ext3fs.h>
#include <fs.h>
#include <process.h>
#include <execve.h>
#include <elfloader.h>
#include <io.h>
#include <key.h>
#include <chdir.h>
#include <rs232c.h>
#include <pit8254.h>
#include <uip.h>
#include <ether.h>

PRIVATE int sys_open(const char* pathname, int flags, mode_t mode);
PRIVATE int sys_read(int fd, const void* buf, size_t count);
PRIVATE int __stdin_read(int fd, const void* buf, size_t count);
PRIVATE void sys_write(int fd, const void* buf, size_t count);
PRIVATE void sys_close(int fd);
PRIVATE int sys_getdentry();
PRIVATE int sys_getpstat();
PRIVATE void sys_memdump(u_int32_t addr, size_t size);
PRIVATE int sys_send(char* buf);

PUBLIC void init_syscall()
{
  set_trap_gate(0x80, &asm_syscall);
}

/* Firstly, we implements systemcall as "C" code, but afterwards we'll
 * modify the code which is written by assembler for efficient kernel.
 */
PUBLIC void i80h_syscall(int is_usermode, u_int32_t iret_eip,
                         u_int32_t iret_cs, u_int32_t iret_eflags,
                         u_int32_t iret_esp, u_int32_t iret_ss,
                         u_int32_t ebp)
{
  u_int32_t *eax = (u_int32_t*)(ebp-4);
  u_int32_t *ecx = (u_int32_t*)(ebp-8);
  u_int32_t *edx = (u_int32_t*)(ebp-12);
  u_int32_t *ebx = (u_int32_t*)(ebp-16);
  u_int32_t *esp = (u_int32_t*)(ebp-20);
  u_int32_t *esi = (u_int32_t*)(ebp-28);
  u_int32_t *edi = (u_int32_t*)(ebp-32);
  u_int32_t *prev_ebp = (u_int32_t*)(ebp);

  u_int32_t nr_syscall = *eax;
  u_int32_t p1 = *ebx;
  u_int32_t p2 = *ecx;
  u_int32_t p3 = *edx;
  u_int32_t p4 = *esi;
  u_int32_t p5 = *edi;
  u_int32_t p6 = *prev_ebp;


  int ret = 0;
  switch (nr_syscall) {
  case SYS_CALL_EXIT:
    sys_exit(p1);
    break;

  case SYS_CALL_FORK:
    ret = sys_fork();
    break;

  case SYS_CALL_READ:
    ret = sys_read(p1, p2, p3);
    break;

  case SYS_CALL_WRITE:
    sys_write(p1, p2, p3);
    break;

  case SYS_CALL_OPEN:
    ret = sys_open(p1, p2, p3);
    break;

  case SYS_CALL_CLOSE:
    sys_close(p1);
    break;

  case SYS_CALL_WAITPID:
    ret = sys_waitpid(p1, p2, p3);
    break;

  case SYS_CALL_EXECVE:
    ret = sys_execve(p1, p2, NULL);
    break;

  case SYS_CALL_CHDIR:
    ret = sys_chdir(p1);
    break;

  case SYS_CALL_GETDENTRY:
    ret = sys_getdentry();
    break;

  case SYS_CALL_GETPSTAT:
    ret = sys_getpstat();
    break;

  case SYS_CALL_BRK:
    ret = sys_brk(p1);
    break;

  case SYS_CALL_KILL:
    ret = sys_kill(p1, p2);
    break;

  case SYS_CALL_SIGNAL:
    ret = sys_signal(p1, p2);
    break;

  case SYS_CALL_MEMDUMP:
    sys_memdump(p1, p2);
    break;

  case SYS_CALL_TIMER:
    sys_timer(p1);
    break;

  case SYS_CALL_SEND:
    sys_send(p1);
    break;
  }

  return ret;
}

PRIVATE int sys_read(int fd, const void* buf, size_t count)
{
  int ret;
  struct file* fs = current->files->fs_fd[fd];
  if (fs->f_stdioflag == FLAG_STDIN) {
    ret = __stdin_read(fd, buf, count);
  } else {
    ret = ext3_read(fd, buf, count);
  }
  return ret;
}

PRIVATE int __stdin_read(int fd, const void* buf, size_t count)
{
  char *p;
  int total = 0;
  char stdin[KEY_BUF+1];
  memset(stdin, 0, KEY_BUF+1);

  asm("sti");
  while (TRUE) {
    //_kprintf("stdin_read\n");
    if (get_stdin(stdin) != NULL) {
      int getlen = strlen(stdin);
      //_kprintf("not null:%c %s getlen:%x\n", stdin[getlen-1], buf, getlen);
      if (stdin[getlen-1] == KEY_ENTER) {
        //_kputc('\n');
        memcpy(buf + total, stdin, getlen-1);
        total += getlen;
        return total;
      } else if (stdin[getlen-1] == KEY_BACK) {
        if (getlen > 0 && total > 0) {
          //_kputc(KEY_BACK);
          memset(buf + total - 1, 0, 1);
          total--;
        }
      } else {
        //_kputc(stdin[getlen-1]);
        memcpy(buf + total, stdin, getlen);
        total += getlen;
        memset(stdin, 0, KEY_BUF+1);
      }
    }
  }
}

PRIVATE void sys_write(int fd, const void* buf, size_t count)
{
  struct file* fs = current->files->fs_fd[fd];
  if (fs->f_stdioflag == FLAG_STDOUT) {
    int i;
    for (i=0; i<count; i++)
      _kputc(((char*)buf)[i]);
  } else if (fs->f_stdioflag == FLAG_STDERR) {
    int i;
    for (i=0; i<count; i++)
      _kputc(((char*)buf)[i]);
  } else if (fs->f_stdioflag == FLAG_FILE) {
    ext3_write(fd, buf, count);
    _kprintf("FLAG_FILE\n");
  } else {
    _kprintf("sys_write else\n");
    // error
  }
}

PRIVATE int sys_open(const char* pathname, int flags, mode_t mode)
{
  disable_pic_interrupt(IRQ_TIMER);

  int fd = open_env(pathname, flags, mode);

  enable_pic_interrupt(IRQ_TIMER);

  return fd;
}

PRIVATE void sys_close(int fd)
{
  close(fd);
}

PRIVATE int sys_getdentry()
{
  return (int)current->dentry;
}

PRIVATE int sys_getpstat()
{
  return (int)current;
}

PRIVATE int sys_send(char* buf)
{
  // set source mac address
  int i;
  for (i=6; i<12; i++) {
    buf[i] = i;//0xff;
  }	

  // set destination mac address
  //for (i=6; i<12; i++)
  //  buf[i] = i - 5;
  buf[0] = 0x00;
  buf[1] = 0x1e;
  buf[2] = 0x0b;
  buf[3] = 0xbc;
  buf[4] = 0x69;
  buf[5] = 0xca;

  buf[12] = 0x00;
  buf[13] = 0x08;

  //memcpy(uip_buf, buf, 16);
  //uip_len = 512;
  ether_send();
  /*
  while (TRUE) {
    int ret = ne2000_send(buf, 512);
    _kprintf("%x\n", ret);
  }
  */
}

PRIVATE void sys_memdump(u_int32_t addr, size_t size)
{
  int i;
  for (i=0; i<size; i++) {
    if (i%16 == 0)
      com1_printf("%x:", addr+i);
    if ((i%16)%4 == 0)
      com1_putc(' ');
    com1_printf("%x", ((char*)(addr+i))[0]);
    if (i%16 == 15)
      com1_putc('\n');
  }
}
