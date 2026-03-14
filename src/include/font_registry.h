#ifndef _FONT_REGISTRY_H
#define _FONT_REGISTRY_H

#include <sodex/const.h>

struct font_face {
  const char *name;
  int narrow_width;
  int wide_width;
  int height;
};

PUBLIC void font_registry_init(void);
PUBLIC const struct font_face *font_registry_default(void);
PUBLIC const unsigned char *font_registry_lookup_narrow(unsigned int codepoint);
PUBLIC const unsigned int *font_registry_lookup_wide(unsigned int codepoint);
PUBLIC int font_registry_glyph_width(unsigned int codepoint);

#endif /* _FONT_REGISTRY_H */
