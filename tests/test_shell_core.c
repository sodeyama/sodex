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

static int build_long_list_script(char *buf, size_t cap, int count)
{
    int len = 0;
    int i;

    if (buf == NULL || cap == 0)
        return -1;
    buf[0] = '\0';
    for (i = 0; i < count; i++) {
        int written = snprintf(buf + len, cap - (size_t)len, "echo item%d\n", i);

        if (written <= 0 || (size_t)written >= cap - (size_t)len)
            return -1;
        len += written;
    }
    return len;
}

static int build_sxi_smoke_script(char *buf, size_t cap)
{
    static const char *lines[] = {
        "/usr/bin/sxi /home/user/sx-examples/hello.sx > /home/user/sxi_hello_out.txt\n",
        "echo AUDIT sxi_hello_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/operators.sx > /home/user/sxi_operators_out.txt\n",
        "echo AUDIT sxi_operators_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/while_counter.sx > /home/user/sxi_while_out.txt\n",
        "echo AUDIT sxi_while_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/for_counter.sx > /home/user/sxi_for_out.txt\n",
        "echo AUDIT sxi_for_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/break_continue.sx > /home/user/sxi_break_out.txt\n",
        "echo AUDIT sxi_break_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/scope_blocks.sx > /home/user/sxi_scope_out.txt\n",
        "echo AUDIT sxi_scope_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/recursive_sum.sx > /home/user/sxi_recursive_out.txt\n",
        "echo AUDIT sxi_recursive_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/import_main.sx > /home/user/sxi_import_out.txt\n",
        "echo AUDIT sxi_import_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/json_report.sx > /home/user/sxi_json_out.txt\n",
        "echo AUDIT sxi_json_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/copy_file.sx > /home/user/sxi_copy_out.txt\n",
        "echo AUDIT sxi_copy_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/proc_capture.sx > /home/user/sxi_proc_out.txt\n",
        "echo AUDIT sxi_proc_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/stdin_echo.sx < /home/user/sx-examples/stdin_source.txt > /home/user/sxi_stdin_out.txt\n",
        "echo AUDIT sxi_stdin_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/argv_fs_time.sx alpha beta > /home/user/sxi_interop_out.txt\n",
        "echo AUDIT sxi_interop_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/spawn_wait.sx > /home/user/sxi_spawn_out.txt\n",
        "echo AUDIT sxi_spawn_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/pipe_roundtrip.sx > /home/user/sxi_pipe_out.txt\n",
        "echo AUDIT sxi_pipe_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/fork_wait.sx > /home/user/sxi_fork_out.txt\n",
        "echo AUDIT sxi_fork_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/env_bytes_result.sx > /home/user/sxi_bytes_out.txt\n",
        "echo AUDIT sxi_bytes_run_status=$?\n",
        "/usr/bin/sxi /home/user/sx-examples/list_map.sx > /home/user/sxi_list_map_out.txt\n",
        "echo AUDIT sxi_list_map_run_status=$?\n",
        "/usr/bin/sxi -e 'io.println(\"INLINE_OK\");' > /home/user/sxi_inline.txt\n",
        "echo AUDIT sxi_inline_status=$?\n",
        "echo AUDIT sxi_smoke_done\n",
        "exit 0\n",
    };
    int len = 0;
    size_t i;

    if (buf == NULL || cap == 0)
        return -1;
    buf[0] = '\0';
    for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        int written = snprintf(buf + len, cap - (size_t)len, "%s", lines[i]);

        if (written <= 0 || (size_t)written >= cap - (size_t)len)
            return -1;
        len += written;
    }
    return len;
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

TEST(parse_long_script_list) {
    struct shell_program program;
    char text[2048];
    int len = 0;

    len = build_long_list_script(text, sizeof(text), 60);
    ASSERT_EQ(len > 0, 1);
    ASSERT_EQ(shell_parse_program(text, len, &program), 60);
    ASSERT_EQ(program.lists[program.root_list_index].item_count, 60);
    ASSERT_EQ(program.pipeline_count, 60);
    ASSERT_STR_EQ(root_pipeline(&program, 0)->commands[0].argv[0], "echo");
    ASSERT_STR_EQ(root_pipeline(&program, 59)->commands[0].argv[1], "item59");
}

TEST(parse_sxi_smoke_script) {
    struct shell_program program;
    char text[8192];
    int len = 0;

    len = build_sxi_smoke_script(text, sizeof(text));
    ASSERT_EQ(len > 0, 1);
    ASSERT_EQ(shell_parse_program(text, len, &program), 42);
    ASSERT_EQ(program.lists[program.root_list_index].item_count, 42);
    ASSERT_EQ(program.pipeline_count, 42);
    ASSERT_STR_EQ(root_pipeline(&program, 0)->commands[0].argv[0], "/usr/bin/sxi");
    ASSERT_STR_EQ(root_pipeline(&program, 41)->commands[0].argv[0], "exit");
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
    RUN_TEST(parse_long_script_list);
    RUN_TEST(parse_sxi_smoke_script);

    TEST_REPORT();
}
