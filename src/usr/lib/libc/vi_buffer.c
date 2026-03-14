#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <utf8.h>
#include <vi.h>
#include <wcwidth.h>

#define VI_INITIAL_LINE_CAP 8
#define VI_INITIAL_CHAR_CAP 16

struct vi_position {
  int row;
  int col;
};

static int vi_buffer_reset_empty(struct vi_buffer *buffer);
static int vi_buffer_ensure_lines(struct vi_buffer *buffer, int needed);
static int vi_line_reserve(struct vi_line *line, int needed);
static int vi_buffer_append_loaded_char(struct vi_buffer *buffer, char ch);
static int vi_buffer_append_loaded_newline(struct vi_buffer *buffer);
static void vi_buffer_clamp_cursor(struct vi_buffer *buffer);
static int vi_is_blank_byte(unsigned char ch);
static int vi_is_continuation_byte(unsigned char ch);
static struct vi_position vi_make_position(int row, int col);
static int vi_position_compare(struct vi_position left, struct vi_position right);
static int vi_position_is_origin(struct vi_position pos);
static int vi_line_prev_boundary(const struct vi_line *line, int index);
static int vi_line_next_boundary(const struct vi_line *line, int index);
static int vi_line_display_col_until(const struct vi_line *line, int byte_limit);
static int vi_line_offset_for_display_col(const struct vi_line *line, int display_col);
static struct vi_position vi_buffer_cursor_position(const struct vi_buffer *buffer);
static void vi_buffer_set_cursor_position(struct vi_buffer *buffer,
                                          struct vi_position pos);
static int vi_buffer_position_is_eof(const struct vi_buffer *buffer,
                                     struct vi_position pos);
static int vi_buffer_position_is_blank(const struct vi_buffer *buffer,
                                       struct vi_position pos);
static struct vi_position vi_buffer_next_position(const struct vi_buffer *buffer,
                                                  struct vi_position pos);
static struct vi_position vi_buffer_prev_position(const struct vi_buffer *buffer,
                                                  struct vi_position pos);
static struct vi_position vi_buffer_word_forward_position(const struct vi_buffer *buffer,
                                                          struct vi_position pos);
static struct vi_position vi_buffer_word_backward_position(const struct vi_buffer *buffer,
                                                           struct vi_position pos);
static struct vi_position vi_buffer_word_end_position(const struct vi_buffer *buffer,
                                                      struct vi_position pos);
static int vi_buffer_first_nonblank_col(const struct vi_buffer *buffer, int row);
static int vi_buffer_insert_empty_line(struct vi_buffer *buffer, int row);
static int vi_buffer_delete_range(struct vi_buffer *buffer,
                                  struct vi_position start,
                                  struct vi_position end);

static int vi_is_blank_byte(unsigned char ch)
{
  return ch == ' ' || ch == '\t';
}

static int vi_is_continuation_byte(unsigned char ch)
{
  return (ch & 0xc0U) == 0x80U;
}

static struct vi_position vi_make_position(int row, int col)
{
  struct vi_position pos;

  pos.row = row;
  pos.col = col;
  return pos;
}

static int vi_position_compare(struct vi_position left, struct vi_position right)
{
  if (left.row < right.row)
    return -1;
  if (left.row > right.row)
    return 1;
  if (left.col < right.col)
    return -1;
  if (left.col > right.col)
    return 1;
  return 0;
}

static int vi_position_is_origin(struct vi_position pos)
{
  return pos.row == 0 && pos.col == 0;
}

static int vi_line_prev_boundary(const struct vi_line *line, int index)
{
  if (line == NULL || line->data == NULL)
    return 0;
  return utf8_prev_char_start(line->data, line->len, index);
}

static int vi_line_next_boundary(const struct vi_line *line, int index)
{
  if (line == NULL || line->data == NULL)
    return index;
  return utf8_next_char_end(line->data, line->len, index);
}

