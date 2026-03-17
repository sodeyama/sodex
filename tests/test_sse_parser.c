/*
 * test_sse_parser.c - Host-side unit tests for SSE parser
 *
 * Build: gcc -DTEST_BUILD -I../src/usr/include -o test_sse_parser \
 *        test_sse_parser.c ../src/usr/lib/libagent/sse_parser.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sse_parser.h"

static int passed = 0;
static int failed = 0;

#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("  FAIL: %s (%s)\n", name, reason); failed++; } while(0)

/* ================================================================
 * Basic protocol tests
 * ================================================================ */

/* Complete single event */
static void test_single_event(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: message_start\ndata: {\"type\":\"message_start\"}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("single_event", "expected SSE_EVENT_DATA");
        return;
    }
    if (strcmp(ev.event_name, "message_start") != 0) {
        TEST_FAIL("single_event", "event_name mismatch");
        return;
    }
    if (strcmp(ev.data, "{\"type\":\"message_start\"}") != 0) {
        TEST_FAIL("single_event", "data mismatch");
        return;
    }
    TEST_PASS("single_event");
}

/* Multiple events in one chunk */
static void test_multiple_events(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data =
        "event: message_start\n"
        "data: {\"type\":\"message_start\"}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"delta\":{\"text\":\"Hello\"}}\n"
        "\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n"
        "\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA || strcmp(ev.event_name, "message_start") != 0) {
        TEST_FAIL("multiple_events", "event 1 failed");
        return;
    }
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA || strcmp(ev.event_name, "content_block_delta") != 0) {
        TEST_FAIL("multiple_events", "event 2 failed");
        return;
    }
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA || strcmp(ev.event_name, "message_stop") != 0) {
        TEST_FAIL("multiple_events", "event 3 failed");
        return;
    }
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_NEED_MORE) {
        TEST_FAIL("multiple_events", "expected NEED_MORE after all events");
        return;
    }
    TEST_PASS("multiple_events");
}

/* Fragmented input across recv() calls */
static void test_fragmented(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *frag1 = "event: content_block_del";
    const char *frag2 = "ta\ndata: {\"type\":\"content_";
    const char *frag3 = "block_delta\"}\n\n";

    sse_parser_init(&p);

    p.consumed = 0;
    ret = sse_feed(&p, frag1, strlen(frag1), &ev);
    if (ret != SSE_EVENT_NEED_MORE) { TEST_FAIL("fragmented", "frag1"); return; }

    p.consumed = 0;
    ret = sse_feed(&p, frag2, strlen(frag2), &ev);
    if (ret != SSE_EVENT_NEED_MORE) { TEST_FAIL("fragmented", "frag2"); return; }

    p.consumed = 0;
    ret = sse_feed(&p, frag3, strlen(frag3), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("fragmented", "frag3 expected DATA"); return; }
    if (strcmp(ev.event_name, "content_block_delta") != 0) {
        TEST_FAIL("fragmented", "event_name mismatch"); return;
    }
    if (strcmp(ev.data, "{\"type\":\"content_block_delta\"}") != 0) {
        TEST_FAIL("fragmented", "data mismatch"); return;
    }
    TEST_PASS("fragmented");
}

/* Multiple data: lines concatenated with \n (W3C spec) */
static void test_multi_data(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\ndata: line1\ndata: line2\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("multi_data", "expected DATA"); return; }
    if (strcmp(ev.data, "line1\nline2") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("multi_data", msg); return;
    }
    TEST_PASS("multi_data");
}

/* Comment lines (: prefix) skipped */
static void test_comment(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = ": this is a comment\nevent: ping\ndata: {\"type\":\"ping\"}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("comment", "expected DATA"); return; }
    if (strcmp(ev.event_name, "ping") != 0) {
        TEST_FAIL("comment", "event_name mismatch"); return;
    }
    TEST_PASS("comment");
}

/* Empty data value */
static void test_empty_data(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\ndata: \n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("empty_data", "expected DATA"); return; }
    if (ev.data_len != 0) {
        char msg[64]; snprintf(msg, sizeof(msg), "data_len=%d", ev.data_len);
        TEST_FAIL("empty_data", msg); return;
    }
    TEST_PASS("empty_data");
}

/* CRLF line endings */
static void test_crlf(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\r\ndata: hello\r\n\r\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("crlf", "expected DATA"); return; }
    if (strcmp(ev.data, "hello") != 0) {
        TEST_FAIL("crlf", "data mismatch"); return;
    }
    TEST_PASS("crlf");
}

/* Default event name = "message" when no event: field */
static void test_default_event_name(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "data: hello\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("default_event_name", "expected DATA"); return; }
    if (strcmp(ev.event_name, "message") != 0) {
        TEST_FAIL("default_event_name", "expected 'message'"); return;
    }
    TEST_PASS("default_event_name");
}

