/*
 * agent.c - Agent bringup test command (Phase A)
 *
 * Runs TCP connection tests, HTTP POST/GET, and JSON parse/write
 * against a mock HTTP server on the host (10.0.2.2:8080).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <http_client.h>
#include <json.h>

#define HOST_IP     "10.0.2.2"
#define HOST_PORT   8080

#define TEST_PASS(name) do { \
    debug_printf("[AGENT-TEST] %s ... PASS\n", name); \
    printf("[AGENT-TEST] %s ... PASS\n", name); \
    passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    debug_printf("[AGENT-TEST] %s ... FAIL (%s)\n", name, reason); \
    printf("[AGENT-TEST] %s ... FAIL (%s)\n", name, reason); \
    failed++; \
} while(0)

static int passed = 0;
static int failed = 0;

/* ---- AT-01: TCP connect/send/recv/close ---- */
static void test_tcp_connect(void)
{
    int sockfd;
    struct sockaddr_in addr;
    int ret;
    char send_data[] = "GET /healthz HTTP/1.1\r\nHost: 10.0.2.2\r\n\r\n";
    char recv_data[2048];
    u_int32_t timeout;

    debug_printf("[AGENT-TEST] tcp_connect %s:%d ...\n", HOST_IP, HOST_PORT);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        TEST_FAIL("tcp_connect", "socket() failed");
        return;
    }

    timeout = 5000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HOST_PORT);
    inet_aton(HOST_IP, &addr.sin_addr);

    ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        TEST_FAIL("tcp_connect", "connect() failed");
        closesocket(sockfd);
        return;
    }

    ret = send_msg(sockfd, send_data, strlen(send_data), 0);
    if (ret < 0) {
        TEST_FAIL("tcp_connect", "send() failed");
        closesocket(sockfd);
        return;
    }

    ret = recv_msg(sockfd, recv_data, sizeof(recv_data) - 1, 0);
    if (ret <= 0) {
        TEST_FAIL("tcp_connect", "recv() failed");
        closesocket(sockfd);
        return;
    }
    recv_data[ret] = '\0';

    closesocket(sockfd);

    /* Verify we got an HTTP response */
    if (strstr(recv_data, "HTTP/1.") != (char *)0)
        TEST_PASS("tcp_connect");
    else
        TEST_FAIL("tcp_connect", "no HTTP response");
}

/* ---- AT-02: connect/close cycle x3 ---- */
static void test_tcp_cycle(void)
{
    int i;
    int sockfd;
    struct sockaddr_in addr;
    int ret;
    u_int32_t timeout;

    for (i = 0; i < 3; i++) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            TEST_FAIL("tcp_cycle_3x", "socket() failed");
            return;
        }

        timeout = 5000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(HOST_PORT);
        inet_aton(HOST_IP, &addr.sin_addr);

        ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret < 0) {
            char msg[32];
            snprintf(msg, sizeof(msg), "connect() failed iter %d", i);
            TEST_FAIL("tcp_cycle_3x", msg);
            closesocket(sockfd);
            return;
        }

        closesocket(sockfd);
        debug_printf("[AGENT-TEST] tcp_cycle iter %d OK\n", i);
    }
    TEST_PASS("tcp_cycle_3x");
}

/* ---- AT-03: connect error handling ---- */
static void test_tcp_error(void)
{
    int sockfd;
    struct sockaddr_in addr;
    int ret;
    u_int32_t timeout;

    /* Try connecting to a port that should not be listening */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        TEST_FAIL("tcp_error_refused", "socket() failed");
        return;
    }

    timeout = 3000;  /* 3 second timeout */
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);  /* Unlikely to be listening */
    inet_aton(HOST_IP, &addr.sin_addr);

    ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    closesocket(sockfd);

    if (ret < 0) {
        debug_printf("[AGENT-TEST] tcp_error: connect returned %d (expected negative)\n", ret);
        TEST_PASS("tcp_error_refused");
    } else {
        TEST_FAIL("tcp_error_refused", "expected connect failure");
    }
}

/* ---- AT-05/06/07: HTTP GET /healthz ---- */
static void test_http_healthz(void)
{
    struct http_request req;
    struct http_response resp;
    char recv_buf[4096];
    int ret;

    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.host = HOST_IP;
    req.path = "/healthz";
    req.port = HOST_PORT;
    req.headers = (const struct http_header *)0;
    req.body = (const char *)0;
    req.body_len = 0;

    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);
    if (ret != HTTP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http_do_request failed: %d", ret);
        TEST_FAIL("http_get_healthz", msg);
        return;
    }

    if (resp.status_code == 200)
        TEST_PASS("http_get_healthz");
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "status=%d", resp.status_code);
        TEST_FAIL("http_get_healthz", msg);
    }
}

