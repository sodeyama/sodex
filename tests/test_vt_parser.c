#include "test_framework.h"
#include <terminal_surface.h>
#include <vt_parser.h>
#include <stdio.h>
#include <string.h>

TEST(plain_text_and_newline) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 2), 0);
    vt_parser_init(&parser, &surface);
    terminal_surface_clear_damage(&surface);

    vt_parser_feed(&parser, "ab\nc", 4);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'a');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'b');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 1)->ch, 'c');
    ASSERT_EQ(surface.cursor_col, 1);
    ASSERT_EQ(surface.cursor_row, 1);

    terminal_surface_free(&surface);
}

TEST(clear_and_home_sequence) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 5, 2), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, "hello", 5);
    vt_parser_feed(&parser, "\x1b[2J\x1b[H", 7);
    vt_parser_feed(&parser, "Z", 1);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'Z');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, ' ');
    ASSERT_EQ(surface.cursor_col, 1);
    ASSERT_EQ(surface.cursor_row, 0);

    terminal_surface_free(&surface);
}

TEST(sgr_changes_colors_and_resets) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 1), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, "\x1b[31mR\x1b[0mN", 11);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'R');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->fg, TERM_COLOR_RED);
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'N');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->fg, TERM_COLOR_LIGHT_GRAY);

    terminal_surface_free(&surface);
}

TEST(cursor_move_save_restore_and_erase_line) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 8, 1), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, "abcd", 4);
    vt_parser_feed(&parser, "\x1b[sXY\x1b[u\x1b[2D\x1b[K", 16);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'a');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'b');
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, ' ');
    ASSERT_EQ(terminal_surface_cell(&surface, 3, 0)->ch, ' ');
    ASSERT_EQ(terminal_surface_cell(&surface, 4, 0)->ch, ' ');
    ASSERT_EQ(terminal_surface_cell(&surface, 5, 0)->ch, ' ');

    terminal_surface_free(&surface);
}

TEST(backspace_space_backspace_erases_previous_cell) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 1), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, "a\b \b", 4);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, ' ');
    ASSERT_EQ(surface.cursor_col, 0);
    ASSERT_EQ(surface.cursor_row, 0);

    terminal_surface_free(&surface);
}

TEST(utf8_wide_text_uses_two_cells) {
    struct terminal_surface surface;
    struct vt_parser parser;
    const char data[] = {(char)0xe3, (char)0x81, (char)0x82, 'B'};

    ASSERT_EQ(terminal_surface_init(&surface, 4, 1), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, data, sizeof(data));

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 0x3042);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->width, 2);
    ASSERT(terminal_surface_cell(&surface, 1, 0)->attr & TERM_ATTR_CONTINUATION);
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, 'B');
    ASSERT_EQ(surface.cursor_col, 3);

    terminal_surface_free(&surface);
}

TEST(wide_backspace_erases_only_last_character) {
    struct terminal_surface surface;
    struct vt_parser parser;
    const char data[] = {
        (char)0xe3, (char)0x81, (char)0x82,
        (char)0xe3, (char)0x81, (char)0x84,
        '\b', ' ', '\b', '\b', ' ', '\b'
    };

    ASSERT_EQ(terminal_surface_init(&surface, 6, 1), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, data, sizeof(data));

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 0x3042);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->width, 2);
    ASSERT(terminal_surface_cell(&surface, 1, 0)->attr & TERM_ATTR_CONTINUATION);
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, ' ');
    ASSERT_EQ(terminal_surface_cell(&surface, 3, 0)->ch, ' ');
    ASSERT_EQ(surface.cursor_col, 2);
    ASSERT_EQ(surface.cursor_row, 0);

    terminal_surface_free(&surface);
}

TEST(erase_line_after_wide_text_keeps_existing_prefix) {
    struct terminal_surface surface;
    struct vt_parser parser;
    const char data[] = {
        '\x1b', '[', '2', 'J',
        '\x1b', '[', 'H',
        (char)0xe3, (char)0x81, (char)0x86,
        (char)0xe3, (char)0x81, (char)0x86,
        (char)0xe3, (char)0x81, (char)0x86,
        '\x1b', '[', 'K'
    };

    ASSERT_EQ(terminal_surface_init(&surface, 16, 4), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, data, sizeof(data));

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 0x3046);
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, 0x3046);
    ASSERT_EQ(terminal_surface_cell(&surface, 4, 0)->ch, 0x3046);
    ASSERT_EQ(surface.cursor_row, 0);
    ASSERT_EQ(surface.cursor_col, 6);

    terminal_surface_free(&surface);
}

TEST(full_vi_redraw_keeps_utf8_first_line) {
    struct terminal_surface surface;
    struct vt_parser parser;
    int row;
    char seq[32];
    const char init_seq[] = "\x1b[0m\x1b[2J\x1b[H";
    const char first_row_seq[] = "\x1b[1;1H";
    const char status_seq[] = "\x1b[48;1H\x1b[7m INSERT hoge.txt [+] insert";
    const char final_cursor_seq[] = "\x1b[0m\x1b[1;7H";
    const char line[] = {(char)0xe3, (char)0x81, (char)0x86,
                         (char)0xe3, (char)0x81, (char)0x86,
                         (char)0xe3, (char)0x81, (char)0x86};

    ASSERT_EQ(terminal_surface_init(&surface, 128, 48), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, init_seq, strlen(init_seq));
    vt_parser_feed(&parser, first_row_seq, strlen(first_row_seq));
    vt_parser_feed(&parser, line, sizeof(line));
    vt_parser_feed(&parser, "\x1b[K", 3);

    for (row = 1; row < 47; row++) {
        snprintf(seq, sizeof(seq), "\x1b[%d;1H\x1b[K", row + 1);
        vt_parser_feed(&parser, seq, strlen(seq));
    }

    vt_parser_feed(&parser, status_seq, strlen(status_seq));
    while (surface.cursor_col < 127) {
        vt_parser_feed(&parser, " ", 1);
    }
    vt_parser_feed(&parser, final_cursor_seq, strlen(final_cursor_seq));

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 0x3046);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->width, 2);
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, 0x3046);
    ASSERT_EQ(terminal_surface_cell(&surface, 4, 0)->ch, 0x3046);
    ASSERT_EQ(surface.cursor_row, 0);
    ASSERT_EQ(surface.cursor_col, 6);

    terminal_surface_free(&surface);
}

