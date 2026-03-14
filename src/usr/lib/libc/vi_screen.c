#include <stdlib.h>
#include <string.h>
#include <vi.h>

#define VI_SCREEN_WRITE_BUFFER 8192

static void vi_screen_flush(void);
static void vi_screen_append(const char *text, int len);
static void vi_screen_write(const char *text);
static void vi_screen_write_n(const char *text, int len);
static char *vi_screen_append_uint(char *p, int value);
static void vi_screen_move_cursor(int row, int col);
static void vi_screen_write_status(enum vi_mode mode, const char *path,
                                   const char *status, int dirty, int cols);
static const char *vi_screen_mode_name(enum vi_mode mode);
static char vi_screen_buffer[VI_SCREEN_WRITE_BUFFER];
static int vi_screen_buffer_len = 0;

static void vi_screen_flush(void)
{
  if (vi_screen_buffer_len > 0) {
    write(1, vi_screen_buffer, (size_t)vi_screen_buffer_len);
    vi_screen_buffer_len = 0;
  }
}

static void vi_screen_append(const char *text, int len)
{
  int remaining;

  if (text == NULL || len <= 0)
    return;

  remaining = len;
  while (remaining > 0) {
    int chunk = VI_SCREEN_WRITE_BUFFER - vi_screen_buffer_len;
    if (chunk <= 0) {
      vi_screen_flush();
      chunk = VI_SCREEN_WRITE_BUFFER;
    }
    if (chunk > remaining)
      chunk = remaining;
    memcpy(vi_screen_buffer + vi_screen_buffer_len, text, (size_t)chunk);
    vi_screen_buffer_len += chunk;
    text += chunk;
    remaining -= chunk;
  }
}

static void vi_screen_write(const char *text)
{
  if (text != NULL)
    vi_screen_append(text, strlen(text));
}

static void vi_screen_write_n(const char *text, int len)
{
  if (text != NULL && len > 0)
    vi_screen_append(text, len);
}

static char *vi_screen_append_uint(char *p, int value)
{
  char tmp[16];
  int len = 0;
  int i;

  if (value <= 0) {
    *p++ = '0';
    return p;
  }

  while (value > 0 && len < (int)sizeof(tmp)) {
    tmp[len++] = (char)('0' + (value % 10));
    value /= 10;
  }
  for (i = len - 1; i >= 0; i--) {
    *p++ = tmp[i];
  }
  return p;
}

static void vi_screen_move_cursor(int row, int col)
{
  char buf[32];
  char *p = buf;

  *p++ = '\x1b';
  *p++ = '[';
  p = vi_screen_append_uint(p, row + 1);
  *p++ = ';';
  p = vi_screen_append_uint(p, col + 1);
  *p++ = 'H';
  *p = '\0';
  vi_screen_write(buf);
}

static const char *vi_screen_mode_name(enum vi_mode mode)
{
  switch (mode) {
  case VI_MODE_INSERT:
    return "INSERT";
  case VI_MODE_COMMAND:
    return "COMMAND";
  default:
    return "NORMAL";
  }
}

static void vi_screen_write_status(enum vi_mode mode, const char *path,
                                   const char *status, int dirty, int cols)
{
  const char *name;
  const char *message;
  const char *mode_name;
  int used = 0;
  int len;

  name = path;
  if (name == NULL || name[0] == '\0')
    name = "[No Name]";
  message = status;
  if (message == NULL)
    message = "";
  mode_name = vi_screen_mode_name(mode);

  vi_screen_write("\x1b[7m");
  vi_screen_write(" ");
  used++;
  vi_screen_write(mode_name);
  used += strlen(mode_name);
  vi_screen_write(" ");
  used++;
  vi_screen_write(name);
  used += strlen(name);
  if (dirty != 0) {
    vi_screen_write(" [+]");
    used += 4;
  }
  if (message[0] != '\0' && used < cols) {
    vi_screen_write(" ");
    used++;
    len = strlen(message);
    if (len > cols - used)
      len = cols - used;
    vi_screen_write_n(message, len);
    used += len;
  }
  while (used < cols) {
    vi_screen_write(" ");
    used++;
  }
  vi_screen_write("\x1b[0m");
}

void vi_screen_redraw(const struct vi_buffer *buffer, enum vi_mode mode,
                      const char *path, const char *status,
                      const char *command, int row_offset,
                      int rows, int cols)
{
  int row;
  int visible_rows;
  int line_index;
  int len;
  int cursor_row;
  int cursor_col;

  if (buffer == NULL || rows <= 0 || cols <= 0)
    return;

  visible_rows = rows - 1;
  if (visible_rows < 1)
    visible_rows = 1;

  vi_screen_write("\x1b[0m\x1b[2J\x1b[H");
  for (row = 0; row < visible_rows; row++) {
    line_index = row_offset + row;
    vi_screen_move_cursor(row, 0);
    if (line_index < buffer->line_count) {
      len = vi_buffer_line_bytes_for_width(buffer, line_index, cols);
      vi_screen_write_n(vi_buffer_line_data(buffer, line_index), len);
    }
    vi_screen_write("\x1b[K");
  }

  vi_screen_move_cursor(rows - 1, 0);
  if (mode == VI_MODE_COMMAND) {
    int used = 1;

    vi_screen_write("\x1b[7m:");
    if (command != NULL && command[0] != '\0') {
      len = strlen(command);
      if (len > cols - 1)
        len = cols - 1;
      vi_screen_write_n(command, len);
      used += len;
    }
    while (used < cols) {
      vi_screen_write(" ");
      used++;
    }
    vi_screen_write("\x1b[0m");
    cursor_row = rows - 1;
    cursor_col = 1;
    if (command != NULL)
      cursor_col += strlen(command);
    if (cursor_col >= cols)
      cursor_col = cols - 1;
  } else {
    vi_screen_write_status(mode, path, status, buffer->dirty, cols);
    cursor_row = buffer->cursor_row - row_offset;
    cursor_col = vi_buffer_cursor_display_col(buffer);
    if (cursor_row < 0)
      cursor_row = 0;
    if (cursor_row >= visible_rows)
      cursor_row = visible_rows - 1;
    if (cursor_col < 0)
      cursor_col = 0;
    if (cursor_col >= cols)
      cursor_col = cols - 1;
  }

  vi_screen_move_cursor(cursor_row, cursor_col);
  vi_screen_flush();
}

void vi_screen_restore(void)
{
  vi_screen_write("\x1b[0m\x1b[2J\x1b[H");
  vi_screen_flush();
}
