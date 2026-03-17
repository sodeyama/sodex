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

/* ---- Test 1: Complete single event ---- */
static void test_single_event(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: message_start\ndata: {\"type\":\"message_start\"}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    /* Feed entire chunk */
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("single_event", "expected SSE_EVENT_DATA");
        return;
    }

    if (strcmp(ev.event_name, "message_start") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "event_name='%s'", ev.event_name);
        TEST_FAIL("single_event", msg);
        return;
    }

    if (strcmp(ev.data, "{\"type\":\"message_start\"}") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("single_event", msg);
        return;
    }

    TEST_PASS("single_event");
}

/* ---- Test 2: Multiple events in one chunk ---- */
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

    /* First event */
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA || strcmp(ev.event_name, "message_start") != 0) {
        TEST_FAIL("multiple_events", "event 1 failed");
        return;
    }

    /* Second event (continue with same chunk, updated consumed offset) */
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA || strcmp(ev.event_name, "content_block_delta") != 0) {
        TEST_FAIL("multiple_events", "event 2 failed");
        return;
    }

    /* Third event */
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA || strcmp(ev.event_name, "message_stop") != 0) {
        TEST_FAIL("multiple_events", "event 3 failed");
        return;
    }

    /* No more events */
    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_NEED_MORE) {
        TEST_FAIL("multiple_events", "expected NEED_MORE after all events");
        return;
    }

    TEST_PASS("multiple_events");
}

/* ---- Test 3: Fragmented input ---- */
static void test_fragmented(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *frag1 = "event: content_block_del";
    const char *frag2 = "ta\ndata: {\"type\":\"content_";
    const char *frag3 = "block_delta\"}\n\n";

    sse_parser_init(&p);

    /* Fragment 1 */
    p.consumed = 0;
    ret = sse_feed(&p, frag1, strlen(frag1), &ev);
    if (ret != SSE_EVENT_NEED_MORE) {
        TEST_FAIL("fragmented", "frag1: expected NEED_MORE");
        return;
    }

    /* Fragment 2 */
    p.consumed = 0;
    ret = sse_feed(&p, frag2, strlen(frag2), &ev);
    if (ret != SSE_EVENT_NEED_MORE) {
        TEST_FAIL("fragmented", "frag2: expected NEED_MORE");
        return;
    }

    /* Fragment 3 - should complete the event */
    p.consumed = 0;
    ret = sse_feed(&p, frag3, strlen(frag3), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("fragmented", "frag3: expected SSE_EVENT_DATA");
        return;
    }

    if (strcmp(ev.event_name, "content_block_delta") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "event_name='%s'", ev.event_name);
        TEST_FAIL("fragmented", msg);
        return;
    }

    if (strcmp(ev.data, "{\"type\":\"content_block_delta\"}") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("fragmented", msg);
        return;
    }

    TEST_PASS("fragmented");
}

/* ---- Test 4: Multiple data lines (concatenation) ---- */
static void test_multi_data(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\ndata: line1\ndata: line2\n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("multi_data", "expected SSE_EVENT_DATA");
        return;
    }

    /* Per W3C spec, multiple data lines are concatenated with \n */
    if (strcmp(ev.data, "line1\nline2") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("multi_data", msg);
        return;
    }

    TEST_PASS("multi_data");
}

/* ---- Test 5: Comment lines ---- */
static void test_comment(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = ": this is a comment\nevent: ping\ndata: {\"type\":\"ping\"}\n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("comment", "expected SSE_EVENT_DATA");
        return;
    }

    if (strcmp(ev.event_name, "ping") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "event_name='%s'", ev.event_name);
        TEST_FAIL("comment", msg);
        return;
    }

    TEST_PASS("comment");
}

/* ---- Test 6: Empty data ---- */
static void test_empty_data(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\ndata: \n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("empty_data", "expected SSE_EVENT_DATA");
        return;
    }

    if (ev.data_len != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "data_len=%d", ev.data_len);
        TEST_FAIL("empty_data", msg);
        return;
    }

    TEST_PASS("empty_data");
}

/* ---- Test 7: CRLF line endings ---- */
static void test_crlf(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "event: test\r\ndata: hello\r\n\r\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("crlf", "expected SSE_EVENT_DATA");
        return;
    }

    if (strcmp(ev.data, "hello") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "data='%s'", ev.data);
        TEST_FAIL("crlf", msg);
        return;
    }

    TEST_PASS("crlf");
}

/* ---- Test 8: Default event name ---- */
static void test_default_event_name(void)
{
    struct sse_parser p;
    struct sse_event ev;
    int ret;
    const char *data = "data: hello\n\n";

    sse_parser_init(&p);
    p.consumed = 0;

    ret = sse_feed(&p, data, strlen(data), &ev);
    if (ret != SSE_EVENT_DATA) {
        TEST_FAIL("default_event_name", "expected SSE_EVENT_DATA");
        return;
    }

    if (strcmp(ev.event_name, "message") != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "event_name='%s'", ev.event_name);
        TEST_FAIL("default_event_name", msg);
        return;
    }

    TEST_PASS("default_event_name");
}

/* ---- Test 9: Claude text-only SSE stream ---- */
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

    while ((ret = sse_feed(&p, stream, strlen(stream), &ev)) == SSE_EVENT_DATA) {
        event_count++;
    }

    if (event_count != 8) {
        char msg[64];
        snprintf(msg, sizeof(msg), "got %d events, expected 8", event_count);
        TEST_FAIL("claude_text_stream", msg);
        return;
    }

    TEST_PASS("claude_text_stream");
}

int main(void)
{
    printf("=== SSE Parser Unit Tests ===\n");

    test_single_event();
    test_multiple_events();
    test_fragmented();
    test_multi_data();
    test_comment();
    test_empty_data();
    test_crlf();
    test_default_event_name();
    test_claude_text_stream();

    printf("\n=== RESULT: %d/%d passed ===\n", passed, passed + failed);
    if (failed == 0)
        printf("ALL TESTS PASSED\n");

    return failed == 0 ? 0 : 1;
}
