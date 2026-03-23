#include "test_framework.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int sxi_command_main(int argc, char **argv);
int sxi_source_needs_more_input(const char *text);

struct captured_run {
    int status;
    char stdout_text[2048];
    char stderr_text[2048];
};

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

static int contains_text(const char *haystack, const char *needle)
{
    size_t i;
    size_t j;

    if (haystack == NULL || needle == NULL)
        return 0;
    if (needle[0] == '\0')
        return 1;
    for (i = 0; haystack[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0'; j++) {
            if (haystack[i + j] == '\0' || haystack[i + j] != needle[j])
                break;
        }
        if (needle[j] == '\0')
            return 1;
    }
    return 0;
}

static int capture_sxi_run(const char *stdin_path,
                           int argc, char **argv,
                           struct captured_run *captured)
{
    const char *stdout_path = "test_sxi_cli_stdout.txt";
    const char *stderr_path = "test_sxi_cli_stderr.txt";
    int saved_stdin = -1;
    int saved_stdout = -1;
    int saved_stderr = -1;
    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;

    if (captured == NULL)
        return -1;
    memset(captured, 0, sizeof(*captured));

    saved_stdin = dup(STDIN_FILENO);
    saved_stdout = dup(STDOUT_FILENO);
    saved_stderr = dup(STDERR_FILENO);
    if (saved_stdin < 0 || saved_stdout < 0 || saved_stderr < 0)
        return -1;

    /* テストの進捗出力が sxi の出力に混ざらないように先に flush する */
    fflush(stdout);
    fflush(stderr);

    if (stdin_path != NULL) {
        stdin_fd = open(stdin_path, O_RDONLY, 0);
        if (stdin_fd < 0)
            return -1;
        dup2(stdin_fd, STDIN_FILENO);
        close(stdin_fd);
    }

    stdout_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stdout_fd < 0 || stderr_fd < 0)
        return -1;
    dup2(stdout_fd, STDOUT_FILENO);
    dup2(stderr_fd, STDERR_FILENO);
    close(stdout_fd);
    close(stderr_fd);

    captured->status = sxi_command_main(argc, argv);

    fflush(stdout);
    fflush(stderr);
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    close(saved_stderr);

    if (read_text_file(stdout_path, captured->stdout_text,
                       sizeof(captured->stdout_text)) < 0)
        return -1;
    if (read_text_file(stderr_path, captured->stderr_text,
                       sizeof(captured->stderr_text)) < 0)
        return -1;
    remove(stdout_path);
    remove(stderr_path);
    return 0;
}

