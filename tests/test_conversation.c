/*
 * test_conversation.c - Multi-turn conversation management tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent/conversation.h"
#include "agent/claude_adapter.h"
#include "agent/tool_dispatch.h"
#include <json.h>

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

/* ---- test_init ---- */
static void test_init(void)
{
    struct conversation conv;

    TEST_START("init");
    conv_init(&conv, "You are a helpful assistant.");
    ASSERT(strcmp(conv.system_prompt, "You are a helpful assistant.") == 0,
           "system_prompt set");
    ASSERT(conv.turn_count == 0, "turn_count == 0");
    ASSERT(conv.total_input_tokens == 0, "input_tokens == 0");
    ASSERT(conv.total_output_tokens == 0, "output_tokens == 0");
    TEST_PASS("init");
}

/* ---- test_append_system_text ---- */
static void test_append_system_text(void)
{
    struct conversation conv;

    TEST_START("append_system_text");
    conv_init(&conv, "Base prompt");
    conv_append_system_text(&conv, "CLAUDE.md", "Project instructions here.");
    ASSERT(strstr(conv.system_prompt, "Base prompt") != NULL,
           "original prompt preserved");
    ASSERT(strstr(conv.system_prompt, "CLAUDE.md") != NULL,
           "header present");
    ASSERT(strstr(conv.system_prompt, "Project instructions here.") != NULL,
           "appended text present");
    TEST_PASS("append_system_text");
}

/* ---- test_add_user_text ---- */
static void test_add_user_text(void)
{
    struct conversation conv;

    TEST_START("add_user_text");
    conv_init(&conv, "sys");
    conv_add_user_text(&conv, "Hello, world!");
    ASSERT(conv.turn_count == 1, "turn_count == 1");
    ASSERT(strcmp(conv.turns[0].role, "user") == 0, "role == user");
    ASSERT(conv.turns[0].block_count == 1, "block_count == 1");
    ASSERT(conv.turns[0].blocks[0].type == CONV_BLOCK_TEXT, "block type text");
    ASSERT(strcmp(conv.turns[0].blocks[0].text.text, "Hello, world!") == 0,
           "text content matches");
    TEST_PASS("add_user_text");
}

/* ---- test_add_multiple_turns ---- */
static void test_add_multiple_turns(void)
{
    struct conversation conv;
    struct claude_response resp;

    TEST_START("add_multiple_turns");
    conv_init(&conv, "sys");

    /* Turn 1: user */
    conv_add_user_text(&conv, "first user message");
    ASSERT(conv.turn_count == 1, "turn_count == 1 after user");

    /* Turn 2: assistant */
    memset(&resp, 0, sizeof(resp));
    resp.block_count = 1;
    resp.blocks[0].type = CLAUDE_CONTENT_TEXT;
    strcpy(resp.blocks[0].text.text, "assistant reply");
    resp.blocks[0].text.text_len = (int)strlen("assistant reply");
    resp.stop_reason = CLAUDE_STOP_END_TURN;
    conv_add_assistant_response(&conv, &resp);
    ASSERT(conv.turn_count == 2, "turn_count == 2 after assistant");

    /* Turn 3: user */
    conv_add_user_text(&conv, "second user message");
    ASSERT(conv.turn_count == 3, "turn_count == 3 after second user");

    ASSERT(strcmp(conv.turns[0].role, "user") == 0, "turn 0 role");
    ASSERT(strcmp(conv.turns[1].role, "assistant") == 0, "turn 1 role");
    ASSERT(strcmp(conv.turns[2].role, "user") == 0, "turn 2 role");
    TEST_PASS("add_multiple_turns");
}

