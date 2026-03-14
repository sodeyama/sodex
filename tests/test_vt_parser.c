#include "test_framework.h"
#include <terminal_surface.h>
#include <vt_parser.h>

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

    TEST_REPORT();
}
