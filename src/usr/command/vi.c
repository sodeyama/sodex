#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <vi.h>
#include <winsize.h>

#define VI_IO_CHUNK 128
#define VI_PATH_SIZE 128
#define VI_HISTORY_LIMIT 32

enum vi_pending {
  VI_PENDING_NONE = 0,
  VI_PENDING_DELETE = 1,
  VI_PENDING_G = 2,
  VI_PENDING_Z = 3
};

struct vi_snapshot {
  char *text;
  int len;
  int cursor_row;
  int cursor_col;
};

struct vi_editor {
  struct vi_buffer buffer;
  enum vi_mode mode;
  enum vi_pending pending;
  int row_offset;
  int should_exit;
  int termios_active;
  int search_direction;
  int last_search_direction;
  int change_active;
  struct termios saved_termios;
  char path[VI_PATH_SIZE];
  char command[VI_MAX_COMMAND];
  char command_prefix;
  char status[VI_STATUS_SIZE];
  char last_search[VI_MAX_COMMAND];
  struct vi_visual_state visual;
  struct vi_snapshot undo[VI_HISTORY_LIMIT];
  int undo_count;
  struct vi_snapshot redo[VI_HISTORY_LIMIT];
  int redo_count;
  struct vi_snapshot pending_snapshot;
};

static void vi_set_status(struct vi_editor *editor, const char *status);
static int vi_current_rows(void);
static int vi_current_cols(void);
static void vi_adjust_view(struct vi_editor *editor, int rows);
static int vi_load_file(struct vi_editor *editor);
static int vi_save_file(struct vi_editor *editor);
static void vi_clear_command(struct vi_editor *editor);
static void vi_clear_pending(struct vi_editor *editor);
static int vi_mode_is_visual(enum vi_mode mode);
static void vi_clear_visual(struct vi_editor *editor);
static void vi_sync_visual(struct vi_editor *editor);
static void vi_enter_command_mode(struct vi_editor *editor);
static void vi_enter_search_mode(struct vi_editor *editor, int direction);
static int vi_begin_insert_change(struct vi_editor *editor);
static void vi_enter_insert_mode(struct vi_editor *editor);
static void vi_enter_insert_after(struct vi_editor *editor);
static void vi_normal_move_left(struct vi_editor *editor);
static void vi_normal_move_right(struct vi_editor *editor);
static void vi_handle_arrow(struct vi_editor *editor, char code);
static void vi_handle_pending_delete(struct vi_editor *editor, char ch);
static void vi_handle_pending_g(struct vi_editor *editor, char ch);
static void vi_handle_pending_z(struct vi_editor *editor, char ch);
static void vi_handle_normal(struct vi_editor *editor, char ch);
static void vi_handle_insert(struct vi_editor *editor, char ch);
static void vi_handle_command(struct vi_editor *editor, char ch);
static void vi_handle_search(struct vi_editor *editor, char ch);
static void vi_process_input(struct vi_editor *editor, const char *buf, int len);
static void vi_snapshot_reset(struct vi_snapshot *snapshot);
static int vi_snapshot_capture(struct vi_snapshot *snapshot,
                               const struct vi_buffer *buffer);
static void vi_snapshot_copy(struct vi_snapshot *dst,
                             const struct vi_snapshot *src);
static int vi_snapshot_clone(struct vi_snapshot *dst,
                             const struct vi_snapshot *src);
static int vi_snapshot_restore_to_buffer(const struct vi_snapshot *snapshot,
                                         struct vi_buffer *buffer);
static void vi_snapshot_stack_clear(struct vi_snapshot *stack, int *count);
static int vi_snapshot_stack_push(struct vi_snapshot *stack, int *count,
                                  const struct vi_snapshot *snapshot);
static int vi_snapshot_stack_pop(struct vi_snapshot *stack, int *count,
                                 struct vi_snapshot *snapshot);
static int vi_bytes_equal(const char *left, const char *right, int len);
static int vi_snapshot_matches_buffer(const struct vi_snapshot *snapshot,
                                      const struct vi_buffer *buffer);
static int vi_begin_change(struct vi_editor *editor);
static void vi_finish_change(struct vi_editor *editor);
static void vi_cancel_change(struct vi_editor *editor);
static int vi_undo(struct vi_editor *editor);
static int vi_redo(struct vi_editor *editor);
static int vi_perform_search(struct vi_editor *editor, const char *needle,
                             int direction, int remember);
static int vi_repeat_search(struct vi_editor *editor, int reverse);
static void vi_enter_visual(struct vi_editor *editor, int linewise);
static void vi_visual_bounds(const struct vi_editor *editor,
                             int *start_row, int *start_col,
                             int *end_row, int *end_col);
static int vi_delete_visual(struct vi_editor *editor);

static void vi_set_status(struct vi_editor *editor, const char *status)
{
  int len;

  if (editor == NULL)
    return;

  memset(editor->status, 0, sizeof(editor->status));
  if (status == NULL)
    return;

  len = strlen(status);
  if (len >= VI_STATUS_SIZE)
    len = VI_STATUS_SIZE - 1;
  memcpy(editor->status, status, len);
}

