#ifndef _USR_TERMINAL_SURFACE_H
#define _USR_TERMINAL_SURFACE_H

#include <sys/types.h>

#define TERM_COLOR_BLACK         0
#define TERM_COLOR_BLUE          1
#define TERM_COLOR_GREEN         2
#define TERM_COLOR_CYAN          3
#define TERM_COLOR_RED           4
#define TERM_COLOR_MAGENTA       5
#define TERM_COLOR_BROWN         6
#define TERM_COLOR_LIGHT_GRAY    7
#define TERM_COLOR_DARK_GRAY     8
#define TERM_COLOR_LIGHT_BLUE    9
#define TERM_COLOR_LIGHT_GREEN   10
#define TERM_COLOR_LIGHT_CYAN    11
#define TERM_COLOR_LIGHT_RED     12
#define TERM_COLOR_LIGHT_MAGENTA 13
#define TERM_COLOR_YELLOW        14
#define TERM_COLOR_WHITE         15

#define TERM_ATTR_BOLD    0x01
#define TERM_ATTR_REVERSE 0x02
#define TERM_ATTR_CONTINUATION 0x04

#define TERM_TAB_WIDTH 8

struct term_cell {
  u_int32_t ch;
  unsigned char fg;
  unsigned char bg;
  unsigned char attr;
  unsigned char width;
};

struct terminal_surface {
  int cols;
  int rows;
  int cursor_col;
  int cursor_row;
  int wrap_pending;
  int saved_col;
  int saved_row;
  int saved_wrap_pending;
  struct term_cell *cells;
  struct term_cell *primary_cells;
  struct term_cell *alternate_cells;
  unsigned char *dirty;
  unsigned char *primary_dirty;
  unsigned char *alternate_dirty;
  int dirty_count;
  int scroll_count;
  int alternate_active;
  int primary_cursor_col;
  int primary_cursor_row;
  int primary_wrap_pending;
  int primary_saved_col;
  int primary_saved_row;
  int primary_saved_wrap_pending;
  int scroll_top;     /* top row of scroll region (0-based, inclusive) */
  int scroll_bottom;  /* bottom row of scroll region (0-based, inclusive) */
};

int terminal_surface_init(struct terminal_surface *surface, int cols, int rows);
void terminal_surface_free(struct terminal_surface *surface);
int terminal_surface_resize(struct terminal_surface *surface, int cols, int rows,
                            const struct term_cell *fill);
void terminal_surface_reset(struct terminal_surface *surface,
                            const struct term_cell *fill);
void terminal_surface_clear(struct terminal_surface *surface,
                            const struct term_cell *fill);
void terminal_surface_clear_region(struct terminal_surface *surface,
                                   int left, int top, int right, int bottom,
                                   const struct term_cell *fill);
void terminal_surface_mark_all_dirty(struct terminal_surface *surface);
void terminal_surface_clear_damage(struct terminal_surface *surface);
int terminal_surface_has_damage(const struct terminal_surface *surface);
int terminal_surface_is_dirty(const struct terminal_surface *surface,
                              int col, int row);
const struct term_cell *terminal_surface_cell(const struct terminal_surface *surface,
                                              int col, int row);
void terminal_surface_set_cursor(struct terminal_surface *surface,
                                 int col, int row);
void terminal_surface_move_cursor(struct terminal_surface *surface,
                                  int dcol, int drow);
void terminal_surface_save_cursor(struct terminal_surface *surface);
void terminal_surface_restore_cursor(struct terminal_surface *surface);
void terminal_surface_enter_alternate(struct terminal_surface *surface,
                                      const struct term_cell *fill);
void terminal_surface_leave_alternate(struct terminal_surface *surface);
int terminal_surface_is_alternate(const struct terminal_surface *surface);
void terminal_surface_set_scroll_region(struct terminal_surface *surface,
                                        int top, int bottom);
void terminal_surface_scroll_up(struct terminal_surface *surface, int lines,
                                const struct term_cell *fill);
void terminal_surface_put_cell(struct terminal_surface *surface,
                               int col, int row,
                               const struct term_cell *cell);
void terminal_surface_write_codepoint(struct terminal_surface *surface,
                                      u_int32_t codepoint, int width,
                                      const struct term_cell *style);
void terminal_surface_write_char(struct terminal_surface *surface,
                                 char ch, const struct term_cell *style);
void terminal_surface_newline(struct terminal_surface *surface,
                              const struct term_cell *fill);
void terminal_surface_carriage_return(struct terminal_surface *surface);
void terminal_surface_backspace(struct terminal_surface *surface);
void terminal_surface_tab(struct terminal_surface *surface,
                          const struct term_cell *style);

#endif /* _USR_TERMINAL_SURFACE_H */