/* ---- test_add_assistant_response ---- */
static void test_add_assistant_response(void)
{
    struct conversation conv;
    struct claude_response resp;

    TEST_START("add_assistant_response");
    conv_init(&conv, "sys");
    conv_add_user_text(&conv, "help me");

    memset(&resp, 0, sizeof(resp));
    resp.block_count = 2;
    resp.blocks[0].type = CLAUDE_CONTENT_TEXT;
    strcpy(resp.blocks[0].text.text, "I'll help");
    resp.blocks[0].text.text_len = 9;
    resp.blocks[1].type = CLAUDE_CONTENT_TOOL_USE;
    strcpy(resp.blocks[1].tool_use.id, "toolu_01");
    strcpy(resp.blocks[1].tool_use.name, "read_file");
    strcpy(resp.blocks[1].tool_use.input_json, "{\"path\":\"/tmp/test\"}");
    resp.blocks[1].tool_use.input_json_len = 20;
    resp.stop_reason = CLAUDE_STOP_TOOL_USE;
    resp.input_tokens = 100;
    resp.output_tokens = 50;

    conv_add_assistant_response(&conv, &resp);
    ASSERT(conv.turn_count == 2, "turn_count == 2");
    ASSERT(strcmp(conv.turns[1].role, "assistant") == 0, "role == assistant");
    ASSERT(conv.turns[1].block_count == 2, "block_count == 2");

    /* Text block */
    ASSERT(conv.turns[1].blocks[0].type == CONV_BLOCK_TEXT, "block 0 text");
    ASSERT(strcmp(conv.turns[1].blocks[0].text.text, "I'll help") == 0,
           "text content");

    /* Tool use block */
    ASSERT(conv.turns[1].blocks[1].type == CONV_BLOCK_TOOL_USE,
           "block 1 tool_use");
    ASSERT(strcmp(conv.turns[1].blocks[1].tool_use.id, "toolu_01") == 0,
           "tool_use id");
    ASSERT(strcmp(conv.turns[1].blocks[1].tool_use.name, "read_file") == 0,
           "tool_use name");
    ASSERT(strcmp(conv.turns[1].blocks[1].tool_use.input_json,
                 "{\"path\":\"/tmp/test\"}") == 0,
           "tool_use input_json");
    TEST_PASS("add_assistant_response");
}

/* ---- test_add_tool_results ---- */
static void test_add_tool_results(void)
{
    struct conversation conv;
    struct tool_dispatch_result res;

    TEST_START("add_tool_results");
    conv_init(&conv, "sys");
    conv_add_user_text(&conv, "do something");

    memset(&res, 0, sizeof(res));
    strcpy(res.tool_use_id, "toolu_01");
    strcpy(res.result_json, "{\"content\":\"file data\"}");
    res.result_len = (int)strlen(res.result_json);
    res.is_error = 0;

    conv_add_tool_results(&conv, &res, 1);
    ASSERT(conv.turn_count == 2, "turn_count == 2");
    ASSERT(strcmp(conv.turns[1].role, "user") == 0, "role == user");
    ASSERT(conv.turns[1].blocks[0].type == CONV_BLOCK_TOOL_RESULT,
           "block type tool_result");
    ASSERT(strcmp(conv.turns[1].blocks[0].tool_result.tool_use_id, "toolu_01") == 0,
           "tool_use_id matches");
    ASSERT(strstr(conv.turns[1].blocks[0].tool_result.content, "file data") != NULL,
           "result content");
    ASSERT(conv.turns[1].blocks[0].tool_result.is_error == 0,
           "is_error == 0");
    TEST_PASS("add_tool_results");
}

/* ---- test_build_messages_json ---- */
static void test_build_messages_json(void)
{
    struct conversation conv;
    struct claude_response resp;
    char buf[8192];
    struct json_writer jw;

    TEST_START("build_messages_json");
    conv_init(&conv, "sys");

    /* Turn 1: user */
    conv_add_user_text(&conv, "hello");

    /* Turn 2: assistant */
    memset(&resp, 0, sizeof(resp));
    resp.block_count = 1;
    resp.blocks[0].type = CLAUDE_CONTENT_TEXT;
    strcpy(resp.blocks[0].text.text, "hi there");
    resp.blocks[0].text.text_len = 8;
    resp.stop_reason = CLAUDE_STOP_END_TURN;
    conv_add_assistant_response(&conv, &resp);

    /* Turn 3: user */
    conv_add_user_text(&conv, "thanks");

    ASSERT(conv.turn_count == 3, "3 turns before build");

    jw_init(&jw, buf, sizeof(buf));
    int rc = conv_build_messages_json(&conv, &jw);
    ASSERT(rc == 0, "build returned 0");
    jw_finish(&jw);

    ASSERT(strstr(buf, "\"role\"") != NULL, "JSON has role");
    ASSERT(strstr(buf, "\"user\"") != NULL, "JSON has user role");
    ASSERT(strstr(buf, "\"assistant\"") != NULL, "JSON has assistant role");
    ASSERT(strstr(buf, "hello") != NULL, "JSON has hello");
    ASSERT(strstr(buf, "hi there") != NULL, "JSON has hi there");
    ASSERT(strstr(buf, "thanks") != NULL, "JSON has thanks");
    TEST_PASS("build_messages_json");
}

