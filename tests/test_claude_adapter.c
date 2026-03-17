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

/* Helper: feed SSE stream through parser + adapter */
static int feed_sse_stream(const char *stream, struct claude_response *resp)
{
    struct sse_parser sp;
    struct sse_event ev;
    int sse_ret, claude_ret;

    sse_parser_init(&sp);
    claude_response_init(resp);
    sp.consumed = 0;

    while ((sse_ret = sse_feed(&sp, stream, strlen(stream), &ev)) == SSE_EVENT_DATA) {
        claude_ret = claude_parse_sse_event(&ev, resp);
        if (claude_ret == 1) return 0;  /* message_stop */
        if (claude_ret < 0) return claude_ret;
    }
    return 0;
}

/* ================================================================
 * Request builder tests
 * ================================================================ */

static void test_build_simple_request(void)
{
    char buf[2048];
    struct json_writer jw;
    struct claude_message msgs[1];
    int ret;

    msgs[0].role = "user";
    msgs[0].content = "Hello";

    jw_init(&jw, buf, sizeof(buf));
    ret = claude_build_request(&jw, "claude-sonnet-4-20250514", msgs, 1, (const char *)0, 1024, 0);
    if (ret != CLAUDE_OK) { TEST_FAIL("build_simple", "build failed"); return; }
    if (!strstr(buf, "\"model\":\"claude-sonnet-4-20250514\"")) { TEST_FAIL("build_simple", "no model"); return; }
    if (!strstr(buf, "\"max_tokens\":1024")) { TEST_FAIL("build_simple", "no max_tokens"); return; }
    if (!strstr(buf, "\"role\":\"user\"")) { TEST_FAIL("build_simple", "no role"); return; }
    if (!strstr(buf, "\"content\":\"Hello\"")) { TEST_FAIL("build_simple", "no content"); return; }
    if (strstr(buf, "\"system\"")) { TEST_FAIL("build_simple", "unexpected system"); return; }
    if (strstr(buf, "\"stream\"")) { TEST_FAIL("build_simple", "unexpected stream"); return; }
    TEST_PASS("build_simple");
}

static void test_build_with_system(void)
{
    char buf[2048];
    struct json_writer jw;
    struct claude_message msgs[1];

    msgs[0].role = "user";
    msgs[0].content = "2+2?";

    jw_init(&jw, buf, sizeof(buf));
    claude_build_request(&jw, "claude-sonnet-4-20250514", msgs, 1, "You are a math tutor.", 512, 1);
    if (!strstr(buf, "\"system\":\"You are a math tutor.\"")) { TEST_FAIL("build_system", "no system"); return; }
    if (!strstr(buf, "\"stream\":true")) { TEST_FAIL("build_system", "no stream"); return; }
    TEST_PASS("build_system");
}

static void test_build_multi_turn(void)
{
    char buf[4096];
    struct json_writer jw;
    struct claude_message msgs[3];

    msgs[0].role = "user";    msgs[0].content = "Hi";
    msgs[1].role = "assistant"; msgs[1].content = "Hello!";
    msgs[2].role = "user";    msgs[2].content = "How are you?";

    jw_init(&jw, buf, sizeof(buf));
    claude_build_request(&jw, "claude-sonnet-4-20250514", msgs, 3, (const char *)0, 2048, 1);
    /* Verify all 3 messages present in order */
    {
        char *p1 = strstr(buf, "\"content\":\"Hi\"");
        char *p2 = strstr(buf, "\"content\":\"Hello!\"");
        char *p3 = strstr(buf, "\"content\":\"How are you?\"");
        if (!p1 || !p2 || !p3) { TEST_FAIL("build_multi_turn", "missing message"); return; }
        if (p1 > p2 || p2 > p3) { TEST_FAIL("build_multi_turn", "wrong order"); return; }
    }
    TEST_PASS("build_multi_turn");
}

/* ================================================================
 * Non-streaming response parser tests
 * ================================================================ */

