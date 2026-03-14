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
    return blank;
  }

  blank.ch = ' ';
  blank.fg = TERM_COLOR_LIGHT_GRAY;
  blank.bg = TERM_COLOR_BLACK;
  blank.attr = 0;
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

int terminal_surface_init(struct terminal_surface *surface, int cols, int rows)
{
  size_t total;
  struct term_cell blank;

  if (surface == NULL || cols <= 0 || rows <= 0)
    return -1;

  memset(surface, 0, sizeof(struct terminal_surface));
  total = (size_t)(cols * rows);

  surface->cells = (struct term_cell *)malloc(sizeof(struct term_cell) * total);
  surface->dirty = (unsigned char *)malloc(total);
  if (surface->cells == NULL || surface->dirty == NULL) {
    terminal_surface_free(surface);
    return -1;
  }

  surface->cols = cols;
  surface->rows = rows;
  blank = terminal_surface_blank_cell(NULL);
  terminal_surface_reset(surface, &blank);
  return 0;
}

void terminal_surface_free(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;

  if (surface->cells != NULL)
    free(surface->cells);
  if (surface->dirty != NULL)
    free(surface->dirty);
  memset(surface, 0, sizeof(struct terminal_surface));
}

int terminal_surface_resize(struct terminal_surface *surface, int cols, int rows,
                            const struct term_cell *fill)
{
  size_t total;
  struct term_cell blank;
  struct term_cell *new_cells;
  unsigned char *new_dirty;
  int copy_cols;
  int copy_rows;
  int row;
  int col;

  if (surface == NULL || cols <= 0 || rows <= 0)
    return -1;

  total = (size_t)(cols * rows);
  new_cells = (struct term_cell *)malloc(sizeof(struct term_cell) * total);
  new_dirty = (unsigned char *)malloc(total);
  if (new_cells == NULL || new_dirty == NULL) {
    if (new_cells != NULL)
      free(new_cells);
    if (new_dirty != NULL)
      free(new_dirty);
    return -1;
  }

  blank = terminal_surface_blank_cell(fill);
  for (row = 0; row < rows; row++) {
    for (col = 0; col < cols; col++) {
      new_cells[row * cols + col] = blank;
      new_dirty[row * cols + col] = 1;
    }
  }

  copy_cols = surface->cols < cols ? surface->cols : cols;
  copy_rows = surface->rows < rows ? surface->rows : rows;
  for (row = 0; row < copy_rows; row++) {
    for (col = 0; col < copy_cols; col++) {
      new_cells[row * cols + col] = surface->cells[row * surface->cols + col];
    }
  }

  if (surface->cells != NULL)
    free(surface->cells);
  if (surface->dirty != NULL)
    free(surface->dirty);

  surface->cells = new_cells;
  surface->dirty = new_dirty;
  surface->cols = cols;
  surface->rows = rows;
  surface->cursor_col = terminal_surface_clamp(surface->cursor_col, 0, cols - 1);
  surface->cursor_row = terminal_surface_clamp(surface->cursor_row, 0, rows - 1);
  surface->saved_col = terminal_surface_clamp(surface->saved_col, 0, cols - 1);
  surface->saved_row = terminal_surface_clamp(surface->saved_row, 0, rows - 1);
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
  surface->saved_col = 0;
  surface->saved_row = 0;
  terminal_surface_clear(surface, fill);
}

void terminal_surface_clear(struct terminal_surface *surface,
                            const struct term_cell *fill)
{
  struct term_cell blank;
  int row;
  int col;

  if (surface == NULL || surface->cells == NULL)
    return;

  blank = terminal_surface_blank_cell(fill);
  for (row = 0; row < surface->rows; row++) {
    for (col = 0; col < surface->cols; col++) {
      int index = terminal_surface_index(surface, col, row);
      surface->cells[index] = blank;
      surface->dirty[index] = 1;
    }
  }
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
}

void terminal_surface_restore_cursor(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;
  terminal_surface_set_cursor(surface, surface->saved_col, surface->saved_row);
}

void terminal_surface_scroll_up(struct terminal_surface *surface, int lines,
                                const struct term_cell *fill)
{
  struct term_cell blank;
  int row;
  int col;

  if (surface == NULL || surface->cells == NULL || lines <= 0)
    return;

  if (lines >= surface->rows) {
    terminal_surface_clear(surface, fill);
    return;
  }

  blank = terminal_surface_blank_cell(fill);
  for (row = 0; row < surface->rows - lines; row++) {
    for (col = 0; col < surface->cols; col++) {
      int dst = terminal_surface_index(surface, col, row);
      int src = terminal_surface_index(surface, col, row + lines);
      surface->cells[dst] = surface->cells[src];
      surface->dirty[dst] = 1;
    }
  }
  for (row = surface->rows - lines; row < surface->rows; row++) {
    for (col = 0; col < surface->cols; col++) {
      int index = terminal_surface_index(surface, col, row);
      surface->cells[index] = blank;
      surface->dirty[index] = 1;
    }
  }
  surface->dirty_count = surface->cols * surface->rows;
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
  if (next.ch == 0)
    next.ch = ' ';

  index = terminal_surface_index(surface, col, row);
  if (surface->cells[index].ch == next.ch &&
      surface->cells[index].fg == next.fg &&
      surface->cells[index].bg == next.bg &&
      surface->cells[index].attr == next.attr) {
    return;
  }

  surface->cells[index] = next;
  terminal_surface_mark_dirty(surface, index);
}

void terminal_surface_write_char(struct terminal_surface *surface,
                                 char ch, const struct term_cell *style)
{
  struct term_cell cell;

  if (surface == NULL || surface->cols <= 0 || surface->rows <= 0)
    return;

  cell = terminal_surface_blank_cell(style);
  cell.ch = (ch == 0) ? ' ' : ch;
  terminal_surface_put_cell(surface,
                            surface->cursor_col,
                            surface->cursor_row,
                            &cell);

  if (surface->cursor_col >= surface->cols - 1) {
    surface->cursor_col = 0;
    if (surface->cursor_row >= surface->rows - 1) {
      terminal_surface_scroll_up(surface, 1, style);
      surface->cursor_row = surface->rows - 1;
    } else {
      surface->cursor_row++;
    }
    return;
  }

  surface->cursor_col++;
}

void terminal_surface_newline(struct terminal_surface *surface,
                              const struct term_cell *fill)
{
  if (surface == NULL)
    return;

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
  surface->cursor_col = 0;
}

void terminal_surface_backspace(struct terminal_surface *surface)
{
  if (surface == NULL)
    return;
  if (surface->cursor_col > 0)
    surface->cursor_col--;
}

void terminal_surface_tab(struct terminal_surface *surface,
                          const struct term_cell *style)
{
  int next_stop;

  if (surface == NULL)
    return;

  next_stop = ((surface->cursor_col / TERM_TAB_WIDTH) + 1) * TERM_TAB_WIDTH;
  while (surface->cursor_col < next_stop) {
    terminal_surface_write_char(surface, ' ', style);
    if (surface->cursor_col == 0)
      break;
  }
}
