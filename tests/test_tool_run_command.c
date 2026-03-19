/*
 * test_tool_run_command.c - run_command の timeout 回帰テスト
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define TEST_BUILD 1
#include "../src/usr/lib/libagent/tool_run_command.c"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        failed++; \
        return; \
    } \
} while (0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); passed++; } while (0)

void bounded_output_init(struct bounded_output *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->artifact_fd = -1;
}

int bounded_output_begin_artifact(struct bounded_output *out,
                                  const char *prefix,
                                  const char *suffix)
{
    (void)out;
    (void)prefix;
    (void)suffix;
    return 0;
}

int bounded_output_append(struct bounded_output *out,
                          const char *data,
                          int len)
{
    int copy_len;

    if (!out || !data || len <= 0)
        return -1;

    copy_len = len;
    if (copy_len > AGENT_BOUNDED_INLINE - out->inline_len)
        copy_len = AGENT_BOUNDED_INLINE - out->inline_len;
    if (copy_len > 0) {
        memcpy(out->inline_buf + out->inline_len, data, (size_t)copy_len);
        out->inline_len += copy_len;
        out->inline_buf[out->inline_len] = '\0';
    }
    out->total_bytes += len;
    return 0;
}

int bounded_output_finish(struct bounded_output *out, int keep_artifact)
{
    (void)out;
    (void)keep_artifact;
    return 0;
}

int bounded_output_write_json(struct bounded_output *out,
                              struct json_writer *jw,
                              const char *full_key,
                              const char *head_key,
                              const char *tail_key)
{
    (void)out;
    (void)jw;
    (void)full_key;
    (void)head_key;
    (void)tail_key;
    return 0;
}

void json_init(struct json_parser *parser)
{
    (void)parser;
}

int json_parse(struct json_parser *parser,
               const char *js, int len,
               struct json_token *tokens, int num_tokens)
{
    (void)parser;
    (void)js;
    (void)len;
    (void)tokens;
    (void)num_tokens;
    return -1;
}

int json_find_key(const char *js,
                  const struct json_token *tokens, int token_count,
                  int obj_token, const char *key)
{
    (void)js;
    (void)tokens;
    (void)token_count;
    (void)obj_token;
    (void)key;
    return -1;
}

int json_token_str(const char *js, const struct json_token *tok,
                   char *out, int out_cap)
{
    (void)js;
    (void)tok;
    (void)out;
    (void)out_cap;
    return -1;
}

void jw_init(struct json_writer *jw, char *buf, int cap)
{
    (void)jw;
    (void)buf;
    (void)cap;
}

void jw_object_start(struct json_writer *jw)
{
    (void)jw;
}

void jw_object_end(struct json_writer *jw)
{
    (void)jw;
}

void jw_key(struct json_writer *jw, const char *key)
{
    (void)jw;
    (void)key;
}

void jw_string(struct json_writer *jw, const char *value)
{
    (void)jw;
    (void)value;
}

void jw_int(struct json_writer *jw, int value)
{
    (void)jw;
    (void)value;
}

void jw_bool(struct json_writer *jw, int value)
{
    (void)jw;
    (void)value;
}

int jw_finish(struct json_writer *jw)
{
    (void)jw;
    return 0;
}

static void test_drain_child_output_reads_until_exit(void)
{
    int pipefd[2];
    pid_t pid;
    int status = -1;
    int timed_out = 0;
    struct bounded_output bounded;

    TEST_START("drain_child_output_reads_until_exit");
    ASSERT(pipe(pipefd) == 0, "pipe should succeed");

    pid = fork();
    ASSERT(pid >= 0, "fork should succeed");
    if (pid == 0) {
        close(pipefd[0]);
        write(pipefd[1], "hello", 5);
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    bounded_output_init(&bounded);
    ASSERT(drain_child_output(pipefd[0], pid, &bounded, &status, &timed_out) == 0,
           "drain should succeed");
    close(pipefd[0]);

    ASSERT(timed_out == 0, "child should not time out");
    ASSERT(bounded.total_bytes == 5, "should capture full output");
    ASSERT(strcmp(bounded.inline_buf, "hello") == 0, "captured output mismatch");
    ASSERT(WIFEXITED(status), "child should exit normally");
    TEST_PASS("drain_child_output_reads_until_exit");
}

static void test_drain_child_output_kills_on_timeout(void)
{
    int pipefd[2];
    pid_t pid;
    int status = -1;
    int timed_out = 0;
    struct bounded_output bounded;

    TEST_START("drain_child_output_kills_on_timeout");
    ASSERT(pipe(pipefd) == 0, "pipe should succeed");

    pid = fork();
    ASSERT(pid >= 0, "fork should succeed");
    if (pid == 0) {
        close(pipefd[0]);
        write(pipefd[1], "start", 5);
        sleep(2);
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    bounded_output_init(&bounded);
    ASSERT(drain_child_output(pipefd[0], pid, &bounded, &status, &timed_out) == 0,
           "drain should succeed");
    close(pipefd[0]);

    ASSERT(timed_out == 1, "child should time out");
    ASSERT(bounded.total_bytes == 5, "partial output should be preserved");
    ASSERT(strcmp(bounded.inline_buf, "start") == 0, "partial output mismatch");
    ASSERT(WIFSIGNALED(status), "child should be terminated by signal");
    ASSERT(WTERMSIG(status) == SIGKILL, "child should be killed with SIGKILL");
    TEST_PASS("drain_child_output_kills_on_timeout");
}

int main(void)
{
    printf("=== tool_run_command tests ===\n\n");
    test_drain_child_output_reads_until_exit();
    test_drain_child_output_kills_on_timeout();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
