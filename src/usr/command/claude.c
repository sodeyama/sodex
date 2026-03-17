/*
 * claude.c - Claude API smoke test command (Phase C)
 *
 * Tests SSE streaming from a mock Claude server via TLS.
 * Usage: claude [host] [port]
 *   Default: 10.0.2.2:4443
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <http_client.h>
#include <json.h>
#include <sse_parser.h>
#include <tls_client.h>
#include <entropy.h>
#include <agent/claude_adapter.h>
#include <agent/llm_provider.h>
#include <agent/claude_client.h>

#define DEFAULT_HOST  "10.0.2.2"
#define DEFAULT_PORT  4443

#define TEST_PASS(name) do { \
    debug_printf("[CLAUDE-TEST] %s ... PASS\n", name); \
    printf("[CLAUDE-TEST] %s ... PASS\n", name); \
    passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    debug_printf("[CLAUDE-TEST] %s ... FAIL (%s)\n", name, reason); \
    printf("[CLAUDE-TEST] %s ... FAIL (%s)\n", name, reason); \
    failed++; \
} while(0)

static int passed = 0;
static int failed = 0;

/* Mock provider that overrides endpoint to point at local mock server */
static struct api_endpoint mock_endpoint;
static struct api_header mock_headers[] = {
    { "content-type",      "application/json" },
    { "anthropic-version", "2023-06-01" },
    { "x-api-key",         "test-key-mock" },
    { (const char *)0,     (const char *)0 }
};

static struct llm_provider mock_provider;

static void init_mock_provider(const char *host, int port)
{
    mock_endpoint.host = host;
    mock_endpoint.path = "/v1/messages";
    mock_endpoint.port = port;

    mock_provider.name = "mock-claude";
    mock_provider.endpoint = &mock_endpoint;
    mock_provider.headers = mock_headers;
    mock_provider.header_count = 3;
    mock_provider.build_request = claude_build_request;
    mock_provider.parse_sse_event = claude_parse_sse_event;
    mock_provider.parse_response = claude_parse_response;
}

/* ---- Test 1: Text response ---- */
static void test_text_response(void)
{
    struct claude_response resp;
    int ret;

    debug_printf("[CLAUDE-TEST] 1. text response ...\n");
    printf("[CLAUDE-TEST] 1. text response ...\n");

    ret = claude_send_message_with_key(&mock_provider,
                                        "Say hello",
                                        "test-key-mock",
                                        &resp);
    if (ret != CLAUDE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "ret=%d", ret);
        TEST_FAIL("text_response", msg);
        return;
    }

    if (resp.stop_reason != CLAUDE_STOP_END_TURN) {
        char msg[64];
        snprintf(msg, sizeof(msg), "stop_reason=%d", resp.stop_reason);
        TEST_FAIL("text_response", msg);
        return;
    }

    if (resp.block_count < 1) {
        TEST_FAIL("text_response", "no blocks");
        return;
    }

    if (resp.blocks[0].type != CLAUDE_CONTENT_TEXT) {
        TEST_FAIL("text_response", "block 0 not text");
        return;
    }

    debug_printf("[CLAUDE-TEST] text=\"%s\"\n", resp.blocks[0].text.text);
    printf("[CLAUDE-TEST] text=\"%s\"\n", resp.blocks[0].text.text);

    /* Check response contains "mock Claude" */
    if (strstr(resp.blocks[0].text.text, "mock Claude") != (char *)0) {
        TEST_PASS("text_response");
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unexpected text: %s", resp.blocks[0].text.text);
        TEST_FAIL("text_response", msg);
    }
}

/* ---- Test 2: SSE parsing standalone ---- */
static void test_sse_parser_basic(void)
{
    struct sse_parser sp;
    struct sse_event ev;
    int ret;
    int count = 0;
    const char *data =
        "event: message_start\n"
        "data: {\"type\":\"message_start\"}\n"
        "\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n"
        "\n";

    debug_printf("[CLAUDE-TEST] 2. SSE parser ...\n");

    sse_parser_init(&sp);
    sp.consumed = 0;
    while ((ret = sse_feed(&sp, data, strlen(data), &ev)) == SSE_EVENT_DATA) {
        count++;
    }

    if (count == 2)
        TEST_PASS("sse_parser_basic");
    else {
        char msg[32];
        snprintf(msg, sizeof(msg), "got %d events", count);
        TEST_FAIL("sse_parser_basic", msg);
    }
}

/* ---- Test 3: JSON + Claude adapter ---- */
static void test_adapter_roundtrip(void)
{
    char buf[2048];
    struct json_writer jw;
    struct claude_message msgs[1];
    struct claude_response resp;
    int ret;

    debug_printf("[CLAUDE-TEST] 3. adapter roundtrip ...\n");

    /* Build request */
    msgs[0].role = "user";
    msgs[0].content = "Hello";

    jw_init(&jw, buf, sizeof(buf));
    ret = claude_build_request(&jw, "test-model", msgs, 1,
                               (const char *)0, 1024, 1);
    if (ret != CLAUDE_OK) {
        TEST_FAIL("adapter_roundtrip", "build failed");
        return;
    }

    /* Parse non-streaming response */
    const char *json =
        "{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"OK\"}],"
        "\"model\":\"test\",\"stop_reason\":\"end_turn\","
        "\"usage\":{\"input_tokens\":5,\"output_tokens\":2}}";

    ret = claude_parse_response(json, strlen(json), &resp);
    if (ret != CLAUDE_OK) {
        TEST_FAIL("adapter_roundtrip", "parse failed");
        return;
    }

    if (resp.stop_reason == CLAUDE_STOP_END_TURN &&
        strcmp(resp.blocks[0].text.text, "OK") == 0) {
        TEST_PASS("adapter_roundtrip");
    } else {
        TEST_FAIL("adapter_roundtrip", "mismatch");
    }
}

/* ---- Main ---- */
int main(int argc, char *argv[])
{
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2)
        host = argv[1];
    if (argc >= 3)
        port = strtol(argv[2], (char **)0, 10);

    debug_printf("[CLAUDE-TEST] === Phase C Claude Integration Test ===\n");
    printf("[CLAUDE-TEST] === Phase C Claude Integration Test ===\n");
    debug_printf("[CLAUDE-TEST] target=%s:%d\n", host, port);
    printf("[CLAUDE-TEST] target=%s:%d\n", host, port);

    /* Init mock provider */
    init_mock_provider(host, port);

    /* Init entropy + PRNG (needed for TLS) */
    entropy_init();
    entropy_collect_jitter(512);
    if (prng_init() < 0) {
        debug_printf("[CLAUDE-TEST] PRNG init failed\n");
        printf("[CLAUDE-TEST] PRNG init failed\n");
    }

    /* Local tests (no network) */
    test_sse_parser_basic();
    test_adapter_roundtrip();

    /* Network test (TLS + SSE) */
    test_text_response();

    debug_printf("[CLAUDE-TEST] === RESULT: %d/%d passed ===\n",
                passed, passed + failed);
    printf("[CLAUDE-TEST] === RESULT: %d/%d passed ===\n",
          passed, passed + failed);

    if (failed == 0) {
        debug_printf("[CLAUDE-TEST] ALL TESTS PASSED\n");
        printf("[CLAUDE-TEST] ALL TESTS PASSED\n");
    }

    exit(failed == 0 ? 0 : 1);
    return 0;
}
