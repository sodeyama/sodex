#include <tty.h>
#include <key.h>
#include <string.h>

#ifndef TEST_BUILD
#include <memory.h>
#include <process.h>
#include <vga.h>
#else
#include <stdlib.h>

PUBLIC void *_tty_test_kalloc(u_int32_t size)
{
  return calloc(1, size);
}

PUBLIC int32_t _tty_test_kfree(void *ptr)
{
  free(ptr);
  return 0;
}

PUBLIC void _kputc(char c)
{
  (void)c;
}

PUBLIC int screen_cols(void)
{
  return 80;
}

PUBLIC int screen_rows(void)
{
  return 25;
}

PUBLIC void sleep_on(struct wait_queue **wq)
{
  (void)wq;
}

PUBLIC void wakeup(struct wait_queue **wq)
{
  (void)wq;
}

#define kalloc _tty_test_kalloc
#define kfree  _tty_test_kfree
#endif

PRIVATE struct tty g_console_tty;
PRIVATE struct tty g_ptys[TTY_POOL_MAX];
PRIVATE int g_tty_initialized = FALSE;
PRIVATE int g_input_mode = INPUT_MODE_CONSOLE;

PRIVATE ssize_t tty_slave_file_read(struct file *file, void *buf, size_t count);
PRIVATE ssize_t tty_slave_file_write(struct file *file, const void *buf,
                                     size_t count);
PRIVATE int tty_slave_file_close(struct file *file);
PRIVATE ssize_t tty_master_file_read(struct file *file, void *buf, size_t count);
PRIVATE ssize_t tty_master_file_write(struct file *file, const void *buf,
                                      size_t count);
PRIVATE int tty_master_file_close(struct file *file);
PRIVATE void tty_reset(struct tty *tty, int has_master);
PRIVATE int tty_ring_empty(struct tty_ring *ring);
PRIVATE int tty_ring_push(struct tty_ring *ring, char c);
PRIVATE int tty_ring_pop(struct tty_ring *ring, char *c);
PRIVATE u_int8_t tty_termios_flags(u_int32_t lflag);
PRIVATE void tty_emit_output(struct tty *tty, char c);
PRIVATE void tty_echo_erase(struct tty *tty, int width);
PRIVATE void tty_echo_interrupt(struct tty *tty);
PRIVATE void tty_push_slave_byte(struct tty *tty, char c);
PRIVATE void tty_receive_char(struct tty *tty, char c);
PRIVATE struct file *tty_new_file(int stdioflag, const struct file_ops *ops,
                                  struct tty *tty);
PRIVATE int tty_utf8_prev_char_start(const char *data, int len, int index);
PRIVATE int tty_utf8_char_width(const char *data, int len);

PRIVATE const struct file_ops g_tty_slave_file_ops = {
  tty_slave_file_read,
  tty_slave_file_write,
  tty_slave_file_close
};

PRIVATE const struct file_ops g_tty_master_file_ops = {
  tty_master_file_read,
  tty_master_file_write,
  tty_master_file_close
};

PUBLIC void init_tty(void)
{
  int i;
  if (g_tty_initialized == TRUE)
    return;

  tty_reset(&g_console_tty, FALSE);
  g_console_tty.active = TRUE;
  for (i = 0; i < TTY_POOL_MAX; i++) {
    memset(&g_ptys[i], 0, sizeof(struct tty));
  }
  g_tty_initialized = TRUE;
}

PUBLIC struct tty *tty_get_console(void)
{
  init_tty();
  return &g_console_tty;
}

PUBLIC struct tty *tty_alloc_pty(void)
{
  int i;
  init_tty();
  for (i = 0; i < TTY_POOL_MAX; i++) {
    if (g_ptys[i].active == FALSE) {
      tty_reset(&g_ptys[i], TRUE);
      g_ptys[i].active = TRUE;
      g_ptys[i].refcount = 1;
      return &g_ptys[i];
    }
  }
  return NULL;
}

