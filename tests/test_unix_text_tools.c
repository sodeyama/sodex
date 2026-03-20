#include "test_framework.h"
#include <unix_text_tools.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ROOT "/tmp/sodex_unix_text_tools_case"

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
    fwrite(text, 1, strlen(text), fp);
    fclose(fp);
    return 1;
}

static int read_text_file(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t nread;

    if (fp == NULL || cap == 0)
        return 0;
    nread = fread(buf, 1, cap - 1, fp);
    fclose(fp);
    buf[nread] = '\0';
    return 1;
}

static int run_tool_capture(int (*fn)(int, char **),
                            int argc,
                            char **argv,
                            const char *stdin_text,
                            char *out,
                            size_t out_cap)
{
    char in_template[] = "/tmp/sodex_tool_in_XXXXXX";
    char out_template[] = "/tmp/sodex_tool_out_XXXXXX";
    int infd = -1;
    int outfd = -1;
    int saved_stdin = -1;
    int saved_stdout = -1;
    int rc;

    if (out == NULL || out_cap == 0)
        return -999;

    if (stdin_text != NULL) {
        infd = mkstemp(in_template);
        if (infd < 0)
            return -998;
        write(infd, stdin_text, strlen(stdin_text));
        lseek(infd, 0, SEEK_SET);
    }

    outfd = mkstemp(out_template);
    if (outfd < 0) {
        if (infd >= 0) {
            close(infd);
            unlink(in_template);
        }
        return -997;
    }

    saved_stdin = dup(STDIN_FILENO);
    saved_stdout = dup(STDOUT_FILENO);
    if (stdin_text != NULL)
        dup2(infd, STDIN_FILENO);
    dup2(outfd, STDOUT_FILENO);

    rc = fn(argc, argv);

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    if (infd >= 0) {
        close(infd);
        unlink(in_template);
    }
    close(outfd);
    if (!read_text_file(out_template, out, out_cap)) {
        unlink(out_template);
        return -996;
    }
    unlink(out_template);
    return rc;
}

TEST(find_filters_by_name_type_and_depth) {
    char case_dir[256];
    char subdir[320];
    char root_txt[320];
    char nested_txt[320];
    char nested_log[320];
    char out[1024];
    char *argv[] = { "find", case_dir, "-type", "f", "-name", "*.txt", "-maxdepth", "1" };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(subdir, sizeof(subdir), case_dir, "sub"));
    ASSERT(mkdir(subdir, 0755) == 0);
    ASSERT(join_path(root_txt, sizeof(root_txt), case_dir, "root.txt"));
    ASSERT(join_path(nested_txt, sizeof(nested_txt), subdir, "nested.txt"));
    ASSERT(join_path(nested_log, sizeof(nested_log), subdir, "nested.log"));
    ASSERT(write_text_file(root_txt, "root\n"));
    ASSERT(write_text_file(nested_txt, "nested\n"));
    ASSERT(write_text_file(nested_log, "log\n"));

    ASSERT_EQ(run_tool_capture(unix_find_main, 8, argv, NULL, out, sizeof(out)), 0);
    ASSERT(strstr(out, "root.txt") != NULL);
    ASSERT(strstr(out, "nested.txt") == NULL);

    cleanup_tree(case_dir);
}

