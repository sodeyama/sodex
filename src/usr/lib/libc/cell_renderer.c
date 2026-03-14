#include <cell_renderer.h>
#include <font8x16.h>
#include <string.h>

static u_int32_t renderer_palette_color(unsigned char index);
static void renderer_resolve_colors(const struct term_cell *cell,
                                    int cursor,
                                    u_int32_t *fg,
                                    u_int32_t *bg);
static void renderer_putpixel(struct cell_renderer *renderer,
                              int x, int y, u_int32_t color);

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
  renderer->cols = info->width / FONT8X16_WIDTH;
  renderer->rows = info->height / FONT8X16_HEIGHT;
  if (renderer->cols <= 0 || renderer->rows <= 0)
    return -1;

  return 0;
}

void cell_renderer_clear(struct cell_renderer *renderer, u_int32_t color)
{
  int x;
  int y;

  if (renderer == 0 || renderer->fb.base == 0)
    return;

  for (y = 0; y < renderer->fb.height; y++) {
    for (x = 0; x < renderer->fb.width; x++)
      renderer_putpixel(renderer, x, y, color);
  }
}

void cell_renderer_draw_cell(struct cell_renderer *renderer,
                             int col, int row,
                             const struct term_cell *cell,
                             int cursor)
{
  const unsigned char *glyph;
  u_int32_t fg;
  u_int32_t bg;
  unsigned char ch;
  int x;
  int y;
  int cell_x;
  int cell_y;

  if (renderer == 0 || renderer->fb.base == 0)
    return;
  if (col < 0 || row < 0 || col >= renderer->cols || row >= renderer->rows)
    return;

  renderer_resolve_colors(cell, cursor, &fg, &bg);
  cell_x = col * FONT8X16_WIDTH;
  cell_y = row * FONT8X16_HEIGHT;
  ch = ' ';
  if (cell != 0 && cell->ch != 0)
    ch = (unsigned char)cell->ch;
  glyph = font8x16_glyph(ch);

  for (y = 0; y < FONT8X16_HEIGHT; y++) {
    for (x = 0; x < FONT8X16_WIDTH; x++) {
      if ((glyph[y] & (1 << (7 - x))) != 0)
        renderer_putpixel(renderer, cell_x + x, cell_y + y, fg);
      else
        renderer_putpixel(renderer, cell_x + x, cell_y + y, bg);
    }
  }
}