PUBLIC void tty_retain(struct tty *tty)
{
  if (tty == NULL)
    return;
  tty->refcount++;
}

PUBLIC void tty_release(struct tty *tty)
{
  if (tty == NULL)
    return;
  if (tty->refcount > 0)
    tty->refcount--;
  if (tty == &g_console_tty)
    return;
  if (tty->refcount == 0)
    tty_reset(tty, FALSE);
}

PUBLIC int tty_install_stdio(struct files_struct* files, struct tty *tty)
{
  struct file *stdin_file;
  struct file *stdout_file;
  struct file *stderr_file;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;

  stdin_file = tty_new_file(FLAG_TTY_SLAVE, &g_tty_slave_file_ops, tty);
  stdout_file = tty_new_file(FLAG_TTY_SLAVE, &g_tty_slave_file_ops, tty);
  stderr_file = tty_new_file(FLAG_TTY_SLAVE, &g_tty_slave_file_ops, tty);
  if (stdin_file == NULL || stdout_file == NULL || stderr_file == NULL) {
    return FALSE;
  }

  stdin_fd = files_alloc_fd(files, stdin_file);
  stdout_fd = files_alloc_fd(files, stdout_file);
  stderr_fd = files_alloc_fd(files, stderr_file);
  if (stdin_fd != STDIN_FILENO ||
      stdout_fd != STDOUT_FILENO ||
      stderr_fd != STDERR_FILENO) {
    return FALSE;
  }
  return TRUE;
}

PUBLIC int tty_openpty(struct files_struct* files)
{
  struct tty *tty;
  struct file *master_file;
  int fd;

  tty = tty_alloc_pty();
  if (tty == NULL)
    return -1;

  master_file = tty_new_file(FLAG_TTY_MASTER, &g_tty_master_file_ops, tty);
  if (master_file == NULL)
  {
    tty_release(tty);
    return -1;
  }

  fd = files_alloc_fd(files, master_file);
  if (fd < 0) {
    file_put(master_file);
    tty_release(tty);
    return -1;
  }
  tty_release(tty);
  return fd;
}

PUBLIC struct tty *tty_lookup_master(struct files_struct* files, int fd)
{
  struct file *file;
  if (fd < 0 || fd >= FILEDESC_MAX)
    return NULL;

  file = files->fs_fd[fd];
  if (file == NULL || file->f_stdioflag != FLAG_TTY_MASTER)
    return NULL;

  return (struct tty *)file->private_data;
}

PUBLIC struct tty *tty_lookup_file(struct files_struct* files, int fd)
{
  struct file *file;

  if (fd < 0 || fd >= FILEDESC_MAX)
    return NULL;

  file = files->fs_fd[fd];
  if (file == NULL)
    return NULL;
  if (file->f_stdioflag != FLAG_TTY_MASTER &&
      file->f_stdioflag != FLAG_TTY_SLAVE)
    return NULL;

  return (struct tty *)file->private_data;
}

PUBLIC int tty_set_input_mode(int mode)
{
  if (mode != INPUT_MODE_CONSOLE && mode != INPUT_MODE_RAW)
    return -1;

  g_input_mode = mode;
  return 0;
}

PUBLIC int tty_get_input_mode(void)
{
  return g_input_mode;
}

PUBLIC int tty_get_termios(struct tty *tty, struct termios *termios)
{
  if (tty == NULL || termios == NULL)
    return -1;

  termios->c_lflag = 0;
  if (tty->flags & TTY_FLAG_ICANON)
    termios->c_lflag |= ICANON;
  if (tty->flags & TTY_FLAG_ECHO)
    termios->c_lflag |= ECHO;
  if (tty->flags & TTY_FLAG_ISIG)
    termios->c_lflag |= ISIG;
  return 0;
}

PUBLIC int tty_set_termios(struct tty *tty, const struct termios *termios)
{
  if (tty == NULL || termios == NULL)
    return -1;

  tty->flags = tty_termios_flags(termios->c_lflag);
  tty->canon_len = 0;
  return 0;
}

