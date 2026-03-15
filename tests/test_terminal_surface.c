#include "test_framework.h"
#include <terminal_surface.h>

TEST(write_char_wraps_and_scrolls) {
    struct terminal_surface surface;

    ASSERT_EQ(terminal_surface_init(&surface, 3, 2), 0);
    terminal_surface_clear_damage(&surface);

    terminal_surface_write_char(&surface, 'a', NULL);
    terminal_surface_write_char(&surface, 'b', NULL);
    terminal_surface_write_char(&surface, 'c', NULL);
    terminal_surface_write_char(&surface, 'd', NULL);
    terminal_surface_write_char(&surface, 'e', NULL);
    terminal_surface_write_char(&surface, 'f', NULL);
    terminal_surface_write_char(&surface, 'g', NULL);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'd');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'e');
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, 'f');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 1)->ch, 'g');
    ASSERT_EQ(surface.cursor_col, 1);
    ASSERT_EQ(surface.cursor_row, 1);
    ASSERT(terminal_surface_has_damage(&surface));

    terminal_surface_free(&surface);
}

TEST(put_cell_marks_only_changed_damage) {
    struct terminal_surface surface;
    struct term_cell cell;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 2), 0);
    terminal_surface_clear_damage(&surface);

    cell.ch = 'Z';
    cell.fg = TERM_COLOR_RED;
    cell.bg = TERM_COLOR_BLACK;
    cell.attr = 0;
    cell.width = 1;
    terminal_surface_put_cell(&surface, 2, 1, &cell);

    ASSERT_EQ(surface.dirty_count, 1);
    ASSERT(terminal_surface_is_dirty(&surface, 2, 1));
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 1)->ch, 'Z');
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 1)->fg, TERM_COLOR_RED);

    terminal_surface_free(&surface);
}

TEST(resize_preserves_overlap_and_marks_full_redraw) {
    struct terminal_surface surface;
    struct term_cell cell;

    ASSERT_EQ(terminal_surface_init(&surface, 2, 2), 0);
    terminal_surface_clear_damage(&surface);

    cell.ch = 'A';
    cell.fg = TERM_COLOR_GREEN;
    cell.bg = TERM_COLOR_BLACK;
    cell.attr = 0;
    cell.width = 1;
    terminal_surface_put_cell(&surface, 0, 0, &cell);
    cell.ch = 'B';
    terminal_surface_put_cell(&surface, 1, 1, &cell);

    ASSERT_EQ(terminal_surface_resize(&surface, 4, 3, NULL), 0);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'A');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 1)->ch, 'B');
    ASSERT_EQ(surface.dirty_count, 12);

    terminal_surface_free(&surface);
}

TEST(write_wide_codepoint_occupies_two_cells) {
    struct terminal_surface surface;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 2), 0);
    terminal_surface_clear_damage(&surface);

    terminal_surface_write_codepoint(&surface, 0x3042, 2, NULL);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 0x3042);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->width, 2);
    ASSERT(terminal_surface_cell(&surface, 1, 0)->attr & TERM_ATTR_CONTINUATION);
    ASSERT_EQ(surface.cursor_col, 2);
    ASSERT_EQ(surface.cursor_row, 0);

    terminal_surface_free(&surface);
}

TEST(writing_last_column_waits_for_next_character_before_scrolling) {
    struct terminal_surface surface;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 2), 0);
    terminal_surface_clear_damage(&surface);

    terminal_surface_write_char(&surface, 'X', NULL);
    terminal_surface_set_cursor(&surface, 0, 1);
    terminal_surface_write_char(&surface, 'a', NULL);
    terminal_surface_write_char(&surface, 'b', NULL);
    terminal_surface_write_char(&surface, 'c', NULL);
    terminal_surface_write_char(&surface, 'd', NULL);

    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'X');
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 1)->ch, 'a');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 1)->ch, 'b');
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 1)->ch, 'c');
    ASSERT_EQ(terminal_surface_cell(&surface, 3, 1)->ch, 'd');
    ASSERT_EQ(surface.cursor_col, 3);
    ASSERT_EQ(surface.cursor_row, 1);

    terminal_surface_set_cursor(&surface, 1, 0);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'X');
    ASSERT_EQ(surface.cursor_col, 1);
    ASSERT_EQ(surface.cursor_row, 0);

    terminal_surface_free(&surface);
}

TEST(alternate_buffer_preserves_primary_screen) {
    struct terminal_surface surface;

    ASSERT_EQ(terminal_surface_init(&surface, 4, 2), 0);
    terminal_surface_write_char(&surface, 'm', NULL);
    terminal_surface_write_char(&surface, 'a', NULL);
    terminal_surface_write_char(&surface, 'i', NULL);
    terminal_surface_write_char(&surface, 'n', NULL);
    terminal_surface_set_cursor(&surface, 1, 1);

    terminal_surface_enter_alternate(&surface, NULL);
    ASSERT(terminal_surface_is_alternate(&surface));
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, ' ');
    terminal_surface_write_char(&surface, 'v', NULL);
    terminal_surface_write_char(&surface, 'i', NULL);

    terminal_surface_leave_alternate(&surface);
    ASSERT(!terminal_surface_is_alternate(&surface));
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'm');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 0)->ch, 'a');
    ASSERT_EQ(terminal_surface_cell(&surface, 2, 0)->ch, 'i');
    ASSERT_EQ(terminal_surface_cell(&surface, 3, 0)->ch, 'n');
    ASSERT_EQ(surface.cursor_col, 1);
    ASSERT_EQ(surface.cursor_row, 1);

    terminal_surface_free(&surface);
}

int main(void)
{
  printf("=== terminal surface tests ===\n");

    RUN_TEST(write_char_wraps_and_scrolls);
    RUN_TEST(put_cell_marks_only_changed_damage);
    RUN_TEST(resize_preserves_overlap_and_marks_full_redraw);
    RUN_TEST(write_wide_codepoint_occupies_two_cells);
    RUN_TEST(writing_last_column_waits_for_next_character_before_scrolling);
    RUN_TEST(alternate_buffer_preserves_primary_screen);

    TEST_REPORT();
}