static int vi_current_rows(void)
{
  struct winsize winsize;

  if (get_winsize(0, &winsize) == 0 && winsize.rows > 0)
    return winsize.rows;
  return 25;
}

static int vi_current_cols(void)
{
  struct winsize winsize;

  if (get_winsize(0, &winsize) == 0 && winsize.cols > 0)
    return winsize.cols;
  return 80;
}

static void vi_adjust_view(struct vi_editor *editor, int rows)
{
  int visible_rows;

  visible_rows = rows - 1;
  if (visible_rows < 1)
    visible_rows = 1;

  if (editor->buffer.cursor_row < editor->row_offset)
    editor->row_offset = editor->buffer.cursor_row;
  if (editor->buffer.cursor_row >= editor->row_offset + visible_rows)
    editor->row_offset = editor->buffer.cursor_row - visible_rows + 1;
  if (editor->row_offset < 0)
    editor->row_offset = 0;
}

static int vi_load_file(struct vi_editor *editor)
{
  int fd;
  char buf[VI_IO_CHUNK];
  int len;
  int i;

  fd = open(editor->path, O_RDONLY, 0);
  if (fd < 0) {
    vi_set_status(editor, "new file");
    return 0;
  }

  if (vi_buffer_load(&editor->buffer, "", 0) < 0) {
    close(fd);
    return -1;
  }

  while (TRUE) {
    len = read(fd, buf, sizeof(buf));
    if (len < 0) {
      close(fd);
      return -1;
    }
    if (len == 0)
      break;
    for (i = 0; i < len; i++) {
      if (buf[i] == '\r')
        continue;
      if (buf[i] == '\n') {
        if (vi_buffer_insert_newline(&editor->buffer) < 0) {
          close(fd);
          return -1;
        }
      } else if (vi_buffer_insert_char(&editor->buffer, buf[i]) < 0) {
        close(fd);
        return -1;
      }
    }
  }

  close(fd);
  editor->buffer.cursor_row = 0;
  editor->buffer.cursor_col = 0;
  vi_buffer_clear_dirty(&editor->buffer);
  vi_set_status(editor, "opened");
  return 0;
}

static int vi_save_file(struct vi_editor *editor)
{
  int fd;
  int i;

  fd = open(editor->path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0) {
    vi_set_status(editor, "write failed");
    return -1;
  }

  for (i = 0; i < editor->buffer.line_count; i++) {
    const char *line = vi_buffer_line_data(&editor->buffer, i);
    int len = vi_buffer_line_length(&editor->buffer, i);

    if (len > 0 && write(fd, line, (size_t)len) != len) {
      close(fd);
      vi_set_status(editor, "write failed");
      return -1;
    }
    if (i + 1 < editor->buffer.line_count) {
      if (write(fd, "\n", 1) != 1) {
        close(fd);
        vi_set_status(editor, "write failed");
        return -1;
      }
    }
  }

  close(fd);
  vi_buffer_clear_dirty(&editor->buffer);
  vi_set_status(editor, "written");
  return 0;
}

static void vi_clear_command(struct vi_editor *editor)
{
  memset(editor->command, 0, sizeof(editor->command));
}

static void vi_clear_pending(struct vi_editor *editor)
{
  editor->pending = VI_PENDING_NONE;
}

static int vi_mode_is_visual(enum vi_mode mode)
{
  return mode == VI_MODE_VISUAL || mode == VI_MODE_VISUAL_LINE;
}

static void vi_clear_visual(struct vi_editor *editor)
{
  memset(&editor->visual, 0, sizeof(editor->visual));
  if (vi_mode_is_visual(editor->mode))
    editor->mode = VI_MODE_NORMAL;
}

static void vi_sync_visual(struct vi_editor *editor)
{
  if (editor->visual.active == 0)
    return;

  editor->visual.end_row = editor->buffer.cursor_row;
  if (editor->visual.linewise != 0) {
    editor->visual.start_col = 0;
    editor->visual.end_col =
      vi_buffer_line_length(&editor->buffer, editor->buffer.cursor_row);
  } else {
    editor->visual.end_col =
      vi_buffer_next_char_col(&editor->buffer,
                              editor->buffer.cursor_row,
                              editor->buffer.cursor_col);
  }
}

static void vi_enter_command_mode(struct vi_editor *editor)
{
  vi_clear_visual(editor);
  editor->mode = VI_MODE_COMMAND;
  editor->command_prefix = ':';
  vi_clear_pending(editor);
  vi_clear_command(editor);
}

static void vi_enter_search_mode(struct vi_editor *editor, int direction)
{
  vi_clear_visual(editor);
  editor->mode = VI_MODE_SEARCH;
  editor->command_prefix = (direction < 0) ? '?' : '/';
  editor->search_direction = direction;
  vi_clear_pending(editor);
  vi_clear_command(editor);
  vi_set_status(editor, "search");
}

