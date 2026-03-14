#ifndef _DISPLAY_BACKEND_H
#define _DISPLAY_BACKEND_H

#include <sodex/const.h>

struct display_backend;

struct display_backend_ops {
  void (*put_cell)(struct display_backend *backend,
                   int x, int y, char color, char c);
  void (*clear)(struct display_backend *backend, char color);
  void (*scroll_up)(struct display_backend *backend, char color);
  void (*flush)(struct display_backend *backend);
};

struct display_backend {
  int cols;
  int rows;
  void *ctx;
  const struct display_backend_ops *ops;
};

#endif /* _DISPLAY_BACKEND_H */
