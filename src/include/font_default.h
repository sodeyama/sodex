#ifndef _FONT_DEFAULT_H
#define _FONT_DEFAULT_H

#include "font8x16_data.h"
#include "font16x16_data.h"

#define FONT_DEFAULT_NAME "font_default"

static inline int font_default_cell_width(void)
{
  return FONT8X16_WIDTH;
}

static inline int font_default_cell_height(void)
{
  return FONT8X16_HEIGHT;
}

static inline int font_default_wide_width(void)
{
  return FONT16X16_WIDTH;
}

static inline int font_default_pixels_for_cells(int cells)
{
  return FONT8X16_WIDTH * cells;
}

static inline const unsigned char *font_default_narrow_glyph(unsigned int codepoint)
{
  if (codepoint < 32 || codepoint > 126)
    codepoint = '?';
  return font8x16_printable[codepoint - 32];
}

static inline const unsigned int *font_default_wide_glyph(unsigned int codepoint)
{
  int left = 0;
  int right = FONT16X16_GLYPH_COUNT - 1;

  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (codepoint < font16x16_glyphs[mid].codepoint) {
      right = mid - 1;
    } else if (codepoint > font16x16_glyphs[mid].codepoint) {
      left = mid + 1;
    } else {
      return font16x16_glyphs[mid].rows;
    }
  }

  return 0;
}

static inline int font_default_glyph_width(unsigned int codepoint)
{
  if (codepoint >= 32 && codepoint <= 126)
    return 1;
  if (font_default_wide_glyph(codepoint) != 0)
    return 2;
  return 1;
}

#endif /* _FONT_DEFAULT_H */
