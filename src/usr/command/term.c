#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cell_renderer.h>
#include <console.h>
#include <debug.h>
#include <fb.h>
#include <ime.h>
#include <ime_conversion.h>
#include <key.h>
#include <sleep.h>
#include <terminal_surface.h>
#include <tty.h>
#include <utf8.h>
#include <vt_parser.h>
#include <wcwidth.h>
#include <winsize.h>

#define TERM_SCROLLBACK_SIZE 32768
#define TERM_LONG_OUTPUT_THRESHOLD 256
#define TERM_PTY_READ_CHUNK 512
#define TERM_PTY_READ_BATCH 8192
#define TERM_INPUT_BUF 128
#define TERM_IME_STATUS_COLS 16
#define TERM_IME_CONVERSION_COLS 40
#define TERM_IME_OVERLAY_MAX_CELLS 96
#define TERM_IME_OVERLAY_MARGIN_COLS 1
#define TERM_IME_TARGET_FG TERM_COLOR_WHITE
#define TERM_IME_TARGET_BG TERM_COLOR_RED
#define TERM_IME_READING_FG TERM_COLOR_BLACK
#define TERM_IME_READING_BG TERM_COLOR_YELLOW
#define TERM_IME_SELECTED_FG TERM_COLOR_WHITE
#define TERM_IME_SELECTED_BG TERM_COLOR_BLUE

struct term_metrics {
  u_int32_t full_redraws;
  u_int32_t partial_redraws;
  u_int32_t scroll_events;
  u_int32_t scroll_lines;
  u_int32_t pty_bytes;
  u_int32_t render_cells;
  u_int32_t max_dirty_cells;
  u_int32_t long_output_marks;
  u_int32_t present_copy_area;
  u_int32_t scroll_fast_path_count;
  u_int32_t scroll_full_fallback_count;
};

enum term_ime_action {
  TERM_IME_ACTION_NONE = 0,
  TERM_IME_ACTION_TOGGLE_LATIN_HIRA = 1,
  TERM_IME_ACTION_SET_HIRAGANA = 2,
  TERM_IME_ACTION_SET_KATAKANA = 3,
  TERM_IME_ACTION_SET_LATIN = 4,
  TERM_IME_ACTION_START_CONVERSION = 5,
  TERM_IME_ACTION_NEXT_CANDIDATE = 6,
  TERM_IME_ACTION_PREV_CANDIDATE = 7,
  TERM_IME_ACTION_COMMIT_CONVERSION = 8,
  TERM_IME_ACTION_CANCEL_CONVERSION = 9
};

struct term_app {
  int master_fd;
  int use_framebuffer;
  int cursor_valid;
  int ime_overlay_valid;
  int last_cursor_col;
  int last_cursor_row;
  int last_ime_overlay_width;
  int last_ime_overlay_start_col;
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
  struct term_cell last_ime_overlay_cells[TERM_IME_OVERLAY_MAX_CELLS];
  u_int8_t *back_buffer;
  u_int32_t back_buffer_size;
  int present_left;
  int present_top;
  int present_right;
  int present_bottom;
  int present_valid;
  struct cell_renderer *active_renderer;
};

PRIVATE struct term_app term_state;

PRIVATE int term_init(struct term_app *app);
PRIVATE int term_current_viewport(struct term_app *app, int *cols, int *rows);
PRIVATE int sync_viewport(struct term_app *app);
PRIVATE void term_release_back_buffer(struct term_app *app);
PRIVATE int term_prepare_back_buffer(struct term_app *app);
PRIVATE int term_cell_width(const struct term_app *app);
PRIVATE int term_cell_height(const struct term_app *app);
PRIVATE int term_pixels_for_cells(const struct term_app *app, int cells);
PRIVATE int pump_master(struct term_app *app);
PRIVATE int pump_keys(struct term_app *app);
PRIVATE int translate_key(struct term_app *app, struct key_event *event,
                          char *buf, int *needs_redraw);
PRIVATE void render_surface(struct term_app *app, int force);
PRIVATE void term_reset_present_bounds(struct term_app *app);
PRIVATE void term_mark_present_rect(struct term_app *app,
                                    int x, int y, int width, int height);
PRIVATE void term_mark_cell_rect(struct term_app *app,
                                 int col, int row, int width_cells);
PRIVATE void term_draw_cell(struct term_app *app,
                            int col, int row,
                            const struct term_cell *cell,
                            int cursor);
PRIVATE void term_copy_back_to_front(struct term_app *app,
                                     int x, int y, int width, int height);
PRIVATE void term_scroll_back_buffer(struct term_app *app, int scroll_delta);
PRIVATE void term_present(struct term_app *app, int scroll_delta);
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
PRIVATE int append_backspaces(char *buf, int len, int count);
PRIVATE int flush_ime(struct term_app *app, char *buf, int len);
PRIVATE enum term_ime_action ime_action_for_event(const struct ime_state *ime,
                                                  const struct key_event *event);
