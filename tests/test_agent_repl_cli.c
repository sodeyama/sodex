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

static void test_text_layout_wraps_sentences(void)
{
    struct agent_text_layout layout;
    char out[256];

    TEST_START("text_layout_wraps_sentences");
    agent_text_layout_init(&layout, 12);
    ASSERT(agent_text_layout_format(&layout,
                                    "一文目です。二文目です。",
                                    -1,
                                    out, sizeof(out)) > 0,
           "format should succeed");
    ASSERT(strcmp(out, "一文目です。\n二文目です。\n") == 0,
           "sentence break formatting mismatch");
    TEST_PASS("text_layout_wraps_sentences");
}

static void test_text_layout_wraps_ascii_width(void)
{
    struct agent_text_layout layout;
    char out[256];

    TEST_START("text_layout_wraps_ascii_width");
    agent_text_layout_init(&layout, 20);
    ASSERT(agent_text_layout_format(&layout,
                                    "12345678901234567890ABC",
                                    -1,
                                    out, sizeof(out)) > 0,
           "format should succeed");
    ASSERT(strcmp(out, "12345678901234567890\nABC") == 0,
           "ascii wrap formatting mismatch");
    TEST_PASS("text_layout_wraps_ascii_width");
}

static void test_tool_result_failure_detection(void)
{
    static const char error_json[] = "{\"error\":\"permission denied\"}";
    static const char exit_fail_json[] = "{\"command\":\"x\",\"exit_code\":127}";
    static const char exit_ok_json[] = "{\"command\":\"x\",\"exit_code\":0}";
    int exit_code = 0;
    char error[64];

    TEST_START("tool_result_failure_detection");
    ASSERT(agent_tool_result_is_failure(error_json,
                                        strlen(error_json), 0) == 1,
           "error field should be failure");
    ASSERT(agent_tool_result_is_failure(exit_fail_json,
                                        strlen(exit_fail_json), 0) == 1,
           "nonzero exit should be failure");
    ASSERT(agent_tool_result_is_failure(exit_ok_json,
                                        strlen(exit_ok_json), 0) == 0,
           "zero exit should be success");
    ASSERT(agent_tool_result_get_exit_code(exit_fail_json,
                                           strlen(exit_fail_json),
                                           &exit_code) == 0,
           "exit_code should parse");
    ASSERT(exit_code == 127, "exit_code mismatch");
    ASSERT(agent_tool_result_copy_string_field(error_json,
                                               strlen(error_json),
                                               "error",
                                               error, sizeof(error)) >= 0,
           "error field should copy");
    ASSERT(strcmp(error, "permission denied") == 0,
           "error field mismatch");
    TEST_PASS("tool_result_failure_detection");
}

static void test_same_failure_detection(void)
{
    static const char failure_a[] =
        "{\"command\":\"websearch 立川 天気\",\"exit_code\":1,"
        "\"output\":\"websearch: JSON parse error (-2)\"}";
    static const char failure_b[] =
        "{\"command\":\"websearch 立川 天気\",\"exit_code\":1,"
        "\"output\":\"websearch: JSON parse error (-2)\"}";
    static const char failure_c[] =
        "{\"command\":\"websearch 東京 天気\",\"exit_code\":1,"
        "\"output\":\"websearch: JSON parse error (-2)\"}";

    TEST_START("same_failure_detection");
    ASSERT(agent_tool_result_same_failure("run_command",
                                          failure_a,
                                          strlen(failure_a),
                                          0,
                                          "run_command",
                                          failure_b,
                                          strlen(failure_b),
                                          0) == 1,
           "same failure should match");
    ASSERT(agent_tool_result_same_failure("run_command",
                                          failure_a,
                                          strlen(failure_a),
                                          0,
                                          "run_command",
                                          failure_c,
                                          strlen(failure_c),
                                          0) == 0,
           "different command should not match");
    ASSERT(agent_tool_result_same_failure("run_command",
                                          failure_a,
                                          strlen(failure_a),
                                          0,
                                          "fetch_url",
                                          failure_b,
                                          strlen(failure_b),
                                          0) == 0,
           "different tool should not match");
    TEST_PASS("same_failure_detection");
}

int main(void)
{
    printf("=== agent repl cli tests ===\n\n");
    test_resume_latest_for_cwd();
    test_text_layout_wraps_sentences();
    test_text_layout_wraps_ascii_width();
    test_tool_result_failure_detection();
    test_same_failure_detection();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
