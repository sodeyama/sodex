/*
 * test_bounded_output.c - Bounded output summary tests
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "agent/bounded_output.h"
#include "json.h"

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

static void test_small_output_inline(void)
{
    struct bounded_output out;
    struct json_writer jw;
    char json_buf[2048];

    TEST_START("small_output_inline");
    bounded_output_init(&out);
    ASSERT(bounded_output_begin_artifact(&out, "small", ".txt") == 0,
           "begin artifact");
    ASSERT(bounded_output_append(&out, "hello world", 11) == 0,
           "append");
    ASSERT(bounded_output_finish(&out, 0) == 0, "finish");
    ASSERT(out.artifact_path[0] == '\0', "artifact removed");

    jw_init(&jw, json_buf, sizeof(json_buf));
    jw_object_start(&jw);
    bounded_output_write_json(&out, &jw, "output", "head", "tail");
    jw_object_end(&jw);
    jw_finish(&jw);

    ASSERT(strstr(json_buf, "\"output\":\"hello world\"") != NULL,
           "inline output");
    TEST_PASS("small_output_inline");
}

static void test_large_output_artifact(void)
{
    struct bounded_output out;
    struct json_writer jw;
    char json_buf[4096];
    char large[1600];
    int i;

    TEST_START("large_output_artifact");
    for (i = 0; i < (int)sizeof(large); i++)
        large[i] = (char)('A' + (i % 26));

    bounded_output_init(&out);
    ASSERT(bounded_output_begin_artifact(&out, "large", ".txt") == 0,
           "begin artifact");
    ASSERT(bounded_output_append(&out, large, sizeof(large)) == 0,
           "append large");
    ASSERT(bounded_output_finish(&out, 1) == 0, "finish");
    ASSERT(out.total_bytes == (int)sizeof(large), "total bytes");
    ASSERT(out.artifact_path[0] != '\0', "artifact kept");
    ASSERT(access(out.artifact_path, F_OK) == 0, "artifact exists");

    jw_init(&jw, json_buf, sizeof(json_buf));
    jw_object_start(&jw);
    bounded_output_write_json(&out, &jw, "output", "output_head", "output_tail");
    jw_object_end(&jw);
    jw_finish(&jw);

    ASSERT(strstr(json_buf, "\"artifact_path\"") != NULL, "artifact path");
    ASSERT(strstr(json_buf, "\"truncated\":true") != NULL, "truncated flag");
    ASSERT(out.omitted_bytes == (int)sizeof(large) - AGENT_BOUNDED_HEAD - AGENT_BOUNDED_TAIL,
           "omitted bytes");
    unlink(out.artifact_path);
    TEST_PASS("large_output_artifact");
}

int main(void)
{
    printf("=== bounded output tests ===\n\n");
    test_small_output_inline();
    test_large_output_artifact();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
