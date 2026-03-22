#include "test_framework.h"
#include <shell.h>
#include <stdio.h>
#include <string.h>

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
        return -1;
    if (fwrite(text, 1, strlen(text), fp) != strlen(text)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int read_text_file(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t read_len;

    if (fp == NULL || buf == NULL || cap == 0)
        return -1;
    read_len = fread(buf, 1, cap - 1, fp);
    buf[read_len] = '\0';
    fclose(fp);
    return 0;
}

static int build_long_assignment_script(char *buf, size_t cap, int count)
{
    int len = 0;
    int i;

    if (buf == NULL || cap == 0)
        return -1;
    buf[0] = '\0';
    for (i = 0; i < count; i++) {
        int written = snprintf(buf + len, cap - (size_t)len, "RESULT=%d\n", i);

        if (written <= 0 || (size_t)written >= cap - (size_t)len)
            return -1;
        len += written;
    }
    return len;
}

TEST(exec_if_else_and_elif) {
    struct shell_state state;
    const char *path = "shell_prog_tmp.txt";

    shell_state_init(&state, 0);
    ASSERT_EQ(write_text_file(path, "ok\n"), 0);
    ASSERT_EQ(shell_execute_string(&state,
                                   "if [ -f shell_prog_tmp.txt ]; then RESULT=file; "
                                   "elif false; then RESULT=bad; else RESULT=none; fi"),
              0);
    ASSERT_STR_EQ(shell_var_get(&state, "RESULT"), "file");
    remove(path);
}

TEST(exec_buffer_if_redirect_creates_file) {
    struct shell_state state;
    const char *path = "shell_prog_buffer.txt";
    char text[64];

    shell_state_init(&state, 0);
    remove(path);
    ASSERT_EQ(shell_execute_buffer(&state, "sh",
                                   "if true; then echo IF_OK > shell_prog_buffer.txt; fi",
                                   0, 0, 0),
              0);
    ASSERT_EQ(read_text_file(path, text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "IF_OK\n");
    remove(path);
}

TEST(exec_for_loop_and_break) {
    struct shell_state state;

    shell_state_init(&state, 0);
    ASSERT_EQ(shell_execute_string(&state,
                                   "RESULT=none; "
                                   "for item in first second; do RESULT=$item; break; RESULT=bad; done"),
              0);
    ASSERT_STR_EQ(shell_var_get(&state, "RESULT"), "first");
}

TEST(exec_for_loop_and_continue) {
    struct shell_state state;

    shell_state_init(&state, 0);
    ASSERT_EQ(shell_execute_string(&state,
                                   "SEEN=none; "
                                   "for item in a b; do continue; SEEN=bad; done"),
              0);
    ASSERT_STR_EQ(shell_var_get(&state, "SEEN"), "none");
}

TEST(exec_while_and_until) {
    struct shell_state state;

    shell_state_init(&state, 0);
    ASSERT_EQ(shell_execute_string(&state,
                                   "A=none; while true; do A=while; break; done; "
                                   "B=none; until false; do B=until; break; done"),
              0);
    ASSERT_STR_EQ(shell_var_get(&state, "A"), "while");
    ASSERT_STR_EQ(shell_var_get(&state, "B"), "until");
}

TEST(break_outside_loop_fails) {
    struct shell_state state;

    shell_state_init(&state, 0);
    ASSERT_EQ(shell_execute_string(&state, "break"), 1);
    ASSERT_EQ(state.loop_control, SHELL_LOOP_NONE);
}

TEST(exec_long_script_list) {
    struct shell_state state;
    char text[2048];
    int len = 0;

    shell_state_init(&state, 0);
    len = build_long_assignment_script(text, sizeof(text), 60);
    ASSERT_EQ(len > 0, 1);
    ASSERT_EQ(shell_execute_buffer(&state, "long.sh", text, 0, 0, 0), 0);
    ASSERT_STR_EQ(shell_var_get(&state, "RESULT"), "59");
}

int main(void)
{
    printf("=== programmable shell tests ===\n");

    RUN_TEST(exec_if_else_and_elif);
    RUN_TEST(exec_buffer_if_redirect_creates_file);
    RUN_TEST(exec_for_loop_and_break);
    RUN_TEST(exec_for_loop_and_continue);
    RUN_TEST(exec_while_and_until);
    RUN_TEST(break_outside_loop_fails);
    RUN_TEST(exec_long_script_list);

    TEST_REPORT();
}
