#include "test_framework.h"
#include <string.h>

#define TEST_BUILD 1
#include "../src/include/admin_server.h"
#include "../src/include/server_audit.h"
#include "../src/include/server_runtime_config.h"

TEST(server_runtime_loads_shared_ssh_config) {
    const char *config =
        "control_token = control-secret\n"
        "debug_shell_port = 10024\n"
        "ssh_port = 10022\n"
        "ssh_password = root-secret\n"
        "ssh_signer_port = 0\n"
        "ssh_hostkey_ed25519_seed = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n"
        "ssh_rng_seed = ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\n";
    struct admin_ssh_config ssh;

    server_runtime_reset();
    ASSERT(server_runtime_load_config_text(config, (int)strlen(config)) > 0);
    ASSERT_EQ(server_runtime_debug_shell_port(), 10024);
    ASSERT_EQ(server_runtime_debug_shell_enabled(), 1);
    ASSERT_EQ(server_runtime_ssh_port(), 10022);
    ASSERT_EQ(server_runtime_ssh_signer_port(), 0);
    ASSERT(server_runtime_ssh_enabled());
    ASSERT_EQ(server_runtime_copy_ssh_config(&ssh), 0);
    ASSERT_EQ(ssh.ssh_port, 10022);
    ASSERT_STR_EQ(ssh.ssh_password, "root-secret");
}

TEST(server_audit_logs_debug_shell_listener_ready) {
    struct admin_request req;
    char response[ADMIN_RESPONSE_MAX];

    server_runtime_reset();
    server_runtime_set_debug_shell_port(10024);
    server_audit_note_listener_ready(ADMIN_LISTENER_DEBUG_SHELL);

    memset(&req, 0, sizeof(req));
    req.action = ADMIN_ACTION_LOG_TAIL;
    req.arg = 8;
    ASSERT(admin_execute_request(&req, response, sizeof(response), 0) > 0);
    ASSERT(strstr(response, "listener_ready kind=debug_shell port=10024") != 0);
}

int main(void) {
    RUN_TEST(server_runtime_loads_shared_ssh_config);
    RUN_TEST(server_audit_logs_debug_shell_listener_ready);
    TEST_REPORT();
}
