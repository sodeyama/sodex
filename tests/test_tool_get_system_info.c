/*
 * test_tool_get_system_info.c - get_system_info の回帰テスト
 */

#include <stdio.h>
#include <string.h>
#include "agent/tool_handlers.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        failed++; \
        return; \
    } \
} while (0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while (0)

static void test_system_info_lists_text_commands(void)
{
    char buf[2048];
    int len;

    TEST_START("system_info_lists_text_commands");
    len = tool_get_system_info("{}", 2, buf, sizeof(buf));
    ASSERT(len > 0, "tool_get_system_info should succeed");
    ASSERT(strstr(buf, "\"text_commands\"") != 0,
           "result should include text_commands");
    ASSERT(strstr(buf, "\"find\"") != 0,
           "result should include find");
    ASSERT(strstr(buf, "\"awk\"") != 0,
           "result should include awk");
    ASSERT(strstr(buf, "\"tee\"") != 0,
           "result should include tee");
    ASSERT(strstr(buf, "\"default_cwd\":\"/home/user\"") != 0,
           "result should include default cwd");
    TEST_PASS("system_info_lists_text_commands");
}

int main(void)
{
    printf("=== get_system_info tests ===\n\n");
    test_system_info_lists_text_commands();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
