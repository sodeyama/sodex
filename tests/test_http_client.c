/*
 * test_http_client.c - Host-side unit test for HTTP client
 *
 * Tests request building and response header parsing only (no networking).
 * Compile: cc -I ../src/usr/include -o test_http tests/test_http_client.c \
 *          src/usr/lib/libagent/http_client.c tests/test_stubs.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_client.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while(0)

/* ---- Request builder tests ---- */

static void test_build_get(void)
{
    char buf[512];
    struct http_request req;
    int len;

    TEST_START("build_get");
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.host = "10.0.2.2";
    req.path = "/healthz";
    req.port = 8080;

    len = http_build_request(buf, sizeof(buf), &req);
    ASSERT(len > 0, "build failed");
    ASSERT(strstr(buf, "GET /healthz HTTP/1.1\r\n") != NULL, "request line");
    ASSERT(strstr(buf, "Host: 10.0.2.2\r\n") != NULL, "Host header");
    ASSERT(strstr(buf, "Connection: close\r\n") != NULL, "Connection header");
    ASSERT(strstr(buf, "\r\n\r\n") != NULL, "header terminator");
    ASSERT(strstr(buf, "Content-Length") == NULL, "no Content-Length for GET");
    TEST_PASS("build_get");
}

static void test_build_post(void)
{
    char buf[1024];
    struct http_request req;
    struct http_header hdrs[3];
    int len;
    const char *body = "{\"hello\":\"world\"}";

    TEST_START("build_post");
    hdrs[0].name = "Content-Type";
    hdrs[0].value = "application/json";
    hdrs[1].name = "x-api-key";
    hdrs[1].value = "sk-ant-test";
    hdrs[2].name = NULL;
    hdrs[2].value = NULL;

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.host = "api.anthropic.com";
    req.path = "/v1/messages";
    req.port = 443;
    req.headers = hdrs;
    req.body = body;
    req.body_len = strlen(body);

    len = http_build_request(buf, sizeof(buf), &req);
    ASSERT(len > 0, "build failed");
    ASSERT(strstr(buf, "POST /v1/messages HTTP/1.1\r\n") != NULL, "request line");
    ASSERT(strstr(buf, "Host: api.anthropic.com\r\n") != NULL, "Host header");
    ASSERT(strstr(buf, "Content-Type: application/json\r\n") != NULL, "CT header");
    ASSERT(strstr(buf, "x-api-key: sk-ant-test\r\n") != NULL, "api-key header");
    ASSERT(strstr(buf, "Content-Length: 17\r\n") != NULL, "Content-Length");
    /* Body should follow headers */
    {
        char *body_start = strstr(buf, "\r\n\r\n");
        ASSERT(body_start != NULL, "header terminator");
        body_start += 4;
        ASSERT(strncmp(body_start, body, strlen(body)) == 0, "body content");
    }
    TEST_PASS("build_post");
}

static void test_build_overflow(void)
{
    char buf[16];
    struct http_request req;
    int len;

    TEST_START("build_overflow");
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.host = "example.com";
    req.path = "/very/long/path/that/wont/fit";
    req.port = 80;

    len = http_build_request(buf, sizeof(buf), &req);
    ASSERT(len == HTTP_ERR_BUF_OVERFLOW, "should overflow");
    TEST_PASS("build_overflow");
}

/* ---- Response parser tests ---- */

static void test_parse_200(void)
{
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 15\r\n"
        "\r\n"
        "{\"status\":\"ok\"}";
    struct http_response r;
    int body_offset;

    TEST_START("parse_200");
    body_offset = http_parse_response_headers(resp, strlen(resp), &r);
    ASSERT(body_offset > 0, "parse failed");
    ASSERT(r.status_code == 200, "status code");
    ASSERT(strcmp(r.status_text, "OK") == 0, "status text");
    ASSERT(r.content_length == 15, "content length");
    ASSERT(strstr(r.content_type, "application/json") != NULL, "content type");
    ASSERT(r.is_chunked == 0, "not chunked");
    ASSERT(r.is_sse == 0, "not SSE");
    ASSERT(r.retry_after == -1, "no retry-after");
    /* Check body offset */
    ASSERT(strcmp(resp + body_offset, "{\"status\":\"ok\"}") == 0, "body content");
    TEST_PASS("parse_200");
}

static void test_parse_429(void)
{
    const char *resp =
        "HTTP/1.1 429 Too Many Requests\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 50\r\n"
        "Retry-After: 30\r\n"
        "\r\n"
        "{\"type\":\"error\",\"error\":{\"type\":\"rate_limit_error\"}}";
    struct http_response r;
    int body_offset;

    TEST_START("parse_429");
    body_offset = http_parse_response_headers(resp, strlen(resp), &r);
    ASSERT(body_offset > 0, "parse failed");
    ASSERT(r.status_code == 429, "status code");
    ASSERT(r.retry_after == 30, "retry-after");
    TEST_PASS("parse_429");
}

static void test_parse_sse(void)
{
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "\r\n";
    struct http_response r;
    int body_offset;

    TEST_START("parse_sse");
    body_offset = http_parse_response_headers(resp, strlen(resp), &r);
    ASSERT(body_offset > 0, "parse failed");
    ASSERT(r.status_code == 200, "status code");
    ASSERT(r.is_sse == 1, "should be SSE");
    ASSERT(r.content_length == -1, "no content length for SSE");
    TEST_PASS("parse_sse");
}

static void test_parse_500(void)
{
    const char *resp =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "error";
    struct http_response r;
    int body_offset;

    TEST_START("parse_500");
    body_offset = http_parse_response_headers(resp, strlen(resp), &r);
    ASSERT(body_offset > 0, "parse failed");
    ASSERT(r.status_code == 500, "status code");
    ASSERT(r.content_length == 5, "content length");
    TEST_PASS("parse_500");
}

static void test_parse_incomplete(void)
{
    const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: text";
    struct http_response r;
    int body_offset;

    TEST_START("parse_incomplete");
    body_offset = http_parse_response_headers(resp, strlen(resp), &r);
    ASSERT(body_offset < 0, "should fail (no header terminator)");
    TEST_PASS("parse_incomplete");
}

static void test_body_complete(void)
{
    struct http_response r;

    TEST_START("body_complete");
    memset(&r, 0, sizeof(r));
    r.content_length = 100;
    ASSERT(http_body_complete(&r, 50) == 0, "not complete at 50");
    ASSERT(http_body_complete(&r, 100) == 1, "complete at 100");
    ASSERT(http_body_complete(&r, 150) == 1, "complete at 150");

    r.content_length = -1;
    ASSERT(http_body_complete(&r, 100) == 0, "unknown length never complete");

    TEST_PASS("body_complete");
}

int main(void)
{
    printf("=== HTTP Client Unit Tests ===\n\n");

    /* Request builder */
    test_build_get();
    test_build_post();
    test_build_overflow();

    /* Response parser */
    test_parse_200();
    test_parse_429();
    test_parse_sse();
    test_parse_500();
    test_parse_incomplete();
    test_body_complete();

    printf("\n=== RESULT: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
