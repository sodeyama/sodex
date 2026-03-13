/*
 * Unit tests for src/lib/memb.c (memory block allocation)
 */
#include "test_framework.h"
#include <stdint.h>

/* Reproduce the memb structures and macros needed for testing */
struct memb_blocks {
    unsigned short size;
    unsigned short num;
    char *count;
    void *mem;
};

extern void  memb_init(struct memb_blocks *m);
extern void *memb_alloc(struct memb_blocks *m);
extern char  memb_free(struct memb_blocks *m, void *ptr);
extern void *memset(void *buf, int ch, unsigned int n);

/* Test pool: 8 blocks of 32 bytes each */
#define TEST_POOL_NUM 8
#define TEST_POOL_SIZE 32

static char test_count[TEST_POOL_NUM];
static char test_mem[TEST_POOL_NUM * TEST_POOL_SIZE];
static struct memb_blocks test_pool = {
    TEST_POOL_SIZE, TEST_POOL_NUM, test_count, test_mem
};

static void setup(void) {
    memb_init(&test_pool);
}

/* === memb_init === */

TEST(memb_init_clears) {
    /* Fill with garbage first */
    memset(test_count, 0xFF, sizeof(test_count));
    memset(test_mem, 0xFF, sizeof(test_mem));
    memb_init(&test_pool);
    /* All counts should be 0 */
    for (int i = 0; i < TEST_POOL_NUM; i++) {
        ASSERT_EQ(test_count[i], 0);
    }
}

/* === memb_alloc === */

TEST(memb_alloc_single) {
    setup();
    void *p = memb_alloc(&test_pool);
    ASSERT_NOT_NULL(p);
}

TEST(memb_alloc_returns_pool_memory) {
    setup();
    void *p = memb_alloc(&test_pool);
    /* Returned pointer should be within test_mem */
    ASSERT((char*)p >= test_mem);
    ASSERT((char*)p < test_mem + sizeof(test_mem));
}

TEST(memb_alloc_different_addresses) {
    setup();
    void *p1 = memb_alloc(&test_pool);
    void *p2 = memb_alloc(&test_pool);
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT(p1 != p2);
}

TEST(memb_alloc_all) {
    setup();
    void *ptrs[TEST_POOL_NUM];
    for (int i = 0; i < TEST_POOL_NUM; i++) {
        ptrs[i] = memb_alloc(&test_pool);
        ASSERT_NOT_NULL(ptrs[i]);
    }
    /* Next allocation should fail */
    void *extra = memb_alloc(&test_pool);
    ASSERT_NULL(extra);
}

/* === memb_free === */

TEST(memb_free_basic) {
    setup();
    void *p = memb_alloc(&test_pool);
    char ret = memb_free(&test_pool, p);
    ASSERT_EQ(ret, 0);  /* reference count should be 0 after free */
}

TEST(memb_free_and_realloc) {
    setup();
    /* Allocate all blocks */
    void *ptrs[TEST_POOL_NUM];
    for (int i = 0; i < TEST_POOL_NUM; i++) {
        ptrs[i] = memb_alloc(&test_pool);
    }
    /* Pool is full */
    ASSERT_NULL(memb_alloc(&test_pool));
    /* Free one block */
    memb_free(&test_pool, ptrs[3]);
    /* Should be able to allocate again */
    void *p = memb_alloc(&test_pool);
    ASSERT_NOT_NULL(p);
}

TEST(memb_free_invalid_ptr) {
    setup();
    char dummy;
    char ret = memb_free(&test_pool, &dummy);
    ASSERT_EQ(ret, -1);  /* Invalid pointer returns -1 */
}

TEST(memb_alloc_after_free_all) {
    setup();
    /* Allocate and free all */
    for (int i = 0; i < TEST_POOL_NUM; i++) {
        void *p = memb_alloc(&test_pool);
        memb_free(&test_pool, p);
    }
    /* Should still be able to allocate */
    void *p = memb_alloc(&test_pool);
    ASSERT_NOT_NULL(p);
}

int main(void)
{
    printf("=== memb.c tests ===\n");

    RUN_TEST(memb_init_clears);
    RUN_TEST(memb_alloc_single);
    RUN_TEST(memb_alloc_returns_pool_memory);
    RUN_TEST(memb_alloc_different_addresses);
    RUN_TEST(memb_alloc_all);
    RUN_TEST(memb_free_basic);
    RUN_TEST(memb_free_and_realloc);
    RUN_TEST(memb_free_invalid_ptr);
    RUN_TEST(memb_alloc_after_free_all);

    TEST_REPORT();
}
