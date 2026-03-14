#include <display/fb_backend.h>
#include <fb.h>
#include <font8x16.h>

PRIVATE void fb_backend_put_cell(struct display_backend *backend,
                                 int x, int y, char color, char c);
PRIVATE void fb_backend_clear(struct display_backend *backend, char color);
PRIVATE void fb_backend_scroll_up(struct display_backend *backend, char color);
PRIVATE void fb_backend_flush(struct display_backend *backend);
PRIVATE u_int32_t fb_backend_color(char color, int foreground);

PRIVATE const u_int32_t fb_palette[16] = {
  0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
  0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
  0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
  0xff5555, 0xff55ff, 0xffff55, 0xffffff
};

PRIVATE const struct display_backend_ops fb_backend_ops = {
  fb_backend_put_cell,
  fb_backend_clear,
  fb_backend_scroll_up,
  fb_backend_flush
};

PRIVATE u_int32_t fb_backend_color(char color, int foreground)
{
  int index;

  if (foreground != 0)
    index = color & 0x0f;
  else
    index = (color >> 4) & 0x0f;
  return fb_palette[index];
}

PRIVATE void fb_backend_put_cell(struct display_backend *backend,
                                 int x, int y, char color, char c)
{
  const unsigned char *glyph;
  u_int32_t fg;
  u_int32_t bg;
  int px;
  int py;
  int cell_x;
  int cell_y;

  (void)backend;
  if (fb_is_available() == 0)
    return;

  cell_x = x * FONT8X16_WIDTH;
  cell_y = y * FONT8X16_HEIGHT;
  fg = fb_backend_color(color, TRUE);
  bg = fb_backend_color(color, FALSE);
  fb_fillrect(cell_x, cell_y, FONT8X16_WIDTH, FONT8X16_HEIGHT, bg);

  if (c == 0)
    c = ' ';
  glyph = font8x16_glyph((unsigned char)c);
  for (py = 0; py < FONT8X16_HEIGHT; py++) {
    for (px = 0; px < FONT8X16_WIDTH; px++) {
      if ((glyph[py] & (1 << (7 - px))) != 0)
        fb_putpixel(cell_x + px, cell_y + py, fg);
    }
  }
}

PRIVATE void fb_backend_clear(struct display_backend *backend, char color)
{
  (void)backend;
  fb_clear(fb_backend_color(color, FALSE));
}

PRIVATE void fb_backend_scroll_up(struct display_backend *backend, char color)
{
  const struct fb_info *info;
  int scroll_pixels;

  (void)backend;
  info = fb_get_info();
  if (info->available == 0)
    return;

  scroll_pixels = FONT8X16_HEIGHT;
  if (info->height <= scroll_pixels) {
    fb_clear(fb_backend_color(color, FALSE));
    return;
  }

  fb_blit(0, 0, 0, scroll_pixels, info->width, info->height - scroll_pixels);
  fb_fillrect(0, info->height - scroll_pixels, info->width, scroll_pixels,
              fb_backend_color(color, FALSE));
}

PRIVATE void fb_backend_flush(struct display_backend *backend)
{
  (void)backend;
  fb_flush();
}

PUBLIC int fb_backend_init(struct display_backend *backend)
{
  const struct fb_info *info;

  if (backend == NULL)
    return -1;
  if (fb_is_available() == 0)
    return -1;

  info = fb_get_info();
  backend->cols = info->width / FONT8X16_WIDTH;
  backend->rows = info->height / FONT8X16_HEIGHT;
  if (backend->cols <= 0 || backend->rows <= 0)
    return -1;
  backend->ctx = NULL;
  backend->ops = &fb_backend_ops;
  return 0;
}
