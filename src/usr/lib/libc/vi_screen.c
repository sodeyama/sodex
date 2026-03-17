#include <debug.h>
#include <stdlib.h>
#include <string.h>
#include <utf8.h>
#include <vi.h>
#include <wcwidth.h>

#define VI_SCREEN_WRITE_BUFFER 8192
#define VI_SCREEN_SYNC_BEGIN "\x1b[?2026h"
#define VI_SCREEN_SYNC_END "\x1b[?2026l"

ssize_t write(int fd, const void *buf, size_t count);

struct vi_screen_cell {
  char bytes[4];
  unsigned char len;
  unsigned char reverse;
  unsigned char continuation;
};

struct vi_screen_state {
  struct vi_screen_cell *cells;
  struct vi_screen_cell *prev_cells;
  int rows;
  int cols;
  int frame_valid;
  int alternate_active;
  u_int32_t redraw_count;
  u_int32_t redraw_bytes_total;
  u_int32_t dirty_rows_total;
  u_int32_t dirty_spans_total;
  u_int32_t full_redraw_fallbacks;
  u_int32_t sync_frame_count;
};

static void vi_screen_flush(void);
static void vi_screen_append(const char *text, int len);
static void vi_screen_write(const char *text);
static void vi_screen_write_n(const char *text, int len);
static char *vi_screen_append_uint(char *p, u_int32_t value);
static void vi_screen_move_cursor(int row, int col);
static int vi_screen_ensure_state(int rows, int cols);
static void vi_screen_release_state(void);
static void vi_screen_clear_row(struct vi_screen_cell *row_cells,
                                int cols, int reverse);
static int vi_screen_visual_byte_bounds(const struct vi_visual_state *visual,
                                        int row, int line_len,
                                        int *start_col, int *end_col);
static void vi_screen_put_bytes(struct vi_screen_cell *row_cells, int cols,
                                int start_col, const char *bytes, int len,
                                int width, int reverse);
static void vi_screen_build_text_row(struct vi_screen_cell *row_cells,
                                     int cols,
                                     const struct vi_buffer *buffer, int row,
                                     const struct vi_visual_state *visual);
static void vi_screen_row_append_ascii(struct vi_screen_cell *row_cells,
                                       int cols, int *used,
                                       const char *text, int reverse);
static void vi_screen_build_status_row(struct vi_screen_cell *row_cells,
                                       int cols,
                                       enum vi_mode mode,
                                       const char *path,
                                       const char *status,
                                       int dirty);
static void vi_screen_build_command_row(struct vi_screen_cell *row_cells,
                                        int cols,
                                        const char *command,
                                        char command_prefix);
static const char *vi_screen_mode_name(enum vi_mode mode);
static struct vi_screen_cell *vi_screen_row(struct vi_screen_cell *cells,
                                            int cols, int row);
static int vi_screen_row_equal(const struct vi_screen_cell *left,
                               const struct vi_screen_cell *right, int cols);
static int vi_screen_find_first_diff(const struct vi_screen_cell *left,
                                     const struct vi_screen_cell *right,
                                     int cols);
static int vi_screen_find_last_diff(const struct vi_screen_cell *left,
                                    const struct vi_screen_cell *right,
                                    int cols);
static int vi_screen_adjust_diff_start(const struct vi_screen_cell *left,
                                       const struct vi_screen_cell *right,
                                       int col);
static void vi_screen_write_row_span(const struct vi_screen_cell *row_cells,
                                     int cols, int start_col, int end_col);
static void vi_screen_build_frame(const struct vi_buffer *buffer,
                                  enum vi_mode mode,
                                  const char *path, const char *status,
                                  const char *command, char command_prefix,
                                  const struct vi_visual_state *visual,
                                  int row_offset, int rows, int cols);
static void vi_screen_swap_frames(void);
static void vi_screen_emit_metric(u_int32_t redraw_bytes,
                                  u_int32_t dirty_rows,
                                  u_int32_t dirty_spans,
                                  int full_fallback);
