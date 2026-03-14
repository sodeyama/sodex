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
    terminal_surface_put_cell(&surface, 0, 0, &cell);
    cell.ch = 'B';
    terminal_surface_put_cell(&surface, 1, 1, &cell);

    ASSERT_EQ(terminal_surface_resize(&surface, 4, 3, NULL), 0);
    ASSERT_EQ(terminal_surface_cell(&surface, 0, 0)->ch, 'A');
    ASSERT_EQ(terminal_surface_cell(&surface, 1, 1)->ch, 'B');
    ASSERT_EQ(surface.dirty_count, 12);

    terminal_surface_free(&surface);
}

int main(void)
{
    printf("=== terminal surface tests ===\n");

    RUN_TEST(write_char_wraps_and_scrolls);
    RUN_TEST(put_cell_marks_only_changed_damage);
    RUN_TEST(resize_preserves_overlap_and_marks_full_redraw);

    TEST_REPORT();
}
