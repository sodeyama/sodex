/*
 * test_agent_repl_cli.c - REPL/continue helper tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent/agent.h"
#include "agent/session.h"

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

static void reset_sessions(void)
{
    system("rm -rf " SESSION_DIR);
}

static void test_resume_latest_for_cwd(void)
{
    struct session_meta a1;
    struct session_meta b1;
    struct session_meta a2;
    char session_id[SESSION_ID_LEN + 1];

    TEST_START("resume_latest_for_cwd");
    reset_sessions();

    ASSERT(session_create(&a1, "mock", "/repo/a") == 0, "create a1");
    ASSERT(session_create(&b1, "mock", "/repo/b") == 0, "create b1");
    ASSERT(session_create(&a2, "mock", "/repo/a") == 0, "create a2");

    ASSERT(agent_resume_latest_for_cwd("/repo/a",
                                       session_id,
                                       sizeof(session_id)) == 0,
           "resolve latest /repo/a");
    ASSERT(strcmp(session_id, a2.id) == 0, "latest session mismatch");
    ASSERT(agent_resume_latest_for_cwd("/missing",
                                       session_id,
                                       sizeof(session_id)) < 0,
           "missing cwd should fail");
    TEST_PASS("resume_latest_for_cwd");
}

int main(void)
{
    printf("=== agent repl cli tests ===\n\n");
    test_resume_latest_for_cwd();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
