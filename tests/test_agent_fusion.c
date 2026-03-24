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

TEST(mode_flag_detection) {
    char *argv1[] = { "eshell", "--agent-fusion", "--agent-mode=agent", 0 };
    char *argv2[] = { "eshell", "--agent-fusion", "--agent-mode=shell", 0 };
    char *argv3[] = { "eshell", "--agent-fusion", 0 };

    ASSERT_EQ(agent_fusion_mode_from_argv(3, argv1, AGENT_FUSION_MODE_AUTO),
              AGENT_FUSION_MODE_AGENT);
    ASSERT_EQ(agent_fusion_mode_from_argv(3, argv2, AGENT_FUSION_MODE_AUTO),
              AGENT_FUSION_MODE_SHELL);
    ASSERT_EQ(agent_fusion_mode_from_argv(2, argv3, AGENT_FUSION_MODE_AUTO),
              AGENT_FUSION_MODE_AUTO);
}

TEST(force_agent_mode_wraps_plain_text) {
    char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
    char *argv[AGENT_FUSION_MAX_ARGS + 1];

    ASSERT_EQ(agent_fusion_build_mode_argv("summarize this directory", 1, storage, argv), 3);
    ASSERT_STR_EQ(argv[0], "agent");
    ASSERT_STR_EQ(argv[1], "run");
    ASSERT_STR_EQ(argv[2], "summarize this directory");
    ASSERT_NULL(argv[3]);
}

TEST(force_agent_mode_keeps_direct_subcommand) {
    char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX];
    char *argv[AGENT_FUSION_MAX_ARGS + 1];

    ASSERT_EQ(agent_fusion_build_mode_argv("memory add mode note", 1, storage, argv), 5);
    ASSERT_STR_EQ(argv[0], "agent");
    ASSERT_STR_EQ(argv[1], "memory");
    ASSERT_STR_EQ(argv[2], "add");
    ASSERT_STR_EQ(argv[3], "mode");
    ASSERT_STR_EQ(argv[4], "note");
    ASSERT_NULL(argv[5]);
}

int main(void)
{
    printf("=== agent fusion tests ===\n");

    RUN_TEST(no_prefix_is_not_agent_route);
    RUN_TEST(default_route_wraps_prompt_with_run);
    RUN_TEST(direct_mode_keeps_agent_subcommand);
    RUN_TEST(direct_mode_preserves_quoted_arguments);
    RUN_TEST(flag_detection);
    RUN_TEST(mode_flag_detection);
    RUN_TEST(force_agent_mode_wraps_plain_text);
    RUN_TEST(force_agent_mode_keeps_direct_subcommand);

    TEST_REPORT();
}
