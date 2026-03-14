#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <console.h>
#include <key.h>
#include <terminal_surface.h>
#include <tty.h>
#include <vt_parser.h>

struct term_app {
  int master_fd;
  struct terminal_surface surface;
  struct vt_parser parser;
};

PRIVATE int term_init(struct term_app *app);
PRIVATE int pump_master(struct term_app *app);
PRIVATE int pump_keys(int master_fd);
PRIVATE int translate_key(struct key_event *event, char *buf);
PRIVATE void render_surface(struct term_app *app, int force);
PRIVATE char render_color(const struct term_cell *cell);
PRIVATE char render_char(const struct term_cell *cell);

int main(int argc, char** argv)
{
  struct term_app app;
  char *shell_argv[2];

  (void)argc;
  (void)argv;
  memset(&app, 0, sizeof(struct term_app));

  app.master_fd = openpty();
  if (app.master_fd < 0) {
    write(1, "term: openpty failed\n", 21);
    return execve("/usr/bin/eshell", 0, 0);
  }

  set_input_mode(INPUT_MODE_RAW);
  if (term_init(&app) < 0) {
    set_input_mode(INPUT_MODE_CONSOLE);
    write(1, "term: console init failed\n", 26);
    return execve("/usr/bin/eshell", 0, 0);
  }

  console_clear();
  render_surface(&app, TRUE);

  shell_argv[0] = "eshell";
  shell_argv[1] = NULL;
  if (execve_pty("/usr/bin/eshell", shell_argv, app.master_fd) < 0) {
    set_input_mode(INPUT_MODE_CONSOLE);
    write(1, "term: fallback to eshell\n", 25);
    return execve("/usr/bin/eshell", 0, 0);
  }

  while (TRUE) {
    int output = pump_master(&app);
    int input = pump_keys(app.master_fd);

    if (output > 0 || terminal_surface_has_damage(&app.surface)) {
      render_surface(&app, FALSE);
    }

    if (output == 0 && input == 0) {
      volatile int spin;
      for (spin = 0; spin < 200000; spin++);
    }
  }

  return 0;
}

PRIVATE int term_init(struct term_app *app)
{
  struct term_cell blank;
  int cols = console_cols();
  int rows = console_rows();

  if (cols <= 0)
    cols = 80;
  if (rows <= 0)
    rows = 25;

  if (terminal_surface_init(&app->surface, cols, rows) < 0)
    return -1;

  vt_parser_init(&app->parser, &app->surface);
  blank = app->parser.default_pen;
  blank.ch = ' ';
  terminal_surface_reset(&app->surface, &blank);
  terminal_surface_mark_all_dirty(&app->surface);
  return 0;
}

PRIVATE int pump_master(struct term_app *app)
{
  char buf[128];
  int total = 0;

  while (TRUE) {
    int len = read(app->master_fd, buf, sizeof(buf));
    if (len <= 0)
      break;

    vt_parser_feed(&app->parser, buf, len);
    total += len;
    if (len < sizeof(buf))
      break;
  }

  return total;
}

PRIVATE int pump_keys(int master_fd)
{
  struct key_event event;
  int total = 0;

  while (getkeyevent(&event) > 0) {
    char buf[4];
    int len = translate_key(&event, buf);
    if (len > 0) {
      write(master_fd, buf, len);
      total += len;
    }
  }

  return total;
}

PRIVATE int translate_key(struct key_event *event, char *buf)
{
  if (event->flags & KEY_EVENT_RELEASE)
    return 0;

  if (event->flags & KEY_EVENT_EXTENDED) {
    switch (event->scancode) {
    case KEY_SCANCODE_UP:
      memcpy(buf, "\x1b[A", 3);
      return 3;
    case KEY_SCANCODE_DOWN:
      memcpy(buf, "\x1b[B", 3);
      return 3;
    case KEY_SCANCODE_RIGHT:
      memcpy(buf, "\x1b[C", 3);
      return 3;
    case KEY_SCANCODE_LEFT:
      memcpy(buf, "\x1b[D", 3);
      return 3;
    default:
      return 0;
    }
  }

  if (event->ascii == 0)
    return 0;

  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'A' && event->ascii <= 'Z') {
    buf[0] = event->ascii - 'A' + 1;
    return 1;
  }
  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'a' && event->ascii <= 'z') {
    buf[0] = event->ascii - 'a' + 1;
    return 1;
  }

  buf[0] = event->ascii;
  return 1;
}

PRIVATE void render_surface(struct term_app *app, int force)
{
  int row;
  int col;

  for (row = 0; row < app->surface.rows; row++) {
    for (col = 0; col < app->surface.cols; col++) {
      const struct term_cell *cell;

      if (force == FALSE &&
          terminal_surface_is_dirty(&app->surface, col, row) == FALSE)
        continue;

      cell = terminal_surface_cell(&app->surface, col, row);
      if (cell == NULL)
        continue;
      console_putc_at(col, row, render_color(cell), render_char(cell));
    }
  }

  terminal_surface_clear_damage(&app->surface);
  console_set_cursor(app->surface.cursor_col, app->surface.cursor_row);
}

PRIVATE char render_color(const struct term_cell *cell)
{
  int fg = cell->fg & 0x0f;
  int bg = cell->bg & 0x0f;

  if ((cell->attr & TERM_ATTR_BOLD) != 0 && fg < 8)
    fg += 8;
  if ((cell->attr & TERM_ATTR_REVERSE) != 0) {
    int tmp = fg;
    fg = bg;
    bg = tmp;
  }
  if (bg >= 8)
    bg -= 8;

  return (char)((bg << 4) | (fg & 0x0f));
}

PRIVATE char render_char(const struct term_cell *cell)
{
  if (cell->ch == 0)
    return ' ';
  return cell->ch;
}
