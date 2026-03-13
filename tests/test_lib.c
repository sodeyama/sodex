/*
 * Unit tests for src/lib/string.c (string/memory functions)
 * and src/lib/lib.c (math functions)
 *
 * We avoid #include <string.h> to prevent conflicts with our own
 * implementations. The test framework uses its own strcmp.
 */
#include "test_framework.h"

/* Declarations for Sodex string functions under test */
unsigned int strlen(const char* s);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, unsigned int n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, unsigned int n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
void* memcpy(void* dest, void* src, unsigned int n);
void* memset(void* buf, int ch, unsigned int n);
int memcmp(const void* s1, const void* s2, unsigned int n);

/* Math functions (compiled with pow->sodex_pow, logn->sodex_logn renames) */
int sodex_pow(int x, int y);
int sodex_logn(int x, int y);

/* === strlen === */

TEST(strlen_empty) {
    ASSERT_EQ(strlen(""), 0);
}

TEST(strlen_hello) {
    ASSERT_EQ(strlen("hello"), 5);
}

TEST(strlen_single_char) {
    ASSERT_EQ(strlen("x"), 1);
}

TEST(strlen_with_spaces) {
    ASSERT_EQ(strlen("hello world"), 11);
}

/* === strcmp === */

TEST(strcmp_equal) {
    ASSERT_EQ(strcmp("abc", "abc"), 0);
}

TEST(strcmp_less) {
    ASSERT(strcmp("abc", "abd") < 0);
}

TEST(strcmp_greater) {
    ASSERT(strcmp("abd", "abc") > 0);
}

TEST(strcmp_empty_strings) {
    ASSERT_EQ(strcmp("", ""), 0);
}

TEST(strcmp_first_empty) {
    ASSERT(strcmp("", "a") < 0);
}

TEST(strcmp_second_empty) {
    ASSERT(strcmp("a", "") > 0);
}

TEST(strcmp_prefix) {
    ASSERT(strcmp("abc", "abcd") < 0);
}

/* === strncmp === */

TEST(strncmp_equal_within_n) {
    ASSERT_EQ(strncmp("abcdef", "abcxyz", 3), 0);
}

TEST(strncmp_diff_within_n) {
    ASSERT(strncmp("abcdef", "abxyz", 3) != 0);
}

TEST(strncmp_zero_n) {
    ASSERT_EQ(strncmp("abc", "xyz", 0), 0);
}

TEST(strncmp_full_match) {
    ASSERT_EQ(strncmp("hello", "hello", 5), 0);
}

/* === strcpy === */

TEST(strcpy_basic) {
    char dst[16];
    strcpy(dst, "hello");
    ASSERT_STR_EQ(dst, "hello");
}

TEST(strcpy_empty) {
    char dst[16] = "old";
    strcpy(dst, "");
    ASSERT_STR_EQ(dst, "");
}

TEST(strcpy_returns_dest) {
    char dst[16];
    char *ret = strcpy(dst, "test");
    ASSERT(ret == dst);
}

/* === strncpy === */

TEST(strncpy_basic) {
    char dst[16] = {0};
    strncpy(dst, "hello", 6);
    ASSERT_STR_EQ(dst, "hello");
}

TEST(strncpy_truncate) {
    char dst[8] = {0};
    strncpy(dst, "hello", 3);
    ASSERT(dst[0] == 'h');
    ASSERT(dst[1] == 'e');
    ASSERT(dst[2] == 'l');
}

TEST(strncpy_pad_zeros) {
    char dst[8] = {'x','x','x','x','x','x','x','x'};
    strncpy(dst, "hi", 6);
    ASSERT(dst[0] == 'h');
    ASSERT(dst[1] == 'i');
    ASSERT(dst[2] == 0);
    ASSERT(dst[3] == 0);
    ASSERT(dst[4] == 0);
    ASSERT(dst[5] == 0);
}

/* === strchr === */

TEST(strchr_found) {
    const char *s = "hello";
    char *p = strchr(s, 'l');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, 'l');
    ASSERT_EQ(p - s, 2);
}

TEST(strchr_not_found) {
    ASSERT_NULL(strchr("hello", 'z'));
}

TEST(strchr_first_char) {
    const char *s = "hello";
    ASSERT(strchr(s, 'h') == s);
}

TEST(strchr_null_terminator) {
    const char *s = "hello";
    char *p = strchr(s, '\0');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(*p, '\0');
}

/* === strrchr === */

TEST(strrchr_found_last) {
    const char *s = "hello";
    char *p = strrchr(s, 'l');
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p - s, 3);
}

TEST(strrchr_not_found) {
    ASSERT_NULL(strrchr("hello", 'z'));
}