static void test_parse_text_response(void)
{
    const char *json =
        "{\"id\":\"msg_01A\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"Hello world!\"}],"
        "\"model\":\"claude-sonnet-4-20250514\",\"stop_reason\":\"end_turn\","
        "\"stop_sequence\":null,"
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";
    struct claude_response resp;

    claude_parse_response(json, strlen(json), &resp);
    if (strcmp(resp.id, "msg_01A") != 0) { TEST_FAIL("parse_text", "id"); return; }
    if (resp.stop_reason != CLAUDE_STOP_END_TURN) { TEST_FAIL("parse_text", "stop_reason"); return; }
    if (resp.block_count != 1) { TEST_FAIL("parse_text", "block_count"); return; }
    if (resp.blocks[0].type != CLAUDE_CONTENT_TEXT) { TEST_FAIL("parse_text", "type"); return; }
    if (strcmp(resp.blocks[0].text.text, "Hello world!") != 0) { TEST_FAIL("parse_text", "text"); return; }
    if (resp.input_tokens != 10 || resp.output_tokens != 5) { TEST_FAIL("parse_text", "tokens"); return; }
    TEST_PASS("parse_text");
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

    claude_parse_response(json, strlen(json), &resp);
    if (resp.stop_reason != CLAUDE_STOP_TOOL_USE) { TEST_FAIL("parse_tool_use", "stop_reason"); return; }
    if (!claude_needs_tool_call(&resp)) { TEST_FAIL("parse_tool_use", "needs_tool_call"); return; }
    if (resp.block_count != 2) { TEST_FAIL("parse_tool_use", "block_count"); return; }
    if (resp.blocks[0].type != CLAUDE_CONTENT_TEXT) { TEST_FAIL("parse_tool_use", "block0 type"); return; }
    if (strcmp(resp.blocks[0].text.text, "Let me check.") != 0) { TEST_FAIL("parse_tool_use", "block0 text"); return; }
    if (resp.blocks[1].type != CLAUDE_CONTENT_TOOL_USE) { TEST_FAIL("parse_tool_use", "block1 type"); return; }
    if (strcmp(resp.blocks[1].tool_use.id, "toolu_01") != 0) { TEST_FAIL("parse_tool_use", "tool id"); return; }
    if (strcmp(resp.blocks[1].tool_use.name, "get_weather") != 0) { TEST_FAIL("parse_tool_use", "tool name"); return; }
    TEST_PASS("parse_tool_use");
}

static void test_parse_max_tokens_response(void)
{
    const char *json =
        "{\"id\":\"msg_03\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"Truncated...\"}],"
        "\"model\":\"claude-sonnet-4-20250514\",\"stop_reason\":\"max_tokens\","
        "\"stop_sequence\":null,"
        "\"usage\":{\"input_tokens\":100,\"output_tokens\":4096}}";
    struct claude_response resp;

    claude_parse_response(json, strlen(json), &resp);
    if (resp.stop_reason != CLAUDE_STOP_MAX_TOKENS) { TEST_FAIL("parse_max_tokens", "stop_reason"); return; }
    TEST_PASS("parse_max_tokens");
}

static void test_parse_stop_sequence_response(void)
{
    const char *json =
        "{\"id\":\"msg_04\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"Done.\"}],"
        "\"model\":\"claude-sonnet-4-20250514\",\"stop_reason\":\"stop_sequence\","
        "\"stop_sequence\":\"END\","
        "\"usage\":{\"input_tokens\":15,\"output_tokens\":3}}";
    struct claude_response resp;

    claude_parse_response(json, strlen(json), &resp);
    if (resp.stop_reason != CLAUDE_STOP_STOP_SEQUENCE) {
        char msg[64]; snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("parse_stop_sequence", msg); return;
    }
    TEST_PASS("parse_stop_sequence");
}

static void test_parse_error_response(void)
{
    const char *json =
        "{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\","
        "\"message\":\"Overloaded\"}}";
    struct claude_response resp;
    int ret;

    ret = claude_parse_response(json, strlen(json), &resp);
    if (ret != CLAUDE_ERR_API) { TEST_FAIL("parse_error", "expected ERR_API"); return; }
    if (resp.stop_reason != CLAUDE_STOP_ERROR) { TEST_FAIL("parse_error", "stop_reason"); return; }
    TEST_PASS("parse_error");
}

static void test_parse_invalid_request_error(void)
{
    const char *json =
        "{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\","
        "\"message\":\"messages: roles must alternate between user and assistant\"}}";
    struct claude_response resp;
    int ret;

    ret = claude_parse_response(json, strlen(json), &resp);
    if (ret != CLAUDE_ERR_API) { TEST_FAIL("parse_invalid_request", "expected ERR_API"); return; }
    if (resp.stop_reason != CLAUDE_STOP_ERROR) { TEST_FAIL("parse_invalid_request", "stop_reason"); return; }
    TEST_PASS("parse_invalid_request");
}

static void test_parse_auth_error(void)
{
    const char *json =
        "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\","
        "\"message\":\"invalid x-api-key\"}}";
    struct claude_response resp;
    int ret;

    ret = claude_parse_response(json, strlen(json), &resp);
    if (ret != CLAUDE_ERR_API) { TEST_FAIL("parse_auth_error", "expected ERR_API"); return; }
    TEST_PASS("parse_auth_error");
}