static u_int32_t vi_screen_current_redraw_bytes = 0;
static char vi_screen_buffer[VI_SCREEN_WRITE_BUFFER];
static int vi_screen_buffer_len = 0;
static struct vi_screen_state vi_screen_state;

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

  vi_screen_current_redraw_bytes += (u_int32_t)len;
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

static char *vi_screen_append_uint(char *p, u_int32_t value)
{
  char tmp[16];
  int len = 0;
  int i;

  if (value == 0) {
    *p++ = '0';
    return p;
  }

  while (value > 0 && len < (int)sizeof(tmp)) {
    tmp[len++] = (char)('0' + (value % 10U));
    value /= 10U;
  }
  for (i = len - 1; i >= 0; i--)
    *p++ = tmp[i];
  return p;
}

static void vi_screen_move_cursor(int row, int col)
{
  char buf[32];
  char *p = buf;

  *p++ = '\x1b';
  *p++ = '[';
  p = vi_screen_append_uint(p, (u_int32_t)(row + 1));
  *p++ = ';';
  p = vi_screen_append_uint(p, (u_int32_t)(col + 1));
  *p++ = 'H';
  vi_screen_write_n(buf, (int)(p - buf));
}

static void vi_screen_release_state(void)
{
  if (vi_screen_state.cells != NULL)
    free(vi_screen_state.cells);
  if (vi_screen_state.prev_cells != NULL)
    free(vi_screen_state.prev_cells);
  memset(&vi_screen_state, 0, sizeof(vi_screen_state));
}

static int vi_screen_ensure_state(int rows, int cols)
{
  int total;
  struct vi_screen_cell *cells;
  struct vi_screen_cell *prev_cells;

  if (rows <= 0 || cols <= 0)
    return -1;
  if (vi_screen_state.rows == rows && vi_screen_state.cols == cols &&
      vi_screen_state.cells != NULL && vi_screen_state.prev_cells != NULL) {
    return 0;
  }

  total = rows * cols;
  cells = (struct vi_screen_cell *)malloc(sizeof(*cells) * (size_t)total);
  prev_cells = (struct vi_screen_cell *)malloc(sizeof(*prev_cells) * (size_t)total);
  if (cells == NULL || prev_cells == NULL) {
    if (cells != NULL)
      free(cells);
    if (prev_cells != NULL)
      free(prev_cells);
    return -1;
  }

  if (vi_screen_state.cells != NULL)
    free(vi_screen_state.cells);
  if (vi_screen_state.prev_cells != NULL)
    free(vi_screen_state.prev_cells);
  memset(cells, 0, sizeof(*cells) * (size_t)total);
  memset(prev_cells, 0, sizeof(*prev_cells) * (size_t)total);
  vi_screen_state.cells = cells;
  vi_screen_state.prev_cells = prev_cells;
  vi_screen_state.rows = rows;
  vi_screen_state.cols = cols;
  vi_screen_state.frame_valid = 0;
  return 0;
}

static void vi_screen_clear_row(struct vi_screen_cell *row_cells,
                                int cols, int reverse)
{
  int col;

  for (col = 0; col < cols; col++) {
    row_cells[col].bytes[0] = ' ';
    row_cells[col].len = 1;
    row_cells[col].reverse = (unsigned char)(reverse != 0);
    row_cells[col].continuation = 0;
  }
}

