/*
 * test_session_restore_full.c - Full-fidelity session restore tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent/session.h"
#include "agent/conversation.h"

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

static void reset_sessions(void)
{
    system("rm -rf " SESSION_DIR);
}

static void build_assistant_tool_turn(struct conv_turn *turn)
{
    memset(turn, 0, sizeof(*turn));
    turn->role = "assistant";
    turn->block_count = 2;
    turn->blocks[0].type = CONV_BLOCK_TEXT;
    strcpy(turn->blocks[0].text.text, "調査します");
    turn->blocks[0].text.text_len = strlen(turn->blocks[0].text.text);
    turn->blocks[1].type = CONV_BLOCK_TOOL_USE;
    strcpy(turn->blocks[1].tool_use.id, "toolu_01");
    strcpy(turn->blocks[1].tool_use.name, "read_file");
    strcpy(turn->blocks[1].tool_use.input_json, "{\"path\":\"/tmp/demo.txt\"}");
    turn->blocks[1].tool_use.input_json_len =
        strlen(turn->blocks[1].tool_use.input_json);
}

static void build_tool_result_turn(struct conv_turn *turn)
{
    memset(turn, 0, sizeof(*turn));
    turn->role = "user";
    turn->block_count = 1;
    turn->blocks[0].type = CONV_BLOCK_TOOL_RESULT;
    strcpy(turn->blocks[0].tool_result.tool_use_id, "toolu_01");
    strcpy(turn->blocks[0].tool_result.content, "{\"content\":\"ok\"}");
    turn->blocks[0].tool_result.content_len =
        strlen(turn->blocks[0].tool_result.content);
    turn->blocks[0].tool_result.is_error = 0;
}

static void test_full_restore(void)
{
    struct session_meta meta;
    struct session_meta meta_loaded;
    struct conversation conv;
    struct conv_turn turn;

    TEST_START("full_restore");
    reset_sessions();

    ASSERT(session_create(&meta, "mock-model", "/repo") == 0, "create");

    conv_init(&conv, "system");
    ASSERT(conv_add_user_text(&conv, "最初の依頼") == 0, "user turn");
    ASSERT(session_append_turn(meta.id, &conv.turns[0], 10, 0) == 0,
           "append user");

    build_assistant_tool_turn(&turn);
    ASSERT(session_append_turn(meta.id, &turn, 20, 30) == 0,
           "append assistant tool");

    build_tool_result_turn(&turn);
    ASSERT(session_append_turn(meta.id, &turn, 0, 0) == 0,
           "append tool result");

    ASSERT(session_append_compact(meta.id, "未完了: demo.txt の確認", 0, 1) == 0,
           "append compact");
    ASSERT(session_append_rename(meta.id, "restore-test") == 0,
           "append rename");

    conv_init(&conv, "base");
    ASSERT(session_load(meta.id, &conv) == 0, "load");
    ASSERT(conv.turn_count == 3, "turn count");
    ASSERT(conv.turns[1].blocks[1].type == CONV_BLOCK_TOOL_USE,
           "tool_use type");
    ASSERT(strcmp(conv.turns[1].blocks[1].tool_use.name, "read_file") == 0,
           "tool_use name");
    ASSERT(conv.turns[2].blocks[0].type == CONV_BLOCK_TOOL_RESULT,
           "tool_result type");
    ASSERT(strstr(conv.system_prompt, "Compact Summary") != NULL,
           "compact summary injected");

    ASSERT(session_read_meta(meta.id, &meta_loaded) == 0, "read meta");
    ASSERT(strcmp(meta_loaded.name, "restore-test") == 0, "rename restored");
    ASSERT(meta_loaded.compact_count == 1, "compact count");
    TEST_PASS("full_restore");
}

int main(void)
{
    printf("=== session restore tests ===\n\n");
    test_full_restore();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