static int vi_begin_insert_change(struct vi_editor *editor)
{
  if (vi_begin_change(editor) < 0)
    return -1;
  return 0;
}

static void vi_enter_insert_mode(struct vi_editor *editor)
{
  editor->mode = VI_MODE_INSERT;
  editor->command_prefix = '\0';
  vi_clear_pending(editor);
  vi_set_status(editor, "insert");
}

static void vi_enter_insert_after(struct vi_editor *editor)
{
  int len;

  len = vi_buffer_line_length(&editor->buffer, editor->buffer.cursor_row);
  if (editor->buffer.cursor_col < len)
    vi_buffer_move_right(&editor->buffer);
  vi_enter_insert_mode(editor);
}

static void vi_normal_move_left(struct vi_editor *editor)
{
  int row;
  int col;

  row = editor->buffer.cursor_row;
  col = editor->buffer.cursor_col;
  vi_buffer_move_left(&editor->buffer);
  if (editor->buffer.cursor_row != row) {
    editor->buffer.cursor_row = row;
    editor->buffer.cursor_col = col;
  }
}

static void vi_normal_move_right(struct vi_editor *editor)
{
  int row;
  int col;
  int len;

  row = editor->buffer.cursor_row;
  col = editor->buffer.cursor_col;
  len = vi_buffer_line_length(&editor->buffer, row);
  vi_buffer_move_right(&editor->buffer);
  if (editor->buffer.cursor_row != row ||
      editor->buffer.cursor_col >= len) {
    editor->buffer.cursor_row = row;
    editor->buffer.cursor_col = col;
  }
}

static void vi_handle_arrow(struct vi_editor *editor, char code)
{
  if (editor->mode == VI_MODE_NORMAL)
    vi_clear_pending(editor);

  switch (code) {
  case 'A':
    vi_buffer_move_up(&editor->buffer);
    break;
  case 'B':
    vi_buffer_move_down(&editor->buffer);
    break;
  case 'C':
    if (editor->mode == VI_MODE_NORMAL || vi_mode_is_visual(editor->mode))
      vi_normal_move_right(editor);
    else
      vi_buffer_move_right(&editor->buffer);
    break;
  case 'D':
    if (editor->mode == VI_MODE_NORMAL || vi_mode_is_visual(editor->mode))
      vi_normal_move_left(editor);
    else
      vi_buffer_move_left(&editor->buffer);
    break;
  default:
    break;
  }

  vi_sync_visual(editor);
}

static void vi_snapshot_reset(struct vi_snapshot *snapshot)
{
  if (snapshot == NULL)
    return;
  if (snapshot->text != NULL)
    free(snapshot->text);
  memset(snapshot, 0, sizeof(*snapshot));
}

static int vi_snapshot_capture(struct vi_snapshot *snapshot,
                               const struct vi_buffer *buffer)
{
  char *data = NULL;
  int len = 0;

  if (snapshot == NULL || buffer == NULL)
    return -1;

  if (vi_buffer_serialize(buffer, &data, &len) < 0)
    return -1;
  vi_snapshot_reset(snapshot);
  snapshot->text = data;
  snapshot->len = len;
  snapshot->cursor_row = buffer->cursor_row;
  snapshot->cursor_col = buffer->cursor_col;
  return 0;
}

static void vi_snapshot_copy(struct vi_snapshot *dst,
                             const struct vi_snapshot *src)
{
  if (dst == NULL || src == NULL)
    return;

  dst->text = src->text;
  dst->len = src->len;
  dst->cursor_row = src->cursor_row;
  dst->cursor_col = src->cursor_col;
}

static int vi_snapshot_clone(struct vi_snapshot *dst,
                             const struct vi_snapshot *src)
{
  if (dst == NULL || src == NULL)
    return -1;

  vi_snapshot_reset(dst);
  if (src->text != NULL) {
    dst->text = (char *)malloc((size_t)src->len + 1);
    if (dst->text == NULL)
      return -1;
    memcpy(dst->text, src->text, (size_t)src->len + 1);
  }
  dst->len = src->len;
  dst->cursor_row = src->cursor_row;
  dst->cursor_col = src->cursor_col;
  return 0;
}

static int vi_snapshot_restore_to_buffer(const struct vi_snapshot *snapshot,
                                         struct vi_buffer *buffer)
{
  int row;
  int col;
  int len;

  if (snapshot == NULL || buffer == NULL)
    return -1;
  if (vi_buffer_load(buffer,
                     snapshot->text != NULL ? snapshot->text : "",
                     snapshot->len) < 0) {
    return -1;
  }

  row = snapshot->cursor_row;
  if (row < 0)
    row = 0;
  if (row >= buffer->line_count)
    row = buffer->line_count - 1;
  col = snapshot->cursor_col;
  if (col < 0)
    col = 0;
  len = vi_buffer_line_length(buffer, row);
  if (col > len)
    col = len;
  buffer->cursor_row = row;
  buffer->cursor_col = col;
  buffer->dirty = 1;
  return 0;
}