PUBLIC int tty_set_winsize(struct tty *tty, u_int16_t cols, u_int16_t rows)
{
  if (tty == NULL || cols == 0 || rows == 0)
    return -1;

  tty->cols = cols;
  tty->rows = rows;
  return 0;
}

PUBLIC int tty_get_winsize(struct tty *tty, struct winsize *winsize)
{
  if (tty == NULL || winsize == NULL)
    return -1;

  winsize->cols = tty->cols;
  winsize->rows = tty->rows;
  return 0;
}

PUBLIC int tty_set_foreground_pid(struct tty *tty, pid_t pid)
{
  if (tty == NULL)
    return -1;

  if (pid < 0)
    pid = 0;
  tty->foreground_pid = pid;
  return 0;
}

PUBLIC pid_t tty_get_foreground_pid(struct tty *tty)
{
  if (tty == NULL)
    return 0;
  return tty->foreground_pid;
}

PUBLIC void tty_feed_console_char(char c)
{
  if (c == 0 || tty_get_input_mode() == INPUT_MODE_RAW)
    return;

  tty_receive_char(tty_get_console(), c);
}

PUBLIC ssize_t tty_slave_read(struct tty *tty, void *buf, size_t count,
                              int block)
{
  char *out = (char *)buf;
  size_t total = 0;
  char c;

  if (tty == NULL || buf == NULL || count == 0)
    return 0;

  while (tty_ring_empty(&tty->slave_rx) == TRUE) {
    if (block == FALSE)
      return 0;
    sleep_on(&tty->slave_wait);
  }

  while (total < count && tty_ring_pop(&tty->slave_rx, &c) == TRUE) {
    out[total++] = c;
    if ((tty->flags & TTY_FLAG_ICANON) && c == '\0') {
      break;
    }
  }

  return (ssize_t)total;
}

PUBLIC ssize_t tty_slave_write(struct tty *tty, const void *buf, size_t count)
{
  const char *p = (const char *)buf;
  size_t i;

  if (tty == NULL || buf == NULL)
    return 0;

  for (i = 0; i < count; i++) {
    tty_emit_output(tty, p[i]);
  }

  return (ssize_t)count;
}

PUBLIC ssize_t tty_master_read(struct tty *tty, void *buf, size_t count)
{
  char *out = (char *)buf;
  size_t total = 0;

  if (tty == NULL || buf == NULL || count == 0)
    return 0;

  while (total < count && tty_ring_pop(&tty->master_rx, &out[total]) == TRUE) {
    total++;
  }

  return (ssize_t)total;
}

PUBLIC ssize_t tty_master_write(struct tty *tty, const void *buf, size_t count)
{
  const char *p = (const char *)buf;
  size_t i;

  if (tty == NULL || buf == NULL)
    return 0;

  for (i = 0; i < count; i++) {
    tty_receive_char(tty, p[i]);
  }

  return (ssize_t)count;
}

PRIVATE ssize_t tty_slave_file_read(struct file *file, void *buf, size_t count)
{
  return tty_slave_read((struct tty *)file->private_data, buf, count, TRUE);
}

PRIVATE ssize_t tty_slave_file_write(struct file *file, const void *buf,
                                     size_t count)
{
  return tty_slave_write((struct tty *)file->private_data, buf, count);
}

PRIVATE int tty_slave_file_close(struct file *file)
{
  tty_release((struct tty *)file->private_data);
  (void)file;
  return TRUE;
}

PRIVATE ssize_t tty_master_file_read(struct file *file, void *buf, size_t count)
{
  return tty_master_read((struct tty *)file->private_data, buf, count);
}

PRIVATE ssize_t tty_master_file_write(struct file *file, const void *buf,
                                      size_t count)
{
  return tty_master_write((struct tty *)file->private_data, buf, count);
}

