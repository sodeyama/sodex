#ifndef _USR_FB_H
#define _USR_FB_H

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

int get_fb_info(struct fb_info *info);
void fb_flush(void);

#endif /* _USR_FB_H */
