#include "test_framework.h"
#include <key.h>
#include <shell_completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ICANON 0x0001U
#define TEST_ROOT "/tmp/sodex_shell_completion_case"

static int g_case_id = 0;

static void cleanup_tree(const char *path)
{
    char cmd[512];

    if (path == NULL || path[0] == '\0')
        return;
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    system(cmd);
}

static int make_case_dir(char *path, size_t cap)
{
    snprintf(path, cap, TEST_ROOT "_%ld_%d", (long)getpid(), g_case_id++);
    cleanup_tree(path);
    return mkdir(path, 0755) == 0;
}

static int join_path(char *dst, size_t cap, const char *dir, const char *name)
{
    return snprintf(dst, cap, "%s/%s", dir, name) < (int)cap;
}

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
        return 0;
    if (text != NULL)
        fwrite(text, 1, strlen(text), fp);
    fclose(fp);
    return 1;
}

static void observe_prompt(struct shell_completion_state *state, const char *cwd)
{
    char prompt[768];

    snprintf(prompt, sizeof(prompt), "sodex %s> ", cwd);
    shell_completion_state_observe_output(state, prompt, (int)strlen(prompt), 0);
}

static int feed_text(struct shell_completion_state *state, const char *text)
{
    return shell_completion_state_feed_input(state, text, (int)strlen(text));
}

static int apply_completion_bytes(struct shell_completion_state *state,
                                  const char *buf, int len)
{
    return shell_completion_state_feed_input(state, buf, len);
}

static int complete_once(struct shell_completion_state *state, int reverse)
{
    char buf[512];
    int len = shell_completion_state_complete(state, reverse, buf, sizeof(buf));

    if (len > 0)
        apply_completion_bytes(state, buf, len);
    return len;
}

static int cancel_once(struct shell_completion_state *state)
{
    char buf[512];
    int len = shell_completion_state_cancel_completion(state, buf, sizeof(buf));

    if (len > 0)
        apply_completion_bytes(state, buf, len);
    return len;
}

TEST(can_track_only_for_shell_prompt_in_canonical_mode) {
    struct shell_completion_state state;

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);

    ASSERT_EQ(shell_completion_state_can_track(&state, 0, TEST_ICANON), 1);
    ASSERT_EQ(shell_completion_state_can_track(&state, 42, TEST_ICANON), 1);
    ASSERT_EQ(shell_completion_state_can_track(&state, 7, TEST_ICANON), 0);
    ASSERT_EQ(shell_completion_state_can_track(&state, 0, 0), 0);
}

TEST(can_complete_requires_valid_state_and_ime_idle) {
    struct shell_completion_state state;
    char ctrl_c = 0x03;

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);

    ASSERT_EQ(shell_completion_state_can_complete(&state, 0, TEST_ICANON, 0), 1);
    ASSERT_EQ(shell_completion_state_can_complete(&state, 0, TEST_ICANON, 1), 0);

    ASSERT_EQ(shell_completion_state_feed_input(&state, &ctrl_c, 1), 0);
    ASSERT_EQ(shell_completion_state_valid(&state), 0);
    ASSERT_EQ(shell_completion_state_can_complete(&state, 0, TEST_ICANON, 0), 0);

    shell_completion_state_observe_output(&state, "^C\n", 3, 0);
    ASSERT_EQ(shell_completion_state_valid(&state), 1);
    ASSERT_EQ(shell_completion_state_can_complete(&state, 0, TEST_ICANON, 0), 1);
}

TEST(feed_input_tracks_utf8_and_backspace) {
    struct shell_completion_state state;
    const char a_utf8[] = { (char)0xe3, (char)0x81, (char)0x82 };
    char backspace = KEY_BACK;

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);

    ASSERT_EQ(shell_completion_state_feed_input(&state, "cat ", 4), 4);
    ASSERT_EQ(shell_completion_state_feed_input(&state, a_utf8, sizeof(a_utf8)), 7);
    ASSERT_EQ(shell_completion_state_feed_input(&state, &backspace, 1), 4);
    ASSERT_EQ(shell_completion_state_feed_input(&state, "b", 1), 5);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat b");
    ASSERT_EQ(shell_completion_state_line_len(&state), 5);
}

TEST(unique_file_completion_appends_space) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "hoge_only.txt"));
    ASSERT(write_text_file(path, "hello\n"));

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat hoge_o"), 10);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat hoge_only.txt ");
    ASSERT_EQ(shell_completion_state_active(&state), 0);

    cleanup_tree(case_dir);
}

TEST(unique_directory_completion_appends_slash) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "dir_only"));
    ASSERT_EQ(mkdir(path, 0755), 0);

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat dir_o"), 9);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat dir_only/");
    ASSERT_EQ(shell_completion_state_active(&state), 0);

    cleanup_tree(case_dir);
}

