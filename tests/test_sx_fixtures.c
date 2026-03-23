#include "test_framework.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int sxi_command_main(int argc, char **argv);

enum fixture_mode {
    FIXTURE_RUN = 0,
    FIXTURE_CHECK = 1,
};

struct captured_run {
    int status;
    char stdout_text[2048];
    char stderr_text[2048];
};

struct fixture_case {
    const char *name;
    enum fixture_mode mode;
    const char *script_path;
    const char *expected_path;
};

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

static int resolve_fixture_path(const char *path, char *resolved, size_t cap)
{
    if (path == NULL || resolved == NULL || cap == 0)
        return -1;
    if (access(path, R_OK) == 0) {
        snprintf(resolved, cap, "%s", path);
        return 0;
    }
    if (snprintf(resolved, cap, "tests/%s", path) >= (int)cap)
        return -1;
    if (access(resolved, R_OK) == 0)
        return 0;
    return -1;
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

static int capture_sxi_run(int argc, char **argv, struct captured_run *captured)
{
    const char *stdout_path = "test_sx_fixtures_stdout.txt";
    const char *stderr_path = "test_sx_fixtures_stderr.txt";
    int saved_stdout = -1;
    int saved_stderr = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;

    if (captured == NULL)
        return -1;
    memset(captured, 0, sizeof(*captured));

    saved_stdout = dup(STDOUT_FILENO);
    saved_stderr = dup(STDERR_FILENO);
    if (saved_stdout < 0 || saved_stderr < 0)
        return -1;

    /* テスト出力が fixture の stdout/stderr に混ざらないように先に flush する。 */
    fflush(stdout);
    fflush(stderr);

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
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
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

static void assert_fixture_case(const struct fixture_case *fixture)
{
    char expected[2048];
    char script_path[512];
    char expected_path[512];
    char *argv_run[] = { "sxi", script_path, NULL };
    char *argv_check[] = { "sxi", "--check", script_path, NULL };
    struct captured_run captured;

    ASSERT_EQ(fixture != NULL, 1);
    ASSERT_EQ(resolve_fixture_path(fixture->script_path, script_path, sizeof(script_path)), 0);
    ASSERT_EQ(resolve_fixture_path(fixture->expected_path, expected_path, sizeof(expected_path)), 0);
    ASSERT_EQ(read_text_file(expected_path, expected, sizeof(expected)), 0);
    if (fixture->mode == FIXTURE_RUN) {
        ASSERT_EQ(capture_sxi_run(2, argv_run, &captured), 0);
        ASSERT_EQ(captured.status, 0);
        ASSERT_STR_EQ(captured.stdout_text, expected);
        ASSERT_STR_EQ(captured.stderr_text, "");
        return;
    }

    ASSERT_EQ(capture_sxi_run(3, argv_check, &captured), 0);
    ASSERT_EQ(captured.status, 2);
    ASSERT_EQ(contains_text(captured.stderr_text, expected), 1);
}

TEST(file_fixture_corpus_matches_expectations) {
    static const struct fixture_case fixtures[] = {
        {
            "recursive_sum",
            FIXTURE_RUN,
            "fixtures/sx/runtime/recursive_sum.sx",
            "fixtures/sx/runtime/recursive_sum.out",
        },
        {
            "relative_import",
            FIXTURE_RUN,
            "fixtures/sx/runtime/import_relative/main.sx",
            "fixtures/sx/runtime/import_relative/main.out",
        },
        {
            "stdlib_import",
            FIXTURE_RUN,
            "fixtures/sx/runtime/import_stdlib.sx",
            "fixtures/sx/runtime/import_stdlib.out",
        },
        {
            "spawn_wait",
            FIXTURE_RUN,
            "fixtures/sx/runtime/spawn_wait.sx",
            "fixtures/sx/runtime/spawn_wait.out",
        },
        {
            "bytes_result",
            FIXTURE_RUN,
            "fixtures/sx/runtime/bytes_result.sx",
            "fixtures/sx/runtime/bytes_result.out",
        },
        {
            "list_map",
            FIXTURE_RUN,
            "fixtures/sx/runtime/list_map.sx",
            "fixtures/sx/runtime/list_map.out",
        },
        {
            "undefined_name",
            FIXTURE_CHECK,
            "fixtures/sx/check/undefined_name.sx",
            "fixtures/sx/check/undefined_name.diag",
        },
        {
            "import_cycle",
            FIXTURE_CHECK,
            "fixtures/sx/check/import_cycle/a.sx",
            "fixtures/sx/check/import_cycle/a.diag",
        },
    };
    size_t i;

    for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
        printf("    fixture: %s\n", fixtures[i].name);
        assert_fixture_case(&fixtures[i]);
    }
}

int main(void)
{
    printf("=== sx fixture tests ===\n");

    RUN_TEST(file_fixture_corpus_matches_expectations);

    TEST_REPORT();
}
