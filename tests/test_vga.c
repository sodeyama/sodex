/*
 * Unit tests for _kvsnprintf (kernel format string processing)
 * extracted from src/vga.c
 */
#include "test_framework.h"
#include <stdarg.h>
#include <stdint.h>

/* Types matching kernel */
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;

#define PUBLIC
#define PRIVATE static

/* Function under test */
extern int _kvsnprintf(char *buf, int size, const char *fmt, va_list ap);

/* Helper to call _kvsnprintf with variadic args */
static int test_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = _kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

/* === %s (string) === */

TEST(format_string) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "hello %s", "world");
    ASSERT_STR_EQ(buf, "hello world");
}

TEST(format_string_empty) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%s", "");
    ASSERT_STR_EQ(buf, "");
}

TEST(format_plain_text) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "no format here");
    ASSERT_STR_EQ(buf, "no format here");
}

/* === %c (char) === */

TEST(format_char) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%c", 'A');
    ASSERT_STR_EQ(buf, "A");
}

TEST(format_char_in_string) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "x%cy", 'Z');
    ASSERT_STR_EQ(buf, "xZy");
}

/* === %x (hex) === */

TEST(format_hex_byte) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%x", 0xFF);
    ASSERT_STR_EQ(buf, "FF");
}

TEST(format_hex_byte_zero) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%x", 0x00);
    ASSERT_STR_EQ(buf, "00");
}

TEST(format_hex_word) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%x", 0xABCD);
    ASSERT_STR_EQ(buf, "ABCD");
}

TEST(format_hex_dword) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%x", 0x12345678);
    ASSERT_STR_EQ(buf, "12345678");
}

TEST(format_hex_small) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%x", 0x0A);
    ASSERT_STR_EQ(buf, "0A");
}

/* === %d (decimal) === */

TEST(format_decimal_positive) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%d", 42);
    ASSERT_STR_EQ(buf, "42");
}

TEST(format_decimal_zero) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%d", 0);
    ASSERT_STR_EQ(buf, "0");
}

TEST(format_decimal_negative) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%d", -1);
    ASSERT_STR_EQ(buf, "-1");
}

TEST(format_decimal_large) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%d", 1000000);
    ASSERT_STR_EQ(buf, "1000000");
}

/* === %% (literal percent) === */

TEST(format_percent) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "100%%");
    ASSERT_STR_EQ(buf, "100%");
}

/* === Mixed formats === */

TEST(format_mixed) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%s=%x", "val", 0xFF);
    ASSERT_STR_EQ(buf, "val=FF");
}

TEST(format_mixed_decimal) {
    char buf[64];
    test_snprintf(buf, sizeof(buf), "%s=%d(0x%x)", "val", 255, 0xFF);
    ASSERT_STR_EQ(buf, "val=255(0xFF)");
}

/* === Buffer overflow protection === */

TEST(format_truncation) {
    char buf[8];
    test_snprintf(buf, sizeof(buf), "hello world");
    /* Should be truncated to fit in 8 bytes (7 chars + null) */
    ASSERT_EQ(buf[7], '\0');
    ASSERT_EQ(buf[0], 'h');
}

TEST(format_return_value) {
    char buf[64];
    int ret = test_snprintf(buf, sizeof(buf), "abc");
    ASSERT_EQ(ret, 3);
}

int main(void)
{
    printf("=== vga _kvsnprintf tests ===\n");

    RUN_TEST(format_string);
    RUN_TEST(format_string_empty);
    RUN_TEST(format_plain_text);

    RUN_TEST(format_char);
    RUN_TEST(format_char_in_string);

    RUN_TEST(format_hex_byte);
    RUN_TEST(format_hex_byte_zero);
    RUN_TEST(format_hex_word);
    RUN_TEST(format_hex_dword);
    RUN_TEST(format_hex_small);

    RUN_TEST(format_decimal_positive);
    RUN_TEST(format_decimal_zero);
    RUN_TEST(format_decimal_negative);
    RUN_TEST(format_decimal_large);

    RUN_TEST(format_percent);

    RUN_TEST(format_mixed);
    RUN_TEST(format_mixed_decimal);

    RUN_TEST(format_truncation);
    RUN_TEST(format_return_value);

    TEST_REPORT();
}
