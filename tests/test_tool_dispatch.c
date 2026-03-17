/*
 * test_tool_dispatch.c - Tool registry, dispatch, and error handling tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent/tool_registry.h"
#include "agent/tool_dispatch.h"
#include "agent/claude_adapter.h"
#include <json.h>

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

/* ---- Mock handlers ---- */

static int mock_handler_ok(const char *input, int input_len,
                           char *result, int cap)
{
    (void)input; (void)input_len;
    return snprintf(result, cap, "{\"status\":\"ok\"}");
}

static int mock_handler_error(const char *input, int input_len,
                              char *result, int cap)
{
    (void)input; (void)input_len; (void)result; (void)cap;
    return -1;
}

static int mock_handler_echo(const char *input, int input_len,
                             char *result, int cap)
{
    int len = input_len < cap - 1 ? input_len : cap - 1;
    memcpy(result, input, len);
    result[len] = '\0';
    return len;
}

static const char *mock_schema = "{\"type\":\"object\",\"properties\":{\"cmd\":{\"type\":\"string\"}}}";

/* ---- Tests ---- */

static void test_registry_init(void)
{
    TEST_START("registry_init");
    tool_registry_init();
    ASSERT(tool_count() == 0, "count should be 0 after init");
    TEST_PASS("registry_init");
}

static void test_register_and_find(void)
{
    const struct tool_def *td;

    TEST_START("register_and_find");
    tool_registry_init();

    ASSERT(tool_register("run_cmd", "Execute a command",
                          mock_schema, mock_handler_ok) == 0,
           "register should succeed");

    ASSERT(tool_count() == 1, "count should be 1");

    td = tool_find("run_cmd");
    ASSERT(td != NULL, "tool_find should return non-NULL");
    ASSERT(strcmp(td->name, "run_cmd") == 0, "name mismatch");
    ASSERT(strcmp(td->description, "Execute a command") == 0, "desc mismatch");
    ASSERT(td->input_schema_json == mock_schema, "schema pointer mismatch");
    ASSERT(td->handler == mock_handler_ok, "handler mismatch");

    TEST_PASS("register_and_find");
}

static void test_register_multiple(void)
{
    TEST_START("register_multiple");
    tool_registry_init();

    ASSERT(tool_register("tool_a", "Tool A", NULL, mock_handler_ok) == 0,
           "register tool_a");
    ASSERT(tool_register("tool_b", "Tool B", NULL, mock_handler_echo) == 0,
           "register tool_b");
    ASSERT(tool_register("tool_c", "Tool C", NULL, mock_handler_error) == 0,
           "register tool_c");

    ASSERT(tool_count() == 3, "count should be 3");
    ASSERT(tool_find("tool_a") != NULL, "find tool_a");
    ASSERT(tool_find("tool_b") != NULL, "find tool_b");
    ASSERT(tool_find("tool_c") != NULL, "find tool_c");

    /* Verify list */
    {
        const struct tool_def *arr[8];
        int n = tool_list(arr, 8);
        ASSERT(n == 3, "tool_list should return 3");
    }

    TEST_PASS("register_multiple");
}

static void test_find_nonexistent(void)
{
    TEST_START("find_nonexistent");
    tool_registry_init();

    ASSERT(tool_find("nonexistent") == NULL,
           "should return NULL for unknown tool");
    ASSERT(tool_find(NULL) == NULL,
           "should return NULL for NULL name");

    TEST_PASS("find_nonexistent");
}

static void test_register_overflow(void)
{
    int i;
    char name[64];

    TEST_START("register_overflow");
    tool_registry_init();

    for (i = 0; i < TOOL_MAX_TOOLS; i++) {
        snprintf(name, sizeof(name), "tool_%d", i);
        ASSERT(tool_register(name, "desc", NULL, mock_handler_ok) == 0,
               "register within capacity");
    }

    ASSERT(tool_count() == TOOL_MAX_TOOLS, "count should be TOOL_MAX_TOOLS");
    ASSERT(tool_register("overflow_tool", "desc", NULL, mock_handler_ok) == -1,
           "register beyond capacity should fail");

    TEST_PASS("register_overflow");
}

static void test_dispatch_success(void)
{
    struct claude_tool_use tu;
    struct tool_dispatch_result result;
    int rc;

    TEST_START("dispatch_success");
    tool_registry_init();
    tool_register("run_cmd", "Execute a command", mock_schema, mock_handler_ok);

    memset(&tu, 0, sizeof(tu));
    strncpy(tu.id, "toolu_abc123", sizeof(tu.id) - 1);
    strncpy(tu.name, "run_cmd", sizeof(tu.name) - 1);
    strncpy(tu.input_json, "{\"cmd\":\"ls\"}", sizeof(tu.input_json) - 1);
    tu.input_json_len = (int)strlen(tu.input_json);

    rc = tool_dispatch(&tu, &result);
    ASSERT(rc == 0, "dispatch should return 0");
    ASSERT(result.is_error == 0, "is_error should be 0");
    ASSERT(strcmp(result.tool_use_id, "toolu_abc123") == 0, "tool_use_id mismatch");
    ASSERT(result.result_len > 0, "result_len should be positive");
    ASSERT(strstr(result.result_json, "\"status\":\"ok\"") != NULL,
           "result should contain status ok");

    TEST_PASS("dispatch_success");
}