/* ================================================================
 * SSE spec edge cases
 * ================================================================ */

/* Bare CR (\r) as line terminator (no LF) */
static void test_bare_cr(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\rdata: cr_only\r\r";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("bare_cr", "expected DATA"); return; }
    if (strcmp(ev.data, "cr_only") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("bare_cr", msg); return;
    }
    TEST_PASS("bare_cr");
}

/* id: field should be silently ignored */
static void test_id_field_ignored(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "id: 42\nevent: test\ndata: payload\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("id_field_ignored", "expected DATA"); return; }
    if (strcmp(ev.event_name, "test") != 0) {
        TEST_FAIL("id_field_ignored", "event_name corrupted"); return;
    }
    if (strcmp(ev.data, "payload") != 0) {
        TEST_FAIL("id_field_ignored", "data corrupted"); return;
    }
    TEST_PASS("id_field_ignored");
}

/* retry: field should be silently ignored */
static void test_retry_field_ignored(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "retry: 5000\nevent: test\ndata: ok\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("retry_field_ignored", "expected DATA"); return; }
    if (strcmp(ev.data, "ok") != 0) {
        TEST_FAIL("retry_field_ignored", "data mismatch"); return;
    }
    TEST_PASS("retry_field_ignored");
}

/* Multiple consecutive empty lines = only one dispatch */
static void test_consecutive_empty_lines(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    int count = 0;
    const char *data = "event: test\ndata: first\n\n\n\nevent: test\ndata: second\n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    while ((ret = sse_feed(&p, data, strlen(data), &ev)) == SSE_EVENT_DATA) {
        count++;
    }
    if (count != 2) {
        char msg[64]; snprintf(msg, sizeof(msg), "got %d events, expected 2", count);
        TEST_FAIL("consecutive_empty_lines", msg); return;
    }
    TEST_PASS("consecutive_empty_lines");
}

/* Field without colon: "data\n" = field "data" with empty value */
static void test_field_no_colon(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    /* "data" without colon means field "data" with empty value */
    const char *data = "event: test\ndata\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("field_no_colon", "expected DATA"); return; }
    /* Should dispatch with empty data (field name "data" matched, value = "") */
    if (ev.data_len != 0) {
        char msg[64]; snprintf(msg, sizeof(msg), "data_len=%d", ev.data_len);
        TEST_FAIL("field_no_colon", msg); return;
    }
    TEST_PASS("field_no_colon");
}

/* Unknown fields should be silently ignored */
static void test_unknown_field(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "unknown: value\nevent: test\ndata: ok\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("unknown_field", "expected DATA"); return; }
    if (strcmp(ev.data, "ok") != 0) {
        TEST_FAIL("unknown_field", "data corrupted"); return;
    }
    TEST_PASS("unknown_field");
}

/* Empty line before any data/event -> no dispatch (nothing to dispatch) */
static void test_empty_line_no_data(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "\n\nevent: test\ndata: ok\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    /* Leading empty lines with no pending data should not dispatch */
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("empty_line_no_data", "expected DATA"); return; }
    if (strcmp(ev.event_name, "test") != 0) {
        TEST_FAIL("empty_line_no_data", "wrong event"); return;
    }
    TEST_PASS("empty_line_no_data");
}

/* Three data: lines */
static void test_three_data_lines(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "data: a\ndata: b\ndata: c\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("three_data_lines", "expected DATA"); return; }
    if (strcmp(ev.data, "a\nb\nc") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("three_data_lines", msg); return;
    }
    TEST_PASS("three_data_lines");
}

/* ================================================================
 * Fragmentation edge cases
 * ================================================================ */

/* Fragment split at \n boundary: chunk1 ends at data line, chunk2="\n" dispatches */
static void test_fragment_at_newline(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *frag1 = "event: test\ndata: hi\n";
    const char *frag2 = "\n";

    sse_parser_init(&p);

    p.consumed = 0;
    ret = sse_feed(&p, frag1, strlen(frag1), &ev);
    if (ret != SSE_EVENT_NEED_MORE) { TEST_FAIL("fragment_at_newline", "frag1"); return; }

    p.consumed = 0;
    ret = sse_feed(&p, frag2, strlen(frag2), &ev);
    if (ret != SSE_EVENT_DATA) { TEST_FAIL("fragment_at_newline", "frag2 expected DATA"); return; }
    if (strcmp(ev.data, "hi") != 0) {
        TEST_FAIL("fragment_at_newline", "data mismatch"); return;
    }
    TEST_PASS("fragment_at_newline");
}

