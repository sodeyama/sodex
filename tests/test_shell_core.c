#include "test_framework.h"
#include <shell.h>
#include <stdio.h>
#include <string.h>

static struct shell_pipeline *root_pipeline(struct shell_program *program, int index)
{
    int node_index = program->lists[program->root_list_index].items[index].node_index;
    int pipeline_index = program->nodes[node_index].data.pipeline_index;

    return &program->pipelines[pipeline_index];
}

TEST(parse_lists_and_background) {
    struct shell_program program;
    const char *text = "PATH=/usr/bin\nfoo && bar || baz &\nqux\n";

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 5);
    ASSERT_EQ(program.lists[program.root_list_index].item_count, 5);
    ASSERT_EQ(program.pipeline_count, 5);
    ASSERT_EQ(program.lists[program.root_list_index].items[0].next_type, SHELL_NEXT_SEQ);
    ASSERT_EQ(root_pipeline(&program, 0)->commands[0].assignment_count, 1);
    ASSERT_STR_EQ(root_pipeline(&program, 1)->commands[0].argv[0], "foo");
    ASSERT_EQ(program.lists[program.root_list_index].items[1].next_type, SHELL_NEXT_AND);
    ASSERT_STR_EQ(root_pipeline(&program, 2)->commands[0].argv[0], "bar");
    ASSERT_EQ(program.lists[program.root_list_index].items[2].next_type, SHELL_NEXT_OR);
    ASSERT_STR_EQ(root_pipeline(&program, 3)->commands[0].argv[0], "baz");
    ASSERT_EQ(program.lists[program.root_list_index].items[3].next_type, SHELL_NEXT_BACKGROUND);
    ASSERT_STR_EQ(root_pipeline(&program, 4)->commands[0].argv[0], "qux");
}

TEST(parse_pipeline_with_redirection) {
    struct shell_program program;
    const char *text = "cat < in.txt | sh > out.txt\n";
    struct shell_command *left;
    struct shell_command *right;
    struct shell_pipeline *pipeline;

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 1);
    ASSERT_EQ(program.pipeline_count, 1);
    pipeline = root_pipeline(&program, 0);
    ASSERT_EQ(pipeline->command_count, 2);
    left = &pipeline->commands[0];
    right = &pipeline->commands[1];
    ASSERT_EQ(left->redirection_count, 1);
    ASSERT_EQ(left->redirections[0].type, SHELL_REDIR_INPUT);
    ASSERT_EQ(left->redirections[0].fd, 0);
    ASSERT_STR_EQ(left->redirections[0].path, "in.txt");
    ASSERT_EQ(right->redirection_count, 1);
    ASSERT_EQ(right->redirections[0].type, SHELL_REDIR_OUTPUT);
    ASSERT_EQ(right->redirections[0].fd, 1);
    ASSERT_STR_EQ(right->redirections[0].path, "out.txt");
}

TEST(parse_fd_redirection_order) {
    struct shell_program program;
    struct shell_command *command;
    struct shell_pipeline *pipeline;
    const char *text = "sh -c \"echo ok\" > out.log 2>&1 2>> err.log 1>&2\n";

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 1);
    pipeline = root_pipeline(&program, 0);
    command = &pipeline->commands[0];
    ASSERT_EQ(command->redirection_count, 4);
    ASSERT_EQ(command->redirections[0].type, SHELL_REDIR_OUTPUT);
    ASSERT_EQ(command->redirections[0].fd, 1);
    ASSERT_STR_EQ(command->redirections[0].path, "out.log");
    ASSERT_EQ(command->redirections[1].type, SHELL_REDIR_DUP);
    ASSERT_EQ(command->redirections[1].fd, 2);
    ASSERT_EQ(command->redirections[1].target_fd, 1);
    ASSERT_EQ(command->redirections[2].type, SHELL_REDIR_APPEND);
    ASSERT_EQ(command->redirections[2].fd, 2);
    ASSERT_STR_EQ(command->redirections[2].path, "err.log");
    ASSERT_EQ(command->redirections[3].type, SHELL_REDIR_DUP);
    ASSERT_EQ(command->redirections[3].fd, 1);
    ASSERT_EQ(command->redirections[3].target_fd, 2);
}

TEST(parse_long_command_arguments) {
    struct shell_program program;
    struct shell_pipeline *pipeline;
    const char *text =
        "start-stop-daemon --service sshd --action start --exec /usr/bin/sshd "
        "--pidfile /var/run/sshd.pid --stdout /var/log/sshd.log "
        "--stderr /var/log/sshd.log --require /usr/bin/sshd "
        "--require /etc/sodex-admin.conf\n";

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 1);
    pipeline = root_pipeline(&program, 0);
    ASSERT_EQ(pipeline->commands[0].argc, 17);
    ASSERT_STR_EQ(pipeline->commands[0].argv[16], "/etc/sodex-admin.conf");
}

TEST(parse_control_flow_nodes) {
    struct shell_program program;
    const char *text =
        "if true; then echo yes; elif false; then echo no; else echo alt; fi\n"
        "for item in a b; do echo $item; done\n"
        "while true; do break; done\n"
        "until false; do continue; done\n";
    int root = 0;

    ASSERT_EQ(shell_parse_program(text, (int)strlen(text), &program), 4);
    root = program.root_list_index;
    ASSERT_EQ(program.lists[root].item_count, 4);
    ASSERT_EQ(program.nodes[program.lists[root].items[0].node_index].type, SHELL_NODE_IF);
    ASSERT_EQ(program.nodes[program.lists[root].items[1].node_index].type, SHELL_NODE_FOR);
    ASSERT_EQ(program.nodes[program.lists[root].items[2].node_index].type, SHELL_NODE_WHILE);
    ASSERT_EQ(program.nodes[program.lists[root].items[3].node_index].type, SHELL_NODE_UNTIL);
    ASSERT_EQ(program.nodes[program.lists[root].items[0].node_index].data.if_node.elif_count, 1);
    ASSERT_EQ(program.nodes[program.lists[root].items[1].node_index].data.for_node.word_count, 2);
}

TEST(parse_incomplete_control_flow) {
    struct shell_program program;

    ASSERT_EQ(shell_parse_program("if true; then\n", 14, &program), SHELL_PARSE_INCOMPLETE);
    ASSERT_EQ(shell_parse_program("for item in a b; do\n", 20, &program), SHELL_PARSE_INCOMPLETE);
    ASSERT_EQ(shell_parse_program("while true; do\n", 15, &program), SHELL_PARSE_INCOMPLETE);
}

TEST(shell_vars_and_params) {
    struct shell_state state;
    char *params[] = {"start", "now"};

    shell_state_init(&state, 0);
    ASSERT_STR_EQ(shell_var_get(&state, "HOME"), "/home/user");
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
    RUN_TEST(parse_fd_redirection_order);
    RUN_TEST(parse_long_command_arguments);
    RUN_TEST(parse_control_flow_nodes);
    RUN_TEST(parse_incomplete_control_flow);
    RUN_TEST(shell_vars_and_params);

    TEST_REPORT();
}
