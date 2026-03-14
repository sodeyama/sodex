#ifndef _USR_FONT16X16_H
#define _USR_FONT16X16_H

#include <sys/types.h>
#include <font_default.h>

static inline const unsigned int *font16x16_glyph(u_int32_t codepoint)
{
  return font_default_wide_glyph(codepoint);
}

#endif /* _USR_FONT16X16_H */
