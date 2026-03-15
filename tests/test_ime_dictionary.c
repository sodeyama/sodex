#include "test_framework.h"
#include <ime_dictionary.h>

TEST(lookup_finds_single_candidate) {
    const char *const *candidates = 0;
    int count = 0;

    ASSERT_EQ(ime_dictionary_lookup("にほんご", &candidates, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(candidates[0], "日本語");
}

TEST(lookup_finds_multiple_candidates) {
    const char *const *candidates = 0;
    int count = 0;

    ASSERT_EQ(ime_dictionary_lookup("かんじ", &candidates, &count), 0);
    ASSERT_EQ(count, 2);
    ASSERT_STR_EQ(candidates[0], "漢字");
    ASSERT_STR_EQ(candidates[1], "感じ");
}

TEST(lookup_returns_not_found) {
    const char *const *candidates = 0;
    int count = 0;

    ASSERT_EQ(ime_dictionary_lookup("みとうろく", &candidates, &count), -1);
    ASSERT_NULL(candidates);
    ASSERT_EQ(count, 0);
}

int main(void)
{
    printf("=== ime dictionary tests ===\n");

    RUN_TEST(lookup_finds_single_candidate);
    RUN_TEST(lookup_finds_multiple_candidates);
    RUN_TEST(lookup_returns_not_found);

    TEST_REPORT();
}
