#include "test_framework.h"
#include <font_default.h>

TEST(ascii_metrics_match_default_pack) {
    const unsigned int *glyph = font_default_narrow_glyph('A');

    ASSERT_NOT_NULL(glyph);
    ASSERT_EQ(font_default_cell_width(), 12);
    ASSERT_EQ(font_default_cell_height(), 24);
    ASSERT_EQ(font_default_pixels_for_cells(2), 24);
}

TEST(lowercase_i_keeps_visible_dot) {
    const unsigned int *glyph = font_default_narrow_glyph('i');

    ASSERT_NOT_NULL(glyph);
    ASSERT(glyph[4] != 0 || glyph[5] != 0 || glyph[6] != 0);
    ASSERT_EQ(glyph[7], 0);
    ASSERT(glyph[8] != 0 || glyph[9] != 0);
}

TEST(wide_lookup_finds_hiragana) {
    const unsigned int *glyph = font_default_wide_glyph(0x3042);

    ASSERT_NOT_NULL(glyph);
    ASSERT_EQ(font_default_glyph_width(0x3042), 2);
    ASSERT(glyph[4] != 0);
}

TEST(wide_lookup_finds_demo_kanji) {
    const unsigned int *glyph = font_default_wide_glyph(0x65e5);

    ASSERT_NOT_NULL(glyph);
    ASSERT_EQ(font_default_glyph_width(0x65e5), 2);
    ASSERT(glyph[4] != 0);
}

TEST(expanded_ime_kanji_have_wide_glyphs) {
    ASSERT_NOT_NULL(font_default_wide_glyph(0x6771));
    ASSERT_NOT_NULL(font_default_wide_glyph(0x4eac));
    ASSERT_NOT_NULL(font_default_wide_glyph(0x5929));
    ASSERT_NOT_NULL(font_default_wide_glyph(0x6c17));
    ASSERT_EQ(font_default_glyph_width(0x6771), 2);
    ASSERT_EQ(font_default_glyph_width(0x4eac), 2);
    ASSERT_EQ(font_default_glyph_width(0x5929), 2);
    ASSERT_EQ(font_default_glyph_width(0x6c17), 2);
}

TEST(missing_wide_glyph_falls_back_to_single_cell) {
    ASSERT_NULL(font_default_wide_glyph(0x1f600));
    ASSERT_EQ(font_default_glyph_width(0x1f600), 1);
}

int main(void)
{
    printf("=== font default tests ===\n");

    RUN_TEST(ascii_metrics_match_default_pack);
    RUN_TEST(lowercase_i_keeps_visible_dot);
    RUN_TEST(wide_lookup_finds_hiragana);
    RUN_TEST(wide_lookup_finds_demo_kanji);
    RUN_TEST(expanded_ime_kanji_have_wide_glyphs);
    RUN_TEST(missing_wide_glyph_falls_back_to_single_cell);

    TEST_REPORT();
}