/* ---- test_token_tracking ---- */
static void test_token_tracking(void)
{
    struct conversation conv;
    struct claude_response resp;

    TEST_START("token_tracking");
    conv_init(&conv, "sys");

    memset(&resp, 0, sizeof(resp));
    resp.input_tokens = 500;
    resp.output_tokens = 200;
    conv_update_tokens(&conv, &resp);

    ASSERT(conv.total_input_tokens == 500, "input_tokens == 500");
    ASSERT(conv.total_output_tokens == 200, "output_tokens == 200");
    ASSERT(conv_total_tokens(&conv) == 700, "total == 700");
    ASSERT(conv_check_tokens(&conv) == 0, "check == 0 (ok)");

    /* Push to warning threshold */
    memset(&resp, 0, sizeof(resp));
    resp.input_tokens = 100000;
    resp.output_tokens = 50000;
    conv_update_tokens(&conv, &resp);

    ASSERT(conv_total_tokens(&conv) == 150700, "total == 150700");
    ASSERT(conv_check_tokens(&conv) == 1, "check == 1 (warning)");
    TEST_PASS("token_tracking");
}

/* ---- test_token_limit ---- */
static void test_token_limit(void)
{
    struct conversation conv;
    struct claude_response resp;

    TEST_START("token_limit");
    conv_init(&conv, "sys");

    memset(&resp, 0, sizeof(resp));
    resp.input_tokens = 150000;
    resp.output_tokens = 40000;
    conv_update_tokens(&conv, &resp);

    ASSERT(conv_total_tokens(&conv) == 190000, "total == 190000");
    ASSERT(conv_check_tokens(&conv) == 2, "check == 2 (limit reached)");
    TEST_PASS("token_limit");
}

/* ---- test_truncate_oldest ---- */
static void test_truncate_oldest(void)
{
    struct conversation conv;

    TEST_START("truncate_oldest");
    conv_init(&conv, "sys");
    conv_add_user_text(&conv, "turn0");
    conv_add_user_text(&conv, "turn1");
    conv_add_user_text(&conv, "turn2");
    conv_add_user_text(&conv, "turn3");
    conv_add_user_text(&conv, "turn4");
    ASSERT(conv.turn_count == 5, "5 turns before truncate");

    int removed = conv_truncate_oldest(&conv, 2);
    ASSERT(removed == 3, "removed == 3");
    ASSERT(conv.turn_count == 2, "turn_count == 2 after truncate");
    ASSERT(strcmp(conv.turns[0].blocks[0].text.text, "turn3") == 0,
           "first remaining is turn3");
    ASSERT(strcmp(conv.turns[1].blocks[0].text.text, "turn4") == 0,
           "second remaining is turn4");
    TEST_PASS("truncate_oldest");
}

/* ---- test_compact ---- */
static void test_compact(void)
{
    struct conversation conv;
    char summary[2048];
    int removed;
    int i;

    TEST_START("compact");
    conv_init(&conv, "system prompt");

    for (i = 0; i < 10; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "message_%d", i);
        conv_add_user_text(&conv, msg);
    }
    ASSERT(conv.turn_count == 10, "10 turns before compact");

    removed = conv_compact(&conv, 4, "current focus task", summary, sizeof(summary));
    ASSERT(removed == 6, "removed == 6");
    ASSERT(conv.turn_count == 4, "turn_count == 4 after compact");

    /* Summary should contain old messages */
    ASSERT(strstr(summary, "message_0") != NULL, "summary has message_0");
    ASSERT(strstr(summary, "message_5") != NULL, "summary has message_5");
    ASSERT(strstr(summary, "current focus task") != NULL,
           "summary has focus");

    /* System prompt should have compact header */
    ASSERT(strstr(conv.system_prompt, "Compact Summary") != NULL,
           "system prompt has Compact Summary");

    /* Remaining turns should be the last 4 */
    ASSERT(strcmp(conv.turns[0].blocks[0].text.text, "message_6") == 0,
           "first remaining is message_6");
    ASSERT(strcmp(conv.turns[3].blocks[0].text.text, "message_9") == 0,
           "last remaining is message_9");
    TEST_PASS("compact");
}

/* ---- test_turn_limit ---- */
static void test_turn_limit(void)
{
    struct conversation conv;
    int i, rc;

    TEST_START("turn_limit");
    conv_init(&conv, "sys");

    for (i = 0; i < CONV_MAX_TURNS; i++) {
        rc = conv_add_user_text(&conv, "msg");
        ASSERT(rc == 0, "add within limit succeeds");
    }
    ASSERT(conv.turn_count == CONV_MAX_TURNS, "turn_count == MAX");

    rc = conv_add_user_text(&conv, "overflow");
    ASSERT(rc == -1, "add beyond limit returns -1");
    ASSERT(conv.turn_count == CONV_MAX_TURNS,
           "turn_count unchanged after overflow");
    TEST_PASS("turn_limit");
}

int main(void)
{
    printf("=== conversation tests ===\n\n");
    test_init();
    test_append_system_text();
    test_add_user_text();
    test_add_multiple_turns();
    test_add_assistant_response();
    test_add_tool_results();
    test_build_messages_json();
    test_token_tracking();
    test_token_limit();
    test_truncate_oldest();
    test_compact();
    test_turn_limit();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
