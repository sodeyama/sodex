#include "test_framework.h"
#include <ime.h>
#include <ime_conversion.h>
#include <ime_dictionary.h>
#include <utf8.h>

#define TEST_BLOB_PATH "ime_dictionary_fixture.blob"
#define TEST_BUF_SIZE 256

static int configure_dictionary(void)
{
    return ime_dictionary_set_blob_path(TEST_BLOB_PATH);
}

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

static void assert_clause_reading(const struct ime_state *ime, int clause_index,
                                  const char *expected)
{
    char reading[TEST_BUF_SIZE];

    ASSERT(ime_copy_clause_reading(ime, clause_index, reading, sizeof(reading)) > 0);
    ASSERT_STR_EQ(reading, expected);
}

TEST(start_conversion_uses_blob_reading) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "gakkou", out), 12);
    ASSERT_STR_EQ(ime_reading(&ime), "がっこう");
    ASSERT_EQ(ime_reading_chars(&ime), 4);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_conversion_active(&ime), 1);
    ASSERT_EQ(ime_candidate_count(&ime), 1);
    ASSERT_EQ(ime_candidate_index(&ime), 0);
    ASSERT_EQ(ime_candidate_page_count(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "学校");
}

TEST(candidate_cycle_moves_forward_and_backward) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kisha", out), 9);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "記者");
    ASSERT_EQ(ime_select_next_candidate(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "汽車");
    ASSERT_EQ(ime_select_prev_candidate(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "記者");
}

TEST(candidate_paging_tracks_selected_window) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int i;

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kikou", out), 9);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_candidate_count(&ime), 8);
    ASSERT_EQ(ime_candidate_page_count(&ime), 2);
    ASSERT_EQ(ime_candidate_page_index(&ime), 0);
    ASSERT_EQ(ime_candidate_page_start(&ime), 0);

    for (i = 0; i < 4; i++)
        ASSERT_EQ(ime_select_next_candidate(&ime), 1);

    ASSERT_EQ(ime_candidate_index(&ime), 4);
    ASSERT_EQ(ime_candidate_page_index(&ime), 1);
    ASSERT_EQ(ime_candidate_page_start(&ime), 4);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "起稿");
}

TEST(sentence_conversion_segments_full_reading_and_commits) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int replace_chars = 0;
    int len;

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kanojohatoukyouniiku", out), 39);
    ASSERT_STR_EQ(ime_reading(&ime), "かのじょはとうきょうにいく");
    ASSERT_EQ(ime_reading_chars(&ime), 13);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_conversion_active(&ime), 1);
    ASSERT_EQ(ime_clause_count(&ime), 5);
    ASSERT_EQ(ime_focused_clause_index(&ime), 0);
    assert_clause_reading(&ime, 0, "かのじょ");
    assert_clause_reading(&ime, 1, "は");
    assert_clause_reading(&ime, 2, "とうきょう");
    assert_clause_reading(&ime, 3, "に");
    assert_clause_reading(&ime, 4, "いく");
    ASSERT_STR_EQ(ime_current_candidate(&ime), "彼女");

    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 24);
    ASSERT_EQ(replace_chars, 13);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "彼女は東京に行く");
}

TEST(sentence_conversion_moves_focus_between_clauses) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kanojohatoukyouniiku", out), 39);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_focus_next_clause(&ime), 1);
    ASSERT_EQ(ime_focused_clause_index(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "は");
    ASSERT_EQ(ime_focus_next_clause(&ime), 1);
    ASSERT_EQ(ime_focused_clause_index(&ime), 2);
    assert_clause_reading(&ime, 2, "とうきょう");
    ASSERT_STR_EQ(ime_current_candidate(&ime), "東京");
    ASSERT_EQ(ime_clause_start_char(&ime, 2), 5);
    ASSERT_EQ(ime_clause_end_char(&ime, 2), 10);
}