PRIVATE void ime_apply_action(struct ime_state *ime, enum term_ime_action action);
PRIVATE void render_ime_overlay(struct term_app *app, int force);
PRIVATE void render_conversion_target(struct term_app *app);
PRIVATE void build_ime_overlay_text(struct term_app *app, char *text, int cap);
PRIVATE int build_ime_overlay_cells(struct term_app *app,
                                    struct term_cell *cells, int cap);
PRIVATE int ime_overlay_cells_equal(const struct term_cell *a,
                                    const struct term_cell *b, int len);
PRIVATE int ime_overlay_region_needs_redraw(const struct term_app *app,
                                            int start_col, int width,
                                            int force);
PRIVATE int utf8_display_width(const char *text);
PRIVATE int append_overlay_ascii(struct term_cell *cells, int len, int cap,
                                 const char *text,
                                 unsigned char fg,
                                 unsigned char bg,
                                 unsigned char attr);
PRIVATE int append_overlay_utf8(struct term_cell *cells, int len, int cap,
                                const char *text,
                                unsigned char fg,
                                unsigned char bg,
                                unsigned char attr);
PRIVATE int append_overlay_codepoint(struct term_cell *cells, int len, int cap,
                                     u_int32_t codepoint,
                                     unsigned char fg,
                                     unsigned char bg,
                                     unsigned char attr);

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

    if (resized == 0 && output == 0 && input == 0 &&
        terminal_surface_has_damage(&app->surface) == 0)
      sleep_ticks(1);
  }

  return 0;
}

PRIVATE int term_init(struct term_app *app)
{
  struct term_cell blank;
  int cols = 0;
  int rows = 0;

  if (term_current_viewport(app, &cols, &rows) < 0)
    return -1;

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
  app->ime_overlay_valid = 0;
  app->last_ime_overlay_width = 0;
  app->last_ime_overlay_start_col = 0;
  app->last_scroll_count = 0;
  app->long_output_base = 0;
  ime_init(&app->ime);
  blank = app->parser.default_pen;
  blank.ch = ' ';
  terminal_surface_reset(&app->surface, &blank);
  terminal_surface_mark_all_dirty(&app->surface);
  if (app->use_framebuffer != 0)
    term_prepare_back_buffer(app);
  return 0;
}

PRIVATE void term_release_back_buffer(struct term_app *app)
{
  if (app == NULL)
    return;
  if (app->back_buffer != NULL)
    free(app->back_buffer);
  app->back_buffer = NULL;
  app->back_buffer_size = 0;
}

PRIVATE int term_prepare_back_buffer(struct term_app *app)
{
  u_int8_t *next;

  if (app == NULL || app->use_framebuffer == 0 ||
      app->fb.available == 0 || app->fb.base == NULL || app->fb.size == 0)
    return -1;
  if (app->back_buffer != NULL && app->back_buffer_size == app->fb.size)
    return 0;

  next = (u_int8_t *)malloc((size_t)app->fb.size);
  if (next == NULL)
    return -1;

  if (app->back_buffer != NULL)
    free(app->back_buffer);
  app->back_buffer = next;
  app->back_buffer_size = app->fb.size;
  return 0;
}

PRIVATE int term_cell_width(const struct term_app *app)
{
  if (app == NULL || app->renderer.cols <= 0 || app->fb.width <= 0)
    return 0;
  return app->fb.width / app->renderer.cols;
}

PRIVATE int term_cell_height(const struct term_app *app)
{
  if (app == NULL || app->renderer.rows <= 0 || app->fb.height <= 0)
    return 0;
  return app->fb.height / app->renderer.rows;
}

PRIVATE int term_pixels_for_cells(const struct term_app *app, int cells)
{
  if (cells <= 0)
    cells = 1;
  return term_cell_width(app) * cells;
}

PRIVATE int term_current_viewport(struct term_app *app, int *cols, int *rows)
{
  if (app == NULL || cols == NULL || rows == NULL)
    return -1;

  memset(&app->fb, 0, sizeof(app->fb));
  if (get_fb_info(&app->fb) == 0 && app->fb.available != 0 &&
      cell_renderer_init(&app->renderer, &app->fb) == 0) {
    app->use_framebuffer = 1;
    *cols = app->renderer.cols;
    *rows = app->renderer.rows;
    return 0;
  }

  app->use_framebuffer = 0;
  term_release_back_buffer(app);
  *cols = console_cols();
  *rows = console_rows();
  return 0;
}

PRIVATE void term_reset_present_bounds(struct term_app *app)
{
  if (app == NULL)
    return;
  app->present_left = 0;
  app->present_top = 0;
  app->present_right = 0;
  app->present_bottom = 0;
  app->present_valid = 0;
}

