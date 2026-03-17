/*
 * test_claude_adapter.c - Host-side unit tests for Claude adapter
 *
 * Build: gcc -DTEST_BUILD -I../src/usr/include -o test_claude_adapter \
 *        test_claude_adapter.c ../src/usr/lib/libagent/claude_adapter.c \
 *        ../src/usr/lib/libagent/json.c ../src/usr/lib/libagent/sse_parser.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "agent/claude_adapter.h"
#include "agent/llm_provider.h"
#include "json.h"
#include "sse_parser.h"

static int passed = 0;
static int failed = 0;

#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("  FAIL: %s (%s)\n", name, reason); failed++; } while(0)

/* ---- Request builder tests ---- */

static void test_build_simple_request(void)
{
    char buf[2048];
    struct json_writer jw;
    struct claude_message msgs[1];
    int ret;

    msgs[0].role = "user";
    msgs[0].content = "Hello";

    jw_init(&jw, buf, sizeof(buf));
    ret = claude_build_request(&jw, "claude-sonnet-4-20250514", msgs, 1,
                               (const char *)0, 1024, 0);
    if (ret != CLAUDE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "ret=%d", ret);
        TEST_FAIL("build_simple_request", msg);
        return;
    }

    /* Verify JSON contains expected fields */
    if (strstr(buf, "\"model\":\"claude-sonnet-4-20250514\"") == (char *)0) {
        TEST_FAIL("build_simple_request", "model not found");
        return;
    }
    if (strstr(buf, "\"max_tokens\":1024") == (char *)0) {
        TEST_FAIL("build_simple_request", "max_tokens not found");
        return;
    }
    if (strstr(buf, "\"role\":\"user\"") == (char *)0) {
        TEST_FAIL("build_simple_request", "role not found");
        return;
    }
    if (strstr(buf, "\"content\":\"Hello\"") == (char *)0) {
        TEST_FAIL("build_simple_request", "content not found");
        return;
    }
    /* Should NOT have system or stream fields */
    if (strstr(buf, "\"system\"") != (char *)0) {
        TEST_FAIL("build_simple_request", "unexpected system field");
        return;
    }
    if (strstr(buf, "\"stream\"") != (char *)0) {
        TEST_FAIL("build_simple_request", "unexpected stream field");
        return;
    }

    TEST_PASS("build_simple_request");
}

static void test_build_request_with_system(void)
{
    char buf[2048];
    struct json_writer jw;
    struct claude_message msgs[1];
    int ret;

    msgs[0].role = "user";
    msgs[0].content = "What is 2+2?";

    jw_init(&jw, buf, sizeof(buf));
    ret = claude_build_request(&jw, "claude-sonnet-4-20250514", msgs, 1,
                               "You are a math tutor.", 512, 1);
    if (ret != CLAUDE_OK) {
        TEST_FAIL("build_request_with_system", "build failed");
        return;
    }

    if (strstr(buf, "\"system\":\"You are a math tutor.\"") == (char *)0) {
        TEST_FAIL("build_request_with_system", "system not found");
        return;
    }
    if (strstr(buf, "\"stream\":true") == (char *)0) {
        TEST_FAIL("build_request_with_system", "stream not found");
        return;
    }

    TEST_PASS("build_request_with_system");
}

/* ---- Non-streaming response parser tests ---- */

static void test_parse_text_response(void)
{
    const char *json =
        "{\"id\":\"msg_01A\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"Hello world!\"}],"
        "\"model\":\"claude-sonnet-4-20250514\",\"stop_reason\":\"end_turn\","
        "\"stop_sequence\":null,"
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";

    struct claude_response resp;
    int ret;

    ret = claude_parse_response(json, strlen(json), &resp);
    if (ret != CLAUDE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "ret=%d", ret);
        TEST_FAIL("parse_text_response", msg);
        return;
    }

    if (strcmp(resp.id, "msg_01A") != 0) {
        TEST_FAIL("parse_text_response", "id mismatch");
        return;
    }
    if (resp.stop_reason != CLAUDE_STOP_END_TURN) {
        char msg[64];
        snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("parse_text_response", msg);
        return;
    }
    if (resp.block_count != 1) {
        char msg[64];
        snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("parse_text_response", msg);
        return;
    }
    if (resp.blocks[0].type != CLAUDE_CONTENT_TEXT) {
        TEST_FAIL("parse_text_response", "type mismatch");
        return;
    }
    if (strcmp(resp.blocks[0].text.text, "Hello world!") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "text='%s'", resp.blocks[0].text.text);
        TEST_FAIL("parse_text_response", msg);
        return;
    }
    if (resp.input_tokens != 10 || resp.output_tokens != 5) {
        TEST_FAIL("parse_text_response", "token count mismatch");
        return;
    }

    TEST_PASS("parse_text_response");
}

