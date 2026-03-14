#include "test_framework.h"
#include <vi.h>

TEST(insert_and_split_lines) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_insert_char(&buffer, 'a'), 0);
    ASSERT_EQ(vi_buffer_insert_char(&buffer, 'b'), 0);
    ASSERT_EQ(vi_buffer_insert_newline(&buffer), 0);
    ASSERT_EQ(vi_buffer_insert_char(&buffer, 'c'), 0);

    ASSERT_EQ(buffer.line_count, 2);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "ab");
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 1), "c");
    ASSERT_EQ(buffer.cursor_row, 1);
    ASSERT_EQ(buffer.cursor_col, 1);

    vi_buffer_free(&buffer);
}

TEST(backspace_merges_lines) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "ab\ncd", 5), 0);
    buffer.cursor_row = 1;
    buffer.cursor_col = 0;

    ASSERT_EQ(vi_buffer_backspace(&buffer), 0);
    ASSERT_EQ(buffer.line_count, 1);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "abcd");
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 2);

    vi_buffer_free(&buffer);
}

TEST(utf8_cursor_and_backspace_follow_codepoint_boundary) {
    struct vi_buffer buffer;
    const char text[] = {'A', (char)0xe3, (char)0x81, (char)0x82, 'B'};

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, text, sizeof(text)), 0);

    buffer.cursor_col = vi_buffer_line_length(&buffer, 0);
    vi_buffer_move_left(&buffer);
    ASSERT_EQ(buffer.cursor_col, 4);
    vi_buffer_move_left(&buffer);
    ASSERT_EQ(buffer.cursor_col, 1);
    ASSERT_EQ(vi_buffer_cursor_display_col(&buffer), 1);

    buffer.cursor_col = 4;
    ASSERT_EQ(vi_buffer_backspace(&buffer), 0);
    ASSERT_EQ(vi_buffer_line_length(&buffer, 0), 2);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "AB");

    vi_buffer_free(&buffer);
}

TEST(line_motions_cover_home_and_end_variants) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "  abc", 5), 0);

    vi_buffer_move_line_first_nonblank(&buffer);
    ASSERT_EQ(buffer.cursor_col, 2);

    vi_buffer_move_line_last_char(&buffer);
    ASSERT_EQ(buffer.cursor_col, 4);

    vi_buffer_move_line_end(&buffer);
    ASSERT_EQ(buffer.cursor_col, 5);

    vi_buffer_move_line_start(&buffer);
    ASSERT_EQ(buffer.cursor_col, 0);

    vi_buffer_free(&buffer);
}

TEST(word_motions_cross_line_boundaries) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "ab cd\nef", 8), 0);

    vi_buffer_move_word_forward(&buffer);
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 3);

    vi_buffer_move_word_forward(&buffer);
    ASSERT_EQ(buffer.cursor_row, 1);
    ASSERT_EQ(buffer.cursor_col, 0);

    vi_buffer_move_word_end(&buffer);
    ASSERT_EQ(buffer.cursor_row, 1);
    ASSERT_EQ(buffer.cursor_col, 1);

    vi_buffer_move_word_backward(&buffer);
    ASSERT_EQ(buffer.cursor_row, 1);
    ASSERT_EQ(buffer.cursor_col, 0);

    vi_buffer_move_word_backward(&buffer);
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 3);

    vi_buffer_free(&buffer);
}

TEST(delete_commands_keep_utf8_boundaries) {
    struct vi_buffer buffer;
    const char text[] = {
        'A', (char)0xe3, (char)0x81, (char)0x82, 'B', ' ', 'C'
    };

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, text, sizeof(text)), 0);

    buffer.cursor_col = 1;
    ASSERT_EQ(vi_buffer_delete_char(&buffer), 0);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "AB C");
    ASSERT_EQ(buffer.cursor_col, 1);

    buffer.cursor_col = 0;
    ASSERT_EQ(vi_buffer_delete_word_end(&buffer), 0);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), " C");
    ASSERT_EQ(buffer.cursor_col, 0);

    vi_buffer_free(&buffer);
}

TEST(delete_word_and_line_commands_work) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "abc def\nghi", 11), 0);

    ASSERT_EQ(vi_buffer_delete_word_forward(&buffer), 0);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "def");
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 0);

    ASSERT_EQ(vi_buffer_delete_to_line_end(&buffer), 0);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "");

    buffer.cursor_row = 1;
    buffer.cursor_col = 0;
    ASSERT_EQ(vi_buffer_delete_line(&buffer), 0);
    ASSERT_EQ(buffer.line_count, 1);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 0), "");

    vi_buffer_free(&buffer);
}

TEST(open_line_helpers_insert_blank_rows) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "ab\ncd", 5), 0);

    buffer.cursor_row = 1;
    buffer.cursor_col = 0;
    ASSERT_EQ(vi_buffer_open_line_above(&buffer), 0);
    ASSERT_EQ(buffer.line_count, 3);
    ASSERT_EQ(buffer.cursor_row, 1);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 1), "");
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 2), "cd");

    ASSERT_EQ(vi_buffer_open_line_below(&buffer), 0);
    ASSERT_EQ(buffer.line_count, 4);
    ASSERT_EQ(buffer.cursor_row, 2);
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 2), "");
    ASSERT_STR_EQ(vi_buffer_line_data(&buffer, 3), "cd");

    vi_buffer_free(&buffer);
}

TEST(command_parser_accepts_minimum_commands) {
    ASSERT_EQ(vi_parse_command("w"), VI_COMMAND_WRITE);
    ASSERT_EQ(vi_parse_command("q"), VI_COMMAND_QUIT);
    ASSERT_EQ(vi_parse_command("wq"), VI_COMMAND_WRITE_QUIT);
    ASSERT_EQ(vi_parse_command("x"), VI_COMMAND_INVALID);
}

int main(void)
{
    printf("=== vi buffer tests ===\n");

    RUN_TEST(insert_and_split_lines);
    RUN_TEST(backspace_merges_lines);
    RUN_TEST(utf8_cursor_and_backspace_follow_codepoint_boundary);
    RUN_TEST(line_motions_cover_home_and_end_variants);
    RUN_TEST(word_motions_cross_line_boundaries);
    RUN_TEST(delete_commands_keep_utf8_boundaries);
    RUN_TEST(delete_word_and_line_commands_work);
    RUN_TEST(open_line_helpers_insert_blank_rows);
    RUN_TEST(command_parser_accepts_minimum_commands);

    TEST_REPORT();
}