PRIVATE void term_mark_present_rect(struct term_app *app,
                                    int x, int y, int width, int height)
{
  int right;
  int bottom;

  if (app == NULL || app->use_framebuffer == 0 || width <= 0 || height <= 0)
    return;
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (x >= app->fb.width || y >= app->fb.height)
    return;
  if (x + width > app->fb.width)
    width = app->fb.width - x;
  if (y + height > app->fb.height)
    height = app->fb.height - y;
  if (width <= 0 || height <= 0)
    return;

  right = x + width;
  bottom = y + height;
  if (app->present_valid == 0) {
    app->present_left = x;
    app->present_top = y;
    app->present_right = right;
    app->present_bottom = bottom;
    app->present_valid = 1;
    return;
  }
  if (x < app->present_left)
    app->present_left = x;
  if (y < app->present_top)
    app->present_top = y;
  if (right > app->present_right)
    app->present_right = right;
  if (bottom > app->present_bottom)
    app->present_bottom = bottom;
}

PRIVATE void term_mark_cell_rect(struct term_app *app,
                                 int col, int row, int width_cells)
{
  int cell_width;
  int cell_height;
  int pixel_width;

  if (width_cells <= 0)
    width_cells = 1;
  cell_width = term_cell_width(app);
  cell_height = term_cell_height(app);
  pixel_width = term_pixels_for_cells(app, width_cells);
  if (cell_width <= 0 || cell_height <= 0 || pixel_width <= 0)
    return;
  term_mark_present_rect(app,
                         col * cell_width,
                         row * cell_height,
                         pixel_width,
                         cell_height);
}

PRIVATE void term_draw_cell(struct term_app *app,
                            int col, int row,
                            const struct term_cell *cell,
                            int cursor)
{
  struct cell_renderer *renderer;
  int width_cells = 1;

  if (app == NULL)
    return;
  renderer = app->active_renderer != NULL ? app->active_renderer : &app->renderer;
  if (cell != NULL &&
      (cell->attr & TERM_ATTR_CONTINUATION) == 0 &&
      cell->width > 0) {
    width_cells = cell->width;
  }
  cell_renderer_draw_cell(renderer, col, row, cell, cursor);
  if (app->back_buffer != NULL)
    term_mark_cell_rect(app, col, row, width_cells);
}

PRIVATE void term_scroll_back_buffer(struct term_app *app, int scroll_delta)
{
  int pixel_rows;
  int copy_bytes;

  if (app == NULL || app->back_buffer == NULL || scroll_delta <= 0)
    return;

  pixel_rows = scroll_delta * term_cell_height(app);
  if (pixel_rows <= 0 || pixel_rows >= app->fb.height)
    return;

  copy_bytes = (app->fb.height - pixel_rows) * app->fb.pitch;
  memmove(app->back_buffer,
          app->back_buffer + pixel_rows * app->fb.pitch,
          (size_t)copy_bytes);
  memset(app->back_buffer + copy_bytes, 0,
         (size_t)(pixel_rows * app->fb.pitch));
}

PRIVATE void term_copy_back_to_front(struct term_app *app,
                                     int x, int y, int width, int height)
{
  int row;
  int col;

  if (app == NULL || app->use_framebuffer == 0 ||
      app->back_buffer == NULL || app->fb.base == NULL)
    return;
  if (width <= 0 || height <= 0)
    return;

  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (x >= app->fb.width || y >= app->fb.height)
    return;
  if (x + width > app->fb.width)
    width = app->fb.width - x;
  if (y + height > app->fb.height)
    height = app->fb.height - y;
  if (width <= 0 || height <= 0)
    return;

  for (row = 0; row < height; row++) {
    volatile u_int32_t *dst =
        (volatile u_int32_t *)((u_int8_t *)app->fb.base +
                               (y + row) * app->fb.pitch + x * 4);
    const u_int32_t *src =
        (const u_int32_t *)(app->back_buffer +
                            (y + row) * app->fb.pitch + x * 4);

    for (col = 0; col < width; col++)
      dst[col] = src[col];
  }
}

PRIVATE void term_present(struct term_app *app, int scroll_delta)
{
  int width;
  int height;

  if (app == NULL || app->use_framebuffer == 0 || app->back_buffer == NULL)
    return;

  if (scroll_delta > 0 && scroll_delta < app->surface.rows) {
    term_copy_back_to_front(app, 0, 0, app->fb.width, app->fb.height);
    app->metrics.present_copy_area +=
        (u_int32_t)(app->fb.width * app->fb.height);
    return;
  }

  if (app->present_valid == 0)
    return;

  width = app->present_right - app->present_left;
  height = app->present_bottom - app->present_top;
  if (width <= 0 || height <= 0)
    return;

  term_copy_back_to_front(app,
                          app->present_left,
                          app->present_top,
                          width, height);
  app->metrics.present_copy_area += (u_int32_t)(width * height);
}

