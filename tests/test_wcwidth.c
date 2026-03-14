#include "test_framework.h"
#include <wcwidth.h>

TEST(ascii_is_single_width) {
    ASSERT_EQ(unicode_wcwidth('A'), 1);
}

TEST(japanese_is_double_width) {
    ASSERT_EQ(unicode_wcwidth(0x3042), 2);
    ASSERT_EQ(unicode_wcwidth(0x65e5), 2);
}

TEST(combining_mark_is_zero_width) {
    ASSERT_EQ(unicode_wcwidth(0x0301), 0);
}

TEST(control_is_non_printable) {
    ASSERT_EQ(unicode_wcwidth('\n'), -1);
}

int main(void)
{
    printf("=== wcwidth tests ===\n");

    RUN_TEST(ascii_is_single_width);
    RUN_TEST(japanese_is_double_width);
    RUN_TEST(combining_mark_is_zero_width);
    RUN_TEST(control_is_non_printable);

    TEST_REPORT();
}
