#include "test_framework.h"
#include <agent_fusion.h>
#include <stdio.h>

TEST(no_prefix_is_not_agent_route) {
    char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
    char *argv[AGENT_FUSION_MAX_ARGS + 1];

    ASSERT_EQ(agent_fusion_build_argv("echo hi", storage, argv), 0);
}

TEST(default_route_wraps_prompt_with_run) {
    char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
    char *argv[AGENT_FUSION_MAX_ARGS + 1];

    ASSERT_EQ(agent_fusion_build_argv("@fix this bug", storage, argv), 3);
    ASSERT_STR_EQ(argv[0], "agent");
    ASSERT_STR_EQ(argv[1], "run");
    ASSERT_STR_EQ(argv[2], "fix this bug");
    ASSERT_NULL(argv[3]);
}

TEST(direct_mode_keeps_agent_subcommand) {
    char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
    char *argv[AGENT_FUSION_MAX_ARGS + 1];

    ASSERT_EQ(agent_fusion_build_argv("@audit", storage, argv), 2);
    ASSERT_STR_EQ(argv[0], "agent");
    ASSERT_STR_EQ(argv[1], "audit");
    ASSERT_NULL(argv[2]);
}

TEST(direct_mode_preserves_quoted_arguments) {
    char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
    char *argv[AGENT_FUSION_MAX_ARGS + 1];

    ASSERT_EQ(agent_fusion_build_argv("@memory add \"fusion note\"", storage, argv), 4);
    ASSERT_STR_EQ(argv[0], "agent");
    ASSERT_STR_EQ(argv[1], "memory");
    ASSERT_STR_EQ(argv[2], "add");
    ASSERT_STR_EQ(argv[3], "fusion note");
    ASSERT_NULL(argv[4]);
}

TEST(flag_detection) {
    char *argv1[] = { "eshell", "--agent-fusion", 0 };
    char *argv2[] = { "eshell", 0 };

    ASSERT_EQ(agent_fusion_enabled(2, argv1), 1);
    ASSERT_EQ(agent_fusion_enabled(1, argv2), 0);
}

int main(void)
{
    printf("=== agent fusion tests ===\n");

    RUN_TEST(no_prefix_is_not_agent_route);
    RUN_TEST(default_route_wraps_prompt_with_run);
    RUN_TEST(direct_mode_keeps_agent_subcommand);
    RUN_TEST(direct_mode_preserves_quoted_arguments);
    RUN_TEST(flag_detection);

    TEST_REPORT();
}
