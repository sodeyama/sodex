#include "test_framework.h"
#include <ime.h>
#include <ime_dict_blob.h>

#define TEST_BLOB_PATH "ime_dictionary_fixture.blob"

static u_int32_t max_bucket_size(const struct ime_dict_blob_context *ctx)
{
    u_int32_t max_size = 0;
    u_int32_t i;

    for (i = 0; i < ctx->header.bucket_count; i++) {
        u_int32_t size = ctx->bucket_offsets[i + 1] - ctx->bucket_offsets[i];

        if (size > max_size)
            max_size = size;
    }
    return max_size;
}

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
    u_int32_t bucket_max;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT(ctx.header.entry_count >= 100000);
    ASSERT(ctx.header.bucket_count >= 4096);
    bucket_max = max_bucket_size(&ctx);
    ASSERT(bucket_max <= 64);
    ime_dict_blob_close(&ctx);
}

TEST(blob_contains_large_dictionary_terms_from_mozc_source) {
    struct ime_dict_blob_context ctx;
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT_EQ(ime_dict_blob_lookup(&ctx, "しゅしょう", storage, sizeof(storage),
                                   candidates, IME_CANDIDATE_MAX, &count), 1);
    ASSERT_EQ(count, 3);
    ASSERT_STR_EQ(candidates[0], "首相");
    ime_dict_blob_close(&ctx);
}

TEST(lookup_returns_not_found_for_unknown_reading) {
    struct ime_dict_blob_context ctx;
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT_EQ(ime_dict_blob_lookup(&ctx, "そでっくすみとうろく", storage, sizeof(storage),
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
    RUN_TEST(blob_contains_large_dictionary_terms_from_mozc_source);
    RUN_TEST(lookup_returns_not_found_for_unknown_reading);

    TEST_REPORT();
}