static int vi_screen_visual_byte_bounds(const struct vi_visual_state *visual,
                                        int row, int line_len,
                                        int *start_col, int *end_col)
{
  int start_row;
  int end_row;
  int start_byte;
  int end_byte;

  if (visual == NULL || visual->active == 0)
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
    *end_col = line_len;
    return *end_col > *start_col;
  }
  if (start_row == end_row) {
    *start_col = start_byte;
    *end_col = end_byte;
  } else if (row == start_row) {
    *start_col = start_byte;
    *end_col = line_len;
  } else if (row == end_row) {
    *start_col = 0;
    *end_col = end_byte;
  } else {
    *start_col = 0;
    *end_col = line_len;
  }

  if (*start_col < 0)
    *start_col = 0;
  if (*end_col < 0)
    *end_col = 0;
  if (*start_col > line_len)
    *start_col = line_len;
  if (*end_col > line_len)
    *end_col = line_len;
  return *end_col > *start_col;
}

static void vi_screen_put_bytes(struct vi_screen_cell *row_cells, int cols,
                                int start_col, const char *bytes, int len,
                                int width, int reverse)
{
  int i;

  if (row_cells == NULL || bytes == NULL || len <= 0 ||
      start_col < 0 || start_col >= cols)
    return;
  if (width <= 0)
    width = 1;
  if (start_col + width > cols)
    return;

  memset(row_cells[start_col].bytes, 0, sizeof(row_cells[start_col].bytes));
  memcpy(row_cells[start_col].bytes, bytes, (size_t)len);
  row_cells[start_col].len = (unsigned char)len;
  row_cells[start_col].reverse = (unsigned char)(reverse != 0);
  row_cells[start_col].continuation = 0;
  for (i = 1; i < width; i++) {
    row_cells[start_col + i].len = 0;
    row_cells[start_col + i].reverse = (unsigned char)(reverse != 0);
    row_cells[start_col + i].continuation = 1;
  }
}

static void vi_screen_build_text_row(struct vi_screen_cell *row_cells,
                                     int cols,
                                     const struct vi_buffer *buffer, int row,
                                     const struct vi_visual_state *visual)
{
  const char *data;
  int line_len;
  int visible_len;
  int select_start = 0;
  int select_end = 0;
  int has_visual;
  int byte_index = 0;
  int display_col = 0;

  vi_screen_clear_row(row_cells, cols, 0);
  if (buffer == NULL || row < 0 || row >= buffer->line_count)
    return;

  data = vi_buffer_line_data(buffer, row);
  line_len = vi_buffer_line_length(buffer, row);
  visible_len = vi_buffer_line_bytes_for_width(buffer, row, cols);
  if (visible_len < line_len)
    line_len = visible_len;
  has_visual = vi_screen_visual_byte_bounds(visual, row, line_len,
                                            &select_start, &select_end);

  while (byte_index < line_len && display_col < cols) {
    u_int32_t codepoint;
    int consumed = 0;
    int width;
    int reverse = 0;

    utf8_decode_one(data + byte_index, line_len - byte_index, &codepoint, &consumed);
    if (consumed <= 0)
      break;
    width = unicode_wcwidth(codepoint);
    if (width <= 0 || width > 2)
      width = 1;
    if (display_col + width > cols)
      break;
    if (has_visual != 0 &&
        byte_index >= select_start &&
        byte_index < select_end) {
      reverse = 1;
    }
    vi_screen_put_bytes(row_cells, cols, display_col,
                        data + byte_index, consumed, width, reverse);
    byte_index += consumed;
    display_col += width;
  }
}