static int vi_line_display_col_until(const struct vi_line *line, int byte_limit)
{
  int index = 0;
  int cols = 0;

  if (line == NULL || line->data == NULL)
    return 0;
  if (byte_limit > line->len)
    byte_limit = line->len;

  while (index < byte_limit) {
    u_int32_t codepoint;
    int consumed;
    int width;

    utf8_decode_one(line->data + index, byte_limit - index, &codepoint, &consumed);
    width = unicode_wcwidth(codepoint);
    if (width < 0)
      width = 1;
    cols += width;
    index += consumed;
  }
  return cols;
}

static int vi_line_offset_for_display_col(const struct vi_line *line, int display_col)
{
  int index = 0;
  int cols = 0;

  if (line == NULL || line->data == NULL)
    return 0;
  if (display_col <= 0)
    return 0;

  while (index < line->len) {
    u_int32_t codepoint;
    int consumed;
    int width;

    utf8_decode_one(line->data + index, line->len - index, &codepoint, &consumed);
    width = unicode_wcwidth(codepoint);
    if (width < 0)
      width = 1;
    if (cols + width > display_col)
      break;
    cols += width;
    index += consumed;
  }
  return index;
}

static struct vi_position vi_buffer_cursor_position(const struct vi_buffer *buffer)
{
  return vi_make_position(buffer->cursor_row, buffer->cursor_col);
}

static void vi_buffer_set_cursor_position(struct vi_buffer *buffer,
                                          struct vi_position pos)
{
  buffer->cursor_row = pos.row;
  buffer->cursor_col = pos.col;
  vi_buffer_clamp_cursor(buffer);
}

static int vi_buffer_position_is_eof(const struct vi_buffer *buffer,
                                     struct vi_position pos)
{
  int last_row;
  int last_col;

  if (buffer == NULL || buffer->line_count <= 0)
    return 1;

  last_row = buffer->line_count - 1;
  last_col = buffer->lines[last_row].len;
  return pos.row >= last_row && pos.col >= last_col;
}

static int vi_buffer_position_is_blank(const struct vi_buffer *buffer,
                                       struct vi_position pos)
{
  const struct vi_line *line;

  if (buffer == NULL || pos.row < 0 || pos.row >= buffer->line_count)
    return 1;

  line = &buffer->lines[pos.row];
  if (pos.col < 0)
    return 1;
  if (pos.col >= line->len)
    return 1;
  return vi_is_blank_byte((unsigned char)line->data[pos.col]);
}

static struct vi_position vi_buffer_next_position(const struct vi_buffer *buffer,
                                                  struct vi_position pos)
{
  const struct vi_line *line;

  if (buffer == NULL || pos.row < 0 || pos.row >= buffer->line_count)
    return pos;

  line = &buffer->lines[pos.row];
  if (pos.col < line->len)
    return vi_make_position(pos.row, vi_line_next_boundary(line, pos.col));
  if (pos.row + 1 < buffer->line_count)
    return vi_make_position(pos.row + 1, 0);
  return pos;
}

static struct vi_position vi_buffer_prev_position(const struct vi_buffer *buffer,
                                                  struct vi_position pos)
{
  const struct vi_line *line;

  if (buffer == NULL || pos.row < 0 || pos.row >= buffer->line_count)
    return pos;

  line = &buffer->lines[pos.row];
  if (pos.col > 0)
    return vi_make_position(pos.row, vi_line_prev_boundary(line, pos.col));
  if (pos.row > 0)
    return vi_make_position(pos.row - 1, buffer->lines[pos.row - 1].len);
  return pos;
}

static struct vi_position vi_buffer_word_forward_position(const struct vi_buffer *buffer,
                                                          struct vi_position pos)
{
  if (buffer == NULL || vi_buffer_position_is_eof(buffer, pos))
    return pos;

