#include "test_framework.h"
#include <stdint.h>
#include <string.h>

#define TEST_BUILD 1
#include "../src/include/admin_server.h"
#include "../src/include/debug_shell_server.h"

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

TEST(parse_debug_shell_preface) {
    char token[ADMIN_TOKEN_MAX];

    ASSERT_EQ(debug_shell_parse_preface("TOKEN control-secret\n", 21,
                                        token, sizeof(token)), 0);
    ASSERT_STR_EQ(token, "control-secret");
}

TEST(reject_invalid_debug_shell_preface) {
    char token[ADMIN_TOKEN_MAX];

    ASSERT_EQ(debug_shell_parse_preface("STATUS\n", 7,
                                        token, sizeof(token)), -1);
    ASSERT_EQ(debug_shell_parse_preface("TOKEN control secret\n", 21,
                                        token, sizeof(token)), -1);
    ASSERT_EQ(debug_shell_parse_preface("TOKEN \n", 7,
                                        token, sizeof(token)), -1);
}

TEST(debug_shell_auth_uses_control_role) {
    struct admin_request req;
    char token[ADMIN_TOKEN_MAX];
    u_int32_t retry_after = 0;

    admin_runtime_reset();
    admin_runtime_set_tokens("status-secret", "control-secret");
    admin_runtime_set_allow_ip(test_ip(10, 0, 2, 2));

    ASSERT_EQ(debug_shell_parse_preface("TOKEN control-secret\n", 21,
                                        token, sizeof(token)), 0);
    memset(&req, 0, sizeof(req));
    req.required_role = ADMIN_ROLE_CONTROL;
    strcpy(req.token, token);
    ASSERT_EQ(admin_authorize_request_detailed(&req, test_ip(10, 0, 2, 2),
                                               &retry_after), ADMIN_AUTH_ALLOW);
    ASSERT_EQ(retry_after, 0);

    ASSERT_EQ(debug_shell_parse_preface("TOKEN status-secret\n", 20,
                                        token, sizeof(token)), 0);
    strcpy(req.token, token);
    ASSERT_EQ(admin_authorize_request_detailed(&req, test_ip(10, 0, 2, 2),
                                               &retry_after), ADMIN_AUTH_DENY);
}

TEST(debug_shell_port_is_disabled_by_default) {
    admin_runtime_reset();

    ASSERT_EQ(admin_runtime_debug_shell_port(), 0);
    ASSERT_EQ(admin_runtime_debug_shell_enabled(), 0);
}

TEST(debug_shell_port_can_be_enabled_from_config) {
    const char *config =
        "control_token = control-secret\n"
        "debug_shell_port = 10024\n";

    admin_runtime_reset();
    ASSERT(admin_runtime_load_config_text(config, (int)strlen(config)) > 0);
    ASSERT_EQ(admin_runtime_debug_shell_port(), 10024);
    ASSERT_EQ(admin_runtime_debug_shell_enabled(), 1);
}

TEST(debug_shell_port_requires_control_token) {
    admin_runtime_reset();
    admin_runtime_set_debug_shell_port(10024);

    ASSERT_EQ(admin_runtime_debug_shell_port(), 10024);
    ASSERT_EQ(admin_runtime_debug_shell_enabled(), 0);
}

int main(void) {
    RUN_TEST(parse_debug_shell_preface);
    RUN_TEST(reject_invalid_debug_shell_preface);
    RUN_TEST(debug_shell_auth_uses_control_role);
    RUN_TEST(debug_shell_port_is_disabled_by_default);
    RUN_TEST(debug_shell_port_can_be_enabled_from_config);
    RUN_TEST(debug_shell_port_requires_control_token);
    TEST_REPORT();
}