/* ---- HTTP POST /echo + JSON parse ---- */
static void test_http_echo_json(void)
{
    struct http_request req;
    struct http_response resp;
    char recv_buf[4096];
    char json_body[256];
    struct json_writer jw;
    struct http_header hdrs[2];
    int ret;
    struct json_parser jp;
    struct json_token tokens[64];
    int ntokens;
    int status_tok;
    char status_val[32];

    /* Build JSON body */
    jw_init(&jw, json_body, sizeof(json_body));
    jw_object_start(&jw);
    jw_key(&jw, "status");
    jw_string(&jw, "ok");
    jw_key(&jw, "count");
    jw_int(&jw, 42);
    jw_object_end(&jw);
    jw_finish(&jw);

    hdrs[0].name = "Content-Type";
    hdrs[0].value = "application/json";
    hdrs[1].name = (const char *)0;
    hdrs[1].value = (const char *)0;

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.host = HOST_IP;
    req.path = "/echo";
    req.port = HOST_PORT;
    req.headers = hdrs;
    req.body = json_body;
    req.body_len = strlen(json_body);

    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);
    if (ret != HTTP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http_do_request failed: %d", ret);
        TEST_FAIL("http_post_echo", msg);
        return;
    }

    if (resp.status_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "status=%d", resp.status_code);
        TEST_FAIL("http_post_echo", msg);
        return;
    }

    /* Parse the echoed JSON body */
    json_init(&jp);
    ntokens = json_parse(&jp, resp.body, resp.body_len, tokens, 64);
    if (ntokens < 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "json_parse failed: %d", ntokens);
        TEST_FAIL("http_post_echo_json", msg);
        return;
    }

    status_tok = json_find_key(resp.body, tokens, ntokens, 0, "status");
    if (status_tok < 0) {
        TEST_FAIL("http_post_echo_json", "key 'status' not found");
        return;
    }

    json_token_str(resp.body, &tokens[status_tok], status_val, sizeof(status_val));
    if (strcmp(status_val, "ok") == 0) {
        TEST_PASS("http_post_echo_json");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "status='%s'", status_val);
        TEST_FAIL("http_post_echo_json", msg);
    }
}

/* ---- HTTP POST /mock/claude ---- */
static void test_http_mock_claude(void)
{
    struct http_request req;
    struct http_response resp;
    char recv_buf[4096];
    char json_body[512];
    struct json_writer jw;
    struct http_header hdrs[2];
    int ret;
    struct json_parser jp;
    struct json_token tokens[128];
    int ntokens;
    int content_tok, first_elem, type_tok, text_tok;
    char type_val[32];
    char text_val[256];

    /* Build Claude API-like request */
    jw_init(&jw, json_body, sizeof(json_body));
    jw_object_start(&jw);
    jw_key(&jw, "model");
    jw_string(&jw, "claude-sonnet-4-20250514");
    jw_key(&jw, "max_tokens");
    jw_int(&jw, 1024);
    jw_key(&jw, "messages");
    jw_array_start(&jw);
    jw_object_start(&jw);
    jw_key(&jw, "role");
    jw_string(&jw, "user");
    jw_key(&jw, "content");
    jw_string(&jw, "Hello");
    jw_object_end(&jw);
    jw_array_end(&jw);
    jw_object_end(&jw);
    jw_finish(&jw);

    hdrs[0].name = "Content-Type";
    hdrs[0].value = "application/json";
    hdrs[1].name = (const char *)0;
    hdrs[1].value = (const char *)0;

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.host = HOST_IP;
    req.path = "/mock/claude";
    req.port = HOST_PORT;
    req.headers = hdrs;
    req.body = json_body;
    req.body_len = strlen(json_body);

    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);
    if (ret != HTTP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http_do_request failed: %d", ret);
        TEST_FAIL("http_mock_claude", msg);
        return;
    }

    if (resp.status_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "status=%d", resp.status_code);
        TEST_FAIL("http_mock_claude", msg);
        return;
    }

    /* Parse response JSON */
    json_init(&jp);
    ntokens = json_parse(&jp, resp.body, resp.body_len, tokens, 128);
    if (ntokens < 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "json_parse failed: %d", ntokens);
        TEST_FAIL("http_mock_claude_parse", msg);
        return;
    }

    /* Navigate: content[0].type == "text" */
    content_tok = json_find_key(resp.body, tokens, ntokens, 0, "content");
    if (content_tok < 0) {
        TEST_FAIL("http_mock_claude_parse", "key 'content' not found");
        return;
    }
    first_elem = json_array_get(tokens, ntokens, content_tok, 0);
    if (first_elem < 0) {
        TEST_FAIL("http_mock_claude_parse", "content[0] not found");
        return;
    }
    type_tok = json_find_key(resp.body, tokens, ntokens, first_elem, "type");
    if (type_tok < 0) {
        TEST_FAIL("http_mock_claude_parse", "content[0].type not found");
        return;
    }
    json_token_str(resp.body, &tokens[type_tok], type_val, sizeof(type_val));

    text_tok = json_find_key(resp.body, tokens, ntokens, first_elem, "text");
    if (text_tok >= 0) {
        json_token_str(resp.body, &tokens[text_tok], text_val, sizeof(text_val));
    } else {
        text_val[0] = '\0';
    }

    if (strcmp(type_val, "text") == 0) {
        debug_printf("[AGENT-TEST] content[0].type=\"text\", text=\"%s\"\n", text_val);
        TEST_PASS("http_mock_claude_parse");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "type='%s'", type_val);
        TEST_FAIL("http_mock_claude_parse", msg);
    }
}

