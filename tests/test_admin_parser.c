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

TEST(parse_ping_without_token) {
    struct admin_request req;

    admin_runtime_reset();
    ASSERT_EQ(admin_parse_command("PING\n", 5, &req), 0);
    ASSERT_EQ(req.action, ADMIN_ACTION_PING);
    ASSERT_EQ(req.required_role, ADMIN_ROLE_HEALTH);
    ASSERT_STR_EQ(req.token, "");
}

TEST(parse_status_with_token_prefix) {
    struct admin_request req;
    const char *line = "TOKEN status-secret STATUS\n";

    admin_runtime_reset();
    ASSERT_EQ(admin_parse_command(line, (int)strlen(line), &req), 0);
    ASSERT_EQ(req.action, ADMIN_ACTION_STATUS);
    ASSERT_EQ(req.required_role, ADMIN_ROLE_STATUS);
    ASSERT_STR_EQ(req.token, "status-secret");
}

TEST(authorize_by_role_and_allowlist) {
    struct admin_request req;
    const char *status_line = "TOKEN status-secret STATUS\n";
    const char *bad_control = "TOKEN status-secret AGENT START\n";
    const char *good_control = "TOKEN control-secret AGENT START\n";

    admin_runtime_reset();
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_set_allow_ip(test_ip(10, 0, 2, 2));

    ASSERT_EQ(admin_parse_command(status_line, (int)strlen(status_line), &req), 0);
    ASSERT(admin_authorize_request(&req, test_ip(10, 0, 2, 2)));
    ASSERT(!admin_authorize_request(&req, test_ip(10, 0, 2, 3)));

    ASSERT_EQ(admin_parse_command(bad_control, (int)strlen(bad_control), &req), 0);
    ASSERT(!admin_authorize_request(&req, test_ip(10, 0, 2, 2)));

    ASSERT_EQ(admin_parse_command(good_control, (int)strlen(good_control), &req), 0);
    ASSERT(admin_authorize_request(&req, test_ip(10, 0, 2, 2)));
}

TEST(execute_agent_start_and_status) {
    struct admin_request req;
    char response[ADMIN_RESPONSE_MAX];
    const char *start_line = "TOKEN control-secret AGENT START\n";
    const char *status_line = "TOKEN status-secret STATUS\n";

    admin_runtime_reset();
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_set_allow_ip(test_ip(10, 0, 2, 2));
    admin_runtime_set_tick(1234);

    ASSERT_EQ(admin_parse_command(start_line, (int)strlen(start_line), &req), 0);
    ASSERT_EQ(admin_execute_request(&req, response, sizeof(response), 0), 17);
    ASSERT_STR_EQ(response, "OK agent=running\n");

    ASSERT_EQ(admin_parse_command(status_line, (int)strlen(status_line), &req), 0);
    ASSERT(admin_execute_request(&req, response, sizeof(response), 0) > 0);
    ASSERT(strstr(response, "agent=running") != 0);
    ASSERT(strstr(response, "uptime_ticks=1234") != 0);
}

TEST(log_tail_uses_audit_ring) {
    struct admin_request req;
    char response[ADMIN_RESPONSE_MAX];
    const char *line = "TOKEN status-secret LOG TAIL 2\n";

    admin_runtime_reset();
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_append_test_audit("line-one");
    admin_runtime_append_test_audit("line-two");

    ASSERT_EQ(admin_parse_command(line, (int)strlen(line), &req), 0);
    ASSERT_EQ(req.action, ADMIN_ACTION_LOG_TAIL);
    ASSERT(admin_execute_request(&req, response, sizeof(response), 0) > 0);
    ASSERT(strstr(response, "line-one") != 0);
    ASSERT(strstr(response, "line-two") != 0);
}

TEST(load_config_text_updates_runtime) {
    struct admin_request req;
    const char *config =
        "# comment\n"
        "status_token = runtime-status\n"
        "control_token = runtime-control\n"
        "allow_ip = 10.0.2.9\n";
    const char *status_line = "TOKEN runtime-status STATUS\n";
    const char *control_line = "TOKEN runtime-control AGENT START\n";

    admin_runtime_reset();
    ASSERT(admin_runtime_load_config_text(config, (int)strlen(config)) > 0);

    ASSERT_EQ(admin_parse_command(status_line, (int)strlen(status_line), &req), 0);
    ASSERT(admin_authorize_request(&req, test_ip(10, 0, 2, 9)));
    ASSERT(!admin_authorize_request(&req, test_ip(10, 0, 2, 2)));

    ASSERT_EQ(admin_parse_command(control_line, (int)strlen(control_line), &req), 0);
    ASSERT(admin_authorize_request(&req, test_ip(10, 0, 2, 9)));
}

