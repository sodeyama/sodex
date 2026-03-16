#include "test_framework.h"

#include "../src/include/ssh_runtime_policy.h"

TEST(auth_failure_closes_on_third_attempt) {
    ASSERT_EQ(ssh_auth_failure_should_close(1), 0);
    ASSERT_EQ(ssh_auth_failure_should_close(2), 0);
    ASSERT_EQ(ssh_auth_failure_should_close(3), 1);
    ASSERT_EQ(ssh_auth_failure_should_close(4), 1);
}

TEST(timeout_reason_is_split_by_auth_state) {
    ASSERT_STR_EQ(ssh_timeout_close_reason(0), "auth_timeout");
    ASSERT_STR_EQ(ssh_timeout_close_reason(1), "idle_timeout");
    ASSERT_STR_EQ(ssh_timeout_audit_label(0), "ssh_auth_timeout_ticks");
    ASSERT_STR_EQ(ssh_timeout_audit_label(1), "ssh_idle_timeout_ticks");
}

int main(void) {
    RUN_TEST(auth_failure_closes_on_third_attempt);
    RUN_TEST(timeout_reason_is_split_by_auth_state);
    TEST_REPORT();
}