  if (!vi_buffer_position_is_blank(buffer, pos)) {
    while (!vi_buffer_position_is_eof(buffer, pos) &&
           !vi_buffer_position_is_blank(buffer, pos)) {
      pos = vi_buffer_next_position(buffer, pos);
    }
  }

  while (!vi_buffer_position_is_eof(buffer, pos) &&
         vi_buffer_position_is_blank(buffer, pos)) {
    pos = vi_buffer_next_position(buffer, pos);
  }

  return pos;
}

static struct vi_position vi_buffer_word_backward_position(const struct vi_buffer *buffer,
                                                           struct vi_position pos)
{
  struct vi_position prev;

  if (buffer == NULL || vi_position_is_origin(pos))
    return pos;

  pos = vi_buffer_prev_position(buffer, pos);
  while (!vi_position_is_origin(pos) &&
         vi_buffer_position_is_blank(buffer, pos)) {
    prev = vi_buffer_prev_position(buffer, pos);
    if (vi_position_compare(prev, pos) == 0)
      break;
    pos = prev;
  }

  while (!vi_position_is_origin(pos)) {
    prev = vi_buffer_prev_position(buffer, pos);
    if (vi_buffer_position_is_blank(buffer, prev))
      break;
    pos = prev;
  }

  return pos;
}

static struct vi_position vi_buffer_word_end_position(const struct vi_buffer *buffer,
                                                      struct vi_position pos)
{
  struct vi_position next;

  if (buffer == NULL || vi_buffer_position_is_eof(buffer, pos))
    return pos;

  while (!vi_buffer_position_is_eof(buffer, pos) &&
         vi_buffer_position_is_blank(buffer, pos)) {
    pos = vi_buffer_next_position(buffer, pos);
  }

  if (vi_buffer_position_is_eof(buffer, pos))
    return pos;

  while (!vi_buffer_position_is_eof(buffer, pos)) {
    next = vi_buffer_next_position(buffer, pos);
    if (vi_position_compare(next, pos) == 0 ||
        vi_buffer_position_is_eof(buffer, next) ||
        vi_buffer_position_is_blank(buffer, next)) {
      return pos;
    }
    pos = next;
  }

  return pos;
}

static int vi_buffer_first_nonblank_col(const struct vi_buffer *buffer, int row)
{
  const struct vi_line *line;
  int col = 0;

  if (buffer == NULL || row < 0 || row >= buffer->line_count)
    return 0;

  line = &buffer->lines[row];
  while (col < line->len && vi_is_blank_byte((unsigned char)line->data[col]))
    col++;
  return col;
}

static int vi_buffer_insert_empty_line(struct vi_buffer *buffer, int row)
{
  int i;

  if (buffer == NULL)
    return -1;
  if (row < 0)
    row = 0;
  if (row > buffer->line_count)
    row = buffer->line_count;

  if (vi_buffer_ensure_lines(buffer, buffer->line_count + 1) < 0)
    return -1;

  for (i = buffer->line_count; i > row; i--) {
    buffer->lines[i] = buffer->lines[i - 1];
  }
  memset(&buffer->lines[row], 0, sizeof(struct vi_line));
  buffer->line_count++;
  if (vi_line_reserve(&buffer->lines[row], 1) < 0) {
    for (i = row; i < buffer->line_count - 1; i++) {
      buffer->lines[i] = buffer->lines[i + 1];
    }
    memset(&buffer->lines[buffer->line_count - 1], 0, sizeof(struct vi_line));
    buffer->line_count--;
    return -1;
  }
  return 0;
}

static int vi_buffer_delete_range(struct vi_buffer *buffer,
                                  struct vi_position start,
                                  struct vi_position end)
{
  struct vi_position tmp;
  struct vi_line *line;
  struct vi_line *first;
  struct vi_line *last;
  int i;
  int tail_len;
  int remove_count;

  if (buffer == NULL)
    return -1;

