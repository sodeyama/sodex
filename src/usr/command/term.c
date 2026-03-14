#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cell_renderer.h>
#include <console.h>
#include <debug.h>
#include <fb.h>
#include <key.h>
#include <terminal_surface.h>
#include <tty.h>
#include <vt_parser.h>
#include <winsize.h>

#define TERM_SCROLLBACK_SIZE 32768
#define TERM_LONG_OUTPUT_THRESHOLD 256
#define TERM_PTY_READ_CHUNK 512
#define TERM_PTY_READ_BATCH 8192

struct term_metrics {
  u_int32_t full_redraws;
  u_int32_t partial_redraws;
  u_int32_t scroll_events;
  u_int32_t scroll_lines;
  u_int32_t pty_bytes;
  u_int32_t render_cells;
  u_int32_t max_dirty_cells;
  u_int32_t long_output_marks;
};

struct term_app {
  int master_fd;
  int use_framebuffer;
  int cursor_valid;
  int last_cursor_col;
  int last_cursor_row;
  int last_scroll_count;
  u_int32_t long_output_base;
  struct fb_info fb;
  struct cell_renderer renderer;
  struct terminal_surface surface;
  struct vt_parser parser;
  struct term_metrics metrics;
  char scrollback[TERM_SCROLLBACK_SIZE];
  int scroll_head;
  int scroll_len;
};

PRIVATE struct term_app term_state;

PRIVATE int term_init(struct term_app *app);
PRIVATE int sync_viewport(struct term_app *app);
PRIVATE int pump_master(struct term_app *app);
PRIVATE int pump_keys(int master_fd);
PRIVATE int translate_key(struct key_event *event, char *buf);
PRIVATE void render_surface(struct term_app *app, int force);
PRIVATE char render_color(const struct term_cell *cell);
PRIVATE char render_char(const struct term_cell *cell);
PRIVATE void reset_surface(struct term_app *app);
PRIVATE void append_scrollback(struct term_app *app, const char *buf, int len);
PRIVATE void replay_scrollback(struct term_app *app);
PRIVATE char *metric_append_text(char *dst, const char *text);
PRIVATE char *metric_append_uint(char *dst, u_int32_t value);
PRIVATE void emit_metric(struct term_app *app, const char *point,
                         u_int32_t last_bytes, u_int32_t last_cells);

