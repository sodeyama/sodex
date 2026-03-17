#ifndef _TTY_H
#define _TTY_H

#include <sodex/const.h>
#include <sys/types.h>
#include <fs.h>
#include "termios.h"
#include <winsize.h>

struct wait_queue;

#define TTY_FLAG_ICANON 0x01
#define TTY_FLAG_ECHO   0x02
#define TTY_FLAG_ISIG   0x04

#define TTY_RING_SIZE   65536
#define TTY_CANON_SIZE  256
#define TTY_POOL_MAX    4

#define INPUT_MODE_CONSOLE 0
#define INPUT_MODE_RAW     1

struct tty_ring {
  char data[TTY_RING_SIZE];
  int head;
  int tail;
};

struct tty {
  int active;
  int has_master;
  int refcount;
  int slave_refs;
  u_int8_t flags;
  u_int16_t cols;
  u_int16_t rows;
  pid_t foreground_pid;
  struct tty_ring slave_rx;
  struct tty_ring master_rx;
  char canon_buf[TTY_CANON_SIZE];
  int canon_len;
  struct wait_queue *slave_wait;
  struct wait_queue *master_wait;
};

PUBLIC void init_tty(void);
PUBLIC struct tty *tty_get_console(void);
PUBLIC struct tty *tty_alloc_pty(void);
PUBLIC void tty_retain(struct tty *tty);
PUBLIC void tty_release(struct tty *tty);
PUBLIC int tty_install_stdio(struct files_struct* files, struct tty *tty);
PUBLIC int tty_openpty(struct files_struct* files);
PUBLIC struct tty *tty_lookup_master(struct files_struct* files, int fd);
PUBLIC struct tty *tty_lookup_file(struct files_struct* files, int fd);
PUBLIC int tty_set_input_mode(int mode);
PUBLIC int tty_get_input_mode(void);
PUBLIC void tty_feed_console_char(char c);
PUBLIC int tty_get_termios(struct tty *tty, struct termios *termios);
PUBLIC int tty_set_termios(struct tty *tty, const struct termios *termios);
PUBLIC int tty_set_winsize(struct tty *tty, u_int16_t cols, u_int16_t rows);
PUBLIC int tty_get_winsize(struct tty *tty, struct winsize *winsize);
PUBLIC int tty_set_foreground_pid(struct tty *tty, pid_t pid);
PUBLIC pid_t tty_get_foreground_pid(struct tty *tty);
PUBLIC ssize_t tty_slave_read(struct tty *tty, void *buf, size_t count,
                              int block);
PUBLIC ssize_t tty_slave_write(struct tty *tty, const void *buf, size_t count);
PUBLIC ssize_t tty_master_read(struct tty *tty, void *buf, size_t count);
PUBLIC ssize_t tty_master_write(struct tty *tty, const void *buf, size_t count);

#endif