static void vi_snapshot_stack_clear(struct vi_snapshot *stack, int *count)
{
  int i;

  if (stack == NULL || count == NULL)
    return;

  for (i = 0; i < *count; i++)
    vi_snapshot_reset(&stack[i]);
  *count = 0;
}

static int vi_snapshot_stack_push(struct vi_snapshot *stack, int *count,
                                  const struct vi_snapshot *snapshot)
{
  int i;

  if (stack == NULL || count == NULL || snapshot == NULL)
    return -1;

  if (*count >= VI_HISTORY_LIMIT) {
    vi_snapshot_reset(&stack[0]);
    for (i = 1; i < VI_HISTORY_LIMIT; i++)
      vi_snapshot_copy(&stack[i - 1], &stack[i]);
    memset(&stack[VI_HISTORY_LIMIT - 1], 0, sizeof(struct vi_snapshot));
    *count = VI_HISTORY_LIMIT - 1;
  }

  if (vi_snapshot_clone(&stack[*count], snapshot) < 0)
    return -1;
  (*count)++;
  return 0;
}

static int vi_snapshot_stack_pop(struct vi_snapshot *stack, int *count,
                                 struct vi_snapshot *snapshot)
{
  if (stack == NULL || count == NULL || snapshot == NULL || *count <= 0)
    return -1;

  vi_snapshot_copy(snapshot, &stack[*count - 1]);
  memset(&stack[*count - 1], 0, sizeof(struct vi_snapshot));
  (*count)--;
  return 0;
}

static int vi_bytes_equal(const char *left, const char *right, int len)
{
  int i;

  if (left == NULL || right == NULL || len < 0)
    return 0;
  for (i = 0; i < len; i++) {
    if (left[i] != right[i])
      return 0;
  }
  return 1;
}

static int vi_snapshot_matches_buffer(const struct vi_snapshot *snapshot,
                                      const struct vi_buffer *buffer)
{
  char *text = NULL;
  int len = 0;
  int same = 0;

  if (snapshot == NULL || buffer == NULL)
    return 0;
  if (vi_buffer_serialize(buffer, &text, &len) < 0)
    return 0;

  same = (snapshot->len == len &&
          snapshot->cursor_row == buffer->cursor_row &&
          snapshot->cursor_col == buffer->cursor_col &&
          ((snapshot->text == NULL && text == NULL) ||
           (snapshot->text != NULL && text != NULL &&
            vi_bytes_equal(snapshot->text, text, len) != 0)));
  if (text != NULL)
    free(text);
  return same;
}

static int vi_begin_change(struct vi_editor *editor)
{
  if (editor == NULL)
    return -1;
  if (editor->change_active != 0)
    return 0;
  if (vi_snapshot_capture(&editor->pending_snapshot, &editor->buffer) < 0) {
    vi_set_status(editor, "history failed");
    return -1;
  }
  editor->change_active = 1;
  return 0;
}

static void vi_finish_change(struct vi_editor *editor)
{
  if (editor == NULL || editor->change_active == 0)
    return;

  if (vi_snapshot_matches_buffer(&editor->pending_snapshot,
                                 &editor->buffer) == 0) {
    if (vi_snapshot_stack_push(editor->undo, &editor->undo_count,
                               &editor->pending_snapshot) == 0) {
      vi_snapshot_stack_clear(editor->redo, &editor->redo_count);
    } else {
      vi_set_status(editor, "history full");
    }
  }
  vi_snapshot_reset(&editor->pending_snapshot);
  editor->change_active = 0;
}

static void vi_cancel_change(struct vi_editor *editor)
{
  if (editor == NULL)
    return;
  vi_snapshot_reset(&editor->pending_snapshot);
  editor->change_active = 0;
}

static int vi_undo(struct vi_editor *editor)
{
  struct vi_snapshot previous;
  struct vi_snapshot current;

  memset(&previous, 0, sizeof(previous));
  memset(&current, 0, sizeof(current));
  if (editor == NULL || editor->undo_count <= 0)
    return -1;
  if (vi_snapshot_capture(&current, &editor->buffer) < 0)
    return -1;
  if (vi_snapshot_stack_pop(editor->undo, &editor->undo_count, &previous) < 0) {
    vi_snapshot_reset(&current);
    return -1;
  }
  if (vi_snapshot_stack_push(editor->redo, &editor->redo_count, &current) < 0) {
    vi_snapshot_reset(&current);
    vi_snapshot_reset(&previous);
    return -1;
  }
  vi_snapshot_reset(&current);
  if (vi_snapshot_restore_to_buffer(&previous, &editor->buffer) < 0) {
    vi_snapshot_reset(&previous);
    return -1;
  }
  vi_snapshot_reset(&previous);
  vi_clear_visual(editor);
  vi_clear_pending(editor);
  editor->mode = VI_MODE_NORMAL;
  vi_set_status(editor, "undo");
  return 0;
}

