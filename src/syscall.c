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
#include <pit8254.h>
#include <uip.h>
#include <ether.h>
#include <socket.h>
#include <tty.h>
#include <fb.h>
#include <pipe.h>
#include <termios.h>
#include <rs232c.h>

PRIVATE int sys_open(const char* pathname, int flags, mode_t mode);
PRIVATE int sys_creat(const char* pathname, mode_t mode);
PRIVATE int sys_read(int fd, void* buf, size_t count);
PRIVATE int __stdin_read(int fd, void* buf, size_t count);
PRIVATE int sys_write(int fd, const void* buf, size_t count);
PRIVATE void sys_close(int fd);
PRIVATE int sys_lseek(int fd, off_t offset, int whence);
PRIVATE int sys_mkdir_call(const char* pathname, mode_t mode);
PRIVATE int sys_unlink_call(const char* pathname);
PRIVATE int sys_rmdir_call(const char* pathname);
PRIVATE int sys_rename_call(const char* oldpath, const char* newpath);
PRIVATE int sys_dup_call(int oldfd);
PRIVATE int sys_pipe_call(int *fd);
PRIVATE int sys_getdentry();
PRIVATE int sys_getpstat();
PRIVATE int sys_getkeyevent(struct key_event *event);
PRIVATE int sys_openpty();
PRIVATE int sys_execve_pty_call(const char *filename, char *const argv[],
                                int master_fd);
PRIVATE int sys_set_input_mode(int mode);
PRIVATE int sys_console_cols(void);
PRIVATE int sys_console_rows(void);
PRIVATE void sys_console_putc_at(int x, int y, char color, char c);
PRIVATE void sys_console_set_cursor(int x, int y);
PRIVATE void sys_console_clear(void);
PRIVATE int sys_get_winsize(int fd, struct winsize *winsize);
PRIVATE int sys_set_winsize(int fd, const struct winsize *winsize);
PRIVATE int sys_get_fb_info(struct fb_info *info);
PRIVATE int sys_debug_write(const char *buf, size_t len);
PRIVATE int sys_tcgetattr(int fd, struct termios *termios);
PRIVATE int sys_tcsetattr(int fd, int optional_actions,
                          const struct termios *termios);
PRIVATE int sys_sleep_ticks(u_int32_t ticks);
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
  u_int32_t *esi = (u_int32_t*)(ebp-28);
  u_int32_t *edi = (u_int32_t*)(ebp-32);

  u_int32_t nr_syscall = *eax;
  u_int32_t p1 = *ebx;
  u_int32_t p2 = *ecx;
  u_int32_t p3 = *edx;
  u_int32_t p4 = *esi;
  u_int32_t p5 = *edi;

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
    ret = sys_write(p1, p2, p3);
    break;

  case SYS_CALL_OPEN:
    ret = sys_open(p1, p2, p3);
    break;

  case SYS_CALL_CREAT:
    ret = sys_creat((const char *)p1, (mode_t)p2);
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

  case SYS_CALL_LSEEK:
    ret = sys_lseek((int)p1, (off_t)p2, (int)p3);
    break;

  case SYS_CALL_GETDENTRY:
    ret = sys_getdentry();
    break;

  case SYS_CALL_GETPSTAT:
    ret = sys_getpstat();
    break;

  case SYS_CALL_GETKEYEVENT:
    ret = sys_getkeyevent((struct key_event *)p1);
    break;

  case SYS_CALL_OPENPTY:
    ret = sys_openpty();
    break;

  case SYS_CALL_EXECVE_PTY:
    ret = sys_execve_pty_call((const char *)p1, (char *const *)p2, p3);
    break;

  case SYS_CALL_SET_INPUT_MODE:
    ret = sys_set_input_mode(p1);
    break;

  case SYS_CALL_CONSOLE_COLS:
    ret = sys_console_cols();
    break;

  case SYS_CALL_CONSOLE_ROWS:
    ret = sys_console_rows();
    break;

  case SYS_CALL_CONSOLE_PUTC_AT:
    sys_console_putc_at((int)p1, (int)p2, (char)p3, (char)p4);
    break;

  case SYS_CALL_CONSOLE_SET_CURSOR:
    sys_console_set_cursor((int)p1, (int)p2);
    break;

  case SYS_CALL_CONSOLE_CLEAR:
    sys_console_clear();
    break;

  case SYS_CALL_GET_WINSIZE:
    ret = sys_get_winsize((int)p1, (struct winsize *)p2);
    break;

  case SYS_CALL_SET_WINSIZE:
    ret = sys_set_winsize((int)p1, (const struct winsize *)p2);
    break;

  case SYS_CALL_GET_FB_INFO:
    ret = sys_get_fb_info((struct fb_info *)p1);
    break;

  case SYS_CALL_DEBUG_WRITE:
    ret = sys_debug_write((const char *)p1, (size_t)p2);
    break;

  case SYS_CALL_TCGETATTR:
    ret = sys_tcgetattr((int)p1, (struct termios *)p2);
    break;

  case SYS_CALL_TCSETATTR:
    ret = sys_tcsetattr((int)p1, (int)p2, (const struct termios *)p3);
    break;

  case SYS_CALL_SLEEP_TICKS:
    ret = sys_sleep_ticks(p1);
    break;

  case SYS_CALL_BRK:
    ret = sys_brk(p1);
    break;

  case SYS_CALL_KILL:
    ret = sys_kill(p1, p2);
    break;

  case SYS_CALL_RENAME:
    ret = sys_rename_call((const char *)p1, (const char *)p2);
    break;

  case SYS_CALL_MKDIR:
    ret = sys_mkdir_call((const char *)p1, (mode_t)p2);
    break;

  case SYS_CALL_RMDIR:
    ret = sys_rmdir_call((const char *)p1);
    break;

  case SYS_CALL_UNLINK:
    ret = sys_unlink_call((const char *)p1);
    break;

  case SYS_CALL_DUP:
    ret = sys_dup_call((int)p1);
    break;

  case SYS_CALL_PIPE:
    ret = sys_pipe_call((int *)p1);
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

  case SYS_CALL_SOCKET:
    ret = kern_socket(p1, p2, p3);
    break;

  case SYS_CALL_BIND:
    ret = kern_bind(p1, (struct sockaddr_in *)p2);
    break;

  case SYS_CALL_LISTEN:
    ret = kern_listen(p1, p2);
    break;

  case SYS_CALL_ACCEPT:
    ret = kern_accept(p1, (struct sockaddr_in *)p2);
    break;

  case SYS_CALL_CONNECT:
    ret = kern_connect(p1, (struct sockaddr_in *)p2);
    break;

  case SYS_CALL_SEND_MSG:
    ret = kern_send(p1, (void *)p2, p3, p4);
    break;

  case SYS_CALL_RECV:
    ret = kern_recv(p1, (void *)p2, p3, p4);
    break;

  case SYS_CALL_SENDTO:
    ret = kern_sendto(p1, (void *)p2, p3, p4, (struct sockaddr_in *)p5);
    break;

  case SYS_CALL_RECVFROM:
    ret = kern_recvfrom(p1, (void *)p2, p3, p4, (struct sockaddr_in *)p5);
    break;

  case SYS_CALL_CLOSE_SOCK:
    ret = kern_close_socket(p1);
    break;
  }

  *eax = ret;
}

