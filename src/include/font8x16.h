#ifndef _FONT8X16_H
#define _FONT8X16_H

#include <font8x16_data.h>

static inline const unsigned char *font8x16_glyph(unsigned char ch)
{
  if (ch < 32 || ch > 126)
    ch = '?';
  return font8x16_printable[ch - 32];
}

#endif /* _FONT8X16_H */