PRIVATE int sync_viewport(struct term_app *app)
{
  struct term_cell blank;
  struct winsize winsize;
  int cols = 0;
  int rows = 0;

  if (term_current_viewport(app, &cols, &rows) < 0)
    return 0;
  if (cols <= 0 || rows <= 0)
    return 0;
  if (cols == app->surface.cols && rows == app->surface.rows)
    return 0;

  blank = app->parser.default_pen;
  blank.ch = ' ';
  if (terminal_surface_resize(&app->surface, cols, rows, &blank) < 0)
    return 0;

  app->ime_overlay_valid = 0;
  app->last_ime_overlay_width = 0;
  app->last_ime_overlay_start_col = 0;
  if (app->use_framebuffer == 0)
    console_clear();
  else
    term_prepare_back_buffer(app);
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

  action = ime_action_for_event(&app->ime, event);
  if (action != TERM_IME_ACTION_NONE) {
    switch (action) {
    case TERM_IME_ACTION_TOGGLE_LATIN_HIRA:
    case TERM_IME_ACTION_SET_HIRAGANA:
    case TERM_IME_ACTION_SET_KATAKANA:
    case TERM_IME_ACTION_SET_LATIN:
      len = flush_ime(app, buf, len);
      if (len < 0)
        return 0;
      ime_reset_segment(&app->ime);
      ime_apply_action(&app->ime, action);
      break;
    case TERM_IME_ACTION_START_CONVERSION:
      len = flush_ime(app, buf, len);
      if (len < 0)
        return 0;
      ime_start_conversion(&app->ime);
      break;
    case TERM_IME_ACTION_NEXT_CANDIDATE:
      ime_select_next_candidate(&app->ime);
      break;
    case TERM_IME_ACTION_PREV_CANDIDATE:
      ime_select_prev_candidate(&app->ime);
      break;
    case TERM_IME_ACTION_COMMIT_CONVERSION:
      {
        char converted[TERM_INPUT_BUF];
        int replace_chars = 0;
        int converted_len;

        converted_len = ime_commit_conversion(&app->ime, converted,
                                              sizeof(converted),
                                              &replace_chars);
        if (converted_len <= 0)
          return 0;
        len = append_backspaces(buf, len, replace_chars);
        if (len < 0)
          return 0;
        len = append_output(buf, len, converted, converted_len);
        if (len < 0)
          return 0;
      }
      break;
    case TERM_IME_ACTION_CANCEL_CONVERSION:
      ime_cancel_conversion(&app->ime);
      break;
    default:
      break;
    }
    if (needs_redraw != NULL)
      *needs_redraw = 1;
    return len;
  }

  if (app->ime.mode == IME_MODE_HIRAGANA &&
      app->ime.preedit_len <= 0 &&
      ime_reading_chars(&app->ime) > 0 &&
      event->ascii == ' ') {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    if (ime_start_conversion(&app->ime) != 0) {
      if (needs_redraw != NULL)
        *needs_redraw = 1;
      return len;
    }
  }

  if (ime_conversion_active(&app->ime) != 0 &&
      event->ascii != KEY_BACK) {
    ime_cancel_conversion(&app->ime);
    ime_reset_segment(&app->ime);
    if (needs_redraw != NULL)
      *needs_redraw = 1;
  }

  if (event->flags & KEY_EVENT_EXTENDED) {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    ime_reset_segment(&app->ime);
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

  if (event->ascii == KEY_BACK) {
    if (app->ime.preedit_len > 0) {
      ime_backspace(&app->ime);
      if (needs_redraw != NULL)
        *needs_redraw = 1;
      return 0;
    }
    if (app->ime.mode != IME_MODE_LATIN &&
        ime_reading_chars(&app->ime) > 0 &&
        ime_drop_last_reading_char(&app->ime) != 0) {
      if (needs_redraw != NULL)
        *needs_redraw = 1;
      buf[0] = KEY_BACK;
      return 1;
    }
  }

  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'A' && event->ascii <= 'Z') {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    ime_reset_segment(&app->ime);
    buf[len] = event->ascii - 'A' + 1;
    return len + 1;
  }
  if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
      event->ascii >= 'a' && event->ascii <= 'z') {
    len = flush_ime(app, buf, len);
    if (len < 0)
      return 0;
    ime_reset_segment(&app->ime);
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

  ime_reset_segment(&app->ime);
  buf[0] = event->ascii;
  return 1;
}