PRIVATE int sys_read(int fd, void* buf, size_t count)
{
  int ret;
  struct file* fs = current->files->fs_fd[fd];
  if (fs == NULL)
    return -1;

  if (fs->f_ops != NULL && fs->f_ops->read != NULL) {
    return fs->f_ops->read(fs, buf, count);
  }

  if (fs->f_stdioflag == FLAG_STDIN) {
    ret = __stdin_read(fd, buf, count);
  } else {
    ret = ext3_read(fd, buf, count);
  }
  return ret;
}

PRIVATE int __stdin_read(int fd, void* buf, size_t count)
{
  char *out = (char *)buf;
  int total = 0;
  char stdin[KEY_BUF+1];
  memset(stdin, 0, KEY_BUF+1);

  screen_save_prompt();
  asm("sti");
  while (TRUE) {
    //_kprintf("stdin_read\n");
    if (get_stdin(stdin) != NULL) {
      int getlen = strlen(stdin);
      //_kprintf("not null:%c %s getlen:%x\n", stdin[getlen-1], buf, getlen);
      if (stdin[getlen-1] == KEY_ENTER) {
        //_kputc('\n');
        memcpy(out + total, stdin, getlen-1);
        total += getlen;
        return total;
      } else if (stdin[getlen-1] == KEY_BACK) {
        if (getlen > 0 && total > 0) {
          //_kputc(KEY_BACK);
          memset(out + total - 1, 0, 1);
          total--;
        }
      } else {
        //_kputc(stdin[getlen-1]);
        memcpy(out + total, stdin, getlen);
        total += getlen;
        memset(stdin, 0, KEY_BUF+1);
      }
    }
  }
}

PRIVATE int sys_write(int fd, const void* buf, size_t count)
{
  struct file* fs = current->files->fs_fd[fd];
  if (fs == NULL)
    return -1;

  if (fs->f_ops != NULL && fs->f_ops->write != NULL)
    return (int)fs->f_ops->write(fs, buf, count);

  if (fs->f_stdioflag == FLAG_STDOUT) {
    int i;
    for (i=0; i<count; i++)
      _kputc(((char*)buf)[i]);
    return (int)count;
  } else if (fs->f_stdioflag == FLAG_STDERR) {
    int i;
    for (i=0; i<count; i++)
      _kputc(((char*)buf)[i]);
    return (int)count;
  } else if (fs->f_stdioflag == FLAG_FILE) {
    return (int)ext3_write(fd, buf, count);
  } else {
    return -1;
  }
}

