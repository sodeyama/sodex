#include <display/vga_text_backend.h>

#ifndef TEST_BUILD
#include <vga.h>
#define VGA_TEXT_VRAM ((char *)0xc00b8000)
#endif

PRIVATE void vga_text_put_cell(struct display_backend *backend,
                               int x, int y, char color, char c);
PRIVATE void vga_text_clear(struct display_backend *backend, char color);
PRIVATE void vga_text_scroll_up(struct display_backend *backend, char color);
PRIVATE void vga_text_flush(struct display_backend *backend);

PRIVATE const struct display_backend_ops vga_text_backend_ops = {
  vga_text_put_cell,
  vga_text_clear,
  vga_text_scroll_up,
  vga_text_flush
};

PUBLIC int vga_text_backend_cols = VGA_TEXT_DEFAULT_COLS;
PUBLIC int vga_text_backend_rows = VGA_TEXT_DEFAULT_ROWS;
#ifndef TEST_BUILD
PUBLIC char *vga_text_backend_vram = VGA_TEXT_VRAM;
#else
PUBLIC char *vga_text_backend_vram = (char *)0;
#endif

#ifdef TEST_BUILD
PRIVATE char test_chars[VGA_TEXT_DEFAULT_ROWS][VGA_TEXT_DEFAULT_COLS];
PRIVATE char test_colors[VGA_TEXT_DEFAULT_ROWS][VGA_TEXT_DEFAULT_COLS];
#endif

PUBLIC void vga_text_backend_init(struct display_backend *backend)
{
  if (backend == NULL) {
    return;
  }
  backend->cols = vga_text_backend_cols;
  backend->rows = vga_text_backend_rows;
  backend->ctx = NULL;
  backend->ops = &vga_text_backend_ops;
}

PRIVATE void vga_text_put_cell(struct display_backend *backend,
                               int x, int y, char color, char c)
{
  if (backend == NULL) {
    return;
  }
  if (x < 0 || y < 0 || x >= backend->cols || y >= backend->rows) {
    return;
  }
#ifdef TEST_BUILD
  test_chars[y][x] = c;
  test_colors[y][x] = color;
#else
  _poscolor_printc(x, y, color, c);
#endif
}

PRIVATE void vga_text_clear(struct display_backend *backend, char color)
{
  int x;
  int y;

  if (backend == NULL) {
    return;
  }
  for (y = 0; y < backend->rows; ++y) {
    for (x = 0; x < backend->cols; ++x) {
      vga_text_put_cell(backend, x, y, color, 0);
    }
  }
}

PRIVATE void vga_text_scroll_up(struct display_backend *backend, char color)
{
  int x;
  int y;

  if (backend == NULL) {
    return;
  }

#ifdef TEST_BUILD
  for (y = 1; y < backend->rows; ++y) {
    for (x = 0; x < backend->cols; ++x) {
      test_chars[y - 1][x] = test_chars[y][x];
      test_colors[y - 1][x] = test_colors[y][x];
    }
  }
  for (x = 0; x < backend->cols; ++x) {
    test_chars[backend->rows - 1][x] = 0;
    test_colors[backend->rows - 1][x] = color;
  }
#else
  for (y = 1; y < backend->rows; ++y) {
    for (x = 0; x < backend->cols; ++x) {
      int src = 2 * (x + (backend->cols * y));
      int dst = 2 * (x + (backend->cols * (y - 1)));
      vga_text_backend_vram[dst] = vga_text_backend_vram[src];
      vga_text_backend_vram[dst + 1] = vga_text_backend_vram[src + 1];
    }
  }
  for (x = 0; x < backend->cols; ++x) {
    int base = 2 * (x + (backend->cols * (backend->rows - 1)));
    vga_text_backend_vram[base] = 0;
    vga_text_backend_vram[base + 1] = color;
  }
#endif
}

PRIVATE void vga_text_flush(struct display_backend *backend)
{
  (void)backend;
}

#ifdef TEST_BUILD
PUBLIC char vga_text_backend_peek_char(int x, int y)
{
  if (x < 0 || y < 0 ||
      x >= VGA_TEXT_DEFAULT_COLS || y >= VGA_TEXT_DEFAULT_ROWS) {
    return 0;
  }
  return test_chars[y][x];
}

PUBLIC char vga_text_backend_peek_color(int x, int y)
{
  if (x < 0 || y < 0 ||
      x >= VGA_TEXT_DEFAULT_COLS || y >= VGA_TEXT_DEFAULT_ROWS) {
    return 0;
  }
  return test_colors[y][x];
}
#endif
