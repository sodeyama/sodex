#include <font_registry.h>
#include <font_default.h>

PRIVATE struct font_face g_default_font;
PRIVATE int g_font_registry_ready = FALSE;

PUBLIC void font_registry_init(void)
{
  if (g_font_registry_ready != FALSE)
    return;

  /* 生成済み font pack を boot 時の既定フォントとして公開する。 */
  g_default_font.name = FONT_DEFAULT_NAME;
  g_default_font.narrow_width = font_default_cell_width();
  g_default_font.wide_width = font_default_wide_width();
  g_default_font.height = font_default_cell_height();
  g_font_registry_ready = TRUE;
}

PUBLIC const struct font_face *font_registry_default(void)
{
  if (g_font_registry_ready == FALSE)
    font_registry_init();
  return &g_default_font;
}

PUBLIC const unsigned char *font_registry_lookup_narrow(unsigned int codepoint)
{
  return font_default_narrow_glyph(codepoint);
}

PUBLIC const unsigned int *font_registry_lookup_wide(unsigned int codepoint)
{
  return font_default_wide_glyph(codepoint);
}

PUBLIC int font_registry_glyph_width(unsigned int codepoint)
{
  return font_default_glyph_width(codepoint);
}