PRIVATE int tty_master_file_close(struct file *file)
{
  struct tty *tty = (struct tty *)file->private_data;
  if (tty != NULL) {
    tty->has_master = FALSE;
    tty_release(tty);
  }
  return TRUE;
}

PRIVATE void tty_reset(struct tty *tty, int has_master)
{
  memset(tty, 0, sizeof(struct tty));
  tty->has_master = has_master;
  tty->flags = TTY_FLAG_ICANON | TTY_FLAG_ECHO | TTY_FLAG_ISIG;
  tty->cols = screen_cols();
  tty->rows = screen_rows();
}

PRIVATE int tty_ring_empty(struct tty_ring *ring)
{
  return ring->head == ring->tail;
}

PRIVATE int tty_ring_push(struct tty_ring *ring, char c)
{
  int next = (ring->tail + 1) % TTY_RING_SIZE;

  /* vi の全画面再描画を落とさないよう、リングは十分大きく持つ。 */
  if (next == ring->head) {
    ring->head = (ring->head + 1) % TTY_RING_SIZE;
  }
  ring->data[ring->tail] = c;
  ring->tail = next;
  return TRUE;
}

PRIVATE int tty_ring_pop(struct tty_ring *ring, char *c)
{
  if (tty_ring_empty(ring) == TRUE)
    return FALSE;

  *c = ring->data[ring->head];
  ring->head = (ring->head + 1) % TTY_RING_SIZE;
  return TRUE;
}

PRIVATE int tty_utf8_prev_char_start(const char *data, int len, int index)
{
  if (data == NULL || len <= 0 || index <= 0)
    return 0;
  if (index > len)
    index = len;
  index--;
  while (index > 0 &&
         (((unsigned char)data[index]) & 0xc0U) == 0x80U) {
    index--;
  }
  return index;
}

PRIVATE int tty_utf8_char_width(const char *data, int len)
{
  unsigned char first;
  u_int32_t codepoint;

  if (data == NULL || len <= 0)
    return 1;

  first = (unsigned char)data[0];
  if (first < 0x80U) {
    if (first < 0x20U || first == 0x7fU)
      return 1;
    return 1;
  }

  codepoint = 0xfffdU;
  if ((first & 0xe0U) == 0xc0U && len >= 2 &&
      (((unsigned char)data[1]) & 0xc0U) == 0x80U) {
    codepoint = ((u_int32_t)(first & 0x1fU) << 6) |
                ((u_int32_t)(unsigned char)data[1] & 0x3fU);
  } else if ((first & 0xf0U) == 0xe0U && len >= 3 &&
             (((unsigned char)data[1]) & 0xc0U) == 0x80U &&
             (((unsigned char)data[2]) & 0xc0U) == 0x80U) {
    codepoint = ((u_int32_t)(first & 0x0fU) << 12) |
                (((u_int32_t)(unsigned char)data[1] & 0x3fU) << 6) |
                ((u_int32_t)(unsigned char)data[2] & 0x3fU);
  } else if ((first & 0xf8U) == 0xf0U && len >= 4 &&
             (((unsigned char)data[1]) & 0xc0U) == 0x80U &&
             (((unsigned char)data[2]) & 0xc0U) == 0x80U &&
             (((unsigned char)data[3]) & 0xc0U) == 0x80U) {
    codepoint = ((u_int32_t)(first & 0x07U) << 18) |
                (((u_int32_t)(unsigned char)data[1] & 0x3fU) << 12) |
                (((u_int32_t)(unsigned char)data[2] & 0x3fU) << 6) |
                ((u_int32_t)(unsigned char)data[3] & 0x3fU);
  }

  if ((codepoint >= 0x3000U && codepoint <= 0x303eU) ||
      (codepoint >= 0x3041U && codepoint <= 0x3096U) ||
      (codepoint >= 0x3099U && codepoint <= 0x30ffU) ||
      (codepoint >= 0x3400U && codepoint <= 0x9fffU) ||
      (codepoint >= 0xff01U && codepoint <= 0xff60U) ||
      (codepoint >= 0xffe0U && codepoint <= 0xffe6U))
    return 2;
  return 1;
}

