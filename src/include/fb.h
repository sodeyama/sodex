#ifndef _FB_H
#define _FB_H

#include <sodex/const.h>
#include <sys/types.h>

struct fb_info {
  int available;
  u_int16_t width;
  u_int16_t height;
  u_int16_t pitch;
  u_int16_t bpp;
  u_int32_t size;
  void *base;
};

PUBLIC int fb_init(void);
PUBLIC int fb_is_available(void);
PUBLIC const struct fb_info *fb_get_info(void);
PUBLIC void fb_putpixel(int x, int y, u_int32_t color);
PUBLIC void fb_fillrect(int x, int y, int width, int height, u_int32_t color);
PUBLIC void fb_blit(int dst_x, int dst_y, int src_x, int src_y,
                    int width, int height);
PUBLIC void fb_clear(u_int32_t color);
PUBLIC void fb_flush(void);

#endif /* _FB_H */