static int vi_redo(struct vi_editor *editor)
{
  struct vi_snapshot next;
  struct vi_snapshot current;

  memset(&next, 0, sizeof(next));
  memset(&current, 0, sizeof(current));
  if (editor == NULL || editor->redo_count <= 0)
    return -1;
  if (vi_snapshot_capture(&current, &editor->buffer) < 0)
    return -1;
  if (vi_snapshot_stack_pop(editor->redo, &editor->redo_count, &next) < 0) {
    vi_snapshot_reset(&current);
    return -1;
  }
  if (vi_snapshot_stack_push(editor->undo, &editor->undo_count, &current) < 0) {
    vi_snapshot_reset(&current);
    vi_snapshot_reset(&next);
    return -1;
  }
  vi_snapshot_reset(&current);
  if (vi_snapshot_restore_to_buffer(&next, &editor->buffer) < 0) {
    vi_snapshot_reset(&next);
    return -1;
  }
  vi_snapshot_reset(&next);
  vi_clear_visual(editor);
  vi_clear_pending(editor);
  editor->mode = VI_MODE_NORMAL;
  vi_set_status(editor, "redo");
  return 0;
}

static int vi_perform_search(struct vi_editor *editor, const char *needle,
                             int direction, int remember)
{
  int row = 0;
  int col = 0;
  int found = -1;
  int last_row;
  int last_col;

  if (editor == NULL || needle == NULL || needle[0] == '\0')
    return -1;

  last_row = editor->buffer.line_count - 1;
  last_col = vi_buffer_line_length(&editor->buffer, last_row);

  if (direction >= 0) {
    row = editor->buffer.cursor_row;
    col = vi_buffer_next_char_col(&editor->buffer,
                                  editor->buffer.cursor_row,
                                  editor->buffer.cursor_col);
    found = vi_buffer_find_forward(&editor->buffer, needle, row, col,
                                   &row, &col);
    if (found < 0) {
      found = vi_buffer_find_forward(&editor->buffer, needle, 0, 0,
                                     &row, &col);
    }
  } else {
    row = editor->buffer.cursor_row;
    col = vi_buffer_prev_char_col(&editor->buffer,
                                  editor->buffer.cursor_row,
                                  editor->buffer.cursor_col);
    found = vi_buffer_find_backward(&editor->buffer, needle, row, col,
                                    &row, &col);
    if (found < 0) {
      found = vi_buffer_find_backward(&editor->buffer, needle,
                                      last_row, last_col,
                                      &row, &col);
    }
  }

  if (found < 0) {
    vi_set_status(editor, "pattern not found");
    return -1;
  }

  editor->buffer.cursor_row = row;
  editor->buffer.cursor_col = col;
  vi_clear_pending(editor);
  vi_clear_visual(editor);
  editor->mode = VI_MODE_NORMAL;
  vi_set_status(editor, "match");
  if (remember != 0) {
    memset(editor->last_search, 0, sizeof(editor->last_search));
    strncpy(editor->last_search, needle, sizeof(editor->last_search) - 1);
    editor->last_search_direction = direction >= 0 ? 1 : -1;
  }
  return 0;
}

static int vi_repeat_search(struct vi_editor *editor, int reverse)
{
  int direction;

  if (editor == NULL || editor->last_search[0] == '\0')
    return -1;

  direction = editor->last_search_direction;
  if (reverse != 0)
    direction = -direction;
  return vi_perform_search(editor, editor->last_search, direction, 0);
}

static void vi_enter_visual(struct vi_editor *editor, int linewise)
{
  vi_clear_pending(editor);
  editor->visual.active = 1;
  editor->visual.linewise = linewise != 0;
  editor->visual.start_row = editor->buffer.cursor_row;
  editor->visual.start_col = (linewise != 0) ? 0 : editor->buffer.cursor_col;
  editor->mode = (linewise != 0) ? VI_MODE_VISUAL_LINE : VI_MODE_VISUAL;
  vi_sync_visual(editor);
  vi_set_status(editor, (linewise != 0) ? "visual line" : "visual");
}

static void vi_visual_bounds(const struct vi_editor *editor,
                             int *start_row, int *start_col,
                             int *end_row, int *end_col)
{
  int row0;
  int col0;
  int row1;
  int col1;

  row0 = editor->visual.start_row;
  col0 = editor->visual.start_col;
  row1 = editor->visual.end_row;
  col1 = editor->visual.end_col;
  if (row0 > row1 || (row0 == row1 && col0 > col1)) {
    int tmp_row = row0;
    int tmp_col = col0;
    row0 = row1;
    col0 = col1;
    row1 = tmp_row;
    col1 = tmp_col;
  }
  if (start_row != NULL)
    *start_row = row0;
  if (start_col != NULL)
    *start_col = col0;
  if (end_row != NULL)
    *end_row = row1;
  if (end_col != NULL)
    *end_col = col1;
}