static void vi_screen_row_append_ascii(struct vi_screen_cell *row_cells,
                                       int cols, int *used,
                                       const char *text, int reverse)
{
  while (text != NULL && *text != '\0' && *used < cols) {
    vi_screen_put_bytes(row_cells, cols, *used, text, 1, 1, reverse);
    (*used)++;
    text++;
  }
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

static void vi_screen_build_status_row(struct vi_screen_cell *row_cells,
                                       int cols,
                                       enum vi_mode mode,
                                       const char *path,
                                       const char *status,
                                       int dirty)
{
  const char *name;
  const char *message;
  int used = 0;

  vi_screen_clear_row(row_cells, cols, 1);
  name = path;
  if (name == NULL || name[0] == '\0')
    name = "[No Name]";
  message = status;
  if (message == NULL)
    message = "";

  vi_screen_row_append_ascii(row_cells, cols, &used, " ", 1);
  vi_screen_row_append_ascii(row_cells, cols, &used,
                             vi_screen_mode_name(mode), 1);
  vi_screen_row_append_ascii(row_cells, cols, &used, " ", 1);
  vi_screen_row_append_ascii(row_cells, cols, &used, name, 1);
  if (dirty != 0)
    vi_screen_row_append_ascii(row_cells, cols, &used, " [+]", 1);
  if (message[0] != '\0' && used < cols) {
    vi_screen_row_append_ascii(row_cells, cols, &used, " ", 1);
    vi_screen_row_append_ascii(row_cells, cols, &used, message, 1);
  }
}

static void vi_screen_build_command_row(struct vi_screen_cell *row_cells,
                                        int cols,
                                        const char *command,
                                        char command_prefix)
{
  int used = 0;

  vi_screen_clear_row(row_cells, cols, 1);
  if (cols <= 0)
    return;

  if (command_prefix == '\0')
    command_prefix = ':';
  vi_screen_put_bytes(row_cells, cols, used, &command_prefix, 1, 1, 1);
  used++;
  vi_screen_row_append_ascii(row_cells, cols, &used, command, 1);
}

static struct vi_screen_cell *vi_screen_row(struct vi_screen_cell *cells,
                                            int cols, int row)
{
  return cells + row * cols;
}

static int vi_screen_row_equal(const struct vi_screen_cell *left,
                               const struct vi_screen_cell *right, int cols)
{
  int col;

  for (col = 0; col < cols; col++) {
    if (left[col].len != right[col].len ||
        left[col].reverse != right[col].reverse ||
        left[col].continuation != right[col].continuation ||
        memcmp(left[col].bytes, right[col].bytes, sizeof(left[col].bytes)) != 0) {
      return 0;
    }
  }
  return 1;
}

static int vi_screen_find_first_diff(const struct vi_screen_cell *left,
                                     const struct vi_screen_cell *right,
                                     int cols)
{
  int col;

  for (col = 0; col < cols; col++) {
    if (left[col].len != right[col].len ||
        left[col].reverse != right[col].reverse ||
        left[col].continuation != right[col].continuation ||
        memcmp(left[col].bytes, right[col].bytes, sizeof(left[col].bytes)) != 0) {
      return col;
    }
  }
  return -1;
}

static int vi_screen_find_last_diff(const struct vi_screen_cell *left,
                                    const struct vi_screen_cell *right,
                                    int cols)
{
  int col;

  for (col = cols - 1; col >= 0; col--) {
    if (left[col].len != right[col].len ||
        left[col].reverse != right[col].reverse ||
        left[col].continuation != right[col].continuation ||
        memcmp(left[col].bytes, right[col].bytes, sizeof(left[col].bytes)) != 0) {
      return col;
    }
  }
  return -1;
}

static int vi_screen_adjust_diff_start(const struct vi_screen_cell *left,
                                       const struct vi_screen_cell *right,
                                       int col)
{
  while (col > 0 &&
         (left[col].continuation != 0 || right[col].continuation != 0)) {
    col--;
  }
  return col;
}

static void vi_screen_write_row_span(const struct vi_screen_cell *row_cells,
                                     int cols, int start_col, int end_col)
{
  int col;
  int reverse = -1;

  if (row_cells == NULL || start_col < 0 || end_col < start_col || start_col >= cols)
    return;
  if (end_col >= cols)
    end_col = cols - 1;

  for (col = start_col; col <= end_col; col++) {
    if (row_cells[col].continuation != 0)
      continue;
    if (reverse != (int)row_cells[col].reverse) {
      if (row_cells[col].reverse != 0)
        vi_screen_write("\x1b[7m");
      else
        vi_screen_write("\x1b[0m");
      reverse = row_cells[col].reverse;
    }
    vi_screen_write_n(row_cells[col].bytes, row_cells[col].len);
  }
  if (reverse > 0)
    vi_screen_write("\x1b[0m");
}

static void vi_screen_build_frame(const struct vi_buffer *buffer,
                                  enum vi_mode mode,
                                  const char *path, const char *status,
                                  const char *command, char command_prefix,
                                  const struct vi_visual_state *visual,
                                  int row_offset, int rows, int cols)
{
  int row;
  int visible_rows;

  visible_rows = rows - 1;
  if (visible_rows < 1)
    visible_rows = 1;

  for (row = 0; row < visible_rows; row++) {
    struct vi_screen_cell *row_cells =
        vi_screen_row(vi_screen_state.cells, cols, row);
    int line_index = row_offset + row;

    vi_screen_build_text_row(row_cells, cols, buffer, line_index, visual);
  }

  if (mode == VI_MODE_COMMAND || mode == VI_MODE_SEARCH) {
    vi_screen_build_command_row(
        vi_screen_row(vi_screen_state.cells, cols, rows - 1),
        cols, command, command_prefix);
  } else {
    vi_screen_build_status_row(
        vi_screen_row(vi_screen_state.cells, cols, rows - 1),
        cols, mode, path, status, buffer != NULL ? buffer->dirty : 0);
  }
}

static void vi_screen_swap_frames(void)
{
  int total = vi_screen_state.rows * vi_screen_state.cols;

  memcpy(vi_screen_state.prev_cells, vi_screen_state.cells,
         sizeof(struct vi_screen_cell) * (size_t)total);
  vi_screen_state.frame_valid = 1;
}

static void vi_screen_emit_metric(u_int32_t redraw_bytes,
                                  u_int32_t dirty_rows,
                                  u_int32_t dirty_spans,
                                  int full_fallback)
{
  char buf[192];
  char *p = buf;

  vi_screen_state.redraw_count++;
  vi_screen_state.redraw_bytes_total += redraw_bytes;
  vi_screen_state.dirty_rows_total += dirty_rows;
  vi_screen_state.dirty_spans_total += dirty_spans;
  if (full_fallback != 0)
    vi_screen_state.full_redraw_fallbacks++;

  memcpy(p, "TERM_METRIC component=vi redraws=", 33);
  p += 33;
  p = vi_screen_append_uint(p, vi_screen_state.redraw_count);
  memcpy(p, " redraw_bytes=", 14);
  p += 14;
  p = vi_screen_append_uint(p, redraw_bytes);
  memcpy(p, " dirty_rows=", 12);
  p += 12;
  p = vi_screen_append_uint(p, dirty_rows);
  memcpy(p, " dirty_spans=", 13);
  p += 13;
  p = vi_screen_append_uint(p, dirty_spans);
  memcpy(p, " full_fallbacks=", 16);
  p += 16;
  p = vi_screen_append_uint(p, vi_screen_state.full_redraw_fallbacks);
  *p++ = '\n';
  debug_write(buf, (size_t)(p - buf));
}

void vi_screen_redraw(const struct vi_buffer *buffer, enum vi_mode mode,
                      const char *path, const char *status,
                      const char *command, char command_prefix,
                      const struct vi_visual_state *visual,
                      int row_offset, int rows, int cols)
{
  int cursor_row;
  int cursor_col;
  int visible_rows;
  int full_fallback = 0;
  int dirty_rows = 0;
  int dirty_spans = 0;
  int row;

  if (buffer == NULL || rows <= 0 || cols <= 0)
    return;
  if (vi_screen_ensure_state(rows, cols) < 0)
    return;

  visible_rows = rows - 1;
  if (visible_rows < 1)
    visible_rows = 1;
  vi_screen_current_redraw_bytes = 0;
  memset(vi_screen_state.cells, 0,
         sizeof(struct vi_screen_cell) * (size_t)(rows * cols));
  vi_screen_build_frame(buffer, mode, path, status, command, command_prefix,
                        visual, row_offset, rows, cols);

  if (mode == VI_MODE_COMMAND || mode == VI_MODE_SEARCH) {
    cursor_row = rows - 1;
    cursor_col = 1;
    if (command != NULL)
      cursor_col += (int)strlen(command);
  } else {
    cursor_row = buffer->cursor_row - row_offset;
    cursor_col = vi_buffer_cursor_display_col(buffer);
    if (cursor_row < 0)
      cursor_row = 0;
    if (cursor_row >= visible_rows)
      cursor_row = visible_rows - 1;
  }
  if (cursor_col < 0)
    cursor_col = 0;
  if (cursor_col >= cols)
    cursor_col = cols - 1;

  if (vi_screen_state.frame_valid == 0)
    full_fallback = 1;

  if (full_fallback != 0) {
    vi_screen_write(VI_SCREEN_SYNC_BEGIN);
    vi_screen_state.sync_frame_count++;
    vi_screen_write("\x1b[0m\x1b[2J\x1b[H");
    for (row = 0; row < rows; row++) {
      vi_screen_move_cursor(row, 0);
      vi_screen_write_row_span(vi_screen_row(vi_screen_state.cells, cols, row),
                               cols, 0, cols - 1);
    }
    dirty_rows = (u_int32_t)rows;
    dirty_spans = (u_int32_t)rows;
    vi_screen_write(VI_SCREEN_SYNC_END);
  } else {
    int wrote_sync = 0;

    for (row = 0; row < rows; row++) {
      struct vi_screen_cell *prev_row =
          vi_screen_row(vi_screen_state.prev_cells, cols, row);
      struct vi_screen_cell *next_row =
          vi_screen_row(vi_screen_state.cells, cols, row);
      int start_col;
      int end_col;

      if (vi_screen_row_equal(prev_row, next_row, cols) != 0)
        continue;
      if (wrote_sync == 0) {
        vi_screen_write(VI_SCREEN_SYNC_BEGIN);
        vi_screen_state.sync_frame_count++;
        wrote_sync = 1;
      }
      start_col = vi_screen_find_first_diff(prev_row, next_row, cols);
      end_col = vi_screen_find_last_diff(prev_row, next_row, cols);
      if (start_col < 0 || end_col < start_col)
        continue;
      start_col = vi_screen_adjust_diff_start(prev_row, next_row, start_col);
      vi_screen_move_cursor(row, start_col);
      vi_screen_write_row_span(next_row, cols, start_col, end_col);
      dirty_rows++;
      dirty_spans++;
    }
    if (wrote_sync != 0)
      vi_screen_write(VI_SCREEN_SYNC_END);
  }

  vi_screen_move_cursor(cursor_row, cursor_col);
  vi_screen_flush();
  vi_screen_emit_metric(vi_screen_current_redraw_bytes,
                        (u_int32_t)dirty_rows,
                        (u_int32_t)dirty_spans,
                        full_fallback);
  vi_screen_swap_frames();
}

void vi_screen_restore(void)
{
  vi_screen_current_redraw_bytes = 0;
  vi_screen_write("\x1b[0m\x1b[?1049l");
  vi_screen_flush();
  vi_screen_state.alternate_active = 0;
  vi_screen_state.frame_valid = 0;
}

void vi_screen_enter(void)
{
  vi_screen_current_redraw_bytes = 0;
  vi_screen_write("\x1b[?1049h");
  vi_screen_flush();
  vi_screen_state.alternate_active = 1;
  vi_screen_state.frame_valid = 0;
}

#ifdef TEST_BUILD
void vi_screen_test_reset_state(void)
{
  vi_screen_buffer_len = 0;
  vi_screen_current_redraw_bytes = 0;
  vi_screen_release_state();
}
#endif