PRIVATE void render_surface(struct term_app *app, int force)
{
  int total_cells = app->surface.cols * app->surface.rows;
  int dirty_before = force != FALSE ? total_cells : app->surface.dirty_count;
  u_int32_t rendered = 0;
  int scroll_delta;
  int apply_scroll_fast_path = FALSE;
  int row;
  int col;

  if (dirty_before > (int)app->metrics.max_dirty_cells)
    app->metrics.max_dirty_cells = (u_int32_t)dirty_before;
  scroll_delta = app->surface.scroll_count - app->last_scroll_count;

  if (app->use_framebuffer != 0) {
    struct cell_renderer back_renderer;
    struct cell_renderer *draw_renderer = &app->renderer;
    const struct term_cell *cell;

    term_reset_present_bounds(app);
    if (app->back_buffer != NULL) {
      back_renderer = app->renderer;
      back_renderer.fb.base = app->back_buffer;
      back_renderer.fb.size = app->back_buffer_size;
      draw_renderer = &back_renderer;
    }
    app->active_renderer = draw_renderer;

    if (force != FALSE) {
      cell_renderer_clear(draw_renderer, 0x000000);
      if (app->back_buffer != NULL)
        term_mark_present_rect(app, 0, 0, app->fb.width, app->fb.height);
    } else if (scroll_delta > 0 && app->back_buffer != NULL) {
      if (scroll_delta < app->surface.rows) {
        term_scroll_back_buffer(app, scroll_delta);
        app->metrics.scroll_fast_path_count++;
        apply_scroll_fast_path = TRUE;
      } else {
        cell_renderer_clear(draw_renderer, 0x000000);
        term_mark_present_rect(app, 0, 0, app->fb.width, app->fb.height);
        app->metrics.scroll_full_fallback_count++;
        force = TRUE;
        dirty_before = total_cells;
      }
    }

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
            term_draw_cell(app, col - 1, row, lead, FALSE);
            rendered++;
            continue;
          }
        }
        term_draw_cell(app, col, row, cell, FALSE);
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
        term_draw_cell(app,
                       app->last_cursor_col,
                       app->last_cursor_row,
                       cell, FALSE);
        rendered++;
      }
    }

    cell = terminal_surface_cell(&app->surface,
                                 app->surface.cursor_col,
                                 app->surface.cursor_row);
    render_conversion_target(app);
    if (cell != NULL) {
      term_draw_cell(app,
                     app->surface.cursor_col,
                     app->surface.cursor_row,
                     cell, TRUE);
      rendered++;
    }
    app->last_cursor_col = app->surface.cursor_col;
    app->last_cursor_row = app->surface.cursor_row;
    app->cursor_valid = 1;
    render_ime_overlay(app, force);
    if (app->back_buffer != NULL)
      term_present(app, apply_scroll_fast_path != FALSE ? scroll_delta : 0);
    app->active_renderer = NULL;
    app->metrics.render_cells += rendered;
    if (force != FALSE || dirty_before == total_cells)
      app->metrics.full_redraws++;
    else
      app->metrics.partial_redraws++;
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
  render_conversion_target(app);
  render_ime_overlay(app, force);
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
  char buf[384];
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
  p = metric_append_text(p, " present_copy_area=");
  p = metric_append_uint(p, app->metrics.present_copy_area);
  p = metric_append_text(p, " scroll_fast_path=");
  p = metric_append_uint(p, app->metrics.scroll_fast_path_count);
  p = metric_append_text(p, " scroll_full_fallback=");
  p = metric_append_uint(p, app->metrics.scroll_full_fallback_count);
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

