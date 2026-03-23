#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>

TEST(atoi_stops_at_newline) {
    ASSERT_EQ(atoi("4\n"), 4);
}

TEST(atoi_skips_leading_space) {
    ASSERT_EQ(atoi(" \t42"), 42);
}

TEST(atoi_accepts_sign) {
    ASSERT_EQ(atoi("-17"), -17);
    ASSERT_EQ(atoi("+23"), 23);
}

TEST(atoi_stops_at_non_digit) {
    ASSERT_EQ(atoi("123abc"), 123);
}

TEST(atoi_handles_empty_text) {
    ASSERT_EQ(atoi(""), 0);
}

TEST(getenv_reads_host_environment) {
    ASSERT_EQ(setenv("SODEX_USR_STDLIB_ENV", "stdlib-ok", 1), 0);
    ASSERT_STR_EQ(getenv("SODEX_USR_STDLIB_ENV"), "stdlib-ok");
}

int main(void)
{
    printf("=== usr stdlib tests ===\n");

    RUN_TEST(atoi_stops_at_newline);
    RUN_TEST(atoi_skips_leading_space);
    RUN_TEST(atoi_accepts_sign);
    RUN_TEST(atoi_stops_at_non_digit);
    RUN_TEST(atoi_handles_empty_text);
    RUN_TEST(getenv_reads_host_environment);

    TEST_REPORT();
}
