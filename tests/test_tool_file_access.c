/*
 * test_tool_file_access.c - file tool の host 単体テスト
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "agent/tool_handlers.h"

static int passed = 0;
static int failed = 0;

#define TEST_ROOT "/tmp/agent_file_tool_tests"

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        failed++; \
        return; \
    } \
} while (0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while (0)

static void reset_test_root(void)
{
    system("rm -rf " TEST_ROOT);
    system("mkdir -p " TEST_ROOT "/nested");
}

static void write_host_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");

    if (!fp)
        return;
    fputs(text, fp);
    fclose(fp);
}

static void test_write_overwrite_and_readback(void)
{
    const char *write_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/out.txt\","
        "\"content\":\"hello\",\"mode\":\"overwrite\"}";
    const char *read_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/out.txt\"}";
    char result[4096];
    int len;

    TEST_START("write_overwrite_and_readback");
    reset_test_root();

    len = tool_write_file(write_input, (int)strlen(write_input),
                          result, sizeof(result));
    ASSERT(len > 0, "write should succeed");
    ASSERT(strstr(result, "\"status\":\"ok\"") != NULL, "write status ok");
    ASSERT(strstr(result, "\"mode\":\"overwrite\"") != NULL, "mode overwrite");

    len = tool_read_file(read_input, (int)strlen(read_input),
                         result, sizeof(result));
    ASSERT(len > 0, "read should succeed");
    ASSERT(strstr(result, "\"content\":\"hello\"") != NULL, "readback content");
    ASSERT(strstr(result, "\"bytes_read\":5") != NULL, "bytes_read 5");
    TEST_PASS("write_overwrite_and_readback");
}

static void test_write_append_mode(void)
{
    const char *write_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/append.txt\","
        "\"content\":\" world\",\"mode\":\"append\"}";
    const char *read_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/append.txt\"}";
    char result[4096];
    int len;

    TEST_START("write_append_mode");
    reset_test_root();

    write_host_file(TEST_ROOT "/append.txt", "hello");
    len = tool_write_file(write_input, (int)strlen(write_input),
                          result, sizeof(result));
    ASSERT(len > 0, "append write should succeed");
    ASSERT(strstr(result, "\"mode\":\"append\"") != NULL, "mode append");

    len = tool_read_file(read_input, (int)strlen(read_input),
                         result, sizeof(result));
    ASSERT(len > 0, "read append result");
    ASSERT(strstr(result, "hello world") != NULL, "append content");
    TEST_PASS("write_append_mode");
}

static void test_write_create_rejects_existing_file(void)
{
    const char *write_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/create.txt\","
        "\"content\":\"second\",\"mode\":\"create\"}";
    char result[4096];
    int len;

    TEST_START("write_create_rejects_existing_file");
    reset_test_root();
    write_host_file(TEST_ROOT "/create.txt", "first");

    len = tool_write_file(write_input, (int)strlen(write_input),
                          result, sizeof(result));
    ASSERT(len > 0, "create should return error payload");
    ASSERT(strstr(result, "\"code\":\"create_failed\"") != NULL,
           "create_failed code");
    TEST_PASS("write_create_rejects_existing_file");
}

static void test_rename_moves_file_and_preserves_content(void)
{
    const char *rename_input =
        "{\"from\":\"/tmp/agent_file_tool_tests/from.txt\","
        "\"to\":\"/tmp/agent_file_tool_tests/to.txt\"}";
    const char *read_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/to.txt\"}";
    char result[4096];
    int len;

    TEST_START("rename_moves_file_and_preserves_content");
    reset_test_root();
    write_host_file(TEST_ROOT "/from.txt", "rename-me");

    len = tool_rename_path(rename_input, (int)strlen(rename_input),
                           result, sizeof(result));
    ASSERT(len > 0, "rename should succeed");
    ASSERT(strstr(result, "\"status\":\"ok\"") != NULL, "rename status ok");
    ASSERT(access(TEST_ROOT "/from.txt", F_OK) != 0, "source removed");
    ASSERT(access(TEST_ROOT "/to.txt", F_OK) == 0, "target exists");

    len = tool_read_file(read_input, (int)strlen(read_input),
                         result, sizeof(result));
    ASSERT(len > 0, "read renamed file");
    ASSERT(strstr(result, "\"content\":\"rename-me\"") != NULL,
           "renamed content preserved");
    TEST_PASS("rename_moves_file_and_preserves_content");
}

static void test_read_offset_limit(void)
{
    const char *read_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/slice.txt\","
        "\"offset\":2,\"limit\":3}";
    char result[4096];
    int len;

    TEST_START("read_offset_limit");
    reset_test_root();
    write_host_file(TEST_ROOT "/slice.txt", "abcdefg");

    len = tool_read_file(read_input, (int)strlen(read_input),
                         result, sizeof(result));
    ASSERT(len > 0, "slice read should succeed");
    ASSERT(strstr(result, "\"content\":\"cde\"") != NULL, "slice content");
    ASSERT(strstr(result, "\"bytes_read\":3") != NULL, "slice size");
    ASSERT(strstr(result, "\"limit_reached\":true") != NULL,
           "limit reached");
    TEST_PASS("read_offset_limit");
}

static void test_relative_paths_resolve_from_cwd(void)
{
    const char *write_input =
        "{\"path\":\"nested/relative.txt\","
        "\"content\":\"rel\",\"mode\":\"overwrite\"}";
    const char *read_input = "{\"path\":\"nested/relative.txt\"}";
    const char *list_input = "{\"path\":\"nested\"}";
    char expected_path[4096];
    char result[4096];
    char saved_cwd[4096];
    char test_cwd[4096];
    int len;

    TEST_START("relative_paths_resolve_from_cwd");
    reset_test_root();
    ASSERT(getcwd(saved_cwd, sizeof(saved_cwd)) != NULL, "save cwd");
    ASSERT(chdir(TEST_ROOT) == 0, "chdir TEST_ROOT");
    ASSERT(getcwd(test_cwd, sizeof(test_cwd)) != NULL, "get test cwd");
    snprintf(expected_path, sizeof(expected_path), "%s/nested/relative.txt",
             test_cwd);

    len = tool_write_file(write_input, (int)strlen(write_input),
                          result, sizeof(result));
    ASSERT(len > 0, "relative write should succeed");
    ASSERT(strstr(result, expected_path) != NULL,
           "write path resolved from cwd");

    len = tool_read_file(read_input, (int)strlen(read_input),
                         result, sizeof(result));
    ASSERT(len > 0, "relative read should succeed");
    ASSERT(strstr(result, "\"content\":\"rel\"") != NULL, "relative read content");

    len = tool_list_dir(list_input, (int)strlen(list_input),
                        result, sizeof(result));
    ASSERT(len > 0, "relative list_dir should succeed");
    ASSERT(strstr(result, expected_path) != NULL,
           "list_dir path resolved from cwd");
    ASSERT(chdir(saved_cwd) == 0, "restore cwd");
    TEST_PASS("relative_paths_resolve_from_cwd");
}

static void test_read_large_file_uses_bounded_output(void)
{
    const char *read_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/big.txt\",\"limit\":4096}";
    char big[1601];
    char result[4096];
    int i;
    int len;

    TEST_START("read_large_file_uses_bounded_output");
    reset_test_root();
    for (i = 0; i < 1600; i++)
        big[i] = (char)('a' + (i % 26));
    big[1600] = '\0';
    write_host_file(TEST_ROOT "/big.txt", big);

    len = tool_read_file(read_input, (int)strlen(read_input),
                         result, sizeof(result));
    ASSERT(len > 0, "large read should succeed");
    ASSERT(strstr(result, "\"content_head\"") != NULL, "head present");
    ASSERT(strstr(result, "\"content_tail\"") != NULL, "tail present");
    ASSERT(strstr(result, "\"artifact_path\"") != NULL, "artifact path present");
    ASSERT(strstr(result, "\"truncated\":true") != NULL, "truncated true");
    TEST_PASS("read_large_file_uses_bounded_output");
}

static void test_list_dir_returns_absolute_paths(void)
{
    const char *list_input =
        "{\"path\":\"/tmp/agent_file_tool_tests/nested\"}";
    char result[4096];
    int len;

    TEST_START("list_dir_returns_absolute_paths");
    reset_test_root();
    write_host_file(TEST_ROOT "/nested/file.txt", "abc");
    mkdir(TEST_ROOT "/nested/subdir", 0755);

    len = tool_list_dir(list_input, (int)strlen(list_input),
                        result, sizeof(result));
    ASSERT(len > 0, "list_dir should succeed");
    ASSERT(strstr(result, "\"name\":\"file.txt\"") != NULL, "file entry");
    ASSERT(strstr(result,
                  "\"path\":\"/tmp/agent_file_tool_tests/nested/file.txt\"") != NULL,
           "absolute path entry");
    ASSERT(strstr(result, "\"type\":\"file\"") != NULL, "file type");
    ASSERT(strstr(result, "\"size\":3") != NULL, "file size");
    ASSERT(strstr(result, "\"name\":\"subdir\"") != NULL, "subdir entry");
    ASSERT(strstr(result, "\"type\":\"dir\"") != NULL, "dir type");
    TEST_PASS("list_dir_returns_absolute_paths");
}

int main(void)
{
    printf("=== file tool access tests ===\n\n");

    test_write_overwrite_and_readback();
    test_write_append_mode();
    test_write_create_rejects_existing_file();
    test_rename_moves_file_and_preserves_content();
    test_read_offset_limit();
    test_relative_paths_resolve_from_cwd();
    test_read_large_file_uses_bounded_output();
    test_list_dir_returns_absolute_paths();

    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