static int vi_delete_visual(struct vi_editor *editor)
{
  int start_row;
  int start_col;
  int end_row;
  int end_col;
  int rc;

  if (editor == NULL || editor->visual.active == 0)
    return -1;
  if (vi_begin_change(editor) < 0)
    return -1;

  vi_visual_bounds(editor, &start_row, &start_col, &end_row, &end_col);
  if (editor->visual.linewise != 0) {
    rc = vi_buffer_delete_lines(&editor->buffer, start_row, end_row);
  } else {
    rc = vi_buffer_delete_range(&editor->buffer,
                                start_row, start_col,
                                end_row, end_col);
  }

  if (rc < 0) {
    vi_cancel_change(editor);
    return -1;
  }

  vi_clear_visual(editor);
  vi_finish_change(editor);
  vi_set_status(editor, "normal");
  return 0;
}

static void vi_handle_pending_delete(struct vi_editor *editor, char ch)
{
  int rc = -1;

  vi_clear_pending(editor);
  if (vi_begin_change(editor) < 0)
    return;

  switch (ch) {
  case 'd':
    rc = vi_buffer_delete_line(&editor->buffer);
    break;
  case 'w':
    rc = vi_buffer_delete_word_forward(&editor->buffer);
    break;
  case 'b':
    rc = vi_buffer_delete_word_backward(&editor->buffer);
    break;
  case 'e':
    rc = vi_buffer_delete_word_end(&editor->buffer);
    break;
  case '0':
    rc = vi_buffer_delete_to_line_start(&editor->buffer);
    break;
  case '$':
    rc = vi_buffer_delete_to_line_end(&editor->buffer);
    break;
  default:
    vi_cancel_change(editor);
    vi_set_status(editor, "unknown command");
    return;
  }

  if (rc < 0) {
    vi_cancel_change(editor);
    return;
  }
  vi_finish_change(editor);
}

static void vi_handle_pending_g(struct vi_editor *editor, char ch)
{
  vi_clear_pending(editor);

  switch (ch) {
  case 'g':
    vi_buffer_move_first_line(&editor->buffer);
    break;
  default:
    vi_set_status(editor, "unknown command");
    break;
  }

  vi_sync_visual(editor);
}

static void vi_handle_pending_z(struct vi_editor *editor, char ch)
{
  vi_clear_pending(editor);

  if (ch != 'Z') {
    vi_set_status(editor, "unknown command");
    return;
  }

  if (vi_save_file(editor) == 0)
    editor->should_exit = 1;
}

