#ifndef _USR_VT_PARSER_H
#define _USR_VT_PARSER_H

#include <sys/types.h>
#include <terminal_surface.h>

#define VT_PARSER_MAX_PARAMS 8

struct vt_parser {
  struct terminal_surface *surface;
  struct term_cell pen;
  struct term_cell default_pen;
  int state;
  int params[VT_PARSER_MAX_PARAMS];
  int param_count;
  int param_active;
};

void vt_parser_init(struct vt_parser *parser, struct terminal_surface *surface);
void vt_parser_reset(struct vt_parser *parser);
void vt_parser_feed(struct vt_parser *parser, const char *data, size_t len);

#endif /* _USR_VT_PARSER_H */
