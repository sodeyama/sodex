#include <fb.h>
#include <bga.h>
#include <string.h>

PRIVATE struct fb_info kernel_fb_info;
PRIVATE int fb_init_done = 0;
PRIVATE int fb_init_result = -1;

PRIVATE int fb_bytes_per_pixel(void);
PRIVATE void fb_copy_bytes(u_int8_t *dst, const u_int8_t *src, u_int32_t size);
PRIVATE int fb_info_valid(void);
PRIVATE int fb_span_valid(const void *ptr, u_int32_t size);

#define FB_MAPPED_WINDOW_BYTES 0x400000U

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
  if (fb_span_valid(dst, size) == FALSE || fb_span_valid(src, size) == FALSE)
    return;

  if (dst < src) {
    for (i = 0; i < size; i++)
      dst[i] = src[i];
  } else {
    for (i = size; i > 0; i--)
      dst[i - 1] = src[i - 1];
  }
}

PRIVATE int fb_info_valid(void)
{
  u_int32_t base;
  u_int32_t end;

  if (kernel_fb_info.available == 0 || kernel_fb_info.base == NULL)
    return FALSE;
  if (kernel_fb_info.width == 0 || kernel_fb_info.height == 0 ||
      kernel_fb_info.pitch == 0 || kernel_fb_info.bpp == 0 ||
      kernel_fb_info.size == 0)
    return FALSE;

  base = (u_int32_t)kernel_fb_info.base;
  end = base + kernel_fb_info.size;
  if (base < BGA_FB_VADDR || base >= BGA_FB_VADDR + FB_MAPPED_WINDOW_BYTES)
    return FALSE;
  if (end < base || end > BGA_FB_VADDR + FB_MAPPED_WINDOW_BYTES)
    return FALSE;
  return TRUE;
}

PRIVATE int fb_span_valid(const void *ptr, u_int32_t size)
{
  u_int32_t start;
  u_int32_t end;
  u_int32_t fb_base;
  u_int32_t fb_end;

  if (fb_info_valid() == FALSE)
    return FALSE;
  if (size == 0)
    return TRUE;

  start = (u_int32_t)ptr;
  end = start + size;
  fb_base = (u_int32_t)kernel_fb_info.base;
  fb_end = fb_base + kernel_fb_info.size;
  if (end < start)
    return FALSE;
  if (start < fb_base || end > fb_end)
    return FALSE;
  return TRUE;
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
  if (fb_info_valid() == FALSE) {
    kernel_fb_info.available = FALSE;
    return FALSE;
  }
  return TRUE;
}

PUBLIC const struct fb_info *fb_get_info(void)
{
  if (fb_info_valid() == FALSE)
    kernel_fb_info.available = FALSE;
  return &kernel_fb_info;
}

PUBLIC void fb_putpixel(int x, int y, u_int32_t color)
{
  int bytes_per_pixel;
  u_int32_t offset;
  u_int8_t *pixel;

  if (fb_is_available() == FALSE)
    return;
  if (x < 0 || y < 0 || x >= kernel_fb_info.width || y >= kernel_fb_info.height)
    return;

  bytes_per_pixel = fb_bytes_per_pixel();
  if (bytes_per_pixel != 4)
    return;

  offset = (u_int32_t)y * kernel_fb_info.pitch + (u_int32_t)x * bytes_per_pixel;
  if (offset > kernel_fb_info.size - (u_int32_t)bytes_per_pixel)
    return;

  pixel = (u_int8_t *)kernel_fb_info.base + offset;
  if (fb_span_valid(pixel, (u_int32_t)bytes_per_pixel) == FALSE)
    return;
  *(u_int32_t *)pixel = color;
}

PUBLIC void fb_fillrect(int x, int y, int width, int height, u_int32_t color)
{
  int row;
  int col;

  if (fb_is_available() == FALSE)
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
  u_int32_t dst_offset;
  u_int32_t row_width;
  u_int32_t src_offset;

  if (fb_is_available() == FALSE)
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

  row_width = (u_int32_t)width * (u_int32_t)bytes_per_pixel;
  if (row_width == 0 || row_width > kernel_fb_info.size)
    return;

  for (row = start; row != end; row += step) {
    u_int8_t *dst;
    const u_int8_t *src;

    dst_offset = (u_int32_t)(dst_y + row) * kernel_fb_info.pitch +
                 (u_int32_t)dst_x * (u_int32_t)bytes_per_pixel;
    src_offset = (u_int32_t)(src_y + row) * kernel_fb_info.pitch +
                 (u_int32_t)src_x * (u_int32_t)bytes_per_pixel;
    if (dst_offset > kernel_fb_info.size || src_offset > kernel_fb_info.size)
      return;

    dst = (u_int8_t *)kernel_fb_info.base + dst_offset;
    src = (u_int8_t *)kernel_fb_info.base + src_offset;
    if (fb_span_valid(dst, row_width) == FALSE ||
        fb_span_valid(src, row_width) == FALSE)
      return;
    fb_copy_bytes(dst, src, row_width);
  }
}

PUBLIC void fb_clear(u_int32_t color)
{
  fb_fillrect(0, 0, kernel_fb_info.width, kernel_fb_info.height, color);
}

PUBLIC void fb_flush(void)
{
  if (fb_is_available() == FALSE)
    return;
  bga_refresh();
}
