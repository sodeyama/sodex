#include <fb.h>
#include <bga.h>
#include <string.h>

PRIVATE struct fb_info kernel_fb_info;
PRIVATE int fb_init_done = 0;
PRIVATE int fb_init_result = -1;

PRIVATE int fb_bytes_per_pixel(void);
PRIVATE void fb_copy_bytes(u_int8_t *dst, const u_int8_t *src, u_int32_t size);

PRIVATE int fb_bytes_per_pixel(void)
{
  if (kernel_fb_info.bpp == 0)
    return 0;
  return kernel_fb_info.bpp / 8;
}

PRIVATE void fb_copy_bytes(u_int8_t *dst, const u_int8_t *src, u_int32_t size)
{
  u_int32_t i;

  if (dst == src || size == 0)
    return;

  if (dst < src) {
    for (i = 0; i < size; i++)
      dst[i] = src[i];
  } else {
    for (i = size; i > 0; i--)
      dst[i - 1] = src[i - 1];
  }
}

PUBLIC int fb_init(void)
{
  if (fb_init_done != 0)
    return fb_init_result;

  memset(&kernel_fb_info, 0, sizeof(kernel_fb_info));
  fb_init_done = 1;
  if (bga_init(&kernel_fb_info) < 0) {
    fb_init_result = -1;
    return -1;
  }

  kernel_fb_info.available = TRUE;
  fb_init_result = 0;
  return 0;
}

PUBLIC int fb_is_available(void)
{
  return kernel_fb_info.available;
}

PUBLIC const struct fb_info *fb_get_info(void)
{
  return &kernel_fb_info;
}

PUBLIC void fb_putpixel(int x, int y, u_int32_t color)
{
  int bytes_per_pixel;
  u_int8_t *pixel;

  if (kernel_fb_info.available == 0 || kernel_fb_info.base == NULL)
    return;
  if (x < 0 || y < 0 || x >= kernel_fb_info.width || y >= kernel_fb_info.height)
    return;

  bytes_per_pixel = fb_bytes_per_pixel();
  if (bytes_per_pixel != 4)
    return;

  pixel = (u_int8_t *)kernel_fb_info.base + y * kernel_fb_info.pitch +
          x * bytes_per_pixel;
  *(u_int32_t *)pixel = color;
}

PUBLIC void fb_fillrect(int x, int y, int width, int height, u_int32_t color)
{
  int row;
  int col;

  if (kernel_fb_info.available == 0 || kernel_fb_info.base == NULL)
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
  if (x + width > kernel_fb_info.width)
    width = kernel_fb_info.width - x;
  if (y + height > kernel_fb_info.height)
    height = kernel_fb_info.height - y;
  if (width <= 0 || height <= 0)
    return;

  for (row = 0; row < height; row++) {
    for (col = 0; col < width; col++)
      fb_putpixel(x + col, y + row, color);
  }
}

PUBLIC void fb_blit(int dst_x, int dst_y, int src_x, int src_y,
                    int width, int height)
{
  int bytes_per_pixel;
  int row;
  int start;
  int end;
  int step;
  int row_width;

  if (kernel_fb_info.available == 0 || kernel_fb_info.base == NULL)
    return;
  if (width <= 0 || height <= 0)
    return;

  if (dst_x < 0) {
    src_x -= dst_x;
    width += dst_x;
    dst_x = 0;
  }
  if (src_x < 0) {
    dst_x -= src_x;
    width += src_x;
    src_x = 0;
  }
  if (dst_y < 0) {
    src_y -= dst_y;
    height += dst_y;
    dst_y = 0;
  }
  if (src_y < 0) {
    dst_y -= src_y;
    height += src_y;
    src_y = 0;
  }
  if (dst_x + width > kernel_fb_info.width)
    width = kernel_fb_info.width - dst_x;
  if (src_x + width > kernel_fb_info.width)
    width = kernel_fb_info.width - src_x;
  if (dst_y + height > kernel_fb_info.height)
    height = kernel_fb_info.height - dst_y;
  if (src_y + height > kernel_fb_info.height)
    height = kernel_fb_info.height - src_y;
  if (width <= 0 || height <= 0)
    return;

  bytes_per_pixel = fb_bytes_per_pixel();
  if (bytes_per_pixel <= 0)
    return;

  if (dst_y < src_y) {
    start = 0;
    end = height;
    step = 1;
  } else {
    start = height - 1;
    end = -1;
    step = -1;
  }

  row_width = width * bytes_per_pixel;
  for (row = start; row != end; row += step) {
    u_int8_t *dst;
    const u_int8_t *src;

    dst = (u_int8_t *)kernel_fb_info.base +
          (dst_y + row) * kernel_fb_info.pitch +
          dst_x * bytes_per_pixel;
    src = (u_int8_t *)kernel_fb_info.base +
          (src_y + row) * kernel_fb_info.pitch +
          src_x * bytes_per_pixel;
    fb_copy_bytes(dst, src, row_width);
  }
}

PUBLIC void fb_clear(u_int32_t color)
{
  fb_fillrect(0, 0, kernel_fb_info.width, kernel_fb_info.height, color);
}

PUBLIC void fb_flush(void)
{
}