static void vi_handle_normal(struct vi_editor *editor, char ch)
{
  int visual_mode;

  visual_mode = vi_mode_is_visual(editor->mode);
  if (ch == '\x1b') {
    if (visual_mode != 0)
      vi_clear_visual(editor);
    vi_clear_pending(editor);
    vi_set_status(editor, "normal");
    return;
  }

  if (visual_mode == 0 && editor->pending == VI_PENDING_DELETE) {
    vi_handle_pending_delete(editor, ch);
    return;
  }
  if (visual_mode == 0 && editor->pending == VI_PENDING_G) {
    vi_handle_pending_g(editor, ch);
    return;
  }
  if (visual_mode == 0 && editor->pending == VI_PENDING_Z) {
    vi_handle_pending_z(editor, ch);
    return;
  }

  if (visual_mode != 0) {
    if (ch == 'd') {
      vi_delete_visual(editor);
      return;
    }
    if (ch == 'v') {
      if (editor->mode == VI_MODE_VISUAL)
        vi_clear_visual(editor);
      else
        vi_enter_visual(editor, 0);
      return;
    }
    if (ch == 'V') {
      if (editor->mode == VI_MODE_VISUAL_LINE)
        vi_clear_visual(editor);
      else
        vi_enter_visual(editor, 1);
      return;
    }
  }

  switch (ch) {
  case 'h':
    vi_normal_move_left(editor);
    break;
  case 'j':
    vi_buffer_move_down(&editor->buffer);
    break;
  case 'k':
    vi_buffer_move_up(&editor->buffer);
    break;
  case 'l':
    vi_normal_move_right(editor);
    break;
  case '0':
    vi_buffer_move_line_start(&editor->buffer);
    break;
  case '^':
    vi_buffer_move_line_first_nonblank(&editor->buffer);
    break;
  case '$':
    vi_buffer_move_line_last_char(&editor->buffer);
    break;
  case 'w':
    vi_buffer_move_word_forward(&editor->buffer);
    break;
  case 'b':
    vi_buffer_move_word_backward(&editor->buffer);
    break;
  case 'e':
    vi_buffer_move_word_end(&editor->buffer);
    break;
  case 'g':
    if (visual_mode == 0)
      editor->pending = VI_PENDING_G;
    break;
  case 'G':
    vi_buffer_move_last_line(&editor->buffer);
    break;
  case 'Z':
    if (visual_mode == 0)
      editor->pending = VI_PENDING_Z;
    break;
  case 'x':
    if (visual_mode == 0 && vi_begin_change(editor) == 0) {
      if (vi_buffer_delete_char(&editor->buffer) == 0)
        vi_finish_change(editor);
      else
        vi_cancel_change(editor);
    }
    break;
  case 'X':
    if (visual_mode == 0 && vi_begin_change(editor) == 0) {
      if (vi_buffer_delete_prev_char(&editor->buffer) == 0)
        vi_finish_change(editor);
      else
        vi_cancel_change(editor);
    }
    break;
  case 'd':
    if (visual_mode == 0)
      editor->pending = VI_PENDING_DELETE;
    break;
  case 'D':
    if (visual_mode == 0 && vi_begin_change(editor) == 0) {
      if (vi_buffer_delete_to_line_end(&editor->buffer) == 0)
        vi_finish_change(editor);
      else
        vi_cancel_change(editor);
    }
    break;
  case 'i':
    if (visual_mode == 0 && vi_begin_insert_change(editor) == 0)
      vi_enter_insert_mode(editor);
    break;
  case 'a':
    if (visual_mode == 0 && vi_begin_insert_change(editor) == 0)
      vi_enter_insert_after(editor);
    break;
  case 'A':
    if (visual_mode == 0 && vi_begin_insert_change(editor) == 0) {
      vi_buffer_move_line_end(&editor->buffer);
      vi_enter_insert_mode(editor);
    }
    break;
  case 'I':
    if (visual_mode == 0 && vi_begin_insert_change(editor) == 0) {
      vi_buffer_move_line_first_nonblank(&editor->buffer);
      vi_enter_insert_mode(editor);
    }
    break;
  case 'o':
    if (visual_mode == 0 && vi_begin_change(editor) == 0) {
      if (vi_buffer_open_line_below(&editor->buffer) == 0)
        vi_enter_insert_mode(editor);
      else
        vi_cancel_change(editor);
    }
    break;
  case 'O':
    if (visual_mode == 0 && vi_begin_change(editor) == 0) {
      if (vi_buffer_open_line_above(&editor->buffer) == 0)
        vi_enter_insert_mode(editor);
      else
        vi_cancel_change(editor);
    }
    break;
  case ':':
    if (visual_mode == 0)
      vi_enter_command_mode(editor);
    break;
  case '/':
    if (visual_mode == 0)
      vi_enter_search_mode(editor, 1);
    break;
  case '?':
    if (visual_mode == 0)
      vi_enter_search_mode(editor, -1);
    break;
  case 'n':
    if (visual_mode == 0)
      vi_repeat_search(editor, 0);
    break;
  case 'N':
    if (visual_mode == 0)
      vi_repeat_search(editor, 1);
    break;
  case 'u':
    if (visual_mode == 0)
      vi_undo(editor);
    break;
  case 0x12:
    if (visual_mode == 0)
      vi_redo(editor);
    break;
  case 'v':
    if (visual_mode == 0)
      vi_enter_visual(editor, 0);
    break;
  case 'V':
    if (visual_mode == 0)
      vi_enter_visual(editor, 1);
    break;
  default:
    break;
  }

  vi_sync_visual(editor);
}

static void vi_handle_insert(struct vi_editor *editor, char ch)
{
  if (ch == '\x1b') {
    editor->mode = VI_MODE_NORMAL;
    vi_clear_pending(editor);
    vi_finish_change(editor);
    vi_set_status(editor, "normal");
    return;
  }
  if (ch == '\b' || ch == 0x7f) {
    vi_buffer_backspace(&editor->buffer);
    return;
  }
  if (ch == '\r' || ch == '\n') {
    vi_buffer_insert_newline(&editor->buffer);
    return;
  }
  if ((unsigned char)ch >= 0x20 && ch != 0x7f)
    vi_buffer_insert_char(&editor->buffer, ch);
}

static void vi_handle_command(struct vi_editor *editor, char ch)
{
  int len;
  enum vi_command_kind command;

  if (ch == '\x1b') {
    editor->mode = VI_MODE_NORMAL;
    editor->command_prefix = '\0';
    vi_clear_pending(editor);
    vi_clear_command(editor);
    return;
  }

  if (ch == '\b' || ch == 0x7f) {
    len = strlen(editor->command);
    if (len > 0)
      editor->command[len - 1] = '\0';
    else {
      editor->mode = VI_MODE_NORMAL;
      editor->command_prefix = '\0';
    }
    return;
  }

  if (ch == '\r' || ch == '\n') {
    command = vi_parse_command(editor->command);
    if (command == VI_COMMAND_WRITE) {
      if (vi_save_file(editor) == 0)
        editor->mode = VI_MODE_NORMAL;
    } else if (command == VI_COMMAND_QUIT) {
      if (editor->buffer.dirty != 0) {
        vi_set_status(editor, "no write since last change");
        editor->mode = VI_MODE_NORMAL;
      } else {
        editor->should_exit = 1;
      }
    } else if (command == VI_COMMAND_WRITE_QUIT) {
      if (vi_save_file(editor) == 0)
        editor->should_exit = 1;
    } else if (command == VI_COMMAND_NONE) {
      editor->mode = VI_MODE_NORMAL;
    } else {
      vi_set_status(editor, "not an editor command");
      editor->mode = VI_MODE_NORMAL;
    }
    if (editor->should_exit == 0) {
      editor->command_prefix = '\0';
      vi_clear_command(editor);
    }
    return;
  }

  len = strlen(editor->command);
  if (ch >= 0x20 && ch <= 0x7e && len + 1 < VI_MAX_COMMAND) {
    editor->command[len] = ch;
    editor->command[len + 1] = '\0';
  }
}

