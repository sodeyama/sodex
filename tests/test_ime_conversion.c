#include "test_framework.h"
#include <ime.h>
#include <ime_conversion.h>
#include <utf8.h>

#define TEST_BUF_SIZE 128

static int feed_text(struct ime_state *ime, const char *text, char *out)
{
    int total = 0;

    while (*text != '\0') {
        int len = ime_feed_ascii(ime, *text, out + total, TEST_BUF_SIZE - total);
        if (len < 0)
            return -1;
        total += len;
        text++;
    }
    return total;
}

TEST(start_conversion_uses_current_reading) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "nihongo", out), 12);
    ASSERT_STR_EQ(ime_reading(&ime), "にほんご");
    ASSERT_EQ(ime_reading_chars(&ime), 4);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_conversion_active(&ime), 1);
    ASSERT_EQ(ime_candidate_count(&ime), 1);
    ASSERT_EQ(ime_candidate_index(&ime), 0);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "日本語");
}

TEST(candidate_cycle_moves_forward_and_backward) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kanji", out), 9);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "漢字");
    ASSERT_EQ(ime_select_next_candidate(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "感じ");
    ASSERT_EQ(ime_select_prev_candidate(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "漢字");
}

TEST(commit_conversion_returns_replace_count_and_clears_segment) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int replace_chars = 0;
    int len;

    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "nihongo", out), 12);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 9);
    ASSERT_EQ(replace_chars, 4);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "日本語");
    ASSERT_STR_EQ(ime_reading(&ime), "");
    ASSERT_EQ(ime_conversion_active(&ime), 0);
}

TEST(backspace_drops_last_reading_char_and_cancels_conversion) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kanji", out), 9);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_drop_last_reading_char(&ime), 1);
    ASSERT_EQ(ime_conversion_active(&ime), 0);
    ASSERT_STR_EQ(ime_reading(&ime), "かん");
    ASSERT_EQ(ime_reading_chars(&ime), 2);
}

int main(void)
{
    printf("=== ime conversion tests ===\n");

    RUN_TEST(start_conversion_uses_current_reading);
    RUN_TEST(candidate_cycle_moves_forward_and_backward);
    RUN_TEST(commit_conversion_returns_replace_count_and_clears_segment);
    RUN_TEST(backspace_drops_last_reading_char_and_cancels_conversion);

    TEST_REPORT();
}