  if (vi_position_compare(start, end) > 0) {
    tmp = start;
    start = end;
    end = tmp;
  }
  if (vi_position_compare(start, end) == 0)
    return 0;

  if (start.row == end.row) {
    line = &buffer->lines[start.row];
    for (i = end.col; i <= line->len; i++) {
      line->data[start.col + i - end.col] = line->data[i];
    }
    line->len -= end.col - start.col;
    buffer->cursor_row = start.row;
    buffer->cursor_col = start.col;
    buffer->dirty = 1;
    return 0;
  }

  first = &buffer->lines[start.row];
  last = &buffer->lines[end.row];
  tail_len = last->len - end.col;
  if (vi_line_reserve(first, start.col + tail_len + 1) < 0)
    return -1;

  if (tail_len > 0)
    memcpy(first->data + start.col, last->data + end.col, tail_len);
  first->len = start.col + tail_len;
  first->data[first->len] = '\0';

  for (i = start.row + 1; i <= end.row; i++) {
    if (buffer->lines[i].data != NULL)
      free(buffer->lines[i].data);
  }

  remove_count = end.row - start.row;
  for (i = end.row + 1; i < buffer->line_count; i++) {
    buffer->lines[i - remove_count] = buffer->lines[i];
  }
  for (i = buffer->line_count - remove_count; i < buffer->line_count; i++) {
    memset(&buffer->lines[i], 0, sizeof(struct vi_line));
  }

  buffer->line_count -= remove_count;
  buffer->cursor_row = start.row;
  buffer->cursor_col = start.col;
  buffer->dirty = 1;
  return 0;
}

static int vi_buffer_reset_empty(struct vi_buffer *buffer)
{
  struct vi_line *lines;

  lines = (struct vi_line *)malloc(sizeof(struct vi_line) * VI_INITIAL_LINE_CAP);
  if (lines == NULL)
    return -1;

  memset(lines, 0, sizeof(struct vi_line) * VI_INITIAL_LINE_CAP);
  buffer->lines = lines;
  buffer->line_cap = VI_INITIAL_LINE_CAP;
  buffer->line_count = 1;
  buffer->cursor_row = 0;
  buffer->cursor_col = 0;
  buffer->dirty = 0;
  return vi_line_reserve(&buffer->lines[0], 1);
}

static int vi_buffer_ensure_lines(struct vi_buffer *buffer, int needed)
{
  struct vi_line *next;
  int next_cap;

  if (needed <= buffer->line_cap)
    return 0;

  next_cap = buffer->line_cap;
  while (next_cap < needed)
    next_cap *= 2;

  next = (struct vi_line *)malloc(sizeof(struct vi_line) * next_cap);
  if (next == NULL)
    return -1;

  memset(next, 0, sizeof(struct vi_line) * next_cap);
  memcpy(next, buffer->lines, sizeof(struct vi_line) * buffer->line_count);
  free(buffer->lines);
  buffer->lines = next;
  buffer->line_cap = next_cap;
  return 0;
}

static int vi_line_reserve(struct vi_line *line, int needed)
{
  char *next;
  int next_cap;

  if (needed <= line->cap)
    return 0;

  next_cap = line->cap;
  if (next_cap <= 0)
    next_cap = VI_INITIAL_CHAR_CAP;
  while (next_cap < needed)
    next_cap *= 2;

  next = (char *)malloc(next_cap);
  if (next == NULL)
    return -1;

  if (line->data != NULL && line->len > 0)
    memcpy(next, line->data, line->len);
  if (line->data != NULL)
    free(line->data);

  line->data = next;
  line->cap = next_cap;
  line->data[line->len] = '\0';
  return 0;
}

static int vi_buffer_append_loaded_char(struct vi_buffer *buffer, char ch)
{
  struct vi_line *line;

  line = &buffer->lines[buffer->line_count - 1];
  if (vi_line_reserve(line, line->len + 2) < 0)
    return -1;

  line->data[line->len++] = ch;
  line->data[line->len] = '\0';
  return 0;
}