TEST(multiple_candidates_extend_cycle_and_cancel) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];
    char overlay[160];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "pair_apple.txt"));
    ASSERT(write_text_file(path, ""));
    ASSERT(join_path(path, sizeof(path), case_dir, "pair_apricot.txt"));
    ASSERT(write_text_file(path, ""));

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat pair_"), 9);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat pair_ap");
    ASSERT_EQ(shell_completion_state_active(&state), 1);
    ASSERT(shell_completion_state_overlay_text(&state, overlay, sizeof(overlay)) > 0);
    ASSERT_STR_EQ(overlay, "CMP 0/2 pair_ap");

    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat pair_apple.txt");
    ASSERT(shell_completion_state_overlay_text(&state, overlay, sizeof(overlay)) > 0);
    ASSERT_STR_EQ(overlay, "CMP 1/2 pair_apple.txt");

    ASSERT(complete_once(&state, 1) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat pair_apricot.txt");
    ASSERT(shell_completion_state_overlay_text(&state, overlay, sizeof(overlay)) > 0);
    ASSERT_STR_EQ(overlay, "CMP 2/2 pair_apricot.txt");

    ASSERT(cancel_once(&state) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat pair_");
    ASSERT_EQ(shell_completion_state_active(&state), 0);

    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat pair_ap");
    ASSERT_EQ(shell_completion_state_active(&state), 1);

    cleanup_tree(case_dir);
}

TEST(escaped_space_completion_uses_backslash_escape) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "two words.txt"));
    ASSERT(write_text_file(path, ""));

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat two\\ w"), 10);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat two\\ words.txt ");

    cleanup_tree(case_dir);
}

TEST(quoted_completion_preserves_open_quote) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "two words.txt"));
    ASSERT(write_text_file(path, ""));

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat \"two w"), 10);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat \"two words.txt");

    cleanup_tree(case_dir);
}

TEST(redirection_completion_targets_last_word) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "out.txt"));
    ASSERT(write_text_file(path, ""));

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat > ou"), 8);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat > out.txt ");

    cleanup_tree(case_dir);
}

TEST(utf8_filename_completion_keeps_utf8) {
    struct shell_completion_state state;
    char case_dir[256];
    char path[320];

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(path, sizeof(path), case_dir, "漢字.txt"));
    ASSERT(write_text_file(path, ""));

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);
    observe_prompt(&state, case_dir);

    ASSERT_EQ(feed_text(&state, "cat 漢"), 7);
    ASSERT(complete_once(&state, 0) > 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "cat 漢字.txt ");

    cleanup_tree(case_dir);
}

TEST(unsupported_control_invalidates_until_newline_output) {
    struct shell_completion_state state;
    char ctrl_a = 0x01;

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);

    ASSERT_EQ(shell_completion_state_feed_input(&state, "cat ", 4), 4);
    ASSERT_EQ(shell_completion_state_feed_input(&state, &ctrl_a, 1), 0);
    ASSERT_EQ(shell_completion_state_valid(&state), 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "");

    ASSERT_EQ(shell_completion_state_feed_input(&state, "x", 1), 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "");

    shell_completion_state_observe_output(&state, "\n", 1, 0);
    ASSERT_EQ(shell_completion_state_valid(&state), 1);
    ASSERT_EQ(shell_completion_state_feed_input(&state, "ls", 2), 2);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "ls");
}

TEST(foreground_child_output_invalidates_tracking) {
    struct shell_completion_state state;

    shell_completion_state_init(&state);
    shell_completion_state_set_shell_pid(&state, 42);

    ASSERT_EQ(shell_completion_state_feed_input(&state, "cat file", 8), 8);
    shell_completion_state_observe_output(&state, "output\n", 7, 99);
    ASSERT_EQ(shell_completion_state_valid(&state), 0);
    ASSERT_STR_EQ(shell_completion_state_line(&state), "");
}

int main(void)
{
    printf("=== shell completion tests ===\n");

    RUN_TEST(can_track_only_for_shell_prompt_in_canonical_mode);
    RUN_TEST(can_complete_requires_valid_state_and_ime_idle);
    RUN_TEST(feed_input_tracks_utf8_and_backspace);
    RUN_TEST(unique_file_completion_appends_space);
    RUN_TEST(unique_directory_completion_appends_slash);
    RUN_TEST(multiple_candidates_extend_cycle_and_cancel);
    RUN_TEST(escaped_space_completion_uses_backslash_escape);
    RUN_TEST(quoted_completion_preserves_open_quote);
    RUN_TEST(redirection_completion_targets_last_word);
    RUN_TEST(utf8_filename_completion_keeps_utf8);
    RUN_TEST(unsupported_control_invalidates_until_newline_output);
    RUN_TEST(foreground_child_output_invalidates_tracking);

    TEST_REPORT();
}
