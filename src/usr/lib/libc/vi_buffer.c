#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <vi.h>

#define VI_INITIAL_LINE_CAP 8
#define VI_INITIAL_CHAR_CAP 16

static int vi_buffer_reset_empty(struct vi_buffer *buffer);
static int vi_buffer_ensure_lines(struct vi_buffer *buffer, int needed);
static int vi_line_reserve(struct vi_line *line, int needed);
static int vi_buffer_append_loaded_char(struct vi_buffer *buffer, char ch);
static int vi_buffer_append_loaded_newline(struct vi_buffer *buffer);
static void vi_buffer_clamp_cursor(struct vi_buffer *buffer);

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

  if (buffer->cursor_row < 0)
    buffer->cursor_row = 0;
  if (buffer->cursor_row >= buffer->line_count)
    buffer->cursor_row = buffer->line_count - 1;

  len = vi_buffer_line_length(buffer, buffer->cursor_row);
  if (buffer->cursor_col < 0)
    buffer->cursor_col = 0;
  if (buffer->cursor_col > len)
    buffer->cursor_col = len;
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

  if (buffer == NULL)
    return -1;

  vi_buffer_clamp_cursor(buffer);
  line = &buffer->lines[buffer->cursor_row];

  if (buffer->cursor_col > 0) {
    for (i = buffer->cursor_col; i <= line->len; i++) {
      line->data[i - 1] = line->data[i];
    }
    line->len--;
    buffer->cursor_col--;
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

void vi_buffer_move_left(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  if (buffer->cursor_col > 0) {
    buffer->cursor_col--;
  } else if (buffer->cursor_row > 0) {
    buffer->cursor_row--;
    buffer->cursor_col = vi_buffer_line_length(buffer, buffer->cursor_row);
  }
}

void vi_buffer_move_right(struct vi_buffer *buffer)
{
  int len;

  if (buffer == NULL)
    return;

  len = vi_buffer_line_length(buffer, buffer->cursor_row);
  if (buffer->cursor_col < len) {
    buffer->cursor_col++;
  } else if (buffer->cursor_row + 1 < buffer->line_count) {
    buffer->cursor_row++;
    buffer->cursor_col = 0;
  }
}

void vi_buffer_move_up(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  if (buffer->cursor_row > 0)
    buffer->cursor_row--;
  vi_buffer_clamp_cursor(buffer);
}

void vi_buffer_move_down(struct vi_buffer *buffer)
{
  if (buffer == NULL)
    return;

  if (buffer->cursor_row + 1 < buffer->line_count)
    buffer->cursor_row++;
  vi_buffer_clamp_cursor(buffer);
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
