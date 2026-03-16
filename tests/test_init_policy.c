#include "test_framework.h"
#include <init_policy.h>
#include <stdio.h>

TEST(parse_service_metadata) {
    struct init_service_info info;
    const char *text =
        "### BEGIN INIT INFO\n"
        "# Provides: sshd ssh\n"
        "# Required-Start: net logger\n"
        "# Default-Start: user server-headless\n"
        "### END INIT INFO\n";

    ASSERT_EQ(init_policy_parse_service("/etc/init.d/sshd", text, &info), 0);
    ASSERT_STR_EQ(info.name, "sshd");
    ASSERT_EQ(info.provide_count, 2);
    ASSERT_STR_EQ(info.provides[1], "ssh");
    ASSERT_EQ(info.required_start_count, 2);
    ASSERT_STR_EQ(info.required_start[0], "net");
    ASSERT_EQ(info.default_start_count, 2);
    ASSERT_STR_EQ(info.default_start[1], "server-headless");
}

TEST(order_services_by_required_start) {
    struct init_service_info services[3];
    int order[3];
    const char *logger_text =
        "### BEGIN INIT INFO\n"
        "# Provides: logger\n"
        "# Default-Start: user\n"
        "### END INIT INFO\n";
    const char *sshd_text =
        "### BEGIN INIT INFO\n"
        "# Provides: sshd\n"
        "# Required-Start: logger\n"
        "# Default-Start: user\n"
        "### END INIT INFO\n";
    const char *monitor_text =
        "### BEGIN INIT INFO\n"
        "# Provides: monitor\n"
        "# Required-Start: sshd\n"
        "# Default-Start: user\n"
        "### END INIT INFO\n";

    ASSERT_EQ(init_policy_parse_service("/etc/init.d/monitor", monitor_text, &services[0]), 0);
    ASSERT_EQ(init_policy_parse_service("/etc/init.d/sshd", sshd_text, &services[1]), 0);
    ASSERT_EQ(init_policy_parse_service("/etc/init.d/logger", logger_text, &services[2]), 0);
    ASSERT_EQ(init_policy_order_services(services, 3, "user", order, 3), 3);
    ASSERT_STR_EQ(services[order[0]].name, "logger");
    ASSERT_STR_EQ(services[order[1]].name, "sshd");
    ASSERT_STR_EQ(services[order[2]].name, "monitor");
}

TEST(parse_inittab_and_respawn) {
    struct init_inittab inittab;
    const char *text =
        "# 最小 inittab\n"
        "initdefault:server-headless\n"
        "sysinit:/etc/init.d/rcS\n"
        "respawn:user:/usr/bin/term\n"
        "respawn:rescue:/usr/bin/eshell\n";

    ASSERT_EQ(init_policy_parse_inittab(text, &inittab), 0);
    ASSERT_STR_EQ(inittab.runlevel, "server-headless");
    ASSERT_STR_EQ(inittab.sysinit, "/etc/init.d/rcS");
    ASSERT_EQ(inittab.respawn_count, 2);
    ASSERT_STR_EQ(init_policy_find_respawn(&inittab, "user"), "/usr/bin/term");
    ASSERT_STR_EQ(init_policy_find_respawn(&inittab, "server-headless"), "");
}

int main(void)
{
    printf("=== init policy tests ===\n");

    RUN_TEST(parse_service_metadata);
    RUN_TEST(order_services_by_required_start);
    RUN_TEST(parse_inittab_and_respawn);

    TEST_REPORT();
}