TEST(sort_uniq_wc_head_tail_workflow) {
    char case_dir[256];
    char data_path[320];
    char out[1024];
    char *sort_argv[] = { "sort", "-n", data_path };
    char *uniq_argv[] = { "uniq", "-c" };
    char *wc_argv[] = { "wc", "-l", "-w", "-c", data_path };
    char *head_argv[] = { "head", "-n", "2", data_path };
    char *tail_argv[] = { "tail", "-n", "2", data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "numbers.txt"));
    ASSERT(write_text_file(data_path, "10\n2\n2\n1\n"));

    ASSERT_EQ(run_tool_capture(unix_sort_main, 3, sort_argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "1\n2\n2\n10\n");

    ASSERT_EQ(run_tool_capture(unix_uniq_main, 2, uniq_argv, "a\na\nb\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "2 a\n1 b\n");

    ASSERT_EQ(run_tool_capture(unix_wc_main, 5, wc_argv, NULL, out, sizeof(out)), 0);
    ASSERT(strstr(out, "4 4 9") != NULL);

    ASSERT_EQ(run_tool_capture(unix_head_main, 4, head_argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "10\n2\n");

    ASSERT_EQ(run_tool_capture(unix_tail_main, 4, tail_argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "2\n1\n");

    cleanup_tree(case_dir);
}

TEST(grep_cut_tr_and_tee_workflow) {
    char case_dir[256];
    char grep_path[320];
    char tee_path[320];
    char out[1024];
    char *grep_argv[] = { "grep", "-n", "foo", grep_path };
    char *cut_argv[] = { "cut", "-d", ":", "-f", "1,3" };
    char *tr_argv[] = { "tr", "-s", " " };
    char *tee_argv[] = { "tee", tee_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(grep_path, sizeof(grep_path), case_dir, "grep.txt"));
    ASSERT(join_path(tee_path, sizeof(tee_path), case_dir, "tee.txt"));
    ASSERT(write_text_file(grep_path, "foo\nbar\nfoo bar\n"));

    ASSERT_EQ(run_tool_capture(unix_grep_main, 4, grep_argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "1:foo\n3:foo bar\n");

    ASSERT_EQ(run_tool_capture(unix_cut_main, 5, cut_argv, "aa:bb:cc\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "aa:cc\n");

    ASSERT_EQ(run_tool_capture(unix_tr_main, 3, tr_argv, "a   b\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "a b\n");

    ASSERT_EQ(run_tool_capture(unix_tee_main, 2, tee_argv, "tee-data\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "tee-data\n");
    ASSERT(read_text_file(tee_path, out, sizeof(out)));
    ASSERT_STR_EQ(out, "tee-data\n");

    cleanup_tree(case_dir);
}

TEST(sed_and_awk_transform_text) {
    char case_dir[256];
    char sed_path[320];
    char awk_path[320];
    char out[1024];
    char *sed_argv[] = { "sed", "-e", "s/foo/bar/g", "-e", "2d", sed_path };
    char *awk_argv[] = { "awk", "-F", ":", "{ print $1, $3 }", awk_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(sed_path, sizeof(sed_path), case_dir, "sed.txt"));
    ASSERT(join_path(awk_path, sizeof(awk_path), case_dir, "awk.txt"));
    ASSERT(write_text_file(sed_path, "foo\nkeep\nfoofoo\n"));
    ASSERT(write_text_file(awk_path, "aa:bb:cc\n"));

    ASSERT_EQ(run_tool_capture(unix_sed_main, 6, sed_argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "bar\nbarbar\n");

    ASSERT_EQ(run_tool_capture(unix_awk_main, 5, awk_argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "aa cc\n");

    cleanup_tree(case_dir);
}

TEST(diff_reports_quiet_and_unified) {
    char case_dir[256];
    char left[320];
    char right[320];
    char out[2048];
    char *diff_q_argv[] = { "diff", "-q", left, right };
    char *diff_u_argv[] = { "diff", "-u", left, right };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(left, sizeof(left), case_dir, "left.txt"));
    ASSERT(join_path(right, sizeof(right), case_dir, "right.txt"));
    ASSERT(write_text_file(left, "one\ntwo\n"));
    ASSERT(write_text_file(right, "one\nthree\n"));

    ASSERT_EQ(run_tool_capture(unix_diff_main, 4, diff_q_argv, NULL, out, sizeof(out)), 1);
    ASSERT(strstr(out, "differ") != NULL);

    ASSERT_EQ(run_tool_capture(unix_diff_main, 4, diff_u_argv, NULL, out, sizeof(out)), 1);
    ASSERT(strstr(out, "--- ") != NULL);
    ASSERT(strstr(out, "+++ ") != NULL);
    ASSERT(strstr(out, "-two") != NULL);
    ASSERT(strstr(out, "+three") != NULL);

    cleanup_tree(case_dir);
}

TEST(find_supports_long_options_and_print) {
    char case_dir[256];
    char subdir[320];
    char root_txt[320];
    char nested_txt[320];
    char out[1024];
    char *argv[] = { "find", case_dir, "--type=f", "--name=*.txt", "--maxdepth=1", "-print" };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(subdir, sizeof(subdir), case_dir, "sub"));
    ASSERT(mkdir(subdir, 0755) == 0);
    ASSERT(join_path(root_txt, sizeof(root_txt), case_dir, "root.txt"));
    ASSERT(join_path(nested_txt, sizeof(nested_txt), subdir, "nested.txt"));
    ASSERT(write_text_file(root_txt, "root\n"));
    ASSERT(write_text_file(nested_txt, "nested\n"));

    ASSERT_EQ(run_tool_capture(unix_find_main, 6, argv, NULL, out, sizeof(out)), 0);
    ASSERT(strstr(out, "root.txt") != NULL);
    ASSERT(strstr(out, "nested.txt") == NULL);

    cleanup_tree(case_dir);
}

TEST(sort_supports_long_options_and_output_file) {
    char case_dir[256];
    char data_path[320];
    char sorted_path[320];
    char output_arg[384];
    char out[1024];
    char *argv[] = { "sort", "--numeric-sort", "--reverse", output_arg, data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "numbers.txt"));
    ASSERT(join_path(sorted_path, sizeof(sorted_path), case_dir, "sorted.txt"));
    ASSERT(write_text_file(data_path, "1\n10\n2\n2\n"));
    ASSERT(snprintf(output_arg, sizeof(output_arg), "--output=%s", sorted_path) < (int)sizeof(output_arg));

    ASSERT_EQ(run_tool_capture(unix_sort_main, 5, argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    ASSERT(read_text_file(sorted_path, out, sizeof(out)));
    ASSERT_STR_EQ(out, "10\n2\n2\n1\n");

    cleanup_tree(case_dir);
}

TEST(uniq_supports_long_options) {
    char out[1024];
    char *argv[] = { "uniq", "--count", "--repeated" };

    ASSERT_EQ(run_tool_capture(unix_uniq_main, 3, argv, "a\na\nb\nc\nc\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "2 a\n2 c\n");
}

TEST(wc_supports_long_options_and_counts_newlines_only) {
    char case_dir[256];
    char data_path[320];
    char out[1024];
    char *argv[] = { "wc", "--lines", "--words", "--bytes", data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "wc.txt"));
    ASSERT(write_text_file(data_path, "aa\nbb"));

    ASSERT_EQ(run_tool_capture(unix_wc_main, 5, argv, NULL, out, sizeof(out)), 0);
    ASSERT(strstr(out, "1 2 5") != NULL);

    cleanup_tree(case_dir);
}

TEST(head_supports_negative_line_counts) {
    char case_dir[256];
    char data_path[320];
    char out[1024];
    char *argv[] = { "head", "--lines=-1", data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "head.txt"));
    ASSERT(write_text_file(data_path, "1\n2\n3\n"));

    ASSERT_EQ(run_tool_capture(unix_head_main, 3, argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "1\n2\n");

    cleanup_tree(case_dir);
}

TEST(tail_supports_from_start_line_counts) {
    char case_dir[256];
    char data_path[320];
    char out[1024];
    char *argv[] = { "tail", "--lines=+2", data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "tail.txt"));
    ASSERT(write_text_file(data_path, "1\n2\n3\n"));

    ASSERT_EQ(run_tool_capture(unix_tail_main, 3, argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "2\n3\n");

    cleanup_tree(case_dir);
}

TEST(grep_supports_long_options_and_stdin_operand) {
    char out[1024];
    char *argv[] = { "grep", "--fixed-strings", "--ignore-case", "--count", "needle", "-" };

    ASSERT_EQ(run_tool_capture(unix_grep_main, 6, argv,
                               "Needle\nother\nNEEDLE\n",
                               out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "2\n");
}

TEST(cut_supports_long_options_and_complement) {
    char out[1024];
    char *argv[] = { "cut", "--delimiter=:", "--fields=2", "--complement" };

    ASSERT_EQ(run_tool_capture(unix_cut_main, 4, argv, "aa:bb:cc\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "aa:cc\n");
}

TEST(tr_supports_long_options) {
    char out[1024];
    char *argv[] = { "tr", "--complement", "--delete", "a-z" };

    ASSERT_EQ(run_tool_capture(unix_tr_main, 4, argv, "abc123\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "abc");
}

TEST(diff_supports_long_options) {
    char case_dir[256];
    char left[320];
    char right[320];
    char out[2048];
    char *brief_argv[] = { "diff", "--brief", left, right };
    char *unified_argv[] = { "diff", "--unified", left, right };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(left, sizeof(left), case_dir, "left.txt"));
    ASSERT(join_path(right, sizeof(right), case_dir, "right.txt"));
    ASSERT(write_text_file(left, "one\ntwo\n"));
    ASSERT(write_text_file(right, "one\nthree\n"));

    ASSERT_EQ(run_tool_capture(unix_diff_main, 4, brief_argv, NULL, out, sizeof(out)), 1);
    ASSERT(strstr(out, "differ") != NULL);

    ASSERT_EQ(run_tool_capture(unix_diff_main, 4, unified_argv, NULL, out, sizeof(out)), 1);
    ASSERT(strstr(out, "--- ") != NULL);
    ASSERT(strstr(out, "+++ ") != NULL);

    cleanup_tree(case_dir);
}

TEST(tee_supports_long_append) {
    char case_dir[256];
    char tee_path[320];
    char out[1024];
    char *argv[] = { "tee", "--append", tee_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(tee_path, sizeof(tee_path), case_dir, "tee.txt"));
    ASSERT(write_text_file(tee_path, "old\n"));

    ASSERT_EQ(run_tool_capture(unix_tee_main, 3, argv, "new\n", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "new\n");
    ASSERT(read_text_file(tee_path, out, sizeof(out)));
    ASSERT_STR_EQ(out, "old\nnew\n");

    cleanup_tree(case_dir);
}

TEST(sed_supports_long_options_and_script_files) {
    char case_dir[256];
    char data_path[320];
    char script_path[320];
    char out[1024];
    char *argv[] = { "sed", "--quiet", "--file", script_path, data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "sed.txt"));
    ASSERT(join_path(script_path, sizeof(script_path), case_dir, "script.sed"));
    ASSERT(write_text_file(data_path, "one\ntwo\n"));
    ASSERT(write_text_file(script_path, "2p\n"));

    ASSERT_EQ(run_tool_capture(unix_sed_main, 5, argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "two\n");

    cleanup_tree(case_dir);
}

TEST(awk_supports_script_files_and_compact_F) {
    char case_dir[256];
    char data_path[320];
    char script_path[320];
    char out[1024];
    char *argv[] = { "awk", "-F:", "-v", "label=start", "-f", script_path, data_path };

    ASSERT(make_case_dir(case_dir, sizeof(case_dir)));
    ASSERT(join_path(data_path, sizeof(data_path), case_dir, "awk.txt"));
    ASSERT(join_path(script_path, sizeof(script_path), case_dir, "script.awk"));
    ASSERT(write_text_file(data_path, "aa:bb\n"));
    ASSERT(write_text_file(script_path,
                           "BEGIN { print label }\n"
                           "{ print $2 }\n"
                           "END { print \"done\" }\n"));

    ASSERT_EQ(run_tool_capture(unix_awk_main, 7, argv, NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "start\nbb\ndone\n");

    cleanup_tree(case_dir);
}

TEST(unix_text_tools_support_help_options) {
    struct help_case {
        const char *name;
        int (*fn)(int, char **);
    };
    struct help_case cases[] = {
        { "find", unix_find_main },
        { "sort", unix_sort_main },
        { "uniq", unix_uniq_main },
        { "wc", unix_wc_main },
        { "head", unix_head_main },
        { "tail", unix_tail_main },
        { "grep", unix_grep_main },
        { "cut", unix_cut_main },
        { "tr", unix_tr_main },
        { "diff", unix_diff_main },
        { "tee", unix_tee_main },
        { "sed", unix_sed_main },
        { "awk", unix_awk_main },
    };
    char out[1024];
    int i;

    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        char *long_argv[] = { (char *)cases[i].name, "--help" };
        char *short_argv[] = { (char *)cases[i].name, "-h" };

        ASSERT_EQ(run_tool_capture(cases[i].fn, 2, long_argv, NULL, out, sizeof(out)), 0);
        ASSERT(strncmp(out, "usage: ", 7) == 0);
        ASSERT(strstr(out, cases[i].name) != NULL);

        ASSERT_EQ(run_tool_capture(cases[i].fn, 2, short_argv, NULL, out, sizeof(out)), 0);
        ASSERT(strncmp(out, "usage: ", 7) == 0);
        ASSERT(strstr(out, cases[i].name) != NULL);
    }
}

int main(void)
{
    RUN_TEST(find_filters_by_name_type_and_depth);
    RUN_TEST(sort_uniq_wc_head_tail_workflow);
    RUN_TEST(grep_cut_tr_and_tee_workflow);
    RUN_TEST(sed_and_awk_transform_text);
    RUN_TEST(diff_reports_quiet_and_unified);
    RUN_TEST(find_supports_long_options_and_print);
    RUN_TEST(sort_supports_long_options_and_output_file);
    RUN_TEST(uniq_supports_long_options);
    RUN_TEST(wc_supports_long_options_and_counts_newlines_only);
    RUN_TEST(head_supports_negative_line_counts);
    RUN_TEST(tail_supports_from_start_line_counts);
    RUN_TEST(grep_supports_long_options_and_stdin_operand);
    RUN_TEST(cut_supports_long_options_and_complement);
    RUN_TEST(tr_supports_long_options);
    RUN_TEST(diff_supports_long_options);
    RUN_TEST(tee_supports_long_append);
    RUN_TEST(sed_supports_long_options_and_script_files);
    RUN_TEST(awk_supports_script_files_and_compact_F);
    RUN_TEST(unix_text_tools_support_help_options);
    TEST_REPORT();
}