static void test_parse_empty_content(void)
{
    const char *json =
        "{\"id\":\"msg_05\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[],"
        "\"model\":\"claude-sonnet-4-20250514\",\"stop_reason\":\"end_turn\","
        "\"usage\":{\"input_tokens\":5,\"output_tokens\":0}}";
    struct claude_response resp;

    claude_parse_response(json, strlen(json), &resp);
    if (resp.block_count != 0) {
        char msg[64]; snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("parse_empty_content", msg); return;
    }
    TEST_PASS("parse_empty_content");
}

/* ================================================================
 * SSE streaming parser tests
 * ================================================================ */

static void test_sse_text_stream(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"mock\",\"stop_reason\":null,\"usage\":{\"input_tokens\":10,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\" world\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":5}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (strcmp(resp.id, "msg_test") != 0) { TEST_FAIL("sse_text", "id"); return; }
    if (resp.stop_reason != CLAUDE_STOP_END_TURN) { TEST_FAIL("sse_text", "stop_reason"); return; }
    if (resp.block_count != 1) { TEST_FAIL("sse_text", "block_count"); return; }
    if (strcmp(resp.blocks[0].text.text, "Hello world") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "text='%s'", resp.blocks[0].text.text);
        TEST_FAIL("sse_text", msg); return;
    }
    if (resp.input_tokens != 10) { TEST_FAIL("sse_text", "input_tokens"); return; }
    if (resp.output_tokens != 5) { TEST_FAIL("sse_text", "output_tokens"); return; }
    TEST_PASS("sse_text");
}

static void test_sse_tool_use_stream(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_tool\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"mock\",\"stop_reason\":null,\"usage\":{\"input_tokens\":20,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Checking.\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_abc\",\"name\":\"get_weather\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"location\\\": \"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"San Francisco\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":30}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.stop_reason != CLAUDE_STOP_TOOL_USE) { TEST_FAIL("sse_tool", "stop_reason"); return; }
    if (!claude_needs_tool_call(&resp)) { TEST_FAIL("sse_tool", "needs_tool_call"); return; }
    if (resp.block_count != 2) { TEST_FAIL("sse_tool", "block_count"); return; }
    if (strcmp(resp.blocks[0].text.text, "Checking.") != 0) { TEST_FAIL("sse_tool", "text"); return; }
    if (resp.blocks[1].type != CLAUDE_CONTENT_TOOL_USE) { TEST_FAIL("sse_tool", "block1 type"); return; }
    if (strcmp(resp.blocks[1].tool_use.id, "toolu_abc") != 0) { TEST_FAIL("sse_tool", "tool id"); return; }
    if (strcmp(resp.blocks[1].tool_use.name, "get_weather") != 0) { TEST_FAIL("sse_tool", "tool name"); return; }
    if (!strstr(resp.blocks[1].tool_use.input_json, "San Francisco")) {
        char msg[256]; snprintf(msg, sizeof(msg), "input='%s'", resp.blocks[1].tool_use.input_json);
        TEST_FAIL("sse_tool", msg); return;
    }
    TEST_PASS("sse_tool");
}

/* max_tokens stop reason in streaming */
static void test_sse_max_tokens(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_mt\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":100,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Truncated output...\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":4096}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.stop_reason != CLAUDE_STOP_MAX_TOKENS) {
        char msg[64]; snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("sse_max_tokens", msg); return;
    }
    if (resp.output_tokens != 4096) { TEST_FAIL("sse_max_tokens", "output_tokens"); return; }
    TEST_PASS("sse_max_tokens");
}

/* stop_sequence stop reason in streaming */
static void test_sse_stop_sequence(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_ss\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":10,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Answer: 42\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"stop_sequence\",\"stop_sequence\":\"END\"},\"usage\":{\"output_tokens\":3}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.stop_reason != CLAUDE_STOP_STOP_SEQUENCE) {
        char msg[64]; snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("sse_stop_sequence", msg); return;
    }
    TEST_PASS("sse_stop_sequence");
}