static void test_parse_tool_use_response(void)
{
    const char *json =
        "{\"id\":\"msg_02\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":["
          "{\"type\":\"text\",\"text\":\"Let me check.\"},"
          "{\"type\":\"tool_use\",\"id\":\"toolu_01\",\"name\":\"get_weather\","
           "\"input\":{\"location\":\"SF\"}}"
        "],"
        "\"model\":\"claude-sonnet-4-20250514\",\"stop_reason\":\"tool_use\","
        "\"stop_sequence\":null,"
        "\"usage\":{\"input_tokens\":20,\"output_tokens\":30}}";

    struct claude_response resp;
    int ret;

    ret = claude_parse_response(json, strlen(json), &resp);
    if (ret != CLAUDE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "ret=%d", ret);
        TEST_FAIL("parse_tool_use_response", msg);
        return;
    }

    if (resp.stop_reason != CLAUDE_STOP_TOOL_USE) {
        TEST_FAIL("parse_tool_use_response", "stop_reason mismatch");
        return;
    }
    if (!claude_needs_tool_call(&resp)) {
        TEST_FAIL("parse_tool_use_response", "needs_tool_call should be true");
        return;
    }
    if (resp.block_count != 2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("parse_tool_use_response", msg);
        return;
    }

    /* Block 0: text */
    if (resp.blocks[0].type != CLAUDE_CONTENT_TEXT) {
        TEST_FAIL("parse_tool_use_response", "block[0] not text");
        return;
    }
    if (strcmp(resp.blocks[0].text.text, "Let me check.") != 0) {
        TEST_FAIL("parse_tool_use_response", "block[0] text mismatch");
        return;
    }

    /* Block 1: tool_use */
    if (resp.blocks[1].type != CLAUDE_CONTENT_TOOL_USE) {
        TEST_FAIL("parse_tool_use_response", "block[1] not tool_use");
        return;
    }
    if (strcmp(resp.blocks[1].tool_use.id, "toolu_01") != 0) {
        TEST_FAIL("parse_tool_use_response", "tool id mismatch");
        return;
    }
    if (strcmp(resp.blocks[1].tool_use.name, "get_weather") != 0) {
        TEST_FAIL("parse_tool_use_response", "tool name mismatch");
        return;
    }

    TEST_PASS("parse_tool_use_response");
}

/* ---- SSE streaming parser tests ---- */

static void test_sse_text_stream(void)
{
    struct sse_parser sp;
    struct sse_event ev;
    struct claude_response resp;
    int ret, sse_ret;
    const char *stream =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"mock\",\"stop_reason\":null,\"usage\":{\"input_tokens\":10,\"output_tokens\":1}}}\n"
        "\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\" world\"}}\n"
        "\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n"
        "\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":5}}\n"
        "\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n"
        "\n";

    sse_parser_init(&sp);
    claude_response_init(&resp);
    sp.consumed = 0;

    while ((sse_ret = sse_feed(&sp, stream, strlen(stream), &ev)) == SSE_EVENT_DATA) {
        ret = claude_parse_sse_event(&ev, &resp);
        if (ret == 1)
            break;  /* message_stop */
    }

    if (strcmp(resp.id, "msg_test") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "id='%s'", resp.id);
        TEST_FAIL("sse_text_stream", msg);
        return;
    }
    if (resp.stop_reason != CLAUDE_STOP_END_TURN) {
        char msg[64];
        snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("sse_text_stream", msg);
        return;
    }
    if (resp.block_count != 1) {
        char msg[64];
        snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("sse_text_stream", msg);
        return;
    }
    if (strcmp(resp.blocks[0].text.text, "Hello world") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "text='%s'", resp.blocks[0].text.text);
        TEST_FAIL("sse_text_stream", msg);
        return;
    }
    if (resp.input_tokens != 10) {
        char msg[64];
        snprintf(msg, sizeof(msg), "input_tokens=%d", resp.input_tokens);
        TEST_FAIL("sse_text_stream", msg);
        return;
    }
    if (resp.output_tokens != 5) {
        char msg[64];
        snprintf(msg, sizeof(msg), "output_tokens=%d", resp.output_tokens);
        TEST_FAIL("sse_text_stream", msg);
        return;
    }

    TEST_PASS("sse_text_stream");
}