static void vi_handle_search(struct vi_editor *editor, char ch)
{
  int len;
  const char *needle;

  if (ch == '\x1b') {
    editor->mode = VI_MODE_NORMAL;
    editor->command_prefix = '\0';
    vi_clear_command(editor);
    vi_set_status(editor, "normal");
    return;
  }

  if (ch == '\b' || ch == 0x7f) {
    len = strlen(editor->command);
    if (len > 0)
      editor->command[len - 1] = '\0';
    else {
      editor->mode = VI_MODE_NORMAL;
      editor->command_prefix = '\0';
    }
    return;
  }

  if (ch == '\r' || ch == '\n') {
    needle = editor->command;
    if (needle[0] == '\0')
      needle = editor->last_search;
    if (needle[0] != '\0')
      vi_perform_search(editor, needle, editor->search_direction, 1);
    else
      vi_set_status(editor, "pattern not found");
    editor->mode = VI_MODE_NORMAL;
    editor->command_prefix = '\0';
    vi_clear_command(editor);
    return;
  }

  len = strlen(editor->command);
  if (ch >= 0x20 && ch <= 0x7e && len + 1 < VI_MAX_COMMAND) {
    editor->command[len] = ch;
    editor->command[len + 1] = '\0';
  }
}

static void vi_process_input(struct vi_editor *editor, const char *buf, int len)
{
  int i;

  for (i = 0; i < len; i++) {
    char ch = buf[i];

    if (ch == '\x1b' && i + 2 < len && buf[i + 1] == '[') {
      vi_handle_arrow(editor, buf[i + 2]);
      i += 2;
      continue;
    }

    if (editor->mode == VI_MODE_COMMAND)
      vi_handle_command(editor, ch);
    else if (editor->mode == VI_MODE_SEARCH)
      vi_handle_search(editor, ch);
    else if (editor->mode == VI_MODE_INSERT)
      vi_handle_insert(editor, ch);
    else
      vi_handle_normal(editor, ch);

    if (editor->should_exit != 0)
      break;
  }
}

int main(int argc, char **argv)
{
  struct vi_editor editor;
  struct termios raw_termios;
  char input[VI_IO_CHUNK];
  int rows;
  int cols;
  int len;

  memset(&editor, 0, sizeof(editor));
  if (argc < 2) {
    printf("usage: vi <path>\n");
    return 1;
  }
  if (vi_buffer_init(&editor.buffer) < 0) {
    printf("vi: buffer init failed\n");
    return 1;
  }

  strncpy(editor.path, argv[1], sizeof(editor.path) - 1);
  editor.mode = VI_MODE_NORMAL;
  vi_set_status(&editor, "normal");

  if (vi_load_file(&editor) < 0) {
    printf("vi: open failed %s\n", editor.path);
    vi_buffer_free(&editor.buffer);
    return 1;
  }

  if (tcgetattr(0, &editor.saved_termios) < 0) {
    printf("vi: tcgetattr failed\n");
    vi_buffer_free(&editor.buffer);
    return 1;
  }

  raw_termios = editor.saved_termios;
  raw_termios.c_lflag &= (u_int32_t)(~(ICANON | ECHO));
  if (tcsetattr(0, TCSANOW, &raw_termios) < 0) {
    printf("vi: tcsetattr failed\n");
    vi_buffer_free(&editor.buffer);
    return 1;
  }
  editor.termios_active = 1;
  vi_screen_enter();

  while (editor.should_exit == 0) {
    rows = vi_current_rows();
    cols = vi_current_cols();
    vi_adjust_view(&editor, rows);
    vi_screen_redraw(&editor.buffer, editor.mode, editor.path, editor.status,
                     editor.command, editor.command_prefix,
                     &editor.visual,
                     editor.row_offset, rows, cols);

    len = read(0, input, sizeof(input));
    if (len <= 0)
      continue;
    vi_process_input(&editor, input, len);
  }

  if (editor.termios_active != 0)
    tcsetattr(0, TCSANOW, &editor.saved_termios);
  vi_screen_restore();
  vi_snapshot_stack_clear(editor.undo, &editor.undo_count);
  vi_snapshot_stack_clear(editor.redo, &editor.redo_count);
  vi_snapshot_reset(&editor.pending_snapshot);
  vi_buffer_free(&editor.buffer);
  return 0;
}
