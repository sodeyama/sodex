#include "test_framework.h"
#include <utf8.h>

TEST(decoder_handles_ascii_and_multibyte) {
    u_int32_t codepoint;
    int consumed;

    ASSERT_EQ(utf8_decode_one("A", 1, &codepoint, &consumed), 1);
    ASSERT_EQ(codepoint, 'A');
    ASSERT_EQ(consumed, 1);

    ASSERT_EQ(utf8_decode_one("\xe3\x81\x82", 3, &codepoint, &consumed), 1);
    ASSERT_EQ(codepoint, 0x3042);
    ASSERT_EQ(consumed, 3);
}

TEST(decoder_replaces_invalid_sequence) {
    u_int32_t codepoint;
    int consumed;

    ASSERT_EQ(utf8_decode_one("\xe3\x28", 2, &codepoint, &consumed), -1);
    ASSERT_EQ(codepoint, UTF8_REPLACEMENT_CHAR);
    ASSERT_EQ(consumed, 1);
}

TEST(encoder_roundtrips_japanese_codepoint) {
    char buf[4];
    u_int32_t codepoint;
    int consumed;
    int len;

    len = utf8_encode(0x65e5, buf);
    ASSERT_EQ(len, 3);
    ASSERT_EQ(utf8_decode_one(buf, len, &codepoint, &consumed), 1);
    ASSERT_EQ(codepoint, 0x65e5);
    ASSERT_EQ(consumed, 3);
}

TEST(prev_and_next_follow_utf8_boundaries) {
    const char text[] = {'A', (char)0xe3, (char)0x81, (char)0x82, 'B', '\0'};
    int len = sizeof(text) - 1;

    ASSERT_EQ(utf8_next_char_end(text, len, 0), 1);
    ASSERT_EQ(utf8_next_char_end(text, len, 1), 4);
    ASSERT_EQ(utf8_prev_char_start(text, len, 4), 1);
    ASSERT_EQ(utf8_prev_char_start(text, len, 5), 4);
}

int main(void)
{
    printf("=== utf8 tests ===\n");

    RUN_TEST(decoder_handles_ascii_and_multibyte);
    RUN_TEST(decoder_replaces_invalid_sequence);
    RUN_TEST(encoder_roundtrips_japanese_codepoint);
    RUN_TEST(prev_and_next_follow_utf8_boundaries);

    TEST_REPORT();
}