static void test_sse_tool_use_stream(void)
{
    struct sse_parser sp;
    struct sse_event ev;
    struct claude_response resp;
    int ret, sse_ret;
    const char *stream =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_tool\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"mock\",\"stop_reason\":null,\"usage\":{\"input_tokens\":20,\"output_tokens\":1}}}\n"
        "\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Checking.\"}}\n"
        "\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n"
        "\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_abc\",\"name\":\"get_weather\",\"input\":{}}}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"location\\\": \"}}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"San Francisco\\\"}\"}}\n"
        "\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":1}\n"
        "\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":30}}\n"
        "\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n"
        "\n";

    sse_parser_init(&sp);
    claude_response_init(&resp);
    sp.consumed = 0;

    while ((sse_ret = sse_feed(&sp, stream, strlen(stream), &ev)) == SSE_EVENT_DATA) {
        ret = claude_parse_sse_event(&ev, &resp);
        if (ret == 1)
            break;
    }

    if (resp.stop_reason != CLAUDE_STOP_TOOL_USE) {
        char msg[64];
        snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("sse_tool_use_stream", msg);
        return;
    }
    if (!claude_needs_tool_call(&resp)) {
        TEST_FAIL("sse_tool_use_stream", "needs_tool_call should be true");
        return;
    }
    if (resp.block_count != 2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("sse_tool_use_stream", msg);
        return;
    }

    /* Block 0: text */
    if (strcmp(resp.blocks[0].text.text, "Checking.") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "text='%s'", resp.blocks[0].text.text);
        TEST_FAIL("sse_tool_use_stream", msg);
        return;
    }

    /* Block 1: tool_use */
    if (resp.blocks[1].type != CLAUDE_CONTENT_TOOL_USE) {
        TEST_FAIL("sse_tool_use_stream", "block[1] not tool_use");
        return;
    }
    if (strcmp(resp.blocks[1].tool_use.id, "toolu_abc") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "id='%s'", resp.blocks[1].tool_use.id);
        TEST_FAIL("sse_tool_use_stream", msg);
        return;
    }
    if (strcmp(resp.blocks[1].tool_use.name, "get_weather") != 0) {
        TEST_FAIL("sse_tool_use_stream", "tool name mismatch");
        return;
    }
    if (strstr(resp.blocks[1].tool_use.input_json, "San Francisco") == (char *)0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "input='%s'", resp.blocks[1].tool_use.input_json);
        TEST_FAIL("sse_tool_use_stream", msg);
        return;
    }

    TEST_PASS("sse_tool_use_stream");
}

/* ---- Tool result builder test ---- */

static void test_build_tool_result(void)
{
    char buf[1024];
    struct json_writer jw;
    int ret;

    jw_init(&jw, buf, sizeof(buf));
    ret = claude_build_tool_result(&jw, "toolu_01", "\"15 degrees\"", 12, 0);
    if (ret != CLAUDE_OK) {
        TEST_FAIL("build_tool_result", "build failed");
        return;
    }

    if (strstr(buf, "\"tool_use_id\":\"toolu_01\"") == (char *)0) {
        TEST_FAIL("build_tool_result", "tool_use_id not found");
        return;
    }
    if (strstr(buf, "\"type\":\"tool_result\"") == (char *)0) {
        TEST_FAIL("build_tool_result", "type not found");
        return;
    }
    /* Should NOT have is_error */
    if (strstr(buf, "is_error") != (char *)0) {
        TEST_FAIL("build_tool_result", "unexpected is_error");
        return;
    }

    TEST_PASS("build_tool_result");
}

static void test_build_tool_result_error(void)
{
    char buf[1024];
    struct json_writer jw;
    int ret;

    jw_init(&jw, buf, sizeof(buf));
    ret = claude_build_tool_result(&jw, "toolu_02", "\"failed\"", 8, 1);
    if (ret != CLAUDE_OK) {
        TEST_FAIL("build_tool_result_error", "build failed");
        return;
    }

    if (strstr(buf, "\"is_error\":true") == (char *)0) {
        TEST_FAIL("build_tool_result_error", "is_error not found");
        return;
    }

    TEST_PASS("build_tool_result_error");
}

/* ---- Provider registration test ---- */

static void test_provider_claude(void)
{
    if (provider_claude.name == (const char *)0 ||
        strcmp(provider_claude.name, "claude") != 0) {
        TEST_FAIL("provider_claude", "name mismatch");
        return;
    }
    if (provider_claude.endpoint == (const struct api_endpoint *)0) {
        TEST_FAIL("provider_claude", "no endpoint");
        return;
    }
    if (provider_claude.endpoint->port != 443) {
        TEST_FAIL("provider_claude", "port mismatch");
        return;
    }
    if (provider_claude.build_request == (void *)0) {
        TEST_FAIL("provider_claude", "no build_request");
        return;
    }
    if (provider_claude.parse_sse_event == (void *)0) {
        TEST_FAIL("provider_claude", "no parse_sse_event");
        return;
    }

    TEST_PASS("provider_claude");
}

int main(void)
{
    printf("=== Claude Adapter Unit Tests ===\n");

    /* Request builder */
    test_build_simple_request();
    test_build_request_with_system();

    /* Response parser (non-streaming) */
    test_parse_text_response();
    test_parse_tool_use_response();

    /* SSE streaming */
    test_sse_text_stream();
    test_sse_tool_use_stream();

    /* Tool result */
    test_build_tool_result();
    test_build_tool_result_error();

    /* Provider */
    test_provider_claude();

    printf("\n=== RESULT: %d/%d passed ===\n", passed, passed + failed);
    if (failed == 0)
        printf("ALL TESTS PASSED\n");

    return failed == 0 ? 0 : 1;
}
