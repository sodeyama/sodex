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
    RUN_TEST(command_parser_accepts_minimum_commands);

    TEST_REPORT();
}