/* ---- HTTP POST /mock/claude/error → 429 ---- */
static void test_http_mock_claude_error(void)
{
    struct http_request req;
    struct http_response resp;
    char recv_buf[4096];
    struct http_header hdrs[2];
    int ret;

    hdrs[0].name = "Content-Type";
    hdrs[0].value = "application/json";
    hdrs[1].name = (const char *)0;
    hdrs[1].value = (const char *)0;

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.host = HOST_IP;
    req.path = "/mock/claude/error";
    req.port = HOST_PORT;
    req.headers = hdrs;
    req.body = "{}";
    req.body_len = 2;

    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);
    if (ret != HTTP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http_do_request failed: %d", ret);
        TEST_FAIL("http_mock_claude_429", msg);
        return;
    }

    if (resp.status_code == 429 && resp.retry_after > 0) {
        debug_printf("[AGENT-TEST] 429 retry_after=%d\n", resp.retry_after);
        TEST_PASS("http_mock_claude_429");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "status=%d, retry_after=%d",
                resp.status_code, resp.retry_after);
        TEST_FAIL("http_mock_claude_429", msg);
    }
}

/* ---- HTTP GET / → HTML ---- */
static void test_http_get_html(void)
{
    struct http_request req;
    struct http_response resp;
    char recv_buf[4096];
    int ret;

    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.host = HOST_IP;
    req.path = "/";
    req.port = HOST_PORT;

    ret = http_do_request(&req, recv_buf, sizeof(recv_buf), &resp);
    if (ret != HTTP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http_do_request failed: %d", ret);
        TEST_FAIL("http_get_html", msg);
        return;
    }

    if (resp.status_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "status=%d", resp.status_code);
        TEST_FAIL("http_get_html", msg);
        return;
    }

    /* Check that we got HTML back */
    if (strstr(resp.content_type, "text/html") != (char *)0 &&
        strstr(resp.body, "<html>") != (char *)0) {
        /* Print the HTML to VGA console */
        printf("\n--- Fetched HTML (%d bytes) ---\n", resp.body_len);
        write(1, resp.body, resp.body_len);
        printf("\n--- End HTML ---\n");
        TEST_PASS("http_get_html");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "ct=%s", resp.content_type);
        TEST_FAIL("http_get_html", msg);
    }
}

/* ---- JSON standalone tests ---- */
static void test_json_parser(void)
{
    const char *js = "{\"name\":\"Jack\",\"age\":27,\"items\":[1,2,3],\"active\":true,\"data\":null}";
    struct json_parser jp;
    struct json_token tokens[32];
    int n;
    int tok;
    char str_val[32];
    int int_val;
    int bool_val;

    json_init(&jp);
    n = json_parse(&jp, js, strlen(js), tokens, 32);
    if (n < 0) {
        char msg[32];
        snprintf(msg, sizeof(msg), "parse err %d", n);
        TEST_FAIL("json_parse_basic", msg);
        return;
    }

    /* Find "name" -> "Jack" */
    tok = json_find_key(js, tokens, n, 0, "name");
    if (tok < 0) { TEST_FAIL("json_parse_basic", "key 'name' not found"); return; }
    json_token_str(js, &tokens[tok], str_val, sizeof(str_val));
    if (strcmp(str_val, "Jack") != 0) { TEST_FAIL("json_parse_basic", "name mismatch"); return; }

    /* Find "age" -> 27 */
    tok = json_find_key(js, tokens, n, 0, "age");
    if (tok < 0) { TEST_FAIL("json_parse_basic", "key 'age' not found"); return; }
    json_token_int(js, &tokens[tok], &int_val);
    if (int_val != 27) { TEST_FAIL("json_parse_basic", "age mismatch"); return; }

    /* Find "items" -> array, items[1] = 2 */
    tok = json_find_key(js, tokens, n, 0, "items");
    if (tok < 0) { TEST_FAIL("json_parse_basic", "key 'items' not found"); return; }
    {
        int elem = json_array_get(tokens, n, tok, 1);
        if (elem < 0) { TEST_FAIL("json_parse_basic", "items[1] not found"); return; }
        json_token_int(js, &tokens[elem], &int_val);
        if (int_val != 2) { TEST_FAIL("json_parse_basic", "items[1] mismatch"); return; }
    }

    /* Find "active" -> true */
    tok = json_find_key(js, tokens, n, 0, "active");
    if (tok < 0) { TEST_FAIL("json_parse_basic", "key 'active' not found"); return; }
    json_token_bool(js, &tokens[tok], &bool_val);
    if (bool_val != 1) { TEST_FAIL("json_parse_basic", "active mismatch"); return; }

    /* Find "data" -> null */
    tok = json_find_key(js, tokens, n, 0, "data");
    if (tok < 0) { TEST_FAIL("json_parse_basic", "key 'data' not found"); return; }
    if (tokens[tok].type != JSON_NULL) { TEST_FAIL("json_parse_basic", "data not null"); return; }

    TEST_PASS("json_parse_basic");
}