TEST(rate_limit_allowlist_rejects_and_audits) {
    char response[ADMIN_RESPONSE_MAX];
    struct admin_request log_req;
    u_int32_t blocked_ip = test_ip(10, 0, 2, 9);
    u_int32_t retry_after = 0;

    admin_runtime_reset();
    admin_runtime_set_allow_ip(test_ip(10, 0, 2, 2));

    ASSERT_EQ(admin_authorize_peer(blocked_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_peer(blocked_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_peer(blocked_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_peer(blocked_ip, &retry_after), ADMIN_AUTH_THROTTLED);
    ASSERT_EQ(retry_after, 100);

    memset(&log_req, 0, sizeof(log_req));
    log_req.action = ADMIN_ACTION_LOG_TAIL;
    log_req.arg = 8;

    ASSERT(admin_execute_request(&log_req, response, sizeof(response), 0) > 0);
    ASSERT(strstr(response, "auth_reject peer=10.0.2.9 reason=allowlist count=1") != 0);
    ASSERT(strstr(response, "auth_throttle peer=10.0.2.9 reason=allowlist count=4 retry=100") != 0);
}

TEST(rate_limit_shares_bucket_across_auth_failure_reasons) {
    struct admin_request req;
    u_int32_t peer_ip = test_ip(10, 0, 2, 9);
    u_int32_t retry_after = 0;
    const char *missing_token = "STATUS\n";
    const char *bad_token = "TOKEN wrong-secret STATUS\n";
    const char *good_token = "TOKEN status-secret STATUS\n";

    admin_runtime_reset();
    admin_runtime_set_tick(0);
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_set_allow_ip(test_ip(10, 0, 2, 2));

    ASSERT_EQ(admin_authorize_peer(peer_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);
    ASSERT_EQ(admin_authorize_peer(peer_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);

    admin_runtime_set_allow_ip(peer_ip);

    ASSERT_EQ(admin_parse_command(missing_token, (int)strlen(missing_token), &req), 0);
    ASSERT_EQ(admin_authorize_request_detailed(&req, peer_ip, &retry_after), ADMIN_AUTH_DENY);
    ASSERT_EQ(retry_after, 0);

    ASSERT_EQ(admin_parse_command(bad_token, (int)strlen(bad_token), &req), 0);
    ASSERT_EQ(admin_authorize_request_detailed(&req, peer_ip, &retry_after), ADMIN_AUTH_THROTTLED);
    ASSERT_EQ(retry_after, 100);

    admin_runtime_set_tick(100);
    ASSERT_EQ(admin_parse_command(good_token, (int)strlen(good_token), &req), 0);
    ASSERT_EQ(admin_authorize_request_detailed(&req, peer_ip, &retry_after), ADMIN_AUTH_ALLOW);
    ASSERT_EQ(retry_after, 0);
}

TEST(listener_ready_emits_serial_ready_marker_to_audit_ring) {
    struct admin_request req;
    char response[ADMIN_RESPONSE_MAX];

    admin_runtime_reset();
    admin_runtime_note_listener_ready(ADMIN_LISTENER_ADMIN);
    admin_runtime_note_listener_ready(ADMIN_LISTENER_HTTP);

    memset(&req, 0, sizeof(req));
    req.action = ADMIN_ACTION_LOG_TAIL;
    req.arg = 8;

    ASSERT(admin_execute_request(&req, response, sizeof(response), 0) > 0);
    ASSERT(strstr(response, "listener_ready kind=admin port=10023") != 0);
    ASSERT(strstr(response, "listener_ready kind=http port=8080") != 0);
    ASSERT(strstr(response, "server_runtime_ready allow_ip=10.0.2.2 admin=10023 http=8080") != 0);
}

int main(void) {
    RUN_TEST(parse_ping_without_token);
    RUN_TEST(parse_status_with_token_prefix);
    RUN_TEST(authorize_by_role_and_allowlist);
    RUN_TEST(execute_agent_start_and_status);
    RUN_TEST(log_tail_uses_audit_ring);
    RUN_TEST(load_config_text_updates_runtime);
    RUN_TEST(rate_limit_allowlist_rejects_and_audits);
    RUN_TEST(rate_limit_shares_bucket_across_auth_failure_reasons);
    RUN_TEST(listener_ready_emits_serial_ready_marker_to_audit_ring);
    TEST_REPORT();
}
