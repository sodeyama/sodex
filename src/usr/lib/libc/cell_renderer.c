#include <cell_renderer.h>
#include <font_default.h>
#include <string.h>

static u_int32_t renderer_palette_color(unsigned char index);
static void renderer_resolve_colors(const struct term_cell *cell,
                                    int cursor,
                                    u_int32_t *fg,
                                    u_int32_t *bg);
static u_int32_t *renderer_row_ptr(struct cell_renderer *renderer,
                                   int x, int y);
static void renderer_putpixel(struct cell_renderer *renderer,
                              int x, int y, u_int32_t color);
static void renderer_fill_rect(struct cell_renderer *renderer,
                               int x, int y, int width, int height,
                               u_int32_t color);
static void renderer_draw_placeholder(struct cell_renderer *renderer,
                                      int cell_x, int cell_y, int cell_width,
                                      u_int32_t fg, u_int32_t bg);

static const u_int32_t renderer_palette[16] = {
  0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
  0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
  0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
  0xff5555, 0xff55ff, 0xffff55, 0xffffff
};

static u_int32_t renderer_palette_color(unsigned char index)
{
  return renderer_palette[index & 0x0f];
}

static u_int32_t *renderer_row_ptr(struct cell_renderer *renderer,
                                   int x, int y)
{
  return (u_int32_t *)((u_int8_t *)renderer->fb.base +
                       y * renderer->fb.pitch + x * 4);
}

static void renderer_resolve_colors(const struct term_cell *cell,
                                    int cursor,
                                    u_int32_t *fg,
                                    u_int32_t *bg)
{
  unsigned char fg_index;
  unsigned char bg_index;

  fg_index = TERM_COLOR_LIGHT_GRAY;
  bg_index = TERM_COLOR_BLACK;
  if (cell != 0) {
    fg_index = cell->fg & 0x0f;
    bg_index = cell->bg & 0x0f;
    if ((cell->attr & TERM_ATTR_BOLD) != 0 && fg_index < 8)
      fg_index += 8;
    if ((cell->attr & TERM_ATTR_REVERSE) != 0) {
      unsigned char tmp;
      tmp = fg_index;
      fg_index = bg_index;
      bg_index = tmp;
    }
  }
  if (cursor != 0) {
    unsigned char tmp;
    tmp = fg_index;
    fg_index = bg_index;
    bg_index = tmp;
  }

  *fg = renderer_palette_color(fg_index);
  *bg = renderer_palette_color(bg_index);
}

static void renderer_putpixel(struct cell_renderer *renderer,
                              int x, int y, u_int32_t color)
{
  u_int8_t *pixel;

  if (renderer == 0 || renderer->fb.base == 0)
    return;
  if (x < 0 || y < 0 || x >= renderer->fb.width || y >= renderer->fb.height)
    return;

  pixel = (u_int8_t *)renderer->fb.base + y * renderer->fb.pitch + x * 4;
  *(u_int32_t *)pixel = color;
}

static void renderer_fill_rect(struct cell_renderer *renderer,
                               int x, int y, int width, int height,
                               u_int32_t color)
{
  int py;
  int px;

  if (renderer == 0 || renderer->fb.base == 0 || width <= 0 || height <= 0)
    return;
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (x >= renderer->fb.width || y >= renderer->fb.height)
    return;
  if (x + width > renderer->fb.width)
    width = renderer->fb.width - x;
  if (y + height > renderer->fb.height)
    height = renderer->fb.height - y;
  if (width <= 0 || height <= 0)
    return;

  for (py = 0; py < height; py++) {
    u_int32_t *row = renderer_row_ptr(renderer, x, y + py);

    for (px = 0; px < width; px++)
      row[px] = color;
  }
}

static void renderer_draw_placeholder(struct cell_renderer *renderer,
                                      int cell_x, int cell_y, int cell_width,
                                      u_int32_t fg, u_int32_t bg)
{
  int x;
  int y;
  int cell_height;

  cell_height = font_default_cell_height();
  renderer_fill_rect(renderer, cell_x, cell_y, cell_width, cell_height, bg);
  for (x = 0; x < cell_width; x++) {
    renderer_putpixel(renderer, cell_x + x, cell_y, fg);
    renderer_putpixel(renderer, cell_x + x, cell_y + cell_height - 1, fg);
  }
  for (y = 0; y < cell_height; y++) {
    renderer_putpixel(renderer, cell_x, cell_y + y, fg);
    renderer_putpixel(renderer, cell_x + cell_width - 1, cell_y + y, fg);
  }
}

