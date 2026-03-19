#include <debug.h>
#include <fs.h>
#include <malloc.h>
#include <pager.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <winsize.h>

#define PAGER_STATUS_SIZE 128
#define PAGER_KEY_UP 1001
#define PAGER_KEY_DOWN 1002

struct pager_app {
  struct pager_document document;
  const char *program_name;
  const char *display_name;
  int less_mode;
  int rows;
  int cols;
  int page_rows;
  char status[PAGER_STATUS_SIZE];
  struct termios saved_termios;
  int termios_active;
};

static void pager_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0)
    src = "";
  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static void pager_write_fd_text(int fd, const char *text)
{
  if (text == 0)
    return;
  write(fd, text, strlen(text));
}

static void pager_write_error(const char *program_name,
                              const char *message,
                              const char *detail)
{
  pager_write_fd_text(STDERR_FILENO, program_name);
  pager_write_fd_text(STDERR_FILENO, ": ");
  pager_write_fd_text(STDERR_FILENO, message);
  if (detail != 0 && detail[0] != '\0') {
    pager_write_fd_text(STDERR_FILENO, " ");
    pager_write_fd_text(STDERR_FILENO, detail);
  }
  pager_write_fd_text(STDERR_FILENO, "\n");
}

static char *pager_read_fd_all(int fd, int *out_len)
{
  char *buf;
  int cap = 512;
  int len = 0;

  if (out_len == 0)
    return 0;

  buf = (char *)malloc((size_t)cap);
  if (buf == 0)
    return 0;

  while (1) {
    int read_len;
    char *next;

    if (len >= cap - 1) {
      int next_cap = cap * 2;

      next = (char *)malloc((size_t)next_cap);
      if (next == 0) {
        free(buf);
        return 0;
      }
      memset(next, 0, (size_t)next_cap);
      memcpy(next, buf, (size_t)len);
      free(buf);
      buf = next;
      cap = next_cap;
    }

    read_len = (int)read(fd, buf + len, (size_t)(cap - len - 1));
    if (read_len <= 0)
      break;
    len += read_len;
  }

  buf[len] = '\0';
  *out_len = len;
  return buf;
}

static int pager_load_path(const char *path, char **out_data, int *out_len)
{
  char *data;
  int fd;

  if (path == 0 || out_data == 0 || out_len == 0)
    return -1;

  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  data = pager_read_fd_all(fd, out_len);
  close(fd);
  if (data == 0)
    return -1;
  *out_data = data;
  return 0;
}

static void pager_update_screen_size(struct pager_app *app)
{
  struct winsize winsize;

  app->rows = 25;
  app->cols = 80;
  if (get_winsize(STDOUT_FILENO, &winsize) == 0) {
    if (winsize.rows > 0)
      app->rows = winsize.rows;
    if (winsize.cols > 0)
      app->cols = winsize.cols;
  }
  app->page_rows = app->rows - 1;
  if (app->page_rows < 1)
    app->page_rows = 1;
}

static int pager_enter_raw(struct pager_app *app)
{
  struct termios raw_termios;

  if (tcgetattr(STDIN_FILENO, &app->saved_termios) < 0)
    return -1;
  raw_termios = app->saved_termios;
  raw_termios.c_lflag &= (u_int32_t)(~(ICANON | ECHO));
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) < 0)
    return -1;
  app->termios_active = 1;
  return 0;
}

static void pager_restore_termios(struct pager_app *app)
{
  if (app == 0 || app->termios_active == 0)
    return;
  tcsetattr(STDIN_FILENO, TCSANOW, &app->saved_termios);
  app->termios_active = 0;
}

static int pager_read_key(void)
{
  unsigned char ch;
  unsigned char seq[2];

  if (read(STDIN_FILENO, &ch, 1) != 1)
    return -1;
  if (ch != 0x1b)
    return (int)ch;
  if (read(STDIN_FILENO, &seq[0], 1) != 1)
    return 0x1b;
  if (seq[0] != '[')
    return 0x1b;
  if (read(STDIN_FILENO, &seq[1], 1) != 1)
    return 0x1b;
  if (seq[1] == 'A')
    return PAGER_KEY_UP;
  if (seq[1] == 'B')
    return PAGER_KEY_DOWN;
  return 0x1b;
}

