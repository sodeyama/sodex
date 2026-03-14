#include "test_framework.h"
#include <cell_renderer.h>
#include <font16x16.h>

TEST(init_computes_grid_size) {
    struct cell_renderer renderer;
    struct fb_info info;
    u_int32_t pixels[16 * 32];

    info.available = 1;
    info.width = 16;
    info.height = 32;
    info.pitch = 16 * 4;
    info.bpp = 32;
    info.size = sizeof(pixels);
    info.base = pixels;

    ASSERT_EQ(cell_renderer_init(&renderer, &info), 0);
    ASSERT_EQ(renderer.cols, 2);
    ASSERT_EQ(renderer.rows, 2);
}

TEST(draw_cell_paints_foreground_and_background) {
    struct cell_renderer renderer;
    struct fb_info info;
    struct term_cell cell;
    u_int32_t pixels[8 * 16];
    int i;
    int saw_fg = 0;
    int saw_bg = 0;

    info.available = 1;
    info.width = 8;
    info.height = 16;
    info.pitch = 8 * 4;
    info.bpp = 32;
    info.size = sizeof(pixels);
    info.base = pixels;

    ASSERT_EQ(cell_renderer_init(&renderer, &info), 0);
    cell.ch = 'A';
    cell.fg = TERM_COLOR_GREEN;
    cell.bg = TERM_COLOR_BLACK;
    cell.attr = 0;
    cell.width = 1;
    cell_renderer_draw_cell(&renderer, 0, 0, &cell, 0);

    for (i = 0; i < (int)(sizeof(pixels) / sizeof(pixels[0])); i++) {
        if (pixels[i] == 0x00aa00)
            saw_fg = 1;
        if (pixels[i] == 0x000000)
            saw_bg = 1;
    }

    ASSERT(saw_fg);
    ASSERT(saw_bg);
}

TEST(cursor_swaps_foreground_and_background) {
    struct cell_renderer renderer;
    struct fb_info info;
    struct term_cell cell;
    u_int32_t pixels[8 * 16];
    int i;

    info.available = 1;
    info.width = 8;
    info.height = 16;
    info.pitch = 8 * 4;
    info.bpp = 32;
    info.size = sizeof(pixels);
    info.base = pixels;

    ASSERT_EQ(cell_renderer_init(&renderer, &info), 0);
    cell.ch = ' ';
    cell.fg = TERM_COLOR_WHITE;
    cell.bg = TERM_COLOR_BLUE;
    cell.attr = 0;
    cell.width = 1;
    cell_renderer_draw_cell(&renderer, 0, 0, &cell, 1);

    for (i = 0; i < (int)(sizeof(pixels) / sizeof(pixels[0])); i++) {
        ASSERT_EQ(pixels[i], 0xffffff);
    }
}

TEST(draws_wide_utf8_glyph_from_font_pack) {
    struct cell_renderer renderer;
    struct fb_info info;
    struct term_cell cell;
    u_int32_t pixels[16 * 32];
    const unsigned int *glyph;
    int i;
    int saw_fg = 0;

    info.available = 1;
    info.width = 16;
    info.height = 16;
    info.pitch = 16 * 4;
    info.bpp = 32;
    info.size = sizeof(pixels);
    info.base = pixels;

    glyph = font16x16_glyph(0x3042);
    ASSERT_NOT_NULL(glyph);
    ASSERT_EQ(glyph[3], 0x0600);

    ASSERT_EQ(cell_renderer_init(&renderer, &info), 0);
    cell.ch = 0x3042;
    cell.fg = TERM_COLOR_GREEN;
    cell.bg = TERM_COLOR_BLACK;
    cell.attr = 0;
    cell.width = 2;
    cell_renderer_draw_cell(&renderer, 0, 0, &cell, 0);

    ASSERT_EQ(pixels[0], 0x000000);
    for (i = 0; i < (int)(sizeof(pixels) / sizeof(pixels[0])); i++) {
        if (pixels[i] == 0x00aa00)
            saw_fg = 1;
    }
    ASSERT(saw_fg);
}

int main(void)
{
    printf("=== cell renderer tests ===\n");

    RUN_TEST(init_computes_grid_size);
    RUN_TEST(draw_cell_paints_foreground_and_background);
    RUN_TEST(cursor_swaps_foreground_and_background);
    RUN_TEST(draws_wide_utf8_glyph_from_font_pack);

    TEST_REPORT();
}
