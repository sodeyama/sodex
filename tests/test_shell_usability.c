#include "test_framework.h"
#include <shell.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

static int ensure_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(path, 0755);
}

TEST(alias_and_unalias) {
    struct shell_state state;
    char text[128];
    const char *path = "shell_alias_out.txt";

    shell_state_init(&state, 0);
    remove(path);
    ASSERT_EQ(shell_execute_string(&state,
                                   "alias hi='echo alias_ok'; hi > shell_alias_out.txt"),
              0);
    ASSERT_EQ(read_text_file(path, text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "alias_ok\n");
    ASSERT_EQ(shell_execute_string(&state, "unalias hi"), 0);
    ASSERT_NULL(shell_alias_get(&state, "hi"));
    remove(path);
}

TEST(type_and_command_v) {
    struct shell_state state;
    char cwd[512];
    char lookup_dir[640];
    char lookup_path[768];
    char text[256];

    shell_state_init(&state, 0);
    ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_EQ(ensure_dir("shell_lookup_bin"), 0);
    ASSERT_EQ(write_text_file("shell_lookup_bin/lookupcmd", "dummy\n"), 0);
    snprintf(lookup_dir, sizeof(lookup_dir), "%s/%s", cwd, "shell_lookup_bin");
    snprintf(lookup_path, sizeof(lookup_path), "%s/%s", lookup_dir, "lookupcmd");
    ASSERT_EQ(shell_var_set(&state, "PATH", lookup_dir, 1), 0);
    ASSERT_EQ(shell_execute_string(&state,
                                   "alias hi='echo alias_ok'; "
                                   "type hi > shell_type_alias.txt; "
                                   "type echo > shell_type_builtin.txt; "
                                   "command -v hi > shell_command_alias.txt; "
                                   "command -v lookupcmd > shell_command_lookup.txt"),
              0);

    ASSERT_EQ(read_text_file("shell_type_alias.txt", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "hi is alias for echo alias_ok\n");
    ASSERT_EQ(read_text_file("shell_type_builtin.txt", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "echo is shell builtin\n");
    ASSERT_EQ(read_text_file("shell_command_alias.txt", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "alias hi='echo alias_ok'\n");
    ASSERT_EQ(read_text_file("shell_command_lookup.txt", text, sizeof(text)), 0);
    snprintf(lookup_dir, sizeof(lookup_dir), "%s\n", lookup_path);
    ASSERT_STR_EQ(text, lookup_dir);

    remove("shell_type_alias.txt");
    remove("shell_type_builtin.txt");
    remove("shell_command_alias.txt");
    remove("shell_command_lookup.txt");
    remove("shell_lookup_bin/lookupcmd");
    rmdir("shell_lookup_bin");
}

TEST(history_helpers_and_builtin) {
    struct shell_state state;
    char text[256];

    shell_state_init(&state, 1);
    ASSERT_EQ(shell_history_add(&state, "echo first"), 0);
    ASSERT_EQ(shell_history_add(&state, "echo second"), 0);
    ASSERT_EQ(shell_history_expand_line(&state, "!!", text, sizeof(text)), 1);
    ASSERT_STR_EQ(text, "echo second");
    ASSERT_EQ(shell_history_expand_line(&state, "!echo f", text, sizeof(text)), 1);
    ASSERT_STR_EQ(text, "echo first");
    ASSERT_EQ(shell_execute_string(&state, "history > shell_history.txt"), 0);
    ASSERT_EQ(read_text_file("shell_history.txt", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "1  echo first\n2  echo second\n");
    remove("shell_history.txt");
}

TEST(tilde_and_glob_expand) {
    struct shell_state state;
    char cwd[512];
    char text[256];

    ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_EQ(ensure_dir("shell_glob_tmp"), 0);
    ASSERT_EQ(ensure_dir("shell_glob_tmp/globdir"), 0);
    ASSERT_EQ(write_text_file("shell_glob_tmp/globdir/a.txt", "a\n"), 0);
    ASSERT_EQ(write_text_file("shell_glob_tmp/globdir/b.txt", "b\n"), 0);
    ASSERT_EQ(write_text_file("shell_glob_tmp/globdir/.hidden.txt", "h\n"), 0);
    ASSERT_EQ(chdir("shell_glob_tmp"), 0);

    shell_state_init(&state, 0);
    ASSERT_EQ(shell_var_set(&state, "HOME", "/tmp/sodex-home", 1), 0);
    ASSERT_EQ(shell_execute_string(&state,
                                   "echo ~ > home.txt; "
                                   "echo globdir/*.txt > glob.txt"),
              0);
    ASSERT_EQ(read_text_file("home.txt", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "/tmp/sodex-home\n");
    ASSERT_EQ(read_text_file("glob.txt", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "globdir/a.txt globdir/b.txt\n");

    remove("home.txt");
    remove("glob.txt");
    remove("globdir/a.txt");
    remove("globdir/b.txt");
    remove("globdir/.hidden.txt");
    rmdir("globdir");
    ASSERT_EQ(chdir(cwd), 0);
    rmdir("shell_glob_tmp");
}

int main(void)
{
    printf("=== shell usability tests ===\n");

    RUN_TEST(alias_and_unalias);
    RUN_TEST(type_and_command_v);
    RUN_TEST(history_helpers_and_builtin);
    RUN_TEST(tilde_and_glob_expand);

    TEST_REPORT();
}