PRIVATE u_int8_t tty_termios_flags(u_int32_t lflag)
{
  u_int8_t flags = 0;

  if (lflag & ICANON)
    flags |= TTY_FLAG_ICANON;
  if (lflag & ECHO)
    flags |= TTY_FLAG_ECHO;
  if (lflag & ISIG)
    flags |= TTY_FLAG_ISIG;
  return flags;
}

PRIVATE void tty_emit_output(struct tty *tty, char c)
{
  if (tty->has_master == TRUE) {
    tty_ring_push(&tty->master_rx, c);
    wakeup(&tty->master_wait);
  } else {
    _kputc(c);
  }
}

PRIVATE void tty_echo_erase(struct tty *tty, int width)
{
  int i;

  if (tty == NULL)
    return;
  if (width <= 0)
    width = 1;

  for (i = 0; i < width; i++) {
    tty_emit_output(tty, KEY_BACK);
    tty_emit_output(tty, ' ');
    tty_emit_output(tty, KEY_BACK);
  }
}

PRIVATE void tty_echo_interrupt(struct tty *tty)
{
  if (tty == NULL)
    return;

  tty_emit_output(tty, '^');
  tty_emit_output(tty, 'C');
  tty_emit_output(tty, '\n');
}

PRIVATE void tty_push_slave_byte(struct tty *tty, char c)
{
  tty_ring_push(&tty->slave_rx, c);
  wakeup(&tty->slave_wait);
}

PRIVATE void tty_receive_char(struct tty *tty, char c)
{
  if ((tty->flags & TTY_FLAG_ISIG) && c == 0x03) {
    tty->canon_len = 0;
    if (tty->flags & TTY_FLAG_ECHO)
      tty_echo_interrupt(tty);
    if ((tty->flags & TTY_FLAG_ICANON) && tty->foreground_pid <= 0)
      tty_push_slave_byte(tty, '\0');
    return;
  }

  if ((tty->flags & TTY_FLAG_ICANON) == 0) {
    tty_push_slave_byte(tty, c);
    if (tty->flags & TTY_FLAG_ECHO)
      tty_emit_output(tty, c);
    return;
  }

  if (c == KEY_BACK || c == 0x7f) {
    if (tty->canon_len > 0) {
      int prev_len = tty_utf8_prev_char_start(tty->canon_buf, tty->canon_len,
                                              tty->canon_len);
      int width = tty_utf8_char_width(tty->canon_buf + prev_len,
                                      tty->canon_len - prev_len);

      tty->canon_len = prev_len;
      if (tty->flags & TTY_FLAG_ECHO)
        tty_echo_erase(tty, width);
    }
    return;
  }

  if (c == KEY_ENTER || c == '\n') {
    int i;
    for (i = 0; i < tty->canon_len; i++) {
      tty_push_slave_byte(tty, tty->canon_buf[i]);
    }
    tty_push_slave_byte(tty, '\0');
    tty->canon_len = 0;
    if (tty->flags & TTY_FLAG_ECHO)
      tty_emit_output(tty, '\n');
    return;
  }

  if (c == 0)
    return;

  if (tty->canon_len < (TTY_CANON_SIZE - 1)) {
    tty->canon_buf[tty->canon_len++] = c;
    if (tty->flags & TTY_FLAG_ECHO)
      tty_emit_output(tty, c);
  }
}

PRIVATE struct file *tty_new_file(int stdioflag, const struct file_ops *ops,
                                  struct tty *tty)
{
  struct file *file = kalloc(sizeof(struct file));
  if (file == NULL)
    return NULL;

  memset(file, 0, sizeof(struct file));
  file->f_stdioflag = stdioflag;
  file->f_ops = ops;
  file->private_data = tty;
  file->f_refcount = 1;
  tty_retain(tty);
  return file;
}