TEST(sentence_conversion_expands_clause_boundary) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kanojohatoukyouniiku", out), 39);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_focus_next_clause(&ime), 1);
    ASSERT_EQ(ime_focus_next_clause(&ime), 1);
    ASSERT_EQ(ime_focused_clause_index(&ime), 2);
    ASSERT_EQ(ime_expand_clause_right(&ime), 1);
    ASSERT_EQ(ime_clause_count(&ime), 4);
    ASSERT_EQ(ime_focused_clause_index(&ime), 2);
    assert_clause_reading(&ime, 2, "とうきょうに");
    ASSERT_STR_EQ(ime_current_candidate(&ime), "とうきょうに");
    assert_clause_reading(&ime, 3, "いく");
}

TEST(common_basic_term_converts_from_expanded_dictionary) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int replace_chars = 0;
    int len;

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "tsukau", out), 9);
    ASSERT_STR_EQ(ime_reading(&ime), "つかう");
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(replace_chars, 3);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "使う");
}

TEST(common_city_and_weather_terms_convert_from_expanded_dictionary) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int replace_chars = 0;
    int len;

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "toukyou", out), 15);
    ASSERT_STR_EQ(ime_reading(&ime), "とうきょう");
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "東京");
    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(replace_chars, 5);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "東京");

    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "tennki", out), 9);
    ASSERT_STR_EQ(ime_reading(&ime), "てんき");
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "天気");
    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(replace_chars, 3);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "天気");
}

TEST(common_large_dictionary_term_converts_from_mozc_source) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int replace_chars = 0;
    int len;

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "shushou", out), 15);
    ASSERT_STR_EQ(ime_reading(&ime), "しゅしょう");
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_STR_EQ(ime_current_candidate(&ime), "首相");
    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(replace_chars, 5);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "首相");
}

TEST(commit_conversion_returns_replace_count_and_clears_segment) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int replace_chars = 0;
    int len;

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "gakkou", out), 12);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    len = ime_commit_conversion(&ime, out, sizeof(out), &replace_chars);
    ASSERT_EQ(len, 6);
    ASSERT_EQ(replace_chars, 4);
    out[len] = '\0';
    ASSERT_STR_EQ(out, "学校");
    ASSERT_STR_EQ(ime_reading(&ime), "");
    ASSERT_EQ(ime_conversion_active(&ime), 0);
}

TEST(backspace_drops_last_reading_char_and_cancels_conversion) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ASSERT_EQ(configure_dictionary(), 0);
    ime_init(&ime);
    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_EQ(feed_text(&ime, "kannji", out), 9);
    ASSERT_EQ(ime_start_conversion(&ime), 1);
    ASSERT_EQ(ime_drop_last_reading_char(&ime), 1);
    ASSERT_EQ(ime_conversion_active(&ime), 0);
    ASSERT_STR_EQ(ime_reading(&ime), "かん");
    ASSERT_EQ(ime_reading_chars(&ime), 2);
}

int main(void)
{
    printf("=== ime conversion tests ===\n");

    RUN_TEST(start_conversion_uses_blob_reading);
    RUN_TEST(candidate_cycle_moves_forward_and_backward);
    RUN_TEST(candidate_paging_tracks_selected_window);
    RUN_TEST(sentence_conversion_segments_full_reading_and_commits);
    RUN_TEST(sentence_conversion_moves_focus_between_clauses);
    RUN_TEST(sentence_conversion_expands_clause_boundary);
    RUN_TEST(common_basic_term_converts_from_expanded_dictionary);
    RUN_TEST(common_city_and_weather_terms_convert_from_expanded_dictionary);
    RUN_TEST(common_large_dictionary_term_converts_from_mozc_source);
    RUN_TEST(commit_conversion_returns_replace_count_and_clears_segment);
    RUN_TEST(backspace_drops_last_reading_char_and_cancels_conversion);

    TEST_REPORT();
}
