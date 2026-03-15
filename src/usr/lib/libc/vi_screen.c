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
static int vi_screen_visual_bounds(const struct vi_visual_state *visual,
                                   int row, int visible_len,
                                   int *start_col, int *end_col);
static void vi_screen_write_line(const struct vi_buffer *buffer, int row,
                                 int cols, const struct vi_visual_state *visual);
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

static int vi_screen_visual_bounds(const struct vi_visual_state *visual,
                                   int row, int visible_len,
                                   int *start_col, int *end_col)
{
  int start_row;
  int end_row;
  int start_byte;
  int end_byte;

  if (visual == NULL || visual->active == 0 || visible_len <= 0)
    return 0;

  start_row = visual->start_row;
  end_row = visual->end_row;
  start_byte = visual->start_col;
  end_byte = visual->end_col;
  if (start_row > end_row ||
      (start_row == end_row && start_byte > end_byte)) {
    int tmp_row = start_row;
    int tmp_col = start_byte;
    start_row = end_row;
    start_byte = end_byte;
    end_row = tmp_row;
    end_byte = tmp_col;
  }

  if (row < start_row || row > end_row)
    return 0;

  if (visual->linewise != 0) {
    *start_col = 0;
    *end_col = visible_len;
    return *end_col > *start_col;
  }

  if (start_row == end_row) {
    *start_col = start_byte;
    *end_col = end_byte;
  } else if (row == start_row) {
    *start_col = start_byte;
    *end_col = visible_len;
  } else if (row == end_row) {
    *start_col = 0;
    *end_col = end_byte;
  } else {
    *start_col = 0;
    *end_col = visible_len;
  }

  if (*start_col < 0)
    *start_col = 0;
  if (*start_col > visible_len)
    *start_col = visible_len;
  if (*end_col < 0)
    *end_col = 0;
  if (*end_col > visible_len)
    *end_col = visible_len;
  return *end_col > *start_col;
}

static void vi_screen_write_line(const struct vi_buffer *buffer, int row,
                                 int cols, const struct vi_visual_state *visual)
{
  const char *data;
  int visible_len;
  int start_col = 0;
  int end_col = 0;

  data = vi_buffer_line_data(buffer, row);
  visible_len = vi_buffer_line_bytes_for_width(buffer, row, cols);
  if (visible_len <= 0)
    return;

  if (vi_screen_visual_bounds(visual, row, visible_len,
                              &start_col, &end_col) == 0) {
    vi_screen_write_n(data, visible_len);
    return;
  }

  if (start_col > 0)
    vi_screen_write_n(data, start_col);
  if (end_col > start_col) {
    vi_screen_write("\x1b[7m");
    vi_screen_write_n(data + start_col, end_col - start_col);
    vi_screen_write("\x1b[0m");
  }
  if (visible_len > end_col)
    vi_screen_write_n(data + end_col, visible_len - end_col);
}

static const char *vi_screen_mode_name(enum vi_mode mode)
{
  switch (mode) {
  case VI_MODE_INSERT:
    return "INSERT";
  case VI_MODE_SEARCH:
    return "SEARCH";
  case VI_MODE_VISUAL:
    return "VISUAL";
  case VI_MODE_VISUAL_LINE:
    return "V-LINE";
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
                      const char *command, char command_prefix,
                      const struct vi_visual_state *visual,
                      int row_offset, int rows, int cols)
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
      vi_screen_write_line(buffer, line_index, cols, visual);
    }
    vi_screen_write("\x1b[K");
  }

  vi_screen_move_cursor(rows - 1, 0);
  if (mode == VI_MODE_COMMAND || mode == VI_MODE_SEARCH) {
    int used = 1;

    vi_screen_write("\x1b[7m");
    vi_screen_write_n(&command_prefix, 1);
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
  vi_screen_write("\x1b[0m\x1b[?1049l");
  vi_screen_flush();
}

void vi_screen_enter(void)
{
  vi_screen_write("\x1b[?1049h");
  vi_screen_flush();
}