static int vi_buffer_append_loaded_newline(struct vi_buffer *buffer)
{
  if (vi_buffer_ensure_lines(buffer, buffer->line_count + 1) < 0)
    return -1;

  memset(&buffer->lines[buffer->line_count], 0, sizeof(struct vi_line));
  buffer->line_count++;
  return vi_line_reserve(&buffer->lines[buffer->line_count - 1], 1);
}

static void vi_buffer_clamp_cursor(struct vi_buffer *buffer)
{
  int len;
  struct vi_line *line;

  if (buffer->cursor_row < 0)
    buffer->cursor_row = 0;
  if (buffer->cursor_row >= buffer->line_count)
    buffer->cursor_row = buffer->line_count - 1;

  len = vi_buffer_line_length(buffer, buffer->cursor_row);
  if (buffer->cursor_col < 0)
    buffer->cursor_col = 0;
  if (buffer->cursor_col > len)
    buffer->cursor_col = len;
  if (buffer->cursor_col < len) {
    line = &buffer->lines[buffer->cursor_row];
    while (buffer->cursor_col > 0 &&
           vi_is_continuation_byte((unsigned char)line->data[buffer->cursor_col])) {
      buffer->cursor_col--;
    }
  }
}

int vi_buffer_init(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return -1;

  memset(buffer, 0, sizeof(*buffer));
  return vi_buffer_reset_empty(buffer);
}

void vi_buffer_free(struct vi_buffer *buffer)
{
  int i;

  if (buffer == NULL)
    return;

  if (buffer->lines != NULL) {
    for (i = 0; i < buffer->line_count; i++) {
      if (buffer->lines[i].data != NULL)
        free(buffer->lines[i].data);
    }
    free(buffer->lines);
  }

  memset(buffer, 0, sizeof(*buffer));
}

int vi_buffer_load(struct vi_buffer *buffer, const char *data, int len)
{
  int i;

  if (buffer == NULL)
    return -1;

  vi_buffer_free(buffer);
  if (vi_buffer_reset_empty(buffer) < 0)
    return -1;

  for (i = 0; i < len; i++) {
    if (data[i] == '\r')
      continue;
    if (data[i] == '\n') {
      if (vi_buffer_append_loaded_newline(buffer) < 0)
        return -1;
    } else {
      if (vi_buffer_append_loaded_char(buffer, data[i]) < 0)
        return -1;
    }
  }

  buffer->cursor_row = 0;
  buffer->cursor_col = 0;
  buffer->dirty = 0;
  return 0;
}

int vi_buffer_insert_char(struct vi_buffer *buffer, char ch)
{
  struct vi_line *line;
  int i;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  line = &buffer->lines[buffer->cursor_row];
  if (vi_line_reserve(line, line->len + 2) < 0)
    return -1;

  for (i = line->len; i >= buffer->cursor_col; i--) {
    line->data[i + 1] = line->data[i];
  }
  line->data[buffer->cursor_col] = ch;
  line->len++;
  buffer->cursor_col++;
  buffer->dirty = 1;
  return 0;
}

int vi_buffer_insert_newline(struct vi_buffer *buffer)
{
  struct vi_line *line;
  struct vi_line *next_line;
  int right_len;
  int i;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  if (vi_buffer_ensure_lines(buffer, buffer->line_count + 1) < 0)
    return -1;

  for (i = buffer->line_count; i > buffer->cursor_row + 1; i--) {
    buffer->lines[i] = buffer->lines[i - 1];
  }
  memset(&buffer->lines[buffer->cursor_row + 1], 0, sizeof(struct vi_line));
  buffer->line_count++;

  line = &buffer->lines[buffer->cursor_row];
  next_line = &buffer->lines[buffer->cursor_row + 1];
  right_len = line->len - buffer->cursor_col;
  if (vi_line_reserve(next_line, right_len + 1) < 0)
    return -1;

  if (right_len > 0)
    memcpy(next_line->data, line->data + buffer->cursor_col, right_len);
  next_line->len = right_len;
  next_line->data[next_line->len] = '\0';

  line->len = buffer->cursor_col;
  line->data[line->len] = '\0';

  buffer->cursor_row++;
  buffer->cursor_col = 0;
  buffer->dirty = 1;
  return 0;
}

