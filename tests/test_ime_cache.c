#include "test_framework.h"
#include <ime.h>
#include <ime_dict_blob.h>

#define TEST_BLOB_PATH "ime_dictionary_fixture.blob"

TEST(repeated_lookup_hits_cache) {
    struct ime_dict_blob_context ctx;
    struct ime_dict_blob_metrics first_metrics;
    const struct ime_dict_blob_metrics *metrics;
    char storage[IME_CANDIDATE_STORAGE_MAX];
    const char *candidates[IME_CANDIDATE_MAX];
    int count = 0;

    ime_dict_blob_init(&ctx);
    ASSERT_EQ(ime_dict_blob_open(&ctx, TEST_BLOB_PATH), 0);
    ASSERT_EQ(ime_dict_blob_lookup(&ctx, "きこう", storage, sizeof(storage),
                                   candidates, IME_CANDIDATE_MAX, &count), 1);
    metrics = ime_dict_blob_get_metrics(&ctx);
    ASSERT_NOT_NULL(metrics);
    first_metrics = *metrics;
    ASSERT(first_metrics.cache_misses > 0);

    ASSERT_EQ(ime_dict_blob_lookup(&ctx, "きこう", storage, sizeof(storage),
                                   candidates, IME_CANDIDATE_MAX, &count), 1);
    metrics = ime_dict_blob_get_metrics(&ctx);
    ASSERT_NOT_NULL(metrics);
    ASSERT_EQ(metrics->cache_misses, first_metrics.cache_misses);
    ASSERT(metrics->cache_hits > first_metrics.cache_hits);
    ime_dict_blob_close(&ctx);
}

TEST(memory_budget_stays_under_one_megabyte) {
    ASSERT(ime_dict_blob_memory_budget() < 1024U * 1024U);
}

int main(void)
{
    printf("=== ime cache tests ===\n");

    RUN_TEST(repeated_lookup_hits_cache);
    RUN_TEST(memory_budget_stays_under_one_megabyte);

    TEST_REPORT();
}