int cell_renderer_init(struct cell_renderer *renderer,
                       const struct fb_info *info)
{
  if (renderer == 0 || info == 0 || info->available == 0 ||
      info->base == 0 || info->bpp != 32)
    return -1;

  memset(renderer, 0, sizeof(*renderer));
  renderer->fb.available = info->available;
  renderer->fb.width = info->width;
  renderer->fb.height = info->height;
  renderer->fb.pitch = info->pitch;
  renderer->fb.bpp = info->bpp;
  renderer->fb.base = info->base;
  renderer->fb.size = info->size;
  renderer->cols = info->width / font_default_cell_width();
  renderer->rows = info->height / font_default_cell_height();
  if (renderer->cols <= 0 || renderer->rows <= 0)
    return -1;

  return 0;
}

void cell_renderer_clear(struct cell_renderer *renderer, u_int32_t color)
{
  int y;
  int x;

  if (renderer == 0 || renderer->fb.base == 0)
    return;

  for (y = 0; y < renderer->fb.height; y++) {
    u_int32_t *row = renderer_row_ptr(renderer, 0, y);

    for (x = 0; x < renderer->fb.width; x++)
      row[x] = color;
  }
}

void cell_renderer_draw_cell(struct cell_renderer *renderer,
                             int col, int row,
                             const struct term_cell *cell,
                             int cursor)
{
  const unsigned int *glyph;
  const unsigned int *wide_glyph;
  u_int32_t fg;
  u_int32_t bg;
  u_int32_t ch;
  int width;
  int x;
  int y;
  int cell_x;
  int cell_y;
  int cell_width;
  int cell_height;
  int pixel_width;
  int draw_width;
  int draw_height;

  if (renderer == 0 || renderer->fb.base == 0)
    return;
  if (col < 0 || row < 0 || col >= renderer->cols || row >= renderer->rows)
    return;

  renderer_resolve_colors(cell, cursor, &fg, &bg);
  cell_width = font_default_cell_width();
  cell_height = font_default_cell_height();
  cell_x = col * cell_width;
  cell_y = row * cell_height;
  width = 1;
  ch = ' ';
  wide_glyph = 0;
  if (cell != 0) {
    if ((cell->attr & TERM_ATTR_CONTINUATION) != 0) {
      renderer_fill_rect(renderer, cell_x, cell_y,
                         cell_width, cell_height, bg);
      return;
    }
    if (cell->ch != 0)
      ch = cell->ch;
    if (cell->width > 0)
      width = cell->width;
  }

  if (width == 1 && ch >= 32 && ch <= 126)
    glyph = font_default_narrow_glyph(ch);
  else
    glyph = 0;
  if (width == 2)
    wide_glyph = font_default_wide_glyph(ch);

  pixel_width = font_default_pixels_for_cells(width);
  draw_width = pixel_width;
  draw_height = cell_height;
  if (cell_x + draw_width > renderer->fb.width)
    draw_width = renderer->fb.width - cell_x;
  if (cell_y + draw_height > renderer->fb.height)
    draw_height = renderer->fb.height - cell_y;
  if (draw_width <= 0 || draw_height <= 0)
    return;

  renderer_fill_rect(renderer, cell_x, cell_y, draw_width, draw_height, bg);
  for (y = 0; y < draw_height; y++) {
    u_int32_t *row_ptr = renderer_row_ptr(renderer, cell_x, cell_y + y);

    for (x = 0; x < draw_width; x++) {
      if (glyph != 0 && x < cell_width &&
          (glyph[y] & (1U << (cell_width - 1 - x))) != 0)
        row_ptr[x] = fg;
      else if (wide_glyph != 0 &&
               (wide_glyph[y] & (1U << (pixel_width - 1 - x))) != 0)
        row_ptr[x] = fg;
    }
  }

  if (glyph == 0 && wide_glyph == 0)
    renderer_draw_placeholder(renderer, cell_x, cell_y,
                              font_default_pixels_for_cells(width), fg, bg);
}
