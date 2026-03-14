#ifndef _USR_CELL_RENDERER_H
#define _USR_CELL_RENDERER_H

#include <sys/types.h>
#include <fb.h>
#include <terminal_surface.h>

struct cell_renderer {
  struct fb_info fb;
  int cols;
  int rows;
};

int cell_renderer_init(struct cell_renderer *renderer,
                       const struct fb_info *info);
void cell_renderer_clear(struct cell_renderer *renderer, u_int32_t color);
void cell_renderer_draw_cell(struct cell_renderer *renderer,
                             int col, int row,
                             const struct term_cell *cell,
                             int cursor);

#endif /* _USR_CELL_RENDERER_H */