static void test_json_writer(void)
{
    char buf[512];
    struct json_writer jw;
    const char *expected = "{\"model\":\"claude-sonnet-4-20250514\",\"max_tokens\":1024,\"messages\":[{\"role\":\"user\",\"content\":\"Hello\"}]}";

    jw_init(&jw, buf, sizeof(buf));
    jw_object_start(&jw);
    jw_key(&jw, "model");
    jw_string(&jw, "claude-sonnet-4-20250514");
    jw_key(&jw, "max_tokens");
    jw_int(&jw, 1024);
    jw_key(&jw, "messages");
    jw_array_start(&jw);
    jw_object_start(&jw);
    jw_key(&jw, "role");
    jw_string(&jw, "user");
    jw_key(&jw, "content");
    jw_string(&jw, "Hello");
    jw_object_end(&jw);
    jw_array_end(&jw);
    jw_object_end(&jw);

    if (jw_finish(&jw) < 0) {
        TEST_FAIL("json_writer", "jw_finish failed");
        return;
    }

    if (strcmp(buf, expected) == 0) {
        TEST_PASS("json_writer");
    } else {
        debug_printf("[AGENT-TEST] json_writer: got '%s'\n", buf);
        debug_printf("[AGENT-TEST] json_writer: exp '%s'\n", expected);
        TEST_FAIL("json_writer", "output mismatch");
    }
}

static void test_json_escape(void)
{
    const char *js = "{\"msg\":\"hello\\nworld\\t\\\"quoted\\\"\"}";
    struct json_parser jp;
    struct json_token tokens[16];
    int n, tok;
    char val[64];

    json_init(&jp);
    n = json_parse(&jp, js, strlen(js), tokens, 16);
    if (n < 0) {
        TEST_FAIL("json_escape", "parse failed");
        return;
    }

    tok = json_find_key(js, tokens, n, 0, "msg");
    if (tok < 0) {
        TEST_FAIL("json_escape", "key not found");
        return;
    }

    json_token_str(js, &tokens[tok], val, sizeof(val));
    if (strcmp(val, "hello\nworld\t\"quoted\"") == 0)
        TEST_PASS("json_escape");
    else {
        debug_printf("[AGENT-TEST] json_escape: got '%s'\n", val);
        TEST_FAIL("json_escape", "mismatch");
    }
}

/* ---- Main ---- */
int main(int argc, char *argv[])
{
    debug_printf("[AGENT-TEST] === Phase A Bringup Test Start ===\n");
    printf("[AGENT-TEST] === Phase A Bringup Test Start ===\n");

    /* JSON standalone tests (no network) */
    test_json_parser();
    test_json_writer();
    test_json_escape();

    /* TCP tests */
    test_tcp_connect();
    test_tcp_cycle();
    test_tcp_error();

    /* HTTP + JSON integration tests */
    test_http_healthz();
    test_http_get_html();
    test_http_echo_json();
    test_http_mock_claude();
    test_http_mock_claude_error();

    debug_printf("[AGENT-TEST] === RESULT: %d/%d passed ===\n",
                passed, passed + failed);
    printf("[AGENT-TEST] === RESULT: %d/%d passed ===\n",
          passed, passed + failed);

    if (failed == 0) {
        debug_printf("[AGENT-TEST] ALL TESTS PASSED\n");
        printf("[AGENT-TEST] ALL TESTS PASSED\n");
    }

    exit(failed == 0 ? 0 : 1);
    return 0;
}