/* Four tiny fragments: each is just a few chars */
static void test_tiny_fragments(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *frags[] = {"ev", "ent", ": ", "t\n", "da", "ta:", " X", "\n\n"};
    int i;
    int got_event = 0;

    sse_parser_init(&p);

    for (i = 0; i < 8; i++) {
        p.consumed = 0;
        ret = sse_feed(&p, frags[i], strlen(frags[i]), &ev);
        if (ret == SSE_EVENT_DATA) {
            got_event = 1;
            break;
        }
    }
    if (!got_event) { TEST_FAIL("tiny_fragments", "no event"); return; }
    if (strcmp(ev.event_name, "t") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "event_name='%s'", ev.event_name);
        TEST_FAIL("tiny_fragments", msg); return;
    }
    if (strcmp(ev.data, "X") != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("tiny_fragments", msg); return;
    }
    TEST_PASS("tiny_fragments");
}

/* ================================================================
 * Claude API realistic streams
 * ================================================================ */

/* Full Claude text-only stream (8 events) */
static void test_claude_text_stream(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    int event_count = 0;
    const char *stream =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"mock\",\"stop_reason\":null,\"usage\":{\"input_tokens\":10,\"output_tokens\":1}}}\n"
        "\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n"
        "\n"
        "event: ping\n"
        "data: {\"type\":\"ping\"}\n"
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

    sse_parser_init(&p);
    p.consumed = 0;
    while ((ret = sse_feed(&p, stream, strlen(stream), &ev)) == SSE_EVENT_DATA)
        event_count++;

    if (event_count != 8) {
        char msg[64]; snprintf(msg, sizeof(msg), "got %d events, expected 8", event_count);
        TEST_FAIL("claude_text_stream", msg); return;
    }
    TEST_PASS("claude_text_stream");
}

/* Claude tool_use stream with text + tool (11 events) */
static void test_claude_tool_stream(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    int event_count = 0;
    const char *stream =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_t\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":20,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Let me check.\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_01\",\"name\":\"get_weather\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"loc\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"ation\\\": \\\"SF\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":30}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    while ((ret = sse_feed(&p, stream, strlen(stream), &ev)) == SSE_EVENT_DATA)
        event_count++;

    if (event_count != 10) {
        char msg[64]; snprintf(msg, sizeof(msg), "got %d events, expected 10", event_count);
        TEST_FAIL("claude_tool_stream", msg); return;
    }
    TEST_PASS("claude_tool_stream");
}

/* Stream with mid-stream error event */
static void test_claude_midstream_error(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    int event_count = 0;
    const char *last_event = "";
    const char *stream =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_err\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":5,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"I will\"}}\n\n"
        "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    while ((ret = sse_feed(&p, stream, strlen(stream), &ev)) == SSE_EVENT_DATA) {
        event_count++;
        last_event = ev.event_name[0] ? ev.event_name : "???";
        (void)last_event;
    }
    if (event_count != 4) {
        char msg[64]; snprintf(msg, sizeof(msg), "got %d events, expected 4", event_count);
        TEST_FAIL("claude_midstream_error", msg); return;
    }
    TEST_PASS("claude_midstream_error");
}

/* Stream with multiple parallel tool calls (3 content blocks) */
static void test_claude_parallel_tools(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    int event_count = 0;
    const char *stream =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_multi\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\",\"stop_reason\":null,\"usage\":{\"input_tokens\":50,\"output_tokens\":1}}}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Both.\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_a\",\"name\":\"get_weather\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"loc\\\": \\\"SF\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":2,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_b\",\"name\":\"get_time\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"tz\\\": \\\"PST\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":2}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":45}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;
    while ((ret = sse_feed(&p, stream, strlen(stream), &ev)) == SSE_EVENT_DATA)
        event_count++;

    if (event_count != 12) {
        char msg[64]; snprintf(msg, sizeof(msg), "got %d events, expected 12", event_count);
        TEST_FAIL("claude_parallel_tools", msg); return;
    }
    TEST_PASS("claude_parallel_tools");
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== SSE Parser Unit Tests ===\n\n");

    printf("[Basic protocol]\n");
    test_single_event();
    test_multiple_events();
    test_fragmented();
    test_multi_data();
    test_comment();
    test_empty_data();
    test_crlf();
    test_default_event_name();

    printf("\n[SSE spec edge cases]\n");
    test_bare_cr();
    test_id_field_ignored();
    test_retry_field_ignored();
    test_consecutive_empty_lines();
    test_field_no_colon();
    test_unknown_field();
    test_empty_line_no_data();
    test_three_data_lines();

    printf("\n[Fragmentation]\n");
    test_fragment_at_newline();
    test_tiny_fragments();

    printf("\n[Claude API streams]\n");
    test_claude_text_stream();
    test_claude_tool_stream();
    test_claude_midstream_error();
    test_claude_parallel_tools();

    printf("\n=== RESULT: %d/%d passed ===\n", passed, passed + failed);
    if (failed == 0)
        printf("ALL TESTS PASSED\n");

    return failed == 0 ? 0 : 1;
}
