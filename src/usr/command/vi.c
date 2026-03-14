#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <vi.h>
#include <winsize.h>

#define VI_IO_CHUNK 128
#define VI_PATH_SIZE 128

struct vi_editor {
  struct vi_buffer buffer;
  enum vi_mode mode;
  int row_offset;
  int should_exit;
  int termios_active;
  struct termios saved_termios;
  char path[VI_PATH_SIZE];
  char command[VI_MAX_COMMAND];
  char status[VI_STATUS_SIZE];
};

static void vi_set_status(struct vi_editor *editor, const char *status);
static int vi_current_rows(void);
static int vi_current_cols(void);
static void vi_adjust_view(struct vi_editor *editor, int rows);
static int vi_load_file(struct vi_editor *editor);
static int vi_save_file(struct vi_editor *editor);
static void vi_clear_command(struct vi_editor *editor);
static void vi_enter_command_mode(struct vi_editor *editor);
static void vi_handle_arrow(struct vi_editor *editor, char code);
static void vi_handle_normal(struct vi_editor *editor, char ch);
static void vi_handle_insert(struct vi_editor *editor, char ch);
static void vi_handle_command(struct vi_editor *editor, char ch);
static void vi_process_input(struct vi_editor *editor, const char *buf, int len);

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

static void vi_enter_command_mode(struct vi_editor *editor)
{
  editor->mode = VI_MODE_COMMAND;
  vi_clear_command(editor);
}

static void vi_handle_arrow(struct vi_editor *editor, char code)
{
  switch (code) {
  case 'A':
    vi_buffer_move_up(&editor->buffer);
    break;
  case 'B':
    vi_buffer_move_down(&editor->buffer);
    break;
  case 'C':
    vi_buffer_move_right(&editor->buffer);
    break;
  case 'D':
    vi_buffer_move_left(&editor->buffer);
    break;
  default:
    break;
  }
}

static void vi_handle_normal(struct vi_editor *editor, char ch)
{
  switch (ch) {
  case 'h':
    vi_buffer_move_left(&editor->buffer);
    break;
  case 'j':
    vi_buffer_move_down(&editor->buffer);
    break;
  case 'k':
    vi_buffer_move_up(&editor->buffer);
    break;
  case 'l':
    vi_buffer_move_right(&editor->buffer);
    break;
  case 'i':
    editor->mode = VI_MODE_INSERT;
    vi_set_status(editor, "insert");
    break;
  case ':':
    vi_enter_command_mode(editor);
    break;
  default:
    break;
  }
}

static void vi_handle_insert(struct vi_editor *editor, char ch)
{
  if (ch == '\x1b') {
    editor->mode = VI_MODE_NORMAL;
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
  if (ch >= 0x20 && ch <= 0x7e)
    vi_buffer_insert_char(&editor->buffer, ch);
}

static void vi_handle_command(struct vi_editor *editor, char ch)
{
  int len;
  enum vi_command_kind command;

  if (ch == '\x1b') {
    editor->mode = VI_MODE_NORMAL;
    vi_clear_command(editor);
    return;
  }

  if (ch == '\b' || ch == 0x7f) {
    len = strlen(editor->command);
    if (len > 0)
      editor->command[len - 1] = '\0';
    else
      editor->mode = VI_MODE_NORMAL;
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
    if (editor->should_exit == 0)
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
  int exit_code = 0;

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

  while (editor.should_exit == 0) {
    rows = vi_current_rows();
    cols = vi_current_cols();
    vi_adjust_view(&editor, rows);
    vi_screen_redraw(&editor.buffer, editor.mode, editor.path, editor.status,
                     editor.command, editor.row_offset, rows, cols);

    len = read(0, input, sizeof(input));
    if (len <= 0)
      continue;
    vi_process_input(&editor, input, len);
  }

  if (editor.termios_active != 0)
    tcsetattr(0, TCSANOW, &editor.saved_termios);
  vi_screen_restore();
  vi_buffer_free(&editor.buffer);
  return exit_code;
}
