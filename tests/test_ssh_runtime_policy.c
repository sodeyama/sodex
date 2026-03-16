#include "test_framework.h"

#include "../src/include/ssh_runtime_policy.h"

TEST(auth_failure_closes_on_third_attempt) {
    ASSERT_EQ(ssh_auth_failure_should_close(1), 0);
    ASSERT_EQ(ssh_auth_failure_should_close(2), 0);
    ASSERT_EQ(ssh_auth_failure_should_close(3), 1);
    ASSERT_EQ(ssh_auth_failure_should_close(4), 1);
}

TEST(timeout_phase_distinguishes_auth_no_channel_and_idle) {
    ASSERT_EQ(ssh_timeout_phase(0, 0, 0), SSH_TIMEOUT_PHASE_AUTH);
    ASSERT_EQ(ssh_timeout_phase(1, 0, 0), SSH_TIMEOUT_PHASE_NO_CHANNEL);
    ASSERT_EQ(ssh_timeout_phase(1, 1, 0), SSH_TIMEOUT_PHASE_NO_CHANNEL);
    ASSERT_EQ(ssh_timeout_phase(1, 1, 1), SSH_TIMEOUT_PHASE_IDLE);
    ASSERT_STR_EQ(ssh_timeout_close_reason_for_phase(SSH_TIMEOUT_PHASE_AUTH), "auth_timeout");
    ASSERT_STR_EQ(ssh_timeout_close_reason_for_phase(SSH_TIMEOUT_PHASE_NO_CHANNEL), "no_channel_timeout");
    ASSERT_STR_EQ(ssh_timeout_close_reason_for_phase(SSH_TIMEOUT_PHASE_IDLE), "idle_timeout");
    ASSERT_STR_EQ(ssh_timeout_audit_label_for_phase(SSH_TIMEOUT_PHASE_NO_CHANNEL),
                  "ssh_no_channel_timeout_ticks");
}

TEST(auth_failure_delay_blocks_until_deadline) {
    u_int32_t delay_until = ssh_auth_failure_delay_until(100);

    ASSERT_EQ(ssh_auth_failure_delay_pending(100, delay_until), 1);
    ASSERT_EQ(ssh_auth_failure_delay_pending(delay_until - 1, delay_until), 1);
    ASSERT_EQ(ssh_auth_failure_delay_pending(delay_until, delay_until), 0);
}

int main(void) {
    RUN_TEST(auth_failure_closes_on_third_attempt);
    RUN_TEST(timeout_phase_distinguishes_auth_no_channel_and_idle);
    RUN_TEST(auth_failure_delay_blocks_until_deadline);
    TEST_REPORT();
}
