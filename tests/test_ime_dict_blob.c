#include "test_framework.h"
#include <ime.h>
#include <ime_dict_blob.h>

#define TEST_BLOB_PATH "ime_dictionary_fixture.blob"

TEST(open_and_lookup_blob_entry) {
    struct ime_dict_blob_context ctx;
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT_EQ(ime_dict_blob_lookup(&ctx, "きこう", storage, sizeof(storage),
                                   candidates, IME_CANDIDATE_MAX, &count), 1);
    ASSERT_EQ(count, 8);
    ASSERT_STR_EQ(candidates[0], "機構");
    ASSERT_STR_EQ(candidates[7], "技工");
    ime_dict_blob_close(&ctx);
}

TEST(blob_contains_hundreds_of_basic_entries) {
    struct ime_dict_blob_context ctx;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT(ctx.header.entry_count >= 400);
    ASSERT(ctx.header.bucket_count >= 64);
    ime_dict_blob_close(&ctx);
}

TEST(lookup_returns_not_found_for_unknown_reading) {
    struct ime_dict_blob_context ctx;
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT_EQ(ime_dict_blob_lookup(&ctx, "みとうろく", storage, sizeof(storage),
                                   candidates, IME_CANDIDATE_MAX, &count), 0);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(candidates[0]);
    ime_dict_blob_close(&ctx);
}

int main(void)
{
    printf("=== ime dict blob tests ===\n");

    RUN_TEST(open_and_lookup_blob_entry);
    RUN_TEST(blob_contains_hundreds_of_basic_entries);
    RUN_TEST(lookup_returns_not_found_for_unknown_reading);

    TEST_REPORT();
}
