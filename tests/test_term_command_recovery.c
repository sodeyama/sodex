#include "test_framework.h"
#include <term_command_recovery.h>
#include <shell.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int ensure_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(path, 0755);
}

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

static int contains_text(const char *text, const char *needle)
{
    return text != NULL && needle != NULL && strstr(text, needle) != NULL;
}

static void setup_recovery_path(struct shell_state *state)
{
    char cwd[512];
    char path_env[768];

    ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_EQ(ensure_dir("recovery_bin"), 0);
    ASSERT_EQ(write_text_file("recovery_bin/ls", "dummy\n"), 0);
    ASSERT_EQ(write_text_file("recovery_bin/rm", "dummy\n"), 0);
    snprintf(path_env, sizeof(path_env), "%s/%s", cwd, "recovery_bin");
    ASSERT_EQ(shell_var_set(state, "PATH", path_env, 1), 0);
}

TEST(command_typo_auto_apply) {
    struct shell_state state;
    struct term_command_recovery_result result;

    shell_state_init(&state, 0);
    setup_recovery_path(&state);
    ASSERT_EQ(term_command_recovery_build(&state, "sl > typo_ls.txt", 127, &result), 1);
    ASSERT_EQ(result.kind, TERM_COMMAND_RECOVERY_SUGGEST);
    ASSERT_STR_EQ(result.reason, "command-typo");
    ASSERT_STR_EQ(result.replacement, "ls > typo_ls.txt");
    ASSERT_EQ(result.auto_apply, 1);
    ASSERT_EQ(result.destructive, 0);

    remove("recovery_bin/ls");
    remove("recovery_bin/rm");
    rmdir("recovery_bin");
}

TEST(history_candidate_is_used) {
    struct shell_state state;
    struct term_command_recovery_result result;

    shell_state_init(&state, 1);
    ASSERT_EQ(shell_var_set(&state, "PATH", "/no/such/path", 1), 0);
    ASSERT_EQ(shell_history_add(&state, "pwd > before.txt"), 0);
    ASSERT_EQ(term_command_recovery_build(&state, "pwc > out.txt", 127, &result), 1);
    ASSERT_STR_EQ(result.replacement, "pwd > out.txt");
    ASSERT_EQ(result.auto_apply, 1);
}

TEST(path_fix_for_cd) {
    struct shell_state state;
    struct term_command_recovery_result result;
    char cwd[512];

    ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    ASSERT_EQ(ensure_dir("recovery_tree"), 0);
    ASSERT_EQ(ensure_dir("recovery_tree/home"), 0);
    ASSERT_EQ(ensure_dir("recovery_tree/home/user"), 0);
    ASSERT_EQ(chdir("recovery_tree"), 0);

    shell_state_init(&state, 0);
    shell_state_set_last_error(&state, "sh: cd failed\n");
    ASSERT_EQ(term_command_recovery_build(&state, "cd hme/user", 1, &result), 1);
    ASSERT_STR_EQ(result.reason, "path");
    ASSERT_STR_EQ(result.replacement, "cd home/user");
    ASSERT_EQ(result.auto_apply, 1);

    ASSERT_EQ(chdir(cwd), 0);
    rmdir("recovery_tree/home/user");
    rmdir("recovery_tree/home");
    rmdir("recovery_tree");
}

TEST(destructive_command_is_not_auto_applied) {
    struct shell_state state;
    struct term_command_recovery_result result;

    shell_state_init(&state, 0);
    setup_recovery_path(&state);
    ASSERT_EQ(term_command_recovery_build(&state, "rmm target.txt", 127, &result), 1);
    ASSERT_STR_EQ(result.replacement, "rm target.txt");
    ASSERT_EQ(result.destructive, 1);
    ASSERT_EQ(result.auto_apply, 0);

    remove("recovery_bin/ls");
    remove("recovery_bin/rm");
    rmdir("recovery_bin");
}

TEST(git_push_upstream_hint) {
    struct shell_state state;
    struct term_command_recovery_result result;

    shell_state_init(&state, 0);
    shell_state_set_last_error(&state,
                               "fatal: The current branch main has no upstream branch.\n");
    ASSERT_EQ(term_command_recovery_build(&state, "git push", 1, &result), 1);
    ASSERT_STR_EQ(result.reason, "git-upstream");
    ASSERT_STR_EQ(result.replacement, "git push --set-upstream origin main");
    ASSERT_EQ(result.auto_apply, 0);
    ASSERT_EQ(result.destructive, 1);
}

TEST(permission_denied_hint) {
    struct shell_state state;
    struct term_command_recovery_result result;

    shell_state_init(&state, 0);
    shell_state_set_last_error(&state, "Permission denied\n");
    ASSERT_EQ(term_command_recovery_build(&state, "./script.sh", 1, &result), 1);
    ASSERT_EQ(result.kind, TERM_COMMAND_RECOVERY_HINT);
    ASSERT_STR_EQ(result.reason, "permission");
    ASSERT(contains_text(result.display, "権限"));
}

int main(void)
{
    printf("=== term command recovery tests ===\n");

    RUN_TEST(command_typo_auto_apply);
    RUN_TEST(history_candidate_is_used);
    RUN_TEST(path_fix_for_cd);
    RUN_TEST(destructive_command_is_not_auto_applied);
    RUN_TEST(git_push_upstream_hint);
    RUN_TEST(permission_denied_hint);

    TEST_REPORT();
}