int main(int argc, char** argv)
{
  struct term_app *app = &term_state;
  char *shell_argv[2];

  (void)argc;
  (void)argv;
  memset(app, 0, sizeof(struct term_app));

  app->master_fd = openpty();
  if (app->master_fd < 0) {
    write(1, "term: openpty failed\n", 21);
    return execve("/usr/bin/eshell", 0, 0);
  }

  set_input_mode(INPUT_MODE_RAW);
  if (term_init(app) < 0) {
    set_input_mode(INPUT_MODE_CONSOLE);
    write(1, "term: console init failed\n", 26);
    return execve("/usr/bin/eshell", 0, 0);
  }

  if (app->use_framebuffer == 0)
    console_clear();
  render_surface(app, TRUE);
  {
    struct winsize winsize;
    winsize.cols = app->surface.cols;
    winsize.rows = app->surface.rows;
    set_winsize(app->master_fd, &winsize);
  }

  shell_argv[0] = "eshell";
  shell_argv[1] = NULL;
  if (execve_pty("/usr/bin/eshell", shell_argv, app->master_fd) < 0) {
    set_input_mode(INPUT_MODE_CONSOLE);
    write(1, "term: fallback to eshell\n", 25);
    return execve("/usr/bin/eshell", 0, 0);
  }

  while (TRUE) {
    int resized = sync_viewport(app);
    int output = pump_master(app);
    int input = pump_keys(app->master_fd);

    if (resized > 0 || output > 0 || terminal_surface_has_damage(&app->surface)) {
      render_surface(app, FALSE);
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
  int cols = 0;
  int rows = 0;

  memset(&app->fb, 0, sizeof(app->fb));
  if (get_fb_info(&app->fb) == 0 && app->fb.available != 0 &&
      cell_renderer_init(&app->renderer, &app->fb) == 0) {
    app->use_framebuffer = 1;
    cols = app->renderer.cols;
    rows = app->renderer.rows;
  } else {
    app->use_framebuffer = 0;
    cols = console_cols();
    rows = console_rows();
  }

  if (cols <= 0)
    cols = 80;
  if (rows <= 0)
    rows = 25;

  if (terminal_surface_init(&app->surface, cols, rows) < 0)
    return -1;

  vt_parser_init(&app->parser, &app->surface);
  app->scroll_head = 0;
  app->scroll_len = 0;
  app->cursor_valid = 0;
  app->last_scroll_count = 0;
  app->long_output_base = 0;
  blank = app->parser.default_pen;
  blank.ch = ' ';
  terminal_surface_reset(&app->surface, &blank);
  terminal_surface_mark_all_dirty(&app->surface);
  return 0;
}

PRIVATE int sync_viewport(struct term_app *app)
{
  struct term_cell blank;
  struct winsize winsize;
  int cols = console_cols();
  int rows = console_rows();

  if (app->use_framebuffer != 0)
    return 0;
  if (cols <= 0 || rows <= 0)
    return 0;
  if (cols == app->surface.cols && rows == app->surface.rows)
    return 0;

  blank = app->parser.default_pen;
  blank.ch = ' ';
  if (terminal_surface_resize(&app->surface, cols, rows, &blank) < 0)
    return 0;

  console_clear();
  replay_scrollback(app);
  winsize.cols = cols;
  winsize.rows = rows;
  set_winsize(app->master_fd, &winsize);
  return 1;
}

PRIVATE int pump_master(struct term_app *app)
{
  char buf[TERM_PTY_READ_CHUNK];
  int total = 0;

  while (TRUE) {
    int len = read(app->master_fd, buf, sizeof(buf));
    if (len <= 0)
      break;

    append_scrollback(app, buf, len);
    vt_parser_feed(&app->parser, buf, len);
    app->metrics.pty_bytes += (u_int32_t)len;
    total += len;
    if (total >= TERM_PTY_READ_BATCH)
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
  int total_cells = app->surface.cols * app->surface.rows;
  int dirty_before = force != FALSE ? total_cells : app->surface.dirty_count;
  u_int32_t rendered = 0;
  int scroll_delta;
  int row;
  int col;

  if (dirty_before > (int)app->metrics.max_dirty_cells)
    app->metrics.max_dirty_cells = (u_int32_t)dirty_before;

  if (app->use_framebuffer != 0) {
    const struct term_cell *cell;

    if (force != FALSE)
      cell_renderer_clear(&app->renderer, 0x000000);

    for (row = 0; row < app->surface.rows; row++) {
      for (col = 0; col < app->surface.cols; col++) {
        if (force == FALSE &&
            terminal_surface_is_dirty(&app->surface, col, row) == FALSE)
          continue;

        cell = terminal_surface_cell(&app->surface, col, row);
        if (cell == NULL)
          continue;
        cell_renderer_draw_cell(&app->renderer, col, row, cell, FALSE);
        rendered++;
      }
    }

    if (app->cursor_valid != 0 &&
        (app->last_cursor_col != app->surface.cursor_col ||
         app->last_cursor_row != app->surface.cursor_row)) {
      cell = terminal_surface_cell(&app->surface,
                                   app->last_cursor_col,
                                   app->last_cursor_row);
      if (cell != NULL) {
        cell_renderer_draw_cell(&app->renderer,
                                app->last_cursor_col,
                                app->last_cursor_row,
                                cell, FALSE);
        rendered++;
      }
    }

    cell = terminal_surface_cell(&app->surface,
                                 app->surface.cursor_col,
                                 app->surface.cursor_row);
    if (cell != NULL) {
      cell_renderer_draw_cell(&app->renderer,
                              app->surface.cursor_col,
                              app->surface.cursor_row,
                              cell, TRUE);
      rendered++;
    }
    app->last_cursor_col = app->surface.cursor_col;
    app->last_cursor_row = app->surface.cursor_row;
    app->cursor_valid = 1;
    app->metrics.render_cells += rendered;
    if (force != FALSE || dirty_before == total_cells)
      app->metrics.full_redraws++;
    else
      app->metrics.partial_redraws++;
    scroll_delta = app->surface.scroll_count - app->last_scroll_count;
    if (scroll_delta > 0) {
      app->metrics.scroll_events++;
      app->metrics.scroll_lines += (u_int32_t)scroll_delta;
      app->last_scroll_count = app->surface.scroll_count;
      emit_metric(app, "scroll", 0, rendered);
    }
    if (app->metrics.pty_bytes - app->long_output_base >= TERM_LONG_OUTPUT_THRESHOLD) {
      app->metrics.long_output_marks++;
      app->long_output_base = app->metrics.pty_bytes;
      emit_metric(app, "long_output", 0, rendered);
    }
    if (force != FALSE || dirty_before == total_cells)
      emit_metric(app, "full_redraw", 0, rendered);
    terminal_surface_clear_damage(&app->surface);
    return;
  }

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
      rendered++;
    }
  }

  app->metrics.render_cells += rendered;
  if (force != FALSE || dirty_before == total_cells)
    app->metrics.full_redraws++;
  else
    app->metrics.partial_redraws++;
  scroll_delta = app->surface.scroll_count - app->last_scroll_count;
  if (scroll_delta > 0) {
    app->metrics.scroll_events++;
    app->metrics.scroll_lines += (u_int32_t)scroll_delta;
    app->last_scroll_count = app->surface.scroll_count;
    emit_metric(app, "scroll", 0, rendered);
  }
  if (app->metrics.pty_bytes - app->long_output_base >= TERM_LONG_OUTPUT_THRESHOLD) {
    app->metrics.long_output_marks++;
    app->long_output_base = app->metrics.pty_bytes;
    emit_metric(app, "long_output", 0, rendered);
  }
  if (force != FALSE || dirty_before == total_cells)
    emit_metric(app, "full_redraw", 0, rendered);
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
  if (cell->ch == 0 || (cell->attr & TERM_ATTR_CONTINUATION) != 0)
    return ' ';
  if (cell->ch < 0x20 || cell->ch > 0x7e)
    return '?';
  return (char)cell->ch;
}

PRIVATE void reset_surface(struct term_app *app)
{
  struct term_cell blank = app->parser.default_pen;
  blank.ch = ' ';
  app->cursor_valid = 0;
  vt_parser_reset(&app->parser);
  terminal_surface_reset(&app->surface, &blank);
}

PRIVATE void append_scrollback(struct term_app *app, const char *buf, int len)
{
  int i;

  for (i = 0; i < len; i++) {
    if (app->scroll_len < TERM_SCROLLBACK_SIZE) {
      int tail = (app->scroll_head + app->scroll_len) % TERM_SCROLLBACK_SIZE;
      app->scrollback[tail] = buf[i];
      app->scroll_len++;
    } else {
      app->scrollback[app->scroll_head] = buf[i];
      app->scroll_head = (app->scroll_head + 1) % TERM_SCROLLBACK_SIZE;
    }
  }
}

PRIVATE void replay_scrollback(struct term_app *app)
{
  int i;

  reset_surface(app);
  for (i = 0; i < app->scroll_len; i++) {
    char ch = app->scrollback[(app->scroll_head + i) % TERM_SCROLLBACK_SIZE];
    vt_parser_feed(&app->parser, &ch, 1);
  }
  app->last_scroll_count = app->surface.scroll_count;
  terminal_surface_mark_all_dirty(&app->surface);
}

PRIVATE char *metric_append_text(char *dst, const char *text)
{
  while (*text != '\0') {
    *dst = *text;
    dst++;
    text++;
  }
  return dst;
}

PRIVATE char *metric_append_uint(char *dst, u_int32_t value)
{
  char tmp[16];
  int i = 0;

  if (value == 0) {
    *dst = '0';
    return dst + 1;
  }

  while (value > 0 && i < (int)sizeof(tmp)) {
    tmp[i++] = (char)('0' + (value % 10));
    value /= 10;
  }
  while (i > 0) {
    i--;
    *dst = tmp[i];
    dst++;
  }
  return dst;
}

PRIVATE void emit_metric(struct term_app *app, const char *point,
                         u_int32_t last_bytes, u_int32_t last_cells)
{
  char buf[256];
  char *p = buf;

  if (app->use_framebuffer == 0)
    return;

  p = metric_append_text(p, "TERM_METRIC point=");
  p = metric_append_text(p, point);
  p = metric_append_text(p, " full=");
  p = metric_append_uint(p, app->metrics.full_redraws);
  p = metric_append_text(p, " partial=");
  p = metric_append_uint(p, app->metrics.partial_redraws);
  p = metric_append_text(p, " scroll_events=");
  p = metric_append_uint(p, app->metrics.scroll_events);
  p = metric_append_text(p, " scroll_lines=");
  p = metric_append_uint(p, app->metrics.scroll_lines);
  p = metric_append_text(p, " pty_bytes=");
  p = metric_append_uint(p, app->metrics.pty_bytes);
  p = metric_append_text(p, " render_cells=");
  p = metric_append_uint(p, app->metrics.render_cells);
  p = metric_append_text(p, " max_dirty=");
  p = metric_append_uint(p, app->metrics.max_dirty_cells);
  p = metric_append_text(p, " long_output_marks=");
  p = metric_append_uint(p, app->metrics.long_output_marks);
  p = metric_append_text(p, " last_bytes=");
  p = metric_append_uint(p, last_bytes);
  p = metric_append_text(p, " last_cells=");
  p = metric_append_uint(p, last_cells);
  *p++ = '\n';
  debug_write(buf, (size_t)(p - buf));
}
