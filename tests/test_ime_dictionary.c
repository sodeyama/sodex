#include "test_framework.h"
#include <ime.h>
#include <ime_dictionary.h>

#define TEST_BLOB_PATH "ime_dictionary_fixture.blob"

static int use_blob_fixture(void)
{
    return ime_dictionary_set_blob_path(TEST_BLOB_PATH);
}

static int use_missing_blob(void)
{
    return ime_dictionary_set_blob_path("missing-ime.blob");
}

TEST(lookup_prefers_blob_dictionary) {
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ASSERT_EQ(use_blob_fixture(), 0);
    ASSERT_EQ(ime_dictionary_lookup("がっこう", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(candidates[0], "学校");
    ASSERT_EQ(ime_dictionary_last_source(), IME_DICTIONARY_SOURCE_BLOB);
}

TEST(blob_lookup_preserves_candidate_order) {
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ASSERT_EQ(use_blob_fixture(), 0);
    ASSERT_EQ(ime_dictionary_lookup("きこう", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 8);
    ASSERT_STR_EQ(candidates[0], "機構");
    ASSERT_STR_EQ(candidates[1], "気候");
    ASSERT_STR_EQ(candidates[7], "技工");
}

TEST(blob_lookup_finds_added_basic_terms) {
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ASSERT_EQ(use_blob_fixture(), 0);
    ASSERT_EQ(ime_dictionary_lookup("びょういん", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(candidates[0], "病院");

    ASSERT_EQ(ime_dictionary_lookup("つかう", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(candidates[0], "使う");

    ASSERT_EQ(ime_dictionary_lookup("げつようび", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(candidates[0], "月曜日");
}

TEST(fallback_lookup_works_when_blob_missing) {
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ASSERT_EQ(use_missing_blob(), 0);
    ASSERT_EQ(ime_dictionary_lookup("にほんご", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(candidates[0], "日本語");
    ASSERT_EQ(ime_dictionary_last_source(), IME_DICTIONARY_SOURCE_FALLBACK);
}

TEST(lookup_returns_not_found_for_unknown_reading) {
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ASSERT_EQ(use_blob_fixture(), 0);
    ASSERT_EQ(ime_dictionary_lookup("みとうろく", storage, sizeof(storage),
                                    candidates, IME_CANDIDATE_MAX, &count), -1);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(candidates[0]);
    ASSERT_EQ(ime_dictionary_last_source(), IME_DICTIONARY_SOURCE_NONE);
}

int main(void)
{
    printf("=== ime dictionary tests ===\n");

    RUN_TEST(lookup_prefers_blob_dictionary);
    RUN_TEST(blob_lookup_preserves_candidate_order);
    RUN_TEST(blob_lookup_finds_added_basic_terms);
    RUN_TEST(fallback_lookup_works_when_blob_missing);
    RUN_TEST(lookup_returns_not_found_for_unknown_reading);

    TEST_REPORT();
}