int vi_buffer_backspace(struct vi_buffer *buffer)
{
  struct vi_line *line;
  struct vi_line *prev;
  int i;
  int prev_len;
  int prev_col;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  line = &buffer->lines[buffer->cursor_row];

  if (buffer->cursor_col > 0) {
    prev_col = vi_line_prev_boundary(line, buffer->cursor_col);
    for (i = buffer->cursor_col; i <= line->len; i++) {
      line->data[prev_col + i - buffer->cursor_col] = line->data[i];
    }
    line->len -= buffer->cursor_col - prev_col;
    buffer->cursor_col = prev_col;
    buffer->dirty = 1;
    return 0;
  }

  if (buffer->cursor_row == 0)
    return 0;

  prev = &buffer->lines[buffer->cursor_row - 1];
  prev_len = prev->len;
  if (vi_line_reserve(prev, prev->len + line->len + 1) < 0)
    return -1;

  if (line->len > 0)
    memcpy(prev->data + prev->len, line->data, line->len);
  prev->len += line->len;
  prev->data[prev->len] = '\0';
  if (line->data != NULL)
    free(line->data);

  for (i = buffer->cursor_row; i < buffer->line_count - 1; i++) {
    buffer->lines[i] = buffer->lines[i + 1];
  }
  memset(&buffer->lines[buffer->line_count - 1], 0, sizeof(struct vi_line));
  buffer->line_count--;
  buffer->cursor_row--;
  buffer->cursor_col = prev_len;
  buffer->dirty = 1;
  return 0;
}

