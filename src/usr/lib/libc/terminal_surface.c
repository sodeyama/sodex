#include <sodex/const.h>
#include <terminal_surface.h>
#include <string.h>

#ifdef TEST_BUILD
#include <stdlib.h>
#else
#include <malloc.h>
#endif

static int terminal_surface_clamp(int value, int min, int max);
static int terminal_surface_index(const struct terminal_surface *surface,
                                  int col, int row);
static struct term_cell terminal_surface_blank_cell(const struct term_cell *fill);
static void terminal_surface_mark_dirty(struct terminal_surface *surface, int index);
static void terminal_surface_fill_buffer(struct term_cell *cells,
                                         unsigned char *dirty,
                                         int cols, int rows,
                                         const struct term_cell *fill,
                                         int mark_dirty);
static int terminal_surface_resize_buffer(struct term_cell **cells,
                                          unsigned char **dirty,
                                          int old_cols, int old_rows,
                                          int new_cols, int new_rows,
                                          const struct term_cell *fill);
static void terminal_surface_blank_out(struct terminal_surface *surface,
                                       int col, int row,
                                       const struct term_cell *fill);
static void terminal_surface_wrap_for_output(struct terminal_surface *surface,
                                             const struct term_cell *fill);

static int terminal_surface_clamp(int value, int min, int max)
{
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

static int terminal_surface_index(const struct terminal_surface *surface,
                                  int col, int row)
{
  return row * surface->cols + col;
}

static struct term_cell terminal_surface_blank_cell(const struct term_cell *fill)
{
  struct term_cell blank;

  if (fill != NULL) {
    blank = *fill;
    blank.ch = ' ';
    blank.attr &= (unsigned char)(~TERM_ATTR_CONTINUATION);
    blank.width = 1;
    return blank;
  }

  blank.ch = ' ';
  blank.fg = TERM_COLOR_LIGHT_GRAY;
  blank.bg = TERM_COLOR_BLACK;
  blank.attr = 0;
  blank.width = 1;
  return blank;
}

static void terminal_surface_mark_dirty(struct terminal_surface *surface, int index)
{
  if (surface == NULL || surface->dirty == NULL)
    return;
  if (surface->dirty[index] == 0) {
    surface->dirty[index] = 1;
    surface->dirty_count++;
  }
}

static void terminal_surface_fill_buffer(struct term_cell *cells,
                                         unsigned char *dirty,
                                         int cols, int rows,
                                         const struct term_cell *fill,
                                         int mark_dirty)
{
  struct term_cell blank;
  int row;
  int col;

  if (cells == NULL || dirty == NULL || cols <= 0 || rows <= 0)
    return;

  blank = terminal_surface_blank_cell(fill);
  for (row = 0; row < rows; row++) {
    for (col = 0; col < cols; col++) {
      int index = row * cols + col;
      cells[index] = blank;
      dirty[index] = (unsigned char)(mark_dirty != 0 ? 1 : 0);
    }
  }
}

static int terminal_surface_resize_buffer(struct term_cell **cells,
                                          unsigned char **dirty,
                                          int old_cols, int old_rows,
                                          int new_cols, int new_rows,
                                          const struct term_cell *fill)
{
  size_t total;
  struct term_cell *next_cells;
  unsigned char *next_dirty;
  int copy_cols;
  int copy_rows;
  int row;
  int col;

  if (cells == NULL || dirty == NULL || new_cols <= 0 || new_rows <= 0)
    return -1;

  total = (size_t)(new_cols * new_rows);
  next_cells = (struct term_cell *)malloc(sizeof(struct term_cell) * total);
  next_dirty = (unsigned char *)malloc(total);
  if (next_cells == NULL || next_dirty == NULL) {
    if (next_cells != NULL)
      free(next_cells);
    if (next_dirty != NULL)
      free(next_dirty);
    return -1;
  }

  terminal_surface_fill_buffer(next_cells, next_dirty,
                               new_cols, new_rows, fill, TRUE);
  if (*cells != NULL && *dirty != NULL && old_cols > 0 && old_rows > 0) {
    copy_cols = old_cols < new_cols ? old_cols : new_cols;
    copy_rows = old_rows < new_rows ? old_rows : new_rows;
    for (row = 0; row < copy_rows; row++) {
      for (col = 0; col < copy_cols; col++) {
        next_cells[row * new_cols + col] = (*cells)[row * old_cols + col];
      }
    }
  }

  if (*cells != NULL)
    free(*cells);
  if (*dirty != NULL)
    free(*dirty);
  *cells = next_cells;
  *dirty = next_dirty;
  return 0;
}

static void terminal_surface_blank_out(struct terminal_surface *surface,
                                       int col, int row,
                                       const struct term_cell *fill)
{
  int index;
  struct term_cell blank;

  if (surface == NULL || surface->cells == NULL)
    return;
  if (col < 0 || row < 0 || col >= surface->cols || row >= surface->rows)
    return;

  blank = terminal_surface_blank_cell(fill);
  index = terminal_surface_index(surface, col, row);
  if (surface->cells[index].ch == blank.ch &&
      surface->cells[index].fg == blank.fg &&
      surface->cells[index].bg == blank.bg &&
      surface->cells[index].attr == blank.attr &&
      surface->cells[index].width == blank.width) {
    return;
  }

  surface->cells[index] = blank;
  terminal_surface_mark_dirty(surface, index);
}

static void terminal_surface_wrap_for_output(struct terminal_surface *surface,
                                             const struct term_cell *fill)
{
  if (surface == NULL || surface->wrap_pending == 0)
    return;

  surface->wrap_pending = 0;
  surface->cursor_col = 0;
  if (surface->cursor_row >= surface->rows - 1) {
    terminal_surface_scroll_up(surface, 1, fill);
    surface->cursor_row = surface->rows - 1;
  } else {
    surface->cursor_row++;
  }
}

int terminal_surface_init(struct terminal_surface *surface, int cols, int rows)
{
  size_t total;
  struct term_cell blank;

  if (surface == NULL || cols <= 0 || rows <= 0)
    return -1;

  memset(surface, 0, sizeof(struct terminal_surface));
  total = (size_t)(cols * rows);

  surface->primary_cells = (struct term_cell *)malloc(sizeof(struct term_cell) * total);
  surface->primary_dirty = (unsigned char *)malloc(total);
  if (surface->primary_cells == NULL || surface->primary_dirty == NULL) {
    terminal_surface_free(surface);
    return -1;
  }

  surface->cols = cols;
  surface->rows = rows;
  surface->cells = surface->primary_cells;
  surface->dirty = surface->primary_dirty;
  blank = terminal_surface_blank_cell(NULL);
  terminal_surface_reset(surface, &blank);
  return 0;
}

void terminal_surface_free(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;

  if (surface->primary_cells != NULL)
    free(surface->primary_cells);
  if (surface->primary_dirty != NULL)
    free(surface->primary_dirty);
  if (surface->alternate_cells != NULL)
    free(surface->alternate_cells);
  if (surface->alternate_dirty != NULL)
    free(surface->alternate_dirty);
  memset(surface, 0, sizeof(struct terminal_surface));
}

int terminal_surface_resize(struct terminal_surface *surface, int cols, int rows,
                            const struct term_cell *fill)
{
  int old_cols;
  int old_rows;

  if (surface == NULL || cols <= 0 || rows <= 0)
    return -1;

  old_cols = surface->cols;
  old_rows = surface->rows;
  if (terminal_surface_resize_buffer(&surface->primary_cells,
                                     &surface->primary_dirty,
                                     old_cols, old_rows,
                                     cols, rows, fill) < 0) {
    return -1;
  }
  if (surface->alternate_cells != NULL && surface->alternate_dirty != NULL) {
    if (terminal_surface_resize_buffer(&surface->alternate_cells,
                                       &surface->alternate_dirty,
                                       old_cols, old_rows,
                                       cols, rows, fill) < 0) {
      return -1;
    }
  }

  surface->cols = cols;
  surface->rows = rows;
  if (surface->alternate_active != 0) {
    surface->cells = surface->alternate_cells;
    surface->dirty = surface->alternate_dirty;
  } else {
    surface->cells = surface->primary_cells;
    surface->dirty = surface->primary_dirty;
  }
  surface->cursor_col = terminal_surface_clamp(surface->cursor_col, 0, cols - 1);
  surface->cursor_row = terminal_surface_clamp(surface->cursor_row, 0, rows - 1);
  if (surface->cursor_col != cols - 1)
    surface->wrap_pending = 0;
  surface->saved_col = terminal_surface_clamp(surface->saved_col, 0, cols - 1);
  surface->saved_row = terminal_surface_clamp(surface->saved_row, 0, rows - 1);
  if (surface->saved_col != cols - 1)
    surface->saved_wrap_pending = 0;
  surface->primary_cursor_col =
    terminal_surface_clamp(surface->primary_cursor_col, 0, cols - 1);
  surface->primary_cursor_row =
    terminal_surface_clamp(surface->primary_cursor_row, 0, rows - 1);
  if (surface->primary_cursor_col != cols - 1)
    surface->primary_wrap_pending = 0;
  surface->primary_saved_col =
    terminal_surface_clamp(surface->primary_saved_col, 0, cols - 1);
  surface->primary_saved_row =
    terminal_surface_clamp(surface->primary_saved_row, 0, rows - 1);
  if (surface->primary_saved_col != cols - 1)
    surface->primary_saved_wrap_pending = 0;
  surface->dirty_count = cols * rows;

  return 0;
}

void terminal_surface_reset(struct terminal_surface *surface,
                            const struct term_cell *fill)
{
  if (surface == NULL)
    return;

  surface->cursor_col = 0;
  surface->cursor_row = 0;
  surface->wrap_pending = 0;
  surface->saved_col = 0;
  surface->saved_row = 0;
  surface->saved_wrap_pending = 0;
  surface->scroll_count = 0;
  terminal_surface_clear(surface, fill);
}

void terminal_surface_clear(struct terminal_surface *surface,
                            const struct term_cell *fill)
{
  struct term_cell blank;

  if (surface == NULL || surface->cells == NULL)
    return;

  blank = terminal_surface_blank_cell(fill);
  terminal_surface_fill_buffer(surface->cells, surface->dirty,
                               surface->cols, surface->rows,
                               &blank, TRUE);
  surface->dirty_count = surface->cols * surface->rows;
}

void terminal_surface_clear_region(struct terminal_surface *surface,
                                   int left, int top,
                                   int right, int bottom,
                                   const struct term_cell *fill)
{
  struct term_cell blank;
  int row;
  int col;

  if (surface == NULL || surface->cells == NULL)
    return;

  left = terminal_surface_clamp(left, 0, surface->cols - 1);
  right = terminal_surface_clamp(right, 0, surface->cols - 1);
  top = terminal_surface_clamp(top, 0, surface->rows - 1);
  bottom = terminal_surface_clamp(bottom, 0, surface->rows - 1);
  if (left > right || top > bottom)
    return;

  blank = terminal_surface_blank_cell(fill);
  for (row = top; row <= bottom; row++) {
    for (col = left; col <= right; col++) {
      int index = terminal_surface_index(surface, col, row);
      surface->cells[index] = blank;
      terminal_surface_mark_dirty(surface, index);
    }
  }
}

void terminal_surface_mark_all_dirty(struct terminal_surface *surface)
{
  int total;
  int i;

  if (surface == NULL || surface->dirty == NULL)
    return;

  total = surface->cols * surface->rows;
  for (i = 0; i < total; i++) {
    surface->dirty[i] = 1;
  }
  surface->dirty_count = total;
}

void terminal_surface_clear_damage(struct terminal_surface *surface)
{
  int total;
  int i;

  if (surface == NULL || surface->dirty == NULL)
    return;

  total = surface->cols * surface->rows;
  for (i = 0; i < total; i++) {
    surface->dirty[i] = 0;
  }
  surface->dirty_count = 0;
}

int terminal_surface_has_damage(const struct terminal_surface *surface)
{
  if (surface == NULL)
    return 0;
  return surface->dirty_count > 0;
}

int terminal_surface_is_dirty(const struct terminal_surface *surface,
                              int col, int row)
{
  if (surface == NULL || surface->dirty == NULL)
    return 0;
  if (col < 0 || row < 0 || col >= surface->cols || row >= surface->rows)
    return 0;
  return surface->dirty[terminal_surface_index(surface, col, row)] != 0;
}

const struct term_cell *terminal_surface_cell(const struct terminal_surface *surface,
                                              int col, int row)
{
  if (surface == NULL || surface->cells == NULL)
    return NULL;
  if (col < 0 || row < 0 || col >= surface->cols || row >= surface->rows)
    return NULL;
  return &surface->cells[terminal_surface_index(surface, col, row)];
}

void terminal_surface_set_cursor(struct terminal_surface *surface,
                                 int col, int row)
{
  if (surface == NULL || surface->cols <= 0 || surface->rows <= 0)
    return;

  surface->cursor_col = terminal_surface_clamp(col, 0, surface->cols - 1);
  surface->cursor_row = terminal_surface_clamp(row, 0, surface->rows - 1);
  surface->wrap_pending = 0;
}

void terminal_surface_move_cursor(struct terminal_surface *surface,
                                  int dcol, int drow)
{
  if (surface == NULL)
    return;
  terminal_surface_set_cursor(surface,
                              surface->cursor_col + dcol,
                              surface->cursor_row + drow);
}

void terminal_surface_save_cursor(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;
  surface->saved_col = surface->cursor_col;
  surface->saved_row = surface->cursor_row;
  surface->saved_wrap_pending = surface->wrap_pending;
}

void terminal_surface_restore_cursor(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;
  surface->cursor_col = surface->saved_col;
  surface->cursor_row = surface->saved_row;
  surface->wrap_pending = surface->saved_wrap_pending;
}

void terminal_surface_enter_alternate(struct terminal_surface *surface,
                                      const struct term_cell *fill)
{
  size_t total;
  struct term_cell blank;

  if (surface == NULL || surface->cols <= 0 || surface->rows <= 0)
    return;
  if (surface->alternate_active != 0) {
    terminal_surface_reset(surface, fill);
    return;
  }

  if (surface->alternate_cells == NULL || surface->alternate_dirty == NULL) {
    total = (size_t)(surface->cols * surface->rows);
    surface->alternate_cells =
      (struct term_cell *)malloc(sizeof(struct term_cell) * total);
    surface->alternate_dirty = (unsigned char *)malloc(total);
    if (surface->alternate_cells == NULL || surface->alternate_dirty == NULL) {
      if (surface->alternate_cells != NULL) {
        free(surface->alternate_cells);
        surface->alternate_cells = NULL;
      }
      if (surface->alternate_dirty != NULL) {
        free(surface->alternate_dirty);
        surface->alternate_dirty = NULL;
      }
      return;
    }
  }

  surface->primary_cursor_col = surface->cursor_col;
  surface->primary_cursor_row = surface->cursor_row;
  surface->primary_wrap_pending = surface->wrap_pending;
  surface->primary_saved_col = surface->saved_col;
  surface->primary_saved_row = surface->saved_row;
  surface->primary_saved_wrap_pending = surface->saved_wrap_pending;

  blank = terminal_surface_blank_cell(fill);
  terminal_surface_fill_buffer(surface->alternate_cells,
                               surface->alternate_dirty,
                               surface->cols, surface->rows,
                               &blank, TRUE);
  surface->cells = surface->alternate_cells;
  surface->dirty = surface->alternate_dirty;
  surface->alternate_active = 1;
  surface->cursor_col = 0;
  surface->cursor_row = 0;
  surface->wrap_pending = 0;
  surface->saved_col = 0;
  surface->saved_row = 0;
  surface->saved_wrap_pending = 0;
  surface->scroll_count = 0;
  surface->dirty_count = surface->cols * surface->rows;
}

void terminal_surface_leave_alternate(struct terminal_surface *surface)
{
  if (surface == NULL || surface->alternate_active == 0)
    return;

  surface->cells = surface->primary_cells;
  surface->dirty = surface->primary_dirty;
  surface->alternate_active = 0;
  surface->cursor_col = surface->primary_cursor_col;
  surface->cursor_row = surface->primary_cursor_row;
  surface->wrap_pending = surface->primary_wrap_pending;
  surface->saved_col = surface->primary_saved_col;
  surface->saved_row = surface->primary_saved_row;
  surface->saved_wrap_pending = surface->primary_saved_wrap_pending;
  terminal_surface_mark_all_dirty(surface);
}

int terminal_surface_is_alternate(const struct terminal_surface *surface)
{
  if (surface == NULL)
    return 0;
  return surface->alternate_active != 0;
}

void terminal_surface_scroll_up(struct terminal_surface *surface, int lines,
                                const struct term_cell *fill)
{
  struct term_cell blank;
  int row;
  int col;

  if (surface == NULL || surface->cells == NULL || lines <= 0)
    return;

  surface->wrap_pending = 0;

  if (lines >= surface->rows) {
    surface->scroll_count += lines;
    terminal_surface_clear(surface, fill);
    return;
  }

  blank = terminal_surface_blank_cell(fill);
  for (row = 0; row < surface->rows - lines; row++) {
    for (col = 0; col < surface->cols; col++) {
      int dst = terminal_surface_index(surface, col, row);
      int src = terminal_surface_index(surface, col, row + lines);
      surface->cells[dst] = surface->cells[src];
      surface->dirty[dst] = surface->dirty[src];
    }
  }
  for (row = surface->rows - lines; row < surface->rows; row++) {
    for (col = 0; col < surface->cols; col++) {
      int index = terminal_surface_index(surface, col, row);
      surface->cells[index] = blank;
      surface->dirty[index] = 1;
    }
  }
  surface->dirty_count = 0;
  for (row = 0; row < surface->rows; row++) {
    for (col = 0; col < surface->cols; col++) {
      if (surface->dirty[terminal_surface_index(surface, col, row)] != 0)
        surface->dirty_count++;
    }
  }
  surface->scroll_count += lines;
}

void terminal_surface_put_cell(struct terminal_surface *surface,
                               int col, int row,
                               const struct term_cell *cell)
{
  int index;
  struct term_cell blank;
  struct term_cell next;

  if (surface == NULL || surface->cells == NULL)
    return;
  if (col < 0 || row < 0 || col >= surface->cols || row >= surface->rows)
    return;

  blank = terminal_surface_blank_cell(cell);
  next = blank;
  if (cell != NULL)
    next = *cell;
  if ((next.attr & TERM_ATTR_CONTINUATION) != 0) {
    next.ch = 0;
    next.width = 0;
  } else if (next.width == 0) {
    next.width = 1;
  }
  if (next.ch == 0 && (next.attr & TERM_ATTR_CONTINUATION) == 0) {
    next.ch = ' ';
  }

  index = terminal_surface_index(surface, col, row);
  if ((surface->cells[index].attr & TERM_ATTR_CONTINUATION) != 0 && col > 0) {
    terminal_surface_blank_out(surface, col - 1, row, &next);
  } else if (surface->cells[index].width > 1 && col + 1 < surface->cols) {
    terminal_surface_blank_out(surface, col + 1, row, &next);
  }
  if (surface->cells[index].ch == next.ch &&
      surface->cells[index].fg == next.fg &&
      surface->cells[index].bg == next.bg &&
      surface->cells[index].attr == next.attr &&
      surface->cells[index].width == next.width) {
    return;
  }

  surface->cells[index] = next;
  terminal_surface_mark_dirty(surface, index);
}

void terminal_surface_write_codepoint(struct terminal_surface *surface,
                                      u_int32_t codepoint, int width,
                                      const struct term_cell *style)
{
  struct term_cell cell;
  struct term_cell continuation;

  if (surface == NULL || surface->cols <= 0 || surface->rows <= 0)
    return;
  if (width <= 0)
    return;
  if (width > 2)
    width = 1;
  if (width == 2 && surface->cols < 2)
    width = 1;

  terminal_surface_wrap_for_output(surface, style);
  if (width == 2 && surface->cursor_col >= surface->cols - 1)
    terminal_surface_newline(surface, style);

  cell = terminal_surface_blank_cell(style);
  cell.ch = (codepoint == 0) ? ' ' : codepoint;
  cell.width = (unsigned char)width;
  terminal_surface_put_cell(surface,
                            surface->cursor_col,
                            surface->cursor_row,
                            &cell);

  if (width == 2) {
    continuation = terminal_surface_blank_cell(style);
    continuation.ch = 0;
    continuation.attr |= TERM_ATTR_CONTINUATION;
    continuation.width = 0;
    terminal_surface_put_cell(surface,
                              surface->cursor_col + 1,
                              surface->cursor_row,
                              &continuation);
  }

  if (surface->cursor_col >= surface->cols - width) {
    surface->cursor_col = surface->cols - 1;
    surface->wrap_pending = 1;
    return;
  }

  surface->cursor_col += width;
  surface->wrap_pending = 0;
}

void terminal_surface_write_char(struct terminal_surface *surface,
                                 char ch, const struct term_cell *style)
{
  terminal_surface_write_codepoint(surface, (u_int32_t)(unsigned char)ch, 1, style);
}

void terminal_surface_newline(struct terminal_surface *surface,
                              const struct term_cell *fill)
{
  if (surface == NULL)
    return;

  surface->wrap_pending = 0;
  surface->cursor_col = 0;
  if (surface->cursor_row >= surface->rows - 1) {
    terminal_surface_scroll_up(surface, 1, fill);
    surface->cursor_row = surface->rows - 1;
  } else {
    surface->cursor_row++;
  }
}

void terminal_surface_carriage_return(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;
  surface->wrap_pending = 0;
  surface->cursor_col = 0;
}

void terminal_surface_backspace(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;
  if (surface->wrap_pending != 0) {
    surface->wrap_pending = 0;
    return;
  }
  if (surface->cursor_col > 0)
    surface->cursor_col--;
}

void terminal_surface_tab(struct terminal_surface *surface,
                          const struct term_cell *style)
{
  int next_stop;

  if (surface == NULL)
    return;

  terminal_surface_wrap_for_output(surface, style);
  next_stop = ((surface->cursor_col / TERM_TAB_WIDTH) + 1) * TERM_TAB_WIDTH;
  while (surface->cursor_col < next_stop) {
    terminal_surface_write_codepoint(surface, ' ', 1, style);
    if (surface->cursor_col == 0)
      break;
  }
}