PRIVATE int sys_open(const char* pathname, int flags, mode_t mode)
{
  disable_pic_interrupt(IRQ_TIMER);

  int fd = ext3_open(pathname, flags, mode);

  enable_pic_interrupt(IRQ_TIMER);

  return fd;
}

PRIVATE int sys_creat(const char* pathname, mode_t mode)
{
  return sys_open(pathname, O_CREAT | O_TRUNC | O_WRONLY, mode);
}

PRIVATE void sys_close(int fd)
{
  close(fd);
}

PRIVATE int sys_lseek(int fd, off_t offset, int whence)
{
  return (int)lseek(fd, offset, whence);
}

PRIVATE int sys_mkdir_call(const char* pathname, mode_t mode)
{
  return ext3_mkdir(pathname, mode);
}

PRIVATE int sys_unlink_call(const char* pathname)
{
  return ext3_unlink(pathname);
}

PRIVATE int sys_rmdir_call(const char* pathname)
{
  return ext3_rmdir(pathname);
}

PRIVATE int sys_rename_call(const char* oldpath, const char* newpath)
{
  return ext3_rename(oldpath, newpath);
}

PRIVATE int sys_dup_call(int oldfd)
{
  return files_dup(current->files, oldfd);
}

PRIVATE int sys_pipe_call(int *fd)
{
  return pipe(fd);
}

PRIVATE int sys_openpty()
{
  return tty_openpty(current->files);
}

PRIVATE int sys_execve_pty_call(const char *filename, char *const argv[],
                                int master_fd)
{
  return (int)sys_execve_pty(filename, argv, master_fd);
}

PRIVATE int sys_getdentry()
{
  return (int)current->dentry;
}

PRIVATE int sys_getpstat()
{
  return (int)current;
}

PRIVATE int sys_getkeyevent(struct key_event *event)
{
  struct key_event next_event;

  if (event == NULL) {
    return 0;
  }
  if (key_pop_event(&next_event) == FALSE) {
    return 0;
  }

  memcpy(event, &next_event, sizeof(struct key_event));
  return 1;
}

PRIVATE int sys_set_input_mode(int mode)
{
  return tty_set_input_mode(mode);
}

PRIVATE int sys_console_cols(void)
{
  return screen_cols();
}

PRIVATE int sys_console_rows(void)
{
  return screen_rows();
}

PRIVATE void sys_console_putc_at(int x, int y, char color, char c)
{
  _poscolor_printc(x, y, color, c);
}

PRIVATE void sys_console_set_cursor(int x, int y)
{
  screen_pointset(x, y);
}

PRIVATE void sys_console_clear(void)
{
  clr_screen();
}

PRIVATE int sys_get_winsize(int fd, struct winsize *winsize)
{
  struct tty *tty = tty_lookup_file(current->files, fd);
  return tty_get_winsize(tty, winsize);
}

PRIVATE int sys_set_winsize(int fd, const struct winsize *winsize)
{
  struct tty *tty;

  if (winsize == NULL)
    return -1;

  tty = tty_lookup_file(current->files, fd);
  return tty_set_winsize(tty, winsize->cols, winsize->rows);
}

PRIVATE int sys_get_fb_info(struct fb_info *info)
{
  const struct fb_info *kernel_info;

  if (info == NULL)
    return -1;

  kernel_info = fb_get_info();
  info->available = kernel_info->available;
  info->width = kernel_info->width;
  info->height = kernel_info->height;
  info->pitch = kernel_info->pitch;
  info->bpp = kernel_info->bpp;
  info->base = kernel_info->base;
  info->size = kernel_info->size;
  return 0;
}

PRIVATE int sys_tcgetattr(int fd, struct termios *termios)
{
  struct tty *tty = tty_lookup_file(current->files, fd);
  return tty_get_termios(tty, termios);
}

PRIVATE int sys_tcsetattr(int fd, int optional_actions,
                          const struct termios *termios)
{
  struct tty *tty;

  if (optional_actions != TCSANOW)
    return -1;

  tty = tty_lookup_file(current->files, fd);
  return tty_set_termios(tty, termios);
}

PRIVATE int sys_sleep_ticks(u_int32_t ticks)
{
  extern volatile u_int32_t kernel_tick;
  u_int32_t deadline;

  if (ticks == 0)
    return 0;

  deadline = kernel_tick + ticks;
  while ((int)(kernel_tick - deadline) < 0) {
    asm("sti\n\thlt");
  }
  return 0;
}

PRIVATE int sys_debug_write(const char *buf, size_t len)
{
  size_t i;

  if (buf == NULL)
    return -1;

  for (i = 0; i < len; i++) {
    if (buf[i] == '\n')
      com1_putc('\r');
    com1_putc(buf[i]);
  }
  return (int)len;
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
  return 0;
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