TEST(cli_executes_inline_code) {
    char *argv[] = { "sxi", "-e", "let code = 7;\nio.println(code);\n", NULL };
    struct captured_run captured;

    ASSERT_EQ(capture_sxi_run(NULL, 3, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "7\n");
    ASSERT_STR_EQ(captured.stderr_text, "");
}

TEST(cli_prints_version_surface) {
    char *argv[] = { "sxi", "--version", NULL };
    struct captured_run captured;

    ASSERT_EQ(capture_sxi_run(NULL, 2, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "sxi 0.1.0 (sx 0.1.0, frontend abi 1, runtime abi 1)\n");
    ASSERT_STR_EQ(captured.stderr_text, "");
}

TEST(cli_reports_check_failure) {
    const char *path = "test_sxi_bad.sx";
    char *argv[] = { "sxi", "--check", "test_sxi_bad.sx", NULL };
    struct captured_run captured;

    ASSERT_EQ(write_text_file(path, "io.println(missing);\n"), 0);
    ASSERT_EQ(capture_sxi_run(NULL, 3, argv, &captured), 0);
    ASSERT_EQ(captured.status, 2);
    ASSERT_EQ(contains_text(captured.stderr_text, "undefined name"), 1);
    remove(path);
}

TEST(cli_executes_relative_imports) {
    const char *dir_path = "test_sxi_imports";
    const char *module_path = "test_sxi_imports/lib.sx";
    const char *main_path = "test_sxi_imports/main.sx";
    char *argv[] = { "sxi", "test_sxi_imports/main.sx", NULL };
    struct captured_run captured;

    mkdir(dir_path, 0755);
    ASSERT_EQ(write_text_file(
                  module_path,
                  "fn choose(flag) -> str {\n"
                  "  if (flag) {\n"
                  "    return \"IMPORT_OK\";\n"
                  "  }\n"
                  "  return \"BAD\";\n"
                  "}\n"),
              0);
    ASSERT_EQ(write_text_file(
                  main_path,
                  "import \"lib.sx\";\n"
                  "let ok = true;\n"
                  "let name = choose(ok);\n"
                  "io.println(name);\n"),
              0);
    ASSERT_EQ(capture_sxi_run(NULL, 2, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "IMPORT_OK\n");
    ASSERT_STR_EQ(captured.stderr_text, "");
    remove(main_path);
    remove(module_path);
    rmdir(dir_path);
}

TEST(cli_executes_stdlib_imports) {
    const char *path = "test_sxi_stdlib_main.sx";
    char *argv[] = { "sxi", "test_sxi_stdlib_main.sx", NULL };
    struct captured_run captured;

    ASSERT_EQ(write_text_file(
                  path,
                  "import \"std/strings\";\n"
                  "io.println(join_words(\"HELLO\", \"STDLIB\"));\n"),
              0);
    ASSERT_EQ(capture_sxi_run(NULL, 2, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "HELLO-STDLIB\n");
    ASSERT_STR_EQ(captured.stderr_text, "");
    remove(path);
}

TEST(cli_passes_script_args_to_runtime) {
    const char *path = "test_sxi_args.sx";
    char *argv[] = { "sxi", "test_sxi_args.sx", "first", "second", NULL };
    struct captured_run captured;

    ASSERT_EQ(write_text_file(
                  path,
                  "io.println(proc.argv_count());\n"
                  "io.println(proc.argv(1));\n"
                  "io.println(proc.argv(2));\n"),
              0);
    ASSERT_EQ(capture_sxi_run(NULL, 4, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "3\nfirst\nsecond\n");
    ASSERT_STR_EQ(captured.stderr_text, "");
    remove(path);
}

TEST(cli_check_mode_accepts_script_args) {
    const char *path = "test_sxi_check_args.sx";
    char *argv[] = { "sxi", "--check", "test_sxi_check_args.sx", "alpha", "beta", NULL };
    struct captured_run captured;

    ASSERT_EQ(write_text_file(
                  path,
                  "test.assert_eq(proc.argv_count(), 3);\n"
                  "test.assert_eq(proc.argv(1), \"alpha\");\n"
                  "test.assert_eq(proc.argv(2), \"beta\");\n"),
              0);
    ASSERT_EQ(capture_sxi_run(NULL, 5, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "");
    ASSERT_STR_EQ(captured.stderr_text, "");
    remove(path);
}

TEST(cli_check_mode_accepts_recursive_function_calls) {
    const char *path = "test_sxi_recursive_check.sx";
    char *argv[] = { "sxi", "--check", "test_sxi_recursive_check.sx", NULL };
    struct captured_run captured;

    ASSERT_EQ(write_text_file(
                  path,
                  "fn sum_to(n) -> i32 {\n"
                  "  if (n == 0) {\n"
                  "    return 0;\n"
                  "  }\n"
                  "  return n + sum_to(n - 1);\n"
                  "}\n"
                  "let value = sum_to(6);\n"
                  "io.println(value);\n"),
              0);
    ASSERT_EQ(capture_sxi_run(NULL, 3, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_STR_EQ(captured.stdout_text, "");
    ASSERT_STR_EQ(captured.stderr_text, "");
    remove(path);
}

TEST(cli_rejects_import_cycle) {
    const char *dir_path = "test_sxi_cycle";
    const char *a_path = "test_sxi_cycle/a.sx";
    const char *b_path = "test_sxi_cycle/b.sx";
    char *argv[] = { "sxi", "--check", "test_sxi_cycle/a.sx", NULL };
    struct captured_run captured;

    mkdir(dir_path, 0755);
    ASSERT_EQ(write_text_file(a_path, "import \"b.sx\";\n"), 0);
    ASSERT_EQ(write_text_file(b_path, "import \"a.sx\";\n"), 0);
    ASSERT_EQ(capture_sxi_run(NULL, 3, argv, &captured), 0);
    ASSERT_EQ(captured.status, 2);
    ASSERT_EQ(contains_text(captured.stderr_text, "import cycle detected"), 1);
    remove(a_path);
    remove(b_path);
    rmdir(dir_path);
}

TEST(cli_reports_stack_trace) {
    const char *path = "test_sxi_trace.sx";
    char *argv[] = { "sxi", "--check", "test_sxi_trace.sx", NULL };
    struct captured_run captured;

    ASSERT_EQ(write_text_file(
                  path,
                  "fn inner() -> str {\n"
                  "  return missing;\n"
                  "}\n"
                  "fn outer() -> str {\n"
                  "  return inner();\n"
                  "}\n"
                  "let value = outer();\n"),
              0);
    ASSERT_EQ(capture_sxi_run(NULL, 3, argv, &captured), 0);
    ASSERT_EQ(captured.status, 2);
    ASSERT_EQ(contains_text(captured.stderr_text, "undefined name"), 1);
    ASSERT_EQ(contains_text(captured.stderr_text, "at inner"), 1);
    ASSERT_EQ(contains_text(captured.stderr_text, "at outer"), 1);
    remove(path);
}

TEST(repl_handles_multiline_load_and_reset) {
    const char *stdin_path = "test_sxi_repl_input.txt";
    const char *load_path = "test_sxi_loaded.sx";
    const char *repl_text =
        "{\n"
        "  let inner = \"BLOCK\";\n"
        "  io.println(inner);\n"
        "}\n"
        ":load test_sxi_loaded.sx\n"
        "io.println(from_load);\n"
        ":reset\n"
        "io.println(from_load);\n"
        ":quit\n";
    char *argv[] = { "sxi", NULL };
    struct captured_run captured;

    ASSERT_EQ(sxi_source_needs_more_input("{\n  let x = 1;\n"), 1);
    ASSERT_EQ(sxi_source_needs_more_input("{\n  let x = 1;\n}\n"), 0);
    ASSERT_EQ(write_text_file(load_path, "let from_load = \"LOAD\";\n"), 0);
    ASSERT_EQ(write_text_file(stdin_path, repl_text), 0);
    ASSERT_EQ(capture_sxi_run(stdin_path, 1, argv, &captured), 0);
    ASSERT_EQ(captured.status, 0);
    ASSERT_EQ(contains_text(captured.stdout_text, "BLOCK\n"), 1);
    ASSERT_EQ(contains_text(captured.stdout_text, "LOAD\n"), 1);
    ASSERT_EQ(contains_text(captured.stderr_text, "undefined name"), 1);
    remove(stdin_path);
    remove(load_path);
}

int main(void)
{
    printf("=== sxi cli tests ===\n");

    RUN_TEST(cli_executes_inline_code);
    RUN_TEST(cli_prints_version_surface);
    RUN_TEST(cli_reports_check_failure);
    RUN_TEST(cli_executes_relative_imports);
    RUN_TEST(cli_executes_stdlib_imports);
    RUN_TEST(cli_passes_script_args_to_runtime);
    RUN_TEST(cli_check_mode_accepts_script_args);
    RUN_TEST(cli_check_mode_accepts_recursive_function_calls);
    RUN_TEST(cli_rejects_import_cycle);
    RUN_TEST(cli_reports_stack_trace);
    RUN_TEST(repl_handles_multiline_load_and_reset);

    TEST_REPORT();
}