static void test_dispatch_not_found(void)
{
    struct claude_tool_use tu;
    struct tool_dispatch_result result;
    int rc;

    TEST_START("dispatch_not_found");
    tool_registry_init();

    memset(&tu, 0, sizeof(tu));
    strncpy(tu.id, "toolu_missing", sizeof(tu.id) - 1);
    strncpy(tu.name, "no_such_tool", sizeof(tu.name) - 1);
    strncpy(tu.input_json, "{}", sizeof(tu.input_json) - 1);
    tu.input_json_len = 2;

    rc = tool_dispatch(&tu, &result);
    ASSERT(rc == -1, "dispatch should return -1 for unknown tool");
    ASSERT(result.is_error == 1, "is_error should be 1");
    ASSERT(strstr(result.result_json, "tool not found") != NULL,
           "result should contain error message");

    TEST_PASS("dispatch_not_found");
}

static void test_dispatch_handler_error(void)
{
    struct claude_tool_use tu;
    struct tool_dispatch_result result;
    int rc;

    TEST_START("dispatch_handler_error");
    tool_registry_init();
    tool_register("fail_tool", "Always fails", NULL, mock_handler_error);

    memset(&tu, 0, sizeof(tu));
    strncpy(tu.id, "toolu_fail", sizeof(tu.id) - 1);
    strncpy(tu.name, "fail_tool", sizeof(tu.name) - 1);
    strncpy(tu.input_json, "{}", sizeof(tu.input_json) - 1);
    tu.input_json_len = 2;

    rc = tool_dispatch(&tu, &result);
    ASSERT(rc == 0, "dispatch should return 0 (dispatch succeeded, handler failed)");
    ASSERT(result.is_error == 1, "is_error should be 1");
    ASSERT(strstr(result.result_json, "tool execution failed") != NULL,
           "result should contain execution failed message");

    TEST_PASS("dispatch_handler_error");
}

static void test_dispatch_all(void)
{
    struct claude_response resp;
    struct tool_dispatch_result results[4];
    int count;

    TEST_START("dispatch_all");
    tool_registry_init();
    tool_register("tool_x", "Tool X", NULL, mock_handler_ok);
    tool_register("tool_y", "Tool Y", NULL, mock_handler_echo);

    memset(&resp, 0, sizeof(resp));
    resp.block_count = 3;

    /* Block 0: text (should be skipped) */
    resp.blocks[0].type = CLAUDE_CONTENT_TEXT;
    strncpy(resp.blocks[0].text.text, "thinking...",
            sizeof(resp.blocks[0].text.text) - 1);
    resp.blocks[0].text.text_len = 11;

    /* Block 1: tool_use for tool_x */
    resp.blocks[1].type = CLAUDE_CONTENT_TOOL_USE;
    strncpy(resp.blocks[1].tool_use.id, "toolu_1",
            sizeof(resp.blocks[1].tool_use.id) - 1);
    strncpy(resp.blocks[1].tool_use.name, "tool_x",
            sizeof(resp.blocks[1].tool_use.name) - 1);
    strncpy(resp.blocks[1].tool_use.input_json, "{\"a\":1}",
            sizeof(resp.blocks[1].tool_use.input_json) - 1);
    resp.blocks[1].tool_use.input_json_len = 7;

    /* Block 2: tool_use for tool_y */
    resp.blocks[2].type = CLAUDE_CONTENT_TOOL_USE;
    strncpy(resp.blocks[2].tool_use.id, "toolu_2",
            sizeof(resp.blocks[2].tool_use.id) - 1);
    strncpy(resp.blocks[2].tool_use.name, "tool_y",
            sizeof(resp.blocks[2].tool_use.name) - 1);
    strncpy(resp.blocks[2].tool_use.input_json, "{\"b\":2}",
            sizeof(resp.blocks[2].tool_use.input_json) - 1);
    resp.blocks[2].tool_use.input_json_len = 7;

    count = tool_dispatch_all(&resp, results, 4);
    ASSERT(count == 2, "should dispatch 2 tool_use blocks");
    ASSERT(strcmp(results[0].tool_use_id, "toolu_1") == 0, "first id mismatch");
    ASSERT(results[0].is_error == 0, "first should succeed");
    ASSERT(strcmp(results[1].tool_use_id, "toolu_2") == 0, "second id mismatch");
    ASSERT(results[1].is_error == 0, "second should succeed");

    TEST_PASS("dispatch_all");
}

static void test_build_definitions(void)
{
    char buf[4096];
    struct json_writer jw;
    int rc;

    TEST_START("build_definitions");
    tool_registry_init();
    tool_register("read_file", "Read a file", mock_schema, mock_handler_ok);
    tool_register("write_file", "Write a file", NULL, mock_handler_ok);

    jw_init(&jw, buf, sizeof(buf));
    rc = tool_build_definitions(&jw);
    ASSERT(rc == 2, "should return count of 2 tools");
    jw_finish(&jw);

    ASSERT(strstr(buf, "\"read_file\"") != NULL,
           "JSON should contain read_file");
    ASSERT(strstr(buf, "\"write_file\"") != NULL,
           "JSON should contain write_file");
    ASSERT(strstr(buf, "\"Read a file\"") != NULL,
           "JSON should contain description");
    ASSERT(strstr(buf, "input_schema") != NULL,
           "JSON should contain input_schema key");

    TEST_PASS("build_definitions");
}

int main(void)
{
    printf("=== tool dispatch tests ===\n\n");

    test_registry_init();
    test_register_and_find();
    test_register_multiple();
    test_find_nonexistent();
    test_register_overflow();
    test_dispatch_success();
    test_dispatch_not_found();
    test_dispatch_handler_error();
    test_dispatch_all();
    test_build_definitions();

    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