TEST(strrchr_single_occurrence) {
    const char *s = "hello";
    char *p = strrchr(s, 'h');
    ASSERT_NOT_NULL(p);
    ASSERT(p == s);
}

/* === memcpy === */

TEST(memcpy_basic) {
    char src[] = "hello";
    char dst[8] = {0};
    memcpy(dst, src, 5);
    ASSERT(dst[0] == 'h');
    ASSERT(dst[1] == 'e');
    ASSERT(dst[4] == 'o');
}

TEST(memcpy_returns_dest) {
    char src[] = "test";
    char dst[8];
    void *ret = memcpy(dst, src, 4);
    ASSERT(ret == dst);
}

/* === memset === */

TEST(memset_basic) {
    char buf[8];
    memset(buf, 'A', 8);
    ASSERT(buf[0] == 'A');
    ASSERT(buf[7] == 'A');
}

TEST(memset_zero) {
    char buf[8] = {'x','x','x','x','x','x','x','x'};
    memset(buf, 0, 8);
    ASSERT(buf[0] == 0);
    ASSERT(buf[7] == 0);
}

TEST(memset_returns_buf) {
    char buf[8];
    void *ret = memset(buf, 0, 8);
    ASSERT(ret == buf);
}

/* === memcmp === */

TEST(memcmp_equal) {
    ASSERT_EQ(memcmp("abc", "abc", 3), 0);
}

TEST(memcmp_less) {
    ASSERT(memcmp("abc", "abd", 3) < 0);
}

TEST(memcmp_greater) {
    ASSERT(memcmp("abd", "abc", 3) > 0);
}

TEST(memcmp_partial) {
    ASSERT_EQ(memcmp("abcdef", "abcxyz", 3), 0);
}

/* === pow === */

TEST(pow_basic) {
    ASSERT_EQ(sodex_pow(2, 3), 8);
}

TEST(pow_zero_exp) {
    ASSERT_EQ(sodex_pow(5, 0), 1);
}

TEST(pow_one_exp) {
    ASSERT_EQ(sodex_pow(7, 1), 7);
}

TEST(pow_one_base) {
    ASSERT_EQ(sodex_pow(1, 100), 1);
}

/* === logn === */

TEST(logn_basic) {
    ASSERT_EQ(sodex_logn(2, 8), 3);
}

TEST(logn_exact) {
    ASSERT_EQ(sodex_logn(3, 27), 3);
}

TEST(logn_one) {
    ASSERT_EQ(sodex_logn(2, 1), 0);
}

/* === main === */

int main(void)
{
    printf("=== lib string/math tests ===\n");

    RUN_TEST(strlen_empty);
    RUN_TEST(strlen_hello);
    RUN_TEST(strlen_single_char);
    RUN_TEST(strlen_with_spaces);

    RUN_TEST(strcmp_equal);
    RUN_TEST(strcmp_less);
    RUN_TEST(strcmp_greater);
    RUN_TEST(strcmp_empty_strings);
    RUN_TEST(strcmp_first_empty);
    RUN_TEST(strcmp_second_empty);
    RUN_TEST(strcmp_prefix);

    RUN_TEST(strncmp_equal_within_n);
    RUN_TEST(strncmp_diff_within_n);
    RUN_TEST(strncmp_zero_n);
    RUN_TEST(strncmp_full_match);

    RUN_TEST(strcpy_basic);
    RUN_TEST(strcpy_empty);
    RUN_TEST(strcpy_returns_dest);

    RUN_TEST(strncpy_basic);
    RUN_TEST(strncpy_truncate);
    RUN_TEST(strncpy_pad_zeros);

    RUN_TEST(strchr_found);
    RUN_TEST(strchr_not_found);
    RUN_TEST(strchr_first_char);
    RUN_TEST(strchr_null_terminator);

    RUN_TEST(strrchr_found_last);
    RUN_TEST(strrchr_not_found);
    RUN_TEST(strrchr_single_occurrence);

    RUN_TEST(memcpy_basic);
    RUN_TEST(memcpy_returns_dest);

    RUN_TEST(memset_basic);
    RUN_TEST(memset_zero);
    RUN_TEST(memset_returns_buf);

    RUN_TEST(memcmp_equal);
    RUN_TEST(memcmp_less);
    RUN_TEST(memcmp_greater);
    RUN_TEST(memcmp_partial);

    RUN_TEST(pow_basic);
    RUN_TEST(pow_zero_exp);
    RUN_TEST(pow_one_exp);
    RUN_TEST(pow_one_base);

    RUN_TEST(logn_basic);
    RUN_TEST(logn_exact);
    RUN_TEST(logn_one);

    TEST_REPORT();
}