static void pager_set_status(struct pager_app *app, const char *text)
{
  if (app == 0)
    return;
  pager_copy_text(app->status, sizeof(app->status), text);
}

static void pager_emit_render_audit(struct pager_app *app)
{
  char buf[96];
  int len;

  if (app == 0)
    return;
  len = snprintf(buf, sizeof(buf),
                 "AUDIT %s_render top=%d rows=%d percent=%d\n",
                 app->program_name,
                 app->document.top_row,
                 app->document.row_count,
                 pager_document_percent(&app->document, app->page_rows));
  if (len > 0)
    debug_write(buf, (size_t)len);
}

static void pager_write_row(struct pager_app *app, int row_index)
{
  const struct pager_row *row;
  const char *line;
  int line_len;
  int count;

  row = pager_document_row(&app->document, row_index);
  if (row == 0)
    return;
  line = vi_buffer_line_data(&app->document.buffer, row->line_index);
  line_len = vi_buffer_line_length(&app->document.buffer, row->line_index);
  if (line == 0 || line_len <= 0)
    return;
  if (row->start_col < 0 || row->start_col > line_len)
    return;
  count = row->end_col - row->start_col;
  if (count <= 0 || row->end_col > line_len)
    return;
  write(STDOUT_FILENO, line + row->start_col, (size_t)count);
}

static void pager_render(struct pager_app *app, const char *prompt_prefix)
{
  char status[PAGER_STATUS_SIZE];
  int i;

  if (app == 0)
    return;

  pager_update_screen_size(app);
  if (app->document.cols != app->cols)
    pager_document_relayout(&app->document, app->cols);
  write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7);
  for (i = 0; i < app->page_rows; i++) {
    int row_index = app->document.top_row + i;

    write(STDOUT_FILENO, "\x1b[K", 3);
    if (row_index < app->document.row_count)
      pager_write_row(app, row_index);
    if (i + 1 < app->rows)
      write(STDOUT_FILENO, "\r\n", 2);
  }

  write(STDOUT_FILENO, "\x1b[7m", 4);
  write(STDOUT_FILENO, "\x1b[K", 3);
  if (prompt_prefix != 0) {
    snprintf(status, sizeof(status), "%s%s",
             prompt_prefix, app->status);
  } else if (app->less_mode != 0) {
    snprintf(status, sizeof(status), "%s  %d%%  %s",
             app->display_name,
             pager_document_percent(&app->document, app->page_rows),
             app->status);
  } else {
    snprintf(status, sizeof(status), "--More--(%d%%) %s",
             pager_document_percent(&app->document, app->page_rows),
             app->status);
  }
  write(STDOUT_FILENO, status, strlen(status));
  write(STDOUT_FILENO, "\x1b[0m", 4);
  pager_emit_render_audit(app);
}

static int pager_prompt(struct pager_app *app,
                        const char *prefix,
                        char *buf, int cap)
{
  int len = 0;

  if (app == 0 || prefix == 0 || buf == 0 || cap <= 0)
    return -1;

  buf[0] = '\0';
  while (1) {
    int key;

    pager_set_status(app, buf);
    pager_render(app, prefix);
    key = pager_read_key();
    if (key < 0 || key == 0x1b)
      return -1;
    if (key == '\r' || key == '\n') {
      buf[len] = '\0';
      return 0;
    }
    if (key == 0x7f || key == '\b') {
      if (len > 0)
        buf[--len] = '\0';
      continue;
    }
    if (key >= 32 && key < 127 && len < cap - 1) {
      buf[len++] = (char)key;
      buf[len] = '\0';
    }
  }
}

static void pager_dump_data(const char *data, int len)
{
  if (data == 0 || len <= 0)
    return;
  write(STDOUT_FILENO, data, (size_t)len);
}

