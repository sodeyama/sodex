#ifndef _USR_FONT8X16_H
#define _USR_FONT8X16_H

#include <font_default.h>

static inline const unsigned char *font8x16_glyph(unsigned char ch)
{
  return font_default_narrow_glyph(ch);
}

#endif /* _USR_FONT8X16_H */
