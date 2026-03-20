#include "test_framework.h"

char* strcpy(char* dest, const char* src);

TEST(strcpy_writes_terminator) {
    char dst[8];
    int i;

    for (i = 0; i < (int)sizeof(dst); i++)
        dst[i] = 'x';
    strcpy(dst, "ab");
    ASSERT_EQ(dst[0], 'a');
    ASSERT_EQ(dst[1], 'b');
    ASSERT_EQ(dst[2], '\0');
}

TEST(strcpy_handles_empty_text) {
    char dst[4];
    int i;

    for (i = 0; i < (int)sizeof(dst); i++)
        dst[i] = 'x';
    strcpy(dst, "");
    ASSERT_EQ(dst[0], '\0');
}

int main(void)
{
    RUN_TEST(strcpy_writes_terminator);
    RUN_TEST(strcpy_handles_empty_text);
    TEST_REPORT();
}