static int pager_run_interactive(struct pager_app *app)
{
  int running = 1;

  if (pager_enter_raw(app) < 0)
    return -1;

  pager_set_status(app, "q:quit");
  while (running != 0) {
    int key;

    pager_render(app, 0);
    key = pager_read_key();
    if (key < 0)
      break;

    if (key == 'q') {
      running = 0;
    } else if (key == ' ' || key == 'f') {
      pager_document_page_down(&app->document, app->page_rows);
      pager_set_status(app, "");
    } else if (key == 'b') {
      pager_document_page_up(&app->document, app->page_rows);
      pager_set_status(app, "");
    } else if (key == '\r' || key == '\n' ||
               key == 'j' || key == PAGER_KEY_DOWN) {
      pager_document_line_down(&app->document, 1);
      pager_set_status(app, "");
    } else if (app->less_mode != 0 &&
               (key == 'k' || key == PAGER_KEY_UP)) {
      pager_document_line_up(&app->document, 1);
      pager_set_status(app, "");
    } else if (app->less_mode != 0 && key == 'g') {
      pager_document_go_top(&app->document);
      pager_set_status(app, "");
    } else if (app->less_mode != 0 && key == 'G') {
      pager_document_go_bottom(&app->document, app->page_rows);
      pager_set_status(app, "");
    } else if (app->less_mode != 0 && key == '/') {
      char needle[PAGER_MAX_SEARCH];

      if (pager_prompt(app, "/", needle, sizeof(needle)) == 0) {
        if (pager_document_search(&app->document, needle, 1,
                                  app->page_rows) < 0)
          pager_set_status(app, "pattern not found");
        else
          pager_set_status(app, "");
      } else {
        pager_set_status(app, "");
      }
    } else if (app->less_mode != 0 && key == 'n') {
      if (pager_document_repeat_search(&app->document, 0,
                                       app->page_rows) < 0)
        pager_set_status(app, "pattern not found");
      else
        pager_set_status(app, "");
    } else if (app->less_mode != 0 && key == 'N') {
      if (pager_document_repeat_search(&app->document, 1,
                                       app->page_rows) < 0)
        pager_set_status(app, "pattern not found");
      else
        pager_set_status(app, "");
    }
  }

  pager_restore_termios(app);
  write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
  debug_write("AUDIT pager_exit\n", 17);
  return 0;
}

int pager_command_main(const char *program_name,
                       int argc, char **argv, int less_mode)
{
  struct pager_app app;
  struct winsize winsize;
  char *data = 0;
  int len = 0;
  int interactive = 1;

  memset(&app, 0, sizeof(app));
  app.program_name = program_name;
  app.display_name = argc >= 2 ? argv[1] : "(stdin)";
  app.less_mode = less_mode;

  if (argc > 2) {
    pager_write_error(program_name, "usage", "[file]");
    return 1;
  }

  if (argc >= 2) {
    if (pager_load_path(argv[1], &data, &len) < 0) {
      pager_write_error(program_name, "open failed", argv[1]);
      return 1;
    }
  } else {
    data = pager_read_fd_all(STDIN_FILENO, &len);
    if (data == 0) {
      pager_write_error(program_name, "read failed", 0);
      return 1;
    }
    interactive = 0;
  }

  if (tcgetattr(STDIN_FILENO, &app.saved_termios) < 0)
    interactive = 0;
  if (interactive != 0 && get_winsize(STDOUT_FILENO, &winsize) < 0)
    interactive = 0;

  if (interactive == 0) {
    pager_dump_data(data, len);
    free(data);
    return 0;
  }

  if (pager_document_init(&app.document) < 0) {
    free(data);
    pager_write_error(program_name, "init failed", 0);
    return 1;
  }
  pager_update_screen_size(&app);
  if (pager_document_load(&app.document, data, len, app.cols) < 0) {
    pager_document_free(&app.document);
    free(data);
    pager_write_error(program_name, "load failed", 0);
    return 1;
  }
  free(data);
  if (pager_run_interactive(&app) < 0) {
    pager_restore_termios(&app);
    pager_document_free(&app.document);
    pager_write_error(program_name, "interactive failed", 0);
    return 1;
  }
  pager_document_free(&app.document);
  return 0;
}
