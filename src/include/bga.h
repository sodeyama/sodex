#ifndef _BGA_H
#define _BGA_H

#include <sodex/const.h>
#include <fb.h>

#define BGA_FB_VADDR 0xFC000000

PUBLIC int bga_init(struct fb_info *info);
PUBLIC void bga_refresh(void);

#endif /* _BGA_H */
