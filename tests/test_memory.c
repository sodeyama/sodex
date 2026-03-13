/*
 * Unit tests for src/memory.c (kernel memory allocator)
 *
 * We compile memory.c with TEST_BUILD to stub out hardware dependencies.
 * The test uses init_mem_core() to initialize the allocator with a
 * host-side buffer as the memory pool.
 */
#include "test_framework.h"
#include <stdint.h>

/* MemHole structure (must match memory.h) */
typedef struct _MemHole {
    uint32_t base;
    uint32_t align_base;
    uint32_t size;
    struct _MemHole* prev;
    struct _MemHole* next;
} MemHole;

/* Constants from memory.h */
#define MIN_MEMSIZE 32
#define KFREE_OK                0
#define KFREE_FAIL_NOT_MHOLE    1

/* Functions under test */
extern void init_mem_core(uint32_t base_addr, uint32_t pool_size);
extern void* kalloc(uint32_t size);
extern int32_t kfree(void* ptr);
extern void* aalloc(uint32_t size, uint8_t align_bit);
extern int32_t afree(void* ptr);

/* We need the global lists to be accessible for verification */
extern MemHole muse_list;
extern MemHole mfree_list;

/*
 * Test memory pool.
 * We use a static buffer and pass its address as the "base_addr" to
 * init_mem_core. The allocator stores addresses as uint32_t integers,
 * so on 64-bit hosts the actual pointer values won't fit. We work
 * around this by using the buffer's address cast to uint32_t.
 *
 * IMPORTANT: This only works correctly on 32-bit or if the buffer
 * happens to be in the low 4GB of address space. For a more robust
 * approach we'd need to modify the allocator, but for now this tests
 * the logic correctly since the allocator only does integer arithmetic
 * on the base/size fields.
 */
#define POOL_SIZE (1024 * 1024)  /* 1MB */
static char memory_pool[POOL_SIZE] __attribute__((aligned(4096)));

static void setup(void) {
    uint32_t base = (uint32_t)(uintptr_t)memory_pool;
    init_mem_core(base, POOL_SIZE);
}

/* === Basic allocation === */

TEST(kalloc_basic) {
    setup();
    void *p = kalloc(100);
    ASSERT_NOT_NULL(p);
}

TEST(kalloc_returns_different_addresses) {
    setup();
    void *p1 = kalloc(100);
    void *p2 = kalloc(100);
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT(p1 != p2);
}

TEST(kalloc_min_size) {
    setup();
    void *p = kalloc(1);
    ASSERT_NOT_NULL(p);
}

/* === Free and re-allocate === */

TEST(kfree_basic) {
    setup();
    void *p = kalloc(100);
    ASSERT_NOT_NULL(p);
    int32_t ret = kfree(p);
    ASSERT_EQ(ret, KFREE_OK);
}

TEST(kalloc_after_free) {
    setup();
    void *p1 = kalloc(100);
    kfree(p1);
    void *p2 = kalloc(100);
    ASSERT_NOT_NULL(p2);
}

/* === Multiple allocations === */

TEST(kalloc_many_small) {
    setup();
    void *ptrs[100];
    int i;
    for (i = 0; i < 100; i++) {
        ptrs[i] = kalloc(64);
        if (ptrs[i] == 0) break;
    }
    ASSERT(i >= 100);

    /* Free all */
    for (int j = 0; j < i; j++) {
        kfree(ptrs[j]);
    }
}

TEST(kalloc_large) {
    setup();
    void *p = kalloc(POOL_SIZE / 2);
    ASSERT_NOT_NULL(p);
}

TEST(kalloc_too_large) {
    setup();
    void *p = kalloc(POOL_SIZE + 1);
    ASSERT_NULL(p);
}

/* === Coalescing === */

TEST(free_and_coalesce_middle) {
    setup();
    void *p1 = kalloc(1000);
    void *p2 = kalloc(1000);
    void *p3 = kalloc(1000);

    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT_NOT_NULL(p3);

    /* Free middle first, then edges */
    kfree(p2);
    kfree(p1);
    kfree(p3);

    /* After coalescing, should be able to allocate a large block */
    void *p4 = kalloc(2800);
    ASSERT_NOT_NULL(p4);
}

TEST(free_all_coalesce) {
    setup();
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kalloc(1000);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    /* Free all in reverse order */
    for (int i = 9; i >= 0; i--) {
        int32_t ret = kfree(ptrs[i]);
        ASSERT_EQ(ret, KFREE_OK);
    }

    /* Pool should be fully coalesced; allocate nearly full size */
    void *big = kalloc(POOL_SIZE - MIN_MEMSIZE - 100);
    ASSERT_NOT_NULL(big);
}

/* === Error cases === */

TEST(kfree_invalid_pointer) {
    setup();
    /* Try to free a pointer that was never allocated */
    int32_t ret = kfree((void*)(uintptr_t)0xDEADBEEF);
    ASSERT_EQ(ret, KFREE_FAIL_NOT_MHOLE);
}

TEST(kalloc_exact_match) {
    setup();
    /* Allocate enough to leave exactly MIN_MEMSIZE or less remaining */
    void *p = kalloc(POOL_SIZE - MIN_MEMSIZE);
    ASSERT_NOT_NULL(p);
}

/* === Aligned allocation === */

TEST(aalloc_page_aligned) {
    setup();
    void *p = aalloc(100, 12);  /* 4KB alignment (2^12) */
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((uintptr_t)p % 4096, 0);
}

TEST(aalloc_and_afree) {
    setup();
    void *p = aalloc(100, 12);
    ASSERT_NOT_NULL(p);
    int32_t ret = afree(p);
    ASSERT_EQ(ret, KFREE_OK);
}

TEST(aalloc_multiple) {
    setup();
    void *p1 = aalloc(4096, 12);
    void *p2 = aalloc(4096, 12);
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT(p1 != p2);
    ASSERT_EQ((uintptr_t)p1 % 4096, 0);
    ASSERT_EQ((uintptr_t)p2 % 4096, 0);
    afree(p1);
    afree(p2);
}

int main(void)
{
    printf("=== memory.c tests ===\n");

    RUN_TEST(kalloc_basic);
    RUN_TEST(kalloc_returns_different_addresses);
    RUN_TEST(kalloc_min_size);

    RUN_TEST(kfree_basic);
    RUN_TEST(kalloc_after_free);

    RUN_TEST(kalloc_many_small);
    RUN_TEST(kalloc_large);
    RUN_TEST(kalloc_too_large);

    RUN_TEST(free_and_coalesce_middle);
    RUN_TEST(free_all_coalesce);

    RUN_TEST(kfree_invalid_pointer);
    RUN_TEST(kalloc_exact_match);

    RUN_TEST(aalloc_page_aligned);
    RUN_TEST(aalloc_and_afree);
    RUN_TEST(aalloc_multiple);

    TEST_REPORT();
}
