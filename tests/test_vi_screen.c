#include "test_framework.h"
#include <string.h>
#include <vi.h>

static char captured_output[65536];
static int captured_output_len = 0;

ssize_t write(int fd, const void *buf, size_t count)
{
    (void)fd;
    if (captured_output_len + (int)count > (int)sizeof(captured_output))
        count = (size_t)((int)sizeof(captured_output) - captured_output_len);
    if ((int)count > 0) {
        memcpy(captured_output + captured_output_len, buf, count);
        captured_output_len += (int)count;
    }
    return (ssize_t)count;
}

int debug_write(const char *buf, size_t len)
{
    (void)buf;
    return (int)len;
}

static void reset_capture(void)
{
    memset(captured_output, 0, sizeof(captured_output));
    captured_output_len = 0;
}

static int contains_bytes(const char *needle, int needle_len)
{
    int i;

    if (needle == NULL || needle_len <= 0)
        return 0;
    for (i = 0; i + needle_len <= captured_output_len; i++) {
        if (memcmp(captured_output + i, needle, (size_t)needle_len) == 0)
            return 1;
    }
    return 0;
}

TEST(incremental_redraw_avoids_full_clear_after_first_frame) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "alpha\nbeta", 10), 0);

    vi_screen_test_reset_state();
    vi_screen_enter();
    reset_capture();
    vi_screen_redraw(&buffer, VI_MODE_NORMAL, "memo.txt", "normal",
                     "", ':', NULL, 0, 4, 20);
    ASSERT(contains_bytes("\x1b[2J", 4));
    ASSERT(contains_bytes("\x1b[?2026h", 8));

    buffer.cursor_row = 0;
    buffer.cursor_col = vi_buffer_line_length(&buffer, 0);
    ASSERT_EQ(vi_buffer_insert_char(&buffer, '!'), 0);

    reset_capture();
    vi_screen_redraw(&buffer, VI_MODE_NORMAL, "memo.txt", "inserted",
                     "", ':', NULL, 0, 4, 20);
    ASSERT(!contains_bytes("\x1b[2J", 4));
    ASSERT(contains_bytes("\x1b[?2026h", 8));
    ASSERT(captured_output_len < 128);

    vi_screen_restore();
    vi_screen_test_reset_state();
    vi_buffer_free(&buffer);
}

TEST(unchanged_redraw_only_moves_cursor) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "alpha\nbeta", 10), 0);

    vi_screen_test_reset_state();
    vi_screen_enter();
    reset_capture();
    vi_screen_redraw(&buffer, VI_MODE_NORMAL, "memo.txt", "normal",
                     "", ':', NULL, 0, 4, 20);

    reset_capture();
    vi_screen_redraw(&buffer, VI_MODE_NORMAL, "memo.txt", "normal",
                     "", ':', NULL, 0, 4, 20);
    ASSERT(!contains_bytes("\x1b[2J", 4));
    ASSERT(!contains_bytes("\x1b[?2026h", 8));
    ASSERT(captured_output_len > 0);
    ASSERT(captured_output_len < 16);

    vi_screen_restore();
    vi_screen_test_reset_state();
    vi_buffer_free(&buffer);
}

TEST(command_row_redraw_does_not_touch_body_rows) {
    struct vi_buffer buffer;

    ASSERT_EQ(vi_buffer_init(&buffer), 0);
    ASSERT_EQ(vi_buffer_load(&buffer, "alpha\nbeta", 10), 0);

    vi_screen_test_reset_state();
    vi_screen_enter();
    reset_capture();
    vi_screen_redraw(&buffer, VI_MODE_NORMAL, "memo.txt", "normal",
                     "", ':', NULL, 0, 4, 20);

    reset_capture();
    vi_screen_redraw(&buffer, VI_MODE_COMMAND, "memo.txt", "normal",
                     "wq", ':', NULL, 0, 4, 20);
    ASSERT(!contains_bytes("\x1b[2J", 4));
    ASSERT(contains_bytes("\x1b[4;1H", 6));
    ASSERT(!contains_bytes("\x1b[1;1H", 6));
    ASSERT(!contains_bytes("\x1b[2;1H", 6));

    vi_screen_restore();
    vi_screen_test_reset_state();
    vi_buffer_free(&buffer);
}

int main(void)
{
    printf("=== vi screen tests ===\n");

    RUN_TEST(incremental_redraw_avoids_full_clear_after_first_frame);
    RUN_TEST(unchanged_redraw_only_moves_cursor);
    RUN_TEST(command_row_redraw_does_not_touch_body_rows);

    TEST_REPORT();
}
