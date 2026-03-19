#include "test_framework.h"
#include <ime.h>
#include <utf8.h>
#include <string.h>

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

static void assert_codepoints(const char *text, int len,
                              const unsigned int *expected, int expected_len)
{
    int index = 0;
    int i;

    for (i = 0; i < expected_len; i++) {
        unsigned int codepoint = 0;
        int consumed = 0;

        ASSERT(index < len);
        ASSERT_EQ(utf8_decode_one(text + index, len - index, &codepoint, &consumed), 1);
        ASSERT_EQ(codepoint, expected[i]);
        index += consumed;
    }
    ASSERT_EQ(index, len);
}

TEST(nihonngo_becomes_hiragana) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int len;
    const unsigned int expected[] = {0x306B, 0x307B, 0x3093, 0x3054};

    ime_init(&ime);
    ime_cycle_mode(&ime);
    len = feed_text(&ime, "nihonngo", out);
    ASSERT_EQ(len, 12);
    ASSERT_STR_EQ(ime_preedit(&ime), "");
    assert_codepoints(out, len, expected, 4);
}

TEST(katakana_mode_uses_same_romaji_table) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int len;
    const unsigned int expected[] = {0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA};

    ime_init(&ime);
    ime_cycle_mode(&ime);
    ime_cycle_mode(&ime);
    len = feed_text(&ime, "aiueo", out);
    ASSERT_EQ(len, 15);
    assert_codepoints(out, len, expected, 5);
}

TEST(double_consonant_becomes_small_tsu) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int len;
    const unsigned int expected[] = {0x304D, 0x3063, 0x3066};

    ime_init(&ime);
    ime_cycle_mode(&ime);
    len = feed_text(&ime, "kitte", out);
    ASSERT_EQ(len, 9);
    assert_codepoints(out, len, expected, 3);
}

TEST(double_n_before_consonant_becomes_n) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int len;
    const unsigned int expected[] = {0x3066, 0x3093, 0x304D};

    ime_init(&ime);
    ime_cycle_mode(&ime);
    len = feed_text(&ime, "tennki", out);
    ASSERT_EQ(len, 9);
    ASSERT_STR_EQ(ime_preedit(&ime), "");
    assert_codepoints(out, len, expected, 3);
}

TEST(double_n_before_vowel_keeps_n_row) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int len;
    const unsigned int expected[] = {0x3093, 0x306A};

    ime_init(&ime);
    ime_cycle_mode(&ime);
    len = feed_text(&ime, "nna", out);
    ASSERT_EQ(len, 6);
    ASSERT_STR_EQ(ime_preedit(&ime), "");
    assert_codepoints(out, len, expected, 2);
}

TEST(single_n_flushes_to_n) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];
    int len;
    const unsigned int expected[] = {0x3093};

    ime_init(&ime);
    ime_cycle_mode(&ime);
    len = ime_feed_ascii(&ime, 'n', out, sizeof(out));
    ASSERT_EQ(len, 0);
    ASSERT_STR_EQ(ime_preedit(&ime), "n");

    len = ime_flush(&ime, out, sizeof(out));
    ASSERT_EQ(len, 3);
    ASSERT_STR_EQ(ime_preedit(&ime), "");
    assert_codepoints(out, len, expected, 1);
}

TEST(backspace_removes_pending_preedit) {
    struct ime_state ime;
    char out[TEST_BUF_SIZE];

    ime_init(&ime);
    ime_cycle_mode(&ime);
    ASSERT_EQ(ime_feed_ascii(&ime, 'n', out, sizeof(out)), 0);
    ASSERT_STR_EQ(ime_preedit(&ime), "n");
    ASSERT_EQ(ime_backspace(&ime), 1);
    ASSERT_STR_EQ(ime_preedit(&ime), "");
    ASSERT_EQ(ime_backspace(&ime), 0);
}

TEST(set_mode_switches_directly) {
    struct ime_state ime;

    ime_init(&ime);
    ASSERT_STR_EQ(ime_mode_label(&ime), "LATN");

    ime_set_mode(&ime, IME_MODE_HIRAGANA);
    ASSERT_STR_EQ(ime_mode_label(&ime), "HIRA");

    ime_set_mode(&ime, IME_MODE_KATAKANA);
    ASSERT_STR_EQ(ime_mode_label(&ime), "KATA");

    ime_set_mode(&ime, IME_MODE_LATIN);
    ASSERT_STR_EQ(ime_mode_label(&ime), "LATN");
}

int main(void)
{
    printf("=== ime romaji tests ===\n");

    RUN_TEST(nihonngo_becomes_hiragana);
    RUN_TEST(katakana_mode_uses_same_romaji_table);
    RUN_TEST(double_consonant_becomes_small_tsu);
    RUN_TEST(double_n_before_consonant_becomes_n);
    RUN_TEST(double_n_before_vowel_keeps_n_row);
    RUN_TEST(single_n_flushes_to_n);
    RUN_TEST(backspace_removes_pending_preedit);
    RUN_TEST(set_mode_switches_directly);

    TEST_REPORT();
}