/* Mid-stream error: partial text accumulated before error */
static void test_sse_midstream_error(void)
{
    struct sse_parser sp;
    struct sse_event ev;
    struct claude_response resp;
    int sse_ret, claude_ret;
    int got_error = 0;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_e\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":5,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"I will help\"}}\n\n"
        "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}\n\n";

    sse_parser_init(&sp);
    claude_response_init(&resp);
    sp.consumed = 0;
    while ((sse_ret = sse_feed(&sp, stream, strlen(stream), &ev)) == SSE_EVENT_DATA) {
        claude_ret = claude_parse_sse_event(&ev, &resp);
        if (claude_ret < 0) { got_error = 1; break; }
    }

    if (!got_error) { TEST_FAIL("sse_midstream_error", "expected error"); return; }
    if (resp.stop_reason != CLAUDE_STOP_ERROR) { TEST_FAIL("sse_midstream_error", "stop_reason"); return; }
    /* Partial text should still be accessible */
    if (resp.block_count < 1) { TEST_FAIL("sse_midstream_error", "no blocks"); return; }
    if (strcmp(resp.blocks[0].text.text, "I will help") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "text='%s'", resp.blocks[0].text.text);
        TEST_FAIL("sse_midstream_error", msg); return;
    }
    TEST_PASS("sse_midstream_error");
}

/* Parallel tool calls: text + 2 tool_use blocks */
static void test_sse_parallel_tools(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_p\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":50,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Checking both.\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_a1\",\"name\":\"get_weather\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"city\\\": \\\"Tokyo\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":2,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_a2\",\"name\":\"get_time\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"tz\\\": \\\"JST\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":2}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":45}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.stop_reason != CLAUDE_STOP_TOOL_USE) { TEST_FAIL("sse_parallel_tools", "stop_reason"); return; }
    if (resp.block_count != 3) {
        char msg[64]; snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("sse_parallel_tools", msg); return;
    }
    /* Block 0: text */
    if (resp.blocks[0].type != CLAUDE_CONTENT_TEXT) { TEST_FAIL("sse_parallel_tools", "b0 type"); return; }
    if (strcmp(resp.blocks[0].text.text, "Checking both.") != 0) { TEST_FAIL("sse_parallel_tools", "b0 text"); return; }
    /* Block 1: tool get_weather */
    if (resp.blocks[1].type != CLAUDE_CONTENT_TOOL_USE) { TEST_FAIL("sse_parallel_tools", "b1 type"); return; }
    if (strcmp(resp.blocks[1].tool_use.name, "get_weather") != 0) { TEST_FAIL("sse_parallel_tools", "b1 name"); return; }
    if (strcmp(resp.blocks[1].tool_use.id, "toolu_a1") != 0) { TEST_FAIL("sse_parallel_tools", "b1 id"); return; }
    if (!strstr(resp.blocks[1].tool_use.input_json, "Tokyo")) { TEST_FAIL("sse_parallel_tools", "b1 input"); return; }
    /* Block 2: tool get_time */
    if (resp.blocks[2].type != CLAUDE_CONTENT_TOOL_USE) { TEST_FAIL("sse_parallel_tools", "b2 type"); return; }
    if (strcmp(resp.blocks[2].tool_use.name, "get_time") != 0) { TEST_FAIL("sse_parallel_tools", "b2 name"); return; }
    if (strcmp(resp.blocks[2].tool_use.id, "toolu_a2") != 0) { TEST_FAIL("sse_parallel_tools", "b2 id"); return; }
    if (!strstr(resp.blocks[2].tool_use.input_json, "JST")) { TEST_FAIL("sse_parallel_tools", "b2 input"); return; }
    /* Usage */
    if (resp.output_tokens != 45) { TEST_FAIL("sse_parallel_tools", "output_tokens"); return; }
    TEST_PASS("sse_parallel_tools");
}

/* Tool input split across many deltas */
static void test_sse_many_json_deltas(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_mj\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":10,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_mj\",\"name\":\"search\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"q\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"uery\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\": \"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"hello\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\" world\\\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":20}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.blocks[0].type != CLAUDE_CONTENT_TOOL_USE) { TEST_FAIL("sse_many_json_deltas", "type"); return; }
    /* Accumulated JSON: {"query": "hello world"} */
    if (!strstr(resp.blocks[0].tool_use.input_json, "query")) {
        char msg[256]; snprintf(msg, sizeof(msg), "input='%s'", resp.blocks[0].tool_use.input_json);
        TEST_FAIL("sse_many_json_deltas", msg); return;
    }
    if (!strstr(resp.blocks[0].tool_use.input_json, "hello world")) {
        TEST_FAIL("sse_many_json_deltas", "missing value"); return;
    }
    TEST_PASS("sse_many_json_deltas");
}

