#include "test_framework.h"
#include <stdint.h>
#include <string.h>

#define TEST_BUILD 1
#include "../src/include/admin_server.h"

static u_int32_t test_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    u_int32_t value = 0;
    uint8_t *raw = (uint8_t *)&value;
    raw[0] = a;
    raw[1] = b;
    raw[2] = c;
    raw[3] = d;
    return value;
}

TEST(parse_health_request) {
    struct http_request req;
    const char *raw =
        "GET /healthz HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    ASSERT_EQ(http_parse_request(raw, (int)strlen(raw), &req), 0);
    ASSERT_STR_EQ(req.method, "GET");
    ASSERT_STR_EQ(req.path, "/healthz");
    ASSERT_STR_EQ(req.token, "");
}

TEST(parse_authorization_header) {
    struct http_request req;
    const char *raw =
        "GET /status HTTP/1.1\r\n"
        "Authorization: Bearer status-secret\r\n"
        "\r\n";

    ASSERT_EQ(http_parse_request(raw, (int)strlen(raw), &req), 0);
    ASSERT_STR_EQ(req.method, "GET");
    ASSERT_STR_EQ(req.path, "/status");
    ASSERT_STR_EQ(req.token, "status-secret");
}

TEST(map_http_to_admin_actions) {
    struct http_request http_req;
    struct admin_request admin_req;

    memset(&http_req, 0, sizeof(http_req));
    strcpy(http_req.method, "POST");
    strcpy(http_req.path, "/agent/start");
    strcpy(http_req.token, "control-secret");

    ASSERT_EQ(http_map_request(&http_req, &admin_req), 0);
    ASSERT_EQ(admin_req.action, ADMIN_ACTION_AGENT_START);
    ASSERT_EQ(admin_req.required_role, ADMIN_ROLE_CONTROL);
    ASSERT_STR_EQ(admin_req.token, "control-secret");
}

TEST(status_request_roundtrip) {
    struct http_request http_req;
    struct admin_request admin_req;
    char body[ADMIN_RESPONSE_MAX];
    const char *raw =
        "GET /status HTTP/1.1\r\n"
        "Authorization: Bearer status-secret\r\n"
        "\r\n";

    admin_runtime_reset();
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_set_allow_ip(test_ip(10, 0, 2, 2));
    admin_runtime_set_tick(4321);

    ASSERT_EQ(http_parse_request(raw, (int)strlen(raw), &http_req), 0);
    ASSERT_EQ(http_map_request(&http_req, &admin_req), 0);
    ASSERT(admin_authorize_request(&admin_req, test_ip(10, 0, 2, 2)));
    ASSERT(admin_execute_request(&admin_req, body, sizeof(body), 1) > 0);
    ASSERT(strstr(body, "\"agent\":\"stopped\"") != 0);
    ASSERT(strstr(body, "\"uptime_ticks\":4321") != 0);
}

TEST(build_http_response) {
    char response[ADMIN_RESPONSE_MAX];

    ASSERT(http_build_response(200, "ok\n", response, sizeof(response), "text/plain") > 0);
    ASSERT(strstr(response, "HTTP/1.1 200 OK") != 0);
    ASSERT(strstr(response, "Content-Length: 3") != 0);
    ASSERT(strstr(response, "\r\n\r\nok\n") != 0);
}

TEST(http_auth_rate_limit_recovers_after_backoff) {
    struct http_request http_req;
    struct admin_request admin_req;
    u_int32_t allowed_ip = test_ip(10, 0, 2, 2);
    u_int32_t retry_after = 0;
    const char *bad_raw =
        "GET /status HTTP/1.1\r\n"
        "Authorization: Bearer wrong-secret\r\n"
        "\r\n";
    const char *good_raw =
        "GET /status HTTP/1.1\r\n"
        "Authorization: Bearer status-secret\r\n"
        "\r\n";

    admin_runtime_reset();
    admin_runtime_set_tick(0);
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_set_allow_ip(allowed_ip);

    ASSERT_EQ(http_parse_request(bad_raw, (int)strlen(bad_raw), &http_req), 0);
    ASSERT_EQ(http_map_request(&http_req, &admin_req), 0);

    ASSERT_EQ(admin_authorize_request_detailed(&admin_req, allowed_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_request_detailed(&admin_req, allowed_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_request_detailed(&admin_req, allowed_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_request_detailed(&admin_req, allowed_ip, &retry_after), ADMIN_AUTH_THROTTLED);
    ASSERT_EQ(retry_after, 100);

    admin_runtime_set_tick(99);
    ASSERT_EQ(http_parse_request(good_raw, (int)strlen(good_raw), &http_req), 0);
    ASSERT_EQ(http_map_request(&http_req, &admin_req), 0);
    ASSERT_EQ(admin_authorize_request_detailed(&admin_req, allowed_ip, &retry_after), ADMIN_AUTH_THROTTLED);
    ASSERT_EQ(retry_after, 1);

    admin_runtime_set_tick(100);
    ASSERT_EQ(admin_authorize_request_detailed(&admin_req, allowed_ip, &retry_after), ADMIN_AUTH_ALLOW);
    ASSERT_EQ(retry_after, 0);
}

int main(void) {
    RUN_TEST(parse_health_request);
    RUN_TEST(parse_authorization_header);
    RUN_TEST(map_http_to_admin_actions);
    RUN_TEST(status_request_roundtrip);
    RUN_TEST(build_http_response);
    RUN_TEST(http_auth_rate_limit_recovers_after_backoff);
    TEST_REPORT();
}