PRIVATE int append_backspaces(char *buf, int len, int count)
{
  int i;

  if (count < 0)
    return -1;
  for (i = 0; i < count; i++) {
    if (len + 1 > TERM_INPUT_BUF)
      return -1;
    buf[len++] = KEY_BACK;
  }
  return len;
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

PRIVATE enum term_ime_action ime_action_for_event(const struct ime_state *ime,
                                                  const struct key_event *event)
{
  if (event == NULL)
    return TERM_IME_ACTION_NONE;
  if (ime != NULL && ime_conversion_active(ime) != 0) {
    if ((event->flags & KEY_EVENT_EXTENDED) != 0) {
      if (event->scancode == KEY_SCANCODE_RIGHT)
        return TERM_IME_ACTION_NEXT_CANDIDATE;
      if (event->scancode == KEY_SCANCODE_LEFT)
        return TERM_IME_ACTION_PREV_CANDIDATE;
    }
    if (event->ascii == ' ' && (event->modifiers & KEY_MOD_SHIFT) != 0)
      return TERM_IME_ACTION_PREV_CANDIDATE;
    if (event->ascii == ' ')
      return TERM_IME_ACTION_NEXT_CANDIDATE;
    if (event->ascii == '\r' || event->ascii == '\n')
      return TERM_IME_ACTION_COMMIT_CONVERSION;
    if (event->ascii == KEY_ESC)
      return TERM_IME_ACTION_CANCEL_CONVERSION;
    if ((event->modifiers & KEY_MOD_CTRL) != 0 &&
        (event->ascii == 'g' || event->ascii == 'G'))
      return TERM_IME_ACTION_CANCEL_CONVERSION;
  }
  if ((event->modifiers & KEY_MOD_CTRL) != 0 && event->ascii == ' ')
    return TERM_IME_ACTION_TOGGLE_LATIN_HIRA;
  if (event->scancode == KEY_SCANCODE_HANKAKU_ZENKAKU)
    return TERM_IME_ACTION_TOGGLE_LATIN_HIRA;
  if (ime != NULL && ime->mode == IME_MODE_HIRAGANA &&
      (ime->preedit_len > 0 || ime_reading_chars(ime) > 0)) {
    if (event->scancode == KEY_SCANCODE_HENKAN)
      return TERM_IME_ACTION_START_CONVERSION;
  }
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

PRIVATE void render_ime_overlay(struct term_app *app, int force)
{
  struct term_cell cell;
  struct term_cell cells[TERM_IME_OVERLAY_MAX_CELLS];
  char text[TERM_IME_STATUS_COLS + 1];
  int clear_start_col;
  int clear_width;
  int overlay_start_col;
  int overlay_width;
  int right_edge_col;
  int width;
  int start_col;
  int needs_redraw;
  int i;
  int text_len;
  int cell_len;

  if (app == NULL || app->surface.rows <= 0 || app->surface.cols <= 0)
    return;

  memset(&cell, 0, sizeof(cell));
  cell.fg = TERM_COLOR_BLACK;
  cell.bg = TERM_COLOR_LIGHT_GRAY;
  cell.width = 1;
  right_edge_col = app->surface.cols - TERM_IME_OVERLAY_MARGIN_COLS;
  if (right_edge_col <= 0)
    right_edge_col = app->surface.cols;

  if (app->use_framebuffer != 0) {
    cell_len = build_ime_overlay_cells(app, cells, TERM_IME_OVERLAY_MAX_CELLS);
    overlay_width = TERM_IME_STATUS_COLS;
    if (ime_conversion_active(&app->ime) != 0)
      overlay_width = TERM_IME_CONVERSION_COLS;
    if (cell_len > overlay_width)
      overlay_width = cell_len;
    if (overlay_width > right_edge_col)
      overlay_width = right_edge_col;
    clear_width = overlay_width;
    if (app->last_ime_overlay_width > clear_width)
      clear_width = app->last_ime_overlay_width;
    if (clear_width > right_edge_col)
      clear_width = right_edge_col;
    clear_start_col = right_edge_col - clear_width;
    overlay_start_col = right_edge_col - overlay_width;
    needs_redraw = force != FALSE ||
                   app->ime_overlay_valid == 0 ||
                   app->last_ime_overlay_width != overlay_width ||
                   app->last_ime_overlay_start_col != overlay_start_col ||
                   ime_overlay_region_needs_redraw(app, clear_start_col,
                                                   clear_width, force) != 0 ||
                   ime_overlay_cells_equal(cells, app->last_ime_overlay_cells,
                                           overlay_width) == 0;
    if (needs_redraw == 0)
      return;
    for (i = 0; i < clear_width; i++) {
      term_draw_cell(app, clear_start_col + i, 0, &cell, FALSE);
    }
    for (i = 0; i < overlay_width && i < cell_len; i++) {
      if ((cells[i].attr & TERM_ATTR_CONTINUATION) != 0)
        continue;
      term_draw_cell(app, overlay_start_col + i, 0, &cells[i], FALSE);
    }
    memcpy(app->last_ime_overlay_cells, cells,
           sizeof(struct term_cell) * (size_t)overlay_width);
    app->ime_overlay_valid = 1;
    app->last_ime_overlay_width = overlay_width;
    app->last_ime_overlay_start_col = overlay_start_col;
    return;
  }

  build_ime_overlay_text(app, text, sizeof(text));
  width = TERM_IME_STATUS_COLS;
  if (width > right_edge_col)
    width = right_edge_col;
  clear_width = width;
  if (app->last_ime_overlay_width > clear_width)
    clear_width = app->last_ime_overlay_width;
  if (clear_width > right_edge_col)
    clear_width = right_edge_col;
  clear_start_col = right_edge_col - clear_width;
  start_col = right_edge_col - width;
  for (i = 0; i < clear_width; i++)
    console_putc_at(clear_start_col + i, 0, render_color(&cell), ' ');
  text_len = (int)strlen(text);
  for (i = 0; i < width; i++) {
    char ch = (i < text_len) ? text[i] : ' ';
    console_putc_at(start_col + i, 0, render_color(&cell), ch);
  }
  app->last_ime_overlay_width = width;
}

PRIVATE int ime_overlay_cells_equal(const struct term_cell *a,
                                    const struct term_cell *b, int len)
{
  int i;

  if (a == NULL || b == NULL || len < 0)
    return FALSE;
  for (i = 0; i < len; i++) {
    if (a[i].ch != b[i].ch || a[i].fg != b[i].fg || a[i].bg != b[i].bg ||
        a[i].attr != b[i].attr || a[i].width != b[i].width)
      return FALSE;
  }
  return TRUE;
}

PRIVATE int ime_overlay_region_needs_redraw(const struct term_app *app,
                                            int start_col, int width,
                                            int force)
{
  int end_col;
  int col;

  if (app == NULL || width <= 0)
    return TRUE;
  if (force != FALSE)
    return TRUE;

  end_col = start_col + width;
  if (app->surface.cursor_row == 0 &&
      app->surface.cursor_col >= start_col &&
      app->surface.cursor_col < end_col)
    return TRUE;
  if (app->cursor_valid != 0 &&
      app->last_cursor_row == 0 &&
      app->last_cursor_col >= start_col &&
      app->last_cursor_col < end_col)
    return TRUE;

  for (col = start_col; col < end_col; col++) {
    if (terminal_surface_is_dirty(&app->surface, col, 0) != FALSE)
      return TRUE;
  }
  return FALSE;
}

PRIVATE void render_conversion_target(struct term_app *app)
{
  const char *reading;
  int target_cols;
  int start_col;
  int row;
  int col;

  if (app == NULL || ime_conversion_active(&app->ime) == 0)
    return;

  reading = ime_reading(&app->ime);
  if (reading == NULL || reading[0] == '\0')
    return;

  target_cols = utf8_display_width(reading);
  if (target_cols <= 0)
    return;

  row = app->surface.cursor_row;
  if (row < 0 || row >= app->surface.rows)
    return;
  if (target_cols > app->surface.cursor_col)
    return;

  start_col = app->surface.cursor_col - target_cols;
  col = start_col;
  while (col < app->surface.cursor_col) {
    const struct term_cell *cell = terminal_surface_cell(&app->surface, col, row);
    struct term_cell marked;

    if (cell == NULL)
      break;
    if ((cell->attr & TERM_ATTR_CONTINUATION) != 0) {
      col++;
      continue;
    }

    marked = *cell;
    marked.fg = TERM_IME_TARGET_FG;
    marked.bg = TERM_IME_TARGET_BG;
    marked.attr &= ~TERM_ATTR_REVERSE;

    if (app->use_framebuffer != 0)
      term_draw_cell(app, col, row, &marked, FALSE);
    else
      console_putc_at(col, row, render_color(&marked), render_char(&marked));

    if (marked.width > 1)
      col += marked.width;
    else
      col++;
  }
}

PRIVATE int build_ime_overlay_cells(struct term_app *app,
                                    struct term_cell *cells, int cap)
{
  const char *mode;
  const char *reading;
  char *p;
  int i;
  int len = 0;
  int page_end;
  int page_start;
  int page_total;
  char count[24];
  char page[24];

  if (app == NULL || cells == NULL || cap <= 0)
    return 0;

  memset(cells, 0, sizeof(struct term_cell) * (size_t)cap);
  for (i = 0; i < cap; i++) {
    cells[i].ch = ' ';
    cells[i].fg = TERM_COLOR_BLACK;
    cells[i].bg = TERM_COLOR_LIGHT_GRAY;
    cells[i].width = 1;
  }

  mode = ime_mode_label(&app->ime);
  len = append_overlay_ascii(cells, len, cap, "IME ",
                             TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
  len = append_overlay_ascii(cells, len, cap, mode,
                             TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
  if (ime_conversion_active(&app->ime) == 0) {
    if (app->ime.preedit_len > 0) {
      len = append_overlay_ascii(cells, len, cap, " ",
                                 TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
      len = append_overlay_ascii(cells, len, cap, ime_preedit(&app->ime),
                                 TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
    }
    return len;
  }

  p = count;
  p = metric_append_uint(p, (u_int32_t)(ime_candidate_index(&app->ime) + 1));
  *p++ = '/';
  p = metric_append_uint(p, (u_int32_t)ime_candidate_count(&app->ime));
  *p = '\0';

  len = append_overlay_ascii(cells, len, cap, " ",
                             TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
  len = append_overlay_ascii(cells, len, cap, count,
                             TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
  page_total = ime_candidate_page_count(&app->ime);
  if (page_total > 1) {
    p = page;
    *p++ = ' ';
    *p++ = 'P';
    p = metric_append_uint(p,
                           (u_int32_t)(ime_candidate_page_index(&app->ime) + 1));
    *p++ = '/';
    p = metric_append_uint(p, (u_int32_t)page_total);
    *p = '\0';
    len = append_overlay_ascii(cells, len, cap, page,
                               TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
  }
  reading = ime_reading(&app->ime);
  if (reading[0] != '\0') {
    len = append_overlay_ascii(cells, len, cap, " [",
                               TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
    len = append_overlay_utf8(cells, len, cap, reading,
                              TERM_IME_READING_FG, TERM_IME_READING_BG, 0);
    len = append_overlay_ascii(cells, len, cap, "]",
                               TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
  }
  page_start = ime_candidate_page_start(&app->ime);
  page_end = page_start + IME_CANDIDATE_PAGE_SIZE;
  if (page_end > ime_candidate_count(&app->ime))
    page_end = ime_candidate_count(&app->ime);
  for (i = page_start; i < page_end; i++) {
    unsigned char fg = TERM_COLOR_BLACK;
    unsigned char bg = TERM_COLOR_LIGHT_GRAY;

    if (i == page_start)
      len = append_overlay_ascii(cells, len, cap, " ",
                                 TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
    else
      len = append_overlay_ascii(cells, len, cap, " / ",
                                 TERM_COLOR_BLACK, TERM_COLOR_LIGHT_GRAY, 0);
    if (i == ime_candidate_index(&app->ime)) {
      fg = TERM_IME_SELECTED_FG;
      bg = TERM_IME_SELECTED_BG;
    }
    len = append_overlay_utf8(cells, len, cap, app->ime.candidates[i],
                              fg, bg, 0);
  }

  return len;
}

PRIVATE int utf8_display_width(const char *text)
{
  int len;
  int index = 0;
  int total = 0;

  if (text == NULL)
    return 0;

  len = (int)strlen(text);
  while (index < len) {
    u_int32_t codepoint;
    int consumed = 0;
    int width;

    utf8_decode_one(text + index, len - index, &codepoint, &consumed);
    if (consumed <= 0)
      break;
    width = unicode_wcwidth(codepoint);
    if (width <= 0)
      width = 1;
    total += width;
    index += consumed;
  }

  return total;
}

PRIVATE int append_overlay_ascii(struct term_cell *cells, int len, int cap,
                                 const char *text,
                                 unsigned char fg,
                                 unsigned char bg,
                                 unsigned char attr)
{
  if (text == NULL)
    return len;
  while (*text != '\0' && len < cap)
    len = append_overlay_codepoint(cells, len, cap,
                                   (unsigned char)*text++,
                                   fg, bg, attr);
  return len;
}

PRIVATE int append_overlay_utf8(struct term_cell *cells, int len, int cap,
                                const char *text,
                                unsigned char fg,
                                unsigned char bg,
                                unsigned char attr)
{
  int text_len;
  int index = 0;

  if (text == NULL)
    return len;

  text_len = (int)strlen(text);
  while (index < text_len && len < cap) {
    u_int32_t codepoint;
    int consumed = 0;

    utf8_decode_one(text + index, text_len - index, &codepoint, &consumed);
    if (consumed <= 0)
      break;
    len = append_overlay_codepoint(cells, len, cap, codepoint,
                                   fg, bg, attr);
    index += consumed;
  }

  return len;
}

PRIVATE int append_overlay_codepoint(struct term_cell *cells, int len, int cap,
                                     u_int32_t codepoint,
                                     unsigned char fg,
                                     unsigned char bg,
                                     unsigned char attr)
{
  int width;

  if (cells == NULL || len >= cap)
    return len;

  width = unicode_wcwidth(codepoint);
  if (width <= 0)
    width = 1;
  if (len + width > cap)
    return cap;

  cells[len].ch = codepoint;
  cells[len].fg = fg;
  cells[len].bg = bg;
  cells[len].attr = attr;
  cells[len].width = (unsigned char)width;
  len++;

  if (width == 2) {
    cells[len].ch = ' ';
    cells[len].fg = fg;
    cells[len].bg = bg;
    cells[len].attr = attr | TERM_ATTR_CONTINUATION;
    cells[len].width = 1;
    len++;
  }

  return len;
}

PRIVATE void build_ime_overlay_text(struct term_app *app, char *text, int cap)
{
  const char *mode;
  const char *preedit;
  const char *candidate;
  char info[24];
  char *p;
  int pos = 0;
  int tail_len;
  int selected;
  int total;

  if (text == NULL || cap <= 0)
    return;

  memset(text, ' ', (size_t)(cap - 1));
  text[cap - 1] = '\0';

  mode = ime_mode_label(&app->ime);
  preedit = ime_preedit(&app->ime);
  candidate = ime_current_candidate(&app->ime);

  if (cap > 4) {
    memcpy(text + pos, "IME ", 4);
    pos += 4;
  }
  while (pos < cap - 1 && *mode != '\0') {
    text[pos++] = *mode;
    mode++;
  }
  if (ime_conversion_active(&app->ime) != 0) {
    selected = ime_candidate_index(&app->ime) + 1;
    total = ime_candidate_count(&app->ime);
    p = info;
    *p++ = ' ';
    *p++ = 'C';
    p = metric_append_uint(p, (u_int32_t)selected);
    *p++ = '/';
    p = metric_append_uint(p, (u_int32_t)total);
    if (ime_candidate_page_count(&app->ime) > 1) {
      *p++ = ' ';
      *p++ = 'P';
      p = metric_append_uint(p,
                             (u_int32_t)(ime_candidate_page_index(&app->ime) + 1));
      *p++ = '/';
      p = metric_append_uint(p, (u_int32_t)ime_candidate_page_count(&app->ime));
    }
    *p = '\0';
    tail_len = (int)strlen(info);
    if (tail_len > cap - 1 - pos)
      tail_len = cap - 1 - pos;
    if (tail_len > 0) {
      memcpy(text + pos, info, (size_t)tail_len);
      pos += tail_len;
    }
    if (candidate[0] != '\0' && pos < cap - 2) {
      text[pos++] = ' ';
      while (*candidate != '\0' && pos < cap - 1) {
        unsigned char ch = (unsigned char)*candidate;
        if (ch >= 0x20 && ch <= 0x7e)
          text[pos++] = *candidate;
        candidate++;
      }
    }
    return;
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