/* Tool-only response (no text block) */
static void test_sse_tool_only(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_to\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":30,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_only\",\"name\":\"run_cmd\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"cmd\\\": \\\"ls\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":15}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.block_count != 1) {
        char msg[64]; snprintf(msg, sizeof(msg), "block_count=%d", resp.block_count);
        TEST_FAIL("sse_tool_only", msg); return;
    }
    if (resp.blocks[0].type != CLAUDE_CONTENT_TOOL_USE) { TEST_FAIL("sse_tool_only", "type"); return; }
    if (strcmp(resp.blocks[0].tool_use.name, "run_cmd") != 0) { TEST_FAIL("sse_tool_only", "name"); return; }
    TEST_PASS("sse_tool_only");
}

/* Ping events in middle of stream should be silently ignored */
static void test_sse_with_pings(void)
{
    struct claude_response resp;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_pg\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":5,\"output_tokens\":1}}}\n\n"
        "event: ping\ndata: {\"type\":\"ping\"}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: ping\ndata: {\"type\":\"ping\"}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi\"}}\n\n"
        "event: ping\ndata: {\"type\":\"ping\"}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":1}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    feed_sse_stream(stream, &resp);
    if (resp.stop_reason != CLAUDE_STOP_END_TURN) { TEST_FAIL("sse_with_pings", "stop_reason"); return; }
    if (strcmp(resp.blocks[0].text.text, "Hi") != 0) { TEST_FAIL("sse_with_pings", "text"); return; }
    TEST_PASS("sse_with_pings");
}

/* ================================================================
 * Tool result builder tests
 * ================================================================ */

static void test_build_tool_result(void)
{
    char buf[1024];
    struct json_writer jw;

    jw_init(&jw, buf, sizeof(buf));
    claude_build_tool_result(&jw, "toolu_01", "\"15 degrees\"", 12, 0);
    if (!strstr(buf, "\"tool_use_id\":\"toolu_01\"")) { TEST_FAIL("tool_result", "id"); return; }
    if (!strstr(buf, "\"type\":\"tool_result\"")) { TEST_FAIL("tool_result", "type"); return; }
    if (strstr(buf, "is_error")) { TEST_FAIL("tool_result", "unexpected is_error"); return; }
    TEST_PASS("tool_result");
}

static void test_build_tool_result_error(void)
{
    char buf[1024];
    struct json_writer jw;

    jw_init(&jw, buf, sizeof(buf));
    claude_build_tool_result(&jw, "toolu_02", "\"failed\"", 8, 1);
    if (!strstr(buf, "\"is_error\":true")) { TEST_FAIL("tool_result_error", "no is_error"); return; }
    TEST_PASS("tool_result_error");
}

/* ================================================================
 * Provider registration test
 * ================================================================ */

static void test_provider_claude(void)
{
    if (!provider_claude.name || strcmp(provider_claude.name, "claude") != 0) {
        TEST_FAIL("provider_claude", "name"); return;
    }
    if (!provider_claude.endpoint || provider_claude.endpoint->port != 443) {
        TEST_FAIL("provider_claude", "endpoint"); return;
    }
    if (!provider_claude.build_request) { TEST_FAIL("provider_claude", "build_request"); return; }
    if (!provider_claude.parse_sse_event) { TEST_FAIL("provider_claude", "parse_sse_event"); return; }
    if (!provider_claude.parse_response) { TEST_FAIL("provider_claude", "parse_response"); return; }
    TEST_PASS("provider_claude");
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== Claude Adapter Unit Tests ===\n\n");

    printf("[Request builder]\n");
    test_build_simple_request();
    test_build_with_system();
    test_build_multi_turn();

    printf("\n[Non-streaming response parser]\n");
    test_parse_text_response();
    test_parse_tool_use_response();
    test_parse_max_tokens_response();
    test_parse_stop_sequence_response();
    test_parse_error_response();
    test_parse_invalid_request_error();
    test_parse_auth_error();
    test_parse_empty_content();

    printf("\n[SSE streaming]\n");
    test_sse_text_stream();
    test_sse_tool_use_stream();
    test_sse_max_tokens();
    test_sse_stop_sequence();
    test_sse_midstream_error();
    test_sse_parallel_tools();
    test_sse_many_json_deltas();
    test_sse_tool_only();
    test_sse_with_pings();

    printf("\n[Tool result builder]\n");
    test_build_tool_result();
    test_build_tool_result_error();

    printf("\n[Provider]\n");
    test_provider_claude();

    printf("\n=== RESULT: %d/%d passed ===\n", passed, passed + failed);
    if (failed == 0)
        printf("ALL TESTS PASSED\n");

    return failed == 0 ? 0 : 1;
}
