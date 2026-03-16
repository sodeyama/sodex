#include "test_framework.h"
#include <shell.h>
#include <stdio.h>
#include <string.h>

TEST(parse_lists_and_background) {
    struct shell_program program;
    const char *text = "PATH=/usr/bin\nfoo && bar || baz &\nqux\n";

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 5);
    ASSERT_EQ(program.pipeline_count, 5);
    ASSERT_EQ(program.pipelines[0].commands[0].assignment_count, 1);
    ASSERT_EQ(program.pipelines[0].next_type, SHELL_NEXT_SEQ);
    ASSERT_STR_EQ(program.pipelines[1].commands[0].argv[0], "foo");
    ASSERT_EQ(program.pipelines[1].next_type, SHELL_NEXT_AND);
    ASSERT_STR_EQ(program.pipelines[2].commands[0].argv[0], "bar");
    ASSERT_EQ(program.pipelines[2].next_type, SHELL_NEXT_OR);
    ASSERT_STR_EQ(program.pipelines[3].commands[0].argv[0], "baz");
    ASSERT_EQ(program.pipelines[3].next_type, SHELL_NEXT_BACKGROUND);
    ASSERT_STR_EQ(program.pipelines[4].commands[0].argv[0], "qux");
}

TEST(parse_pipeline_with_redirection) {
    struct shell_program program;
    const char *text = "cat < in.txt | sh > out.txt\n";

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 1);
    ASSERT_EQ(program.pipeline_count, 1);
    ASSERT_EQ(program.pipelines[0].command_count, 2);
    ASSERT_STR_EQ(program.pipelines[0].commands[0].input_path, "in.txt");
    ASSERT_STR_EQ(program.pipelines[0].commands[1].output_path, "out.txt");
}

TEST(shell_vars_and_params) {
    struct shell_state state;
    char *params[] = {"start", "now"};

    shell_state_init(&state, 0);
    ASSERT_EQ(shell_var_set(&state, "NAME", "value", 1), 0);
    ASSERT_STR_EQ(shell_var_get(&state, "NAME"), "value");
    shell_state_set_script(&state, "/etc/init.d/sshd", 2, params);
    ASSERT_STR_EQ(state.script_name, "/etc/init.d/sshd");
    ASSERT_EQ(state.param_count, 2);
    ASSERT_STR_EQ(state.param_storage[0], "start");
    ASSERT_STR_EQ(state.param_storage[1], "now");
}

int main(void)
{
    printf("=== shell core tests ===\n");

    RUN_TEST(parse_lists_and_background);
    RUN_TEST(parse_pipeline_with_redirection);
    RUN_TEST(shell_vars_and_params);

    TEST_REPORT();
}