int vi_buffer_delete_char(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;
  struct vi_line *line;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  line = &buffer->lines[buffer->cursor_row];
  if (buffer->cursor_col >= line->len)
    return 0;

  start = vi_buffer_cursor_position(buffer);
  end = vi_make_position(start.row,
                         vi_line_next_boundary(line, start.col));
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_delete_prev_char(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;
  struct vi_line *line;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  if (buffer->cursor_col <= 0)
    return 0;

  line = &buffer->lines[buffer->cursor_row];
  start = vi_make_position(buffer->cursor_row,
                           vi_line_prev_boundary(line, buffer->cursor_col));
  end = vi_buffer_cursor_position(buffer);
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_delete_to_line_start(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  if (buffer->cursor_col <= 0)
    return 0;

  start = vi_make_position(buffer->cursor_row, 0);
  end = vi_buffer_cursor_position(buffer);
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_delete_to_line_end(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;
  int len;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  len = vi_buffer_line_length(buffer, buffer->cursor_row);
  if (buffer->cursor_col >= len)
    return 0;

  start = vi_buffer_cursor_position(buffer);
  end = vi_make_position(buffer->cursor_row, len);
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_delete_line(struct vi_buffer *buffer)
{
  struct vi_line *line;
  int row;
  int i;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  row = buffer->cursor_row;
  line = &buffer->lines[row];

  if (buffer->line_count == 1) {
    if (line->data == NULL && vi_line_reserve(line, 1) < 0)
      return -1;
    if (line->len == 0)
      return 0;
    line->len = 0;
    line->data[0] = '\0';
    buffer->cursor_col = 0;
    buffer->dirty = 1;
    return 0;
  }

  if (line->data != NULL)
    free(line->data);
  for (i = row; i < buffer->line_count - 1; i++) {
    buffer->lines[i] = buffer->lines[i + 1];
  }
  memset(&buffer->lines[buffer->line_count - 1], 0, sizeof(struct vi_line));
  buffer->line_count--;
  if (row >= buffer->line_count)
    row = buffer->line_count - 1;
  buffer->cursor_row = row;
  buffer->cursor_col = 0;
  buffer->dirty = 1;
  return 0;
}

int vi_buffer_delete_word_forward(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  start = vi_buffer_cursor_position(buffer);
  end = vi_buffer_word_forward_position(buffer, start);
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_delete_word_backward(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  end = vi_buffer_cursor_position(buffer);
  start = vi_buffer_word_backward_position(buffer, end);
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_delete_word_end(struct vi_buffer *buffer)
{
  struct vi_position start;
  struct vi_position end;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  start = vi_buffer_cursor_position(buffer);
  end = vi_buffer_word_end_position(buffer, start);
  if (!vi_buffer_position_is_eof(buffer, end))
    end = vi_buffer_next_position(buffer, end);
  return vi_buffer_delete_range(buffer, start, end);
}

int vi_buffer_open_line_below(struct vi_buffer *buffer)
{
  int row;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  row = buffer->cursor_row + 1;
  if (vi_buffer_insert_empty_line(buffer, row) < 0)
    return -1;
  buffer->cursor_row = row;
  buffer->cursor_col = 0;
  buffer->dirty = 1;
  return 0;
}

int vi_buffer_open_line_above(struct vi_buffer *buffer)
{
  int row;

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  row = buffer->cursor_row;
  if (vi_buffer_insert_empty_line(buffer, row) < 0)
    return -1;
  buffer->cursor_row = row;
  buffer->cursor_col = 0;
  buffer->dirty = 1;
  return 0;
}

void vi_buffer_move_left(struct vi_buffer *buffer)
{
  struct vi_line *line;

  if (buffer == NULL)
    return;

  if (buffer->cursor_col > 0) {
    line = &buffer->lines[buffer->cursor_row];
    buffer->cursor_col = vi_line_prev_boundary(line, buffer->cursor_col);
  } else if (buffer->cursor_row > 0) {
    buffer->cursor_row--;
    buffer->cursor_col = vi_buffer_line_length(buffer, buffer->cursor_row);
  }
}

void vi_buffer_move_line_start(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  buffer->cursor_col = 0;
}

void vi_buffer_move_line_first_nonblank(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  buffer->cursor_col = vi_buffer_first_nonblank_col(buffer, buffer->cursor_row);
}

void vi_buffer_move_line_last_char(struct vi_buffer *buffer)
{
  int len;

  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  len = vi_buffer_line_length(buffer, buffer->cursor_row);
  if (len <= 0)
    buffer->cursor_col = 0;
  else
    buffer->cursor_col = vi_line_prev_boundary(&buffer->lines[buffer->cursor_row], len);
}

void vi_buffer_move_line_end(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  buffer->cursor_col = vi_buffer_line_length(buffer, buffer->cursor_row);
}

void vi_buffer_move_right(struct vi_buffer *buffer)
{
  int len;
  struct vi_line *line;

  if (buffer == NULL)
    return;

  len = vi_buffer_line_length(buffer, buffer->cursor_row);
  if (buffer->cursor_col < len) {
    line = &buffer->lines[buffer->cursor_row];
    buffer->cursor_col = vi_line_next_boundary(line, buffer->cursor_col);
  } else if (buffer->cursor_row + 1 < buffer->line_count) {
    buffer->cursor_row++;
    buffer->cursor_col = 0;
  }
}

void vi_buffer_move_up(struct vi_buffer *buffer)
{
  int target_col;

  if (buffer == NULL)
    return;

  target_col = vi_buffer_cursor_display_col(buffer);
  if (buffer->cursor_row > 0)
    buffer->cursor_row--;
  buffer->cursor_col = vi_line_offset_for_display_col(&buffer->lines[buffer->cursor_row],
                                                      target_col);
}

void vi_buffer_move_down(struct vi_buffer *buffer)
{
  int target_col;

  if (buffer == NULL)
    return;

  target_col = vi_buffer_cursor_display_col(buffer);
  if (buffer->cursor_row + 1 < buffer->line_count)
    buffer->cursor_row++;
  buffer->cursor_col = vi_line_offset_for_display_col(&buffer->lines[buffer->cursor_row],
                                                      target_col);
}

void vi_buffer_move_first_line(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  buffer->cursor_row = 0;
  buffer->cursor_col = vi_buffer_first_nonblank_col(buffer, 0);
}

void vi_buffer_move_last_line(struct vi_buffer *buffer)
{
  int row;

  if (buffer == NULL)
    return;

  row = buffer->line_count - 1;
  buffer->cursor_row = row;
  buffer->cursor_col = vi_buffer_first_nonblank_col(buffer, row);
}

void vi_buffer_move_word_forward(struct vi_buffer *buffer)
{
  struct vi_position pos;

  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  pos = vi_buffer_word_forward_position(buffer, vi_buffer_cursor_position(buffer));
  vi_buffer_set_cursor_position(buffer, pos);
}

void vi_buffer_move_word_backward(struct vi_buffer *buffer)
{
  struct vi_position pos;

  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  pos = vi_buffer_word_backward_position(buffer, vi_buffer_cursor_position(buffer));
  vi_buffer_set_cursor_position(buffer, pos);
}

void vi_buffer_move_word_end(struct vi_buffer *buffer)
{
  struct vi_position pos;

  if (buffer == NULL)
    return;

  vi_buffer_clamp_cursor(buffer);
  pos = vi_buffer_word_end_position(buffer, vi_buffer_cursor_position(buffer));
  vi_buffer_set_cursor_position(buffer, pos);
}

const char *vi_buffer_line_data(const struct vi_buffer *buffer, int row)
{
  if (buffer == NULL || row < 0 || row >= buffer->line_count)
    return "";
  if (buffer->lines[row].data == NULL)
    return "";
  return buffer->lines[row].data;
}

int vi_buffer_line_length(const struct vi_buffer *buffer, int row)
{
  if (buffer == NULL || row < 0 || row >= buffer->line_count)
    return 0;
  return buffer->lines[row].len;
}

int vi_buffer_line_display_width(const struct vi_buffer *buffer, int row)
{
  if (buffer == NULL || row < 0 || row >= buffer->line_count)
    return 0;
  return vi_line_display_col_until(&buffer->lines[row], buffer->lines[row].len);
}

int vi_buffer_line_bytes_for_width(const struct vi_buffer *buffer, int row, int cols)
{
  if (buffer == NULL || row < 0 || row >= buffer->line_count)
    return 0;
  return vi_line_offset_for_display_col(&buffer->lines[row], cols);
}

int vi_buffer_cursor_display_col(const struct vi_buffer *buffer)
{
  if (buffer == NULL || buffer->cursor_row < 0 || buffer->cursor_row >= buffer->line_count)
    return 0;
  return vi_line_display_col_until(&buffer->lines[buffer->cursor_row],
                                   buffer->cursor_col);
}

void vi_buffer_clear_dirty(struct vi_buffer *buffer)
{
  if (buffer != NULL)
    buffer->dirty = 0;
}

enum vi_command_kind vi_parse_command(const char *command)
{
  if (command == NULL || command[0] == '\0')
    return VI_COMMAND_NONE;
  if (strcmp(command, "w") == 0)
    return VI_COMMAND_WRITE;
  if (strcmp(command, "q") == 0)
    return VI_COMMAND_QUIT;
  if (strcmp(command, "wq") == 0)
    return VI_COMMAND_WRITE_QUIT;
  return VI_COMMAND_INVALID;
}
