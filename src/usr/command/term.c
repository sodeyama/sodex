#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cell_renderer.h>
#include <console.h>
#include <debug.h>
#include <fb.h>
#include <ime.h>
#include <key.h>
#include <terminal_surface.h>
#include <tty.h>
#include <vt_parser.h>
#include <winsize.h>

#define TERM_SCROLLBACK_SIZE 32768
#define TERM_LONG_OUTPUT_THRESHOLD 256
#define TERM_PTY_READ_CHUNK 512
#define TERM_PTY_READ_BATCH 8192
#define TERM_INPUT_BUF 64
#define TERM_IME_STATUS_COLS 16

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

enum term_ime_action {
  TERM_IME_ACTION_NONE = 0,
  TERM_IME_ACTION_TOGGLE_LATIN_HIRA = 1,
  TERM_IME_ACTION_SET_HIRAGANA = 2,
  TERM_IME_ACTION_SET_KATAKANA = 3,
  TERM_IME_ACTION_SET_LATIN = 4
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
  struct ime_state ime;
};

PRIVATE struct term_app term_state;

PRIVATE int term_init(struct term_app *app);
PRIVATE int sync_viewport(struct term_app *app);
PRIVATE int pump_master(struct term_app *app);
PRIVATE int pump_keys(struct term_app *app);
PRIVATE int translate_key(struct term_app *app, struct key_event *event,
                          char *buf, int *needs_redraw);
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
PRIVATE int append_output(char *buf, int len, const char *src, int src_len);
PRIVATE int flush_ime(struct term_app *app, char *buf, int len);
PRIVATE enum term_ime_action ime_action_for_event(const struct key_event *event);
PRIVATE void ime_apply_action(struct ime_state *ime, enum term_ime_action action);
PRIVATE void render_ime_overlay(struct term_app *app);
PRIVATE void build_ime_overlay_text(struct term_app *app, char *text, int cap);

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
    int input = pump_keys(app);

    if (resized > 0 || output > 0 || input > 0 ||
        terminal_surface_has_damage(&app->surface)) {
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
  ime_init(&app->ime);
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

PRIVATE int pump_keys(struct term_app *app)
{
  struct key_event event;
  int total = 0;

  while (getkeyevent(&event) > 0) {
    char buf[TERM_INPUT_BUF];
    int needs_redraw = 0;
    int len = translate_key(app, &event, buf, &needs_redraw);
    if (len > 0) {
      write(app->master_fd, buf, len);
      total += len;
    }
    if (needs_redraw != 0 && len == 0)
      total++;
  }

  return total;
}

PRIVATE int translate_key(struct term_app *app, struct key_event *event,
                          char *buf, int *needs_redraw)
{
  int len = 0;
  enum term_ime_action action;

  if (needs_redraw != NULL)
    *needs_redraw = 0;
  if (event->flags & KEY_EVENT_RELEASE)
    return 0;

  action = ime_action_for_event(event);
  if (action != TERM_IME_ACTION_NONE) {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    ime_apply_action(&app->ime, action);
    if (needs_redraw != NULL)
      *needs_redraw = 1;
    return len;
  }

  if (event->flags & KEY_EVENT_EXTENDED) {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    switch (event->scancode) {
    case KEY_SCANCODE_UP:
      return append_output(buf, len, "\x1b[A", 3);
    case KEY_SCANCODE_DOWN:
      return append_output(buf, len, "\x1b[B", 3);
    case KEY_SCANCODE_RIGHT:
      return append_output(buf, len, "\x1b[C", 3);
    case KEY_SCANCODE_LEFT:
      return append_output(buf, len, "\x1b[D", 3);
    default:
      return len;
    }
  }

  if (event->ascii == 0)
    return 0;

  if (event->ascii == KEY_BACK && app->ime.preedit_len > 0) {
    ime_backspace(&app->ime);
    if (needs_redraw != NULL)
      *needs_redraw = 1;
    return 0;
  }

  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'A' && event->ascii <= 'Z') {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    buf[len] = event->ascii - 'A' + 1;
    return len + 1;
  }
  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'a' && event->ascii <= 'z') {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    buf[len] = event->ascii - 'a' + 1;
    return len + 1;
  }

  if (app->ime.mode != IME_MODE_LATIN) {
    len = ime_feed_ascii(&app->ime, (char)event->ascii, buf, TERM_INPUT_BUF);
    if (needs_redraw != NULL)
      *needs_redraw = 1;
    if (len < 0)
      return 0;
    return len;
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
        if ((cell->attr & TERM_ATTR_CONTINUATION) != 0 && col > 0) {
          const struct term_cell *lead =
              terminal_surface_cell(&app->surface, col - 1, row);
          if (lead != NULL &&
              (lead->attr & TERM_ATTR_CONTINUATION) == 0 &&
              lead->width > 1) {
            cell_renderer_draw_cell(&app->renderer, col - 1, row, lead, FALSE);
            rendered++;
            continue;
          }
        }
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
    render_ime_overlay(app);
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
  render_ime_overlay(app);
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

PRIVATE int append_output(char *buf, int len, const char *src, int src_len)
{
  if (len < 0 || len + src_len > TERM_INPUT_BUF)
    return -1;
  memcpy(buf + len, src, (size_t)src_len);
  return len + src_len;
}

PRIVATE int flush_ime(struct term_app *app, char *buf, int len)
{
  int emitted;

  if (app->ime.preedit_len <= 0)
    return len;

  emitted = ime_flush(&app->ime, buf + len, TERM_INPUT_BUF - len);
  if (emitted < 0)
    return -1;
  return len + emitted;
}

PRIVATE enum term_ime_action ime_action_for_event(const struct key_event *event)
{
  if (event == NULL)
    return TERM_IME_ACTION_NONE;
  if ((event->modifiers & KEY_MOD_CTRL) != 0 && event->ascii == ' ')
    return TERM_IME_ACTION_TOGGLE_LATIN_HIRA;
  if (event->scancode == KEY_SCANCODE_HANKAKU_ZENKAKU)
    return TERM_IME_ACTION_TOGGLE_LATIN_HIRA;
  if (event->scancode == KEY_SCANCODE_HENKAN)
    return TERM_IME_ACTION_SET_HIRAGANA;
  if (event->scancode == KEY_SCANCODE_MUHENKAN)
    return TERM_IME_ACTION_SET_LATIN;
  if (event->scancode == KEY_SCANCODE_KANA) {
    if ((event->modifiers & KEY_MOD_SHIFT) != 0)
      return TERM_IME_ACTION_SET_KATAKANA;
    return TERM_IME_ACTION_SET_HIRAGANA;
  }
  return TERM_IME_ACTION_NONE;
}

PRIVATE void ime_apply_action(struct ime_state *ime, enum term_ime_action action)
{
  if (ime == NULL)
    return;

  switch (action) {
  case TERM_IME_ACTION_TOGGLE_LATIN_HIRA:
    if (ime->mode == IME_MODE_LATIN)
      ime_set_mode(ime, IME_MODE_HIRAGANA);
    else
      ime_set_mode(ime, IME_MODE_LATIN);
    break;
  case TERM_IME_ACTION_SET_HIRAGANA:
    ime_set_mode(ime, IME_MODE_HIRAGANA);
    break;
  case TERM_IME_ACTION_SET_KATAKANA:
    ime_set_mode(ime, IME_MODE_KATAKANA);
    break;
  case TERM_IME_ACTION_SET_LATIN:
    ime_set_mode(ime, IME_MODE_LATIN);
    break;
  default:
    break;
  }
}

PRIVATE void render_ime_overlay(struct term_app *app)
{
  struct term_cell cell;
  char text[TERM_IME_STATUS_COLS + 1];
  int width;
  int start_col;
  int i;
  int text_len;

  if (app == NULL || app->surface.rows <= 0 || app->surface.cols <= 0)
    return;

  build_ime_overlay_text(app, text, sizeof(text));
  width = TERM_IME_STATUS_COLS;
  if (width > app->surface.cols)
    width = app->surface.cols;
  start_col = app->surface.cols - width;
  text_len = (int)strlen(text);

  memset(&cell, 0, sizeof(cell));
  cell.fg = TERM_COLOR_BLACK;
  cell.bg = TERM_COLOR_LIGHT_GRAY;
  cell.width = 1;

  if (app->use_framebuffer != 0) {
    for (i = 0; i < width; i++) {
      cell.ch = (u_int32_t)((i < text_len) ? text[i] : ' ');
      cell_renderer_draw_cell(&app->renderer, start_col + i, 0, &cell, FALSE);
    }
    return;
  }

  for (i = 0; i < width; i++) {
    char ch = (i < text_len) ? text[i] : ' ';
    console_putc_at(start_col + i, 0, render_color(&cell), ch);
  }
}

PRIVATE void build_ime_overlay_text(struct term_app *app, char *text, int cap)
{
  const char *mode;
  const char *preedit;
  int pos = 0;
  int tail_len;

  if (text == NULL || cap <= 0)
    return;

  memset(text, ' ', (size_t)(cap - 1));
  text[cap - 1] = '\0';

  mode = ime_mode_label(&app->ime);
  preedit = ime_preedit(&app->ime);

  if (cap > 4) {
    memcpy(text + pos, "IME ", 4);
    pos += 4;
  }
  while (pos < cap - 1 && *mode != '\0') {
    text[pos++] = *mode;
    mode++;
  }
  if (*preedit == '\0' || pos >= cap - 2)
    return;

  text[pos++] = ' ';
  tail_len = (int)strlen(preedit);
  while (tail_len > cap - 1 - pos) {
    preedit++;
    tail_len--;
  }
  memcpy(text + pos, preedit, (size_t)tail_len);
}
