#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

/* Use our own strcmp for ASSERT_STR_EQ to avoid pulling in host <string.h> */
static int _test_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

static int _test_count = 0;
static int _test_pass = 0;
static int _test_fail = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        _test_count++; \
        printf("  [TEST] %s ... ", #name); \
        test_##name(); \
        _test_pass++; \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAIL\n"); \
            printf("    %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #expr); \
            _test_fail++; \
            _test_pass--; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        long _a = (long)(a); \
        long _b = (long)(b); \
        if (_a != _b) { \
            printf("FAIL\n"); \
            printf("    %s:%d: expected %ld, got %ld\n", __FILE__, __LINE__, _b, _a); \
            _test_fail++; \
            _test_pass--; \
            return; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (_test_strcmp((a), (b)) != 0) { \
            printf("FAIL\n"); \
            printf("    %s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, (b), (a)); \
            _test_fail++; \
            _test_pass--; \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(ptr) ASSERT((ptr) == 0)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != 0)

#define RUN_TEST(name) run_test_##name()

#define TEST_REPORT() \
    do { \
        printf("\n--- Results: %d/%d passed", _test_pass, _test_count); \
        if (_test_fail > 0) printf(", %d FAILED", _test_fail); \
        printf(" ---\n"); \
        return _test_fail > 0 ? 1 : 0; \
    } while (0)

#endif
