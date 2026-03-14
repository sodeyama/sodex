#ifndef _DISPLAY_VGA_TEXT_BACKEND_H
#define _DISPLAY_VGA_TEXT_BACKEND_H

#include <sodex/const.h>
#include <display_backend.h>

#define VGA_TEXT_DEFAULT_COLS 80
#define VGA_TEXT_DEFAULT_ROWS 25

EXTERN int vga_text_backend_cols;
EXTERN int vga_text_backend_rows;
EXTERN char *vga_text_backend_vram;

PUBLIC void vga_text_backend_init(struct display_backend *backend);

#ifdef TEST_BUILD
PUBLIC char vga_text_backend_peek_char(int x, int y);
PUBLIC char vga_text_backend_peek_color(int x, int y);
#endif

#endif /* _DISPLAY_VGA_TEXT_BACKEND_H */