TEST(alternate_screen_restores_primary_content) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 8, 2), 0);
    vt_parser_init(&parser, &surface);

    vt_parser_feed(&parser, "prompt", 6);
    vt_parser_feed(&parser, "\x1b[?1049h", 8);
    vt_parser_feed(&parser, "vi", 2);
    ASSERT(terminal_surface_is_alternate(&surface));
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'v');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'i');

    vt_parser_feed(&parser, "\x1b[?1049l", 8);

    ASSERT(!terminal_surface_is_alternate(&surface));
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'p');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'r');
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, 'o');
    ASSERT_EQ(terminal_surface_cell(&surface, 3, 0)->ch, 'm');
    ASSERT_EQ(terminal_surface_cell(&surface, 4, 0)->ch, 'p');
    ASSERT_EQ(terminal_surface_cell(&surface, 5, 0)->ch, 't');

    terminal_surface_free(&surface);
}

TEST(decstbm_scroll_region_basic) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 10, 5), 0);
    vt_parser_init(&parser, &surface);

    /* Place characters on specific rows using cursor positioning */
    vt_parser_feed(&parser, "\x1b[1;1HA", 7);
    vt_parser_feed(&parser, "\x1b[2;1HB", 7);
    vt_parser_feed(&parser, "\x1b[3;1HC", 7);
    vt_parser_feed(&parser, "\x1b[4;1HD", 7);
    vt_parser_feed(&parser, "\x1b[5;1HE", 7);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'A');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 4)->ch, 'E');

    /* Set scroll region to rows 2-4 (1-indexed: ESC[2;4r) */
    vt_parser_feed(&parser, "\x1b[2;4r", 6);
    /* DECSTBM resets cursor to home */
    ASSERT_EQ(surface.cursor_col, 0);
    ASSERT_EQ(surface.cursor_row, 0);

    /* Verify scroll region is set correctly */
    ASSERT_EQ(surface.scroll_top, 1);
    ASSERT_EQ(surface.scroll_bottom, 3);

    terminal_surface_free(&surface);
}

TEST(decstbm_scroll_within_region) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 10, 5), 0);
    vt_parser_init(&parser, &surface);

    /* Fill rows with identifiable content */
    vt_parser_feed(&parser, "\x1b[1;1H", 6);
    vt_parser_feed(&parser, "1\n2\n3\n4\n5", 9);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, '1');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 1)->ch, '2');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 2)->ch, '3');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 3)->ch, '4');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 4)->ch, '5');

    /* Set scroll region rows 2-4 (0-indexed: 1-3) */
    vt_parser_feed(&parser, "\x1b[2;4r", 6);

    /* Move cursor to row 4 (bottom of scroll region) and add newline */
    vt_parser_feed(&parser, "\x1b[4;1H", 6);
    vt_parser_feed(&parser, "X\n", 2);

    /* Row 0 should be untouched (outside scroll region) */
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, '1');

    /* Row 4 should be untouched (outside scroll region) */
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 4)->ch, '5');

    /* Within scroll region: row 1 had '2', should now have '3' (scrolled up) */
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 1)->ch, '3');
    /* Row 2 had '3', now should have 'X' (was row 3 before scroll) */
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 2)->ch, 'X');

    terminal_surface_free(&surface);
}

TEST(decstbm_reset_region) {
    struct terminal_surface surface;
    struct vt_parser parser;

    ASSERT_EQ(terminal_surface_init(&surface, 10, 5), 0);
    vt_parser_init(&parser, &surface);

    /* Set restricted region */
    vt_parser_feed(&parser, "\x1b[2;4r", 6);
    ASSERT_EQ(surface.scroll_top, 1);
    ASSERT_EQ(surface.scroll_bottom, 3);

    /* Reset with ESC[r (no params) → full screen */
    vt_parser_feed(&parser, "\x1b[r", 3);
    ASSERT_EQ(surface.scroll_top, 0);
    ASSERT_EQ(surface.scroll_bottom, 4);

    terminal_surface_free(&surface);
}

int main(void)
{
    printf("=== vt parser tests ===\n");

    RUN_TEST(plain_text_and_newline);
    RUN_TEST(clear_and_home_sequence);
    RUN_TEST(sgr_changes_colors_and_resets);
    RUN_TEST(cursor_move_save_restore_and_erase_line);
    RUN_TEST(backspace_space_backspace_erases_previous_cell);
    RUN_TEST(utf8_wide_text_uses_two_cells);
    RUN_TEST(wide_backspace_erases_only_last_character);
    RUN_TEST(erase_line_after_wide_text_keeps_existing_prefix);
    RUN_TEST(full_vi_redraw_keeps_utf8_first_line);
    RUN_TEST(alternate_screen_restores_primary_content);
    RUN_TEST(decstbm_scroll_region_basic);
    RUN_TEST(decstbm_scroll_within_region);
    RUN_TEST(decstbm_reset_region);

    TEST_REPORT();
}
