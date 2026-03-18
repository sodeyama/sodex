/*
 * test_compaction.c - Conversation compaction tests
 */

#include <stdio.h>
#include <string.h>
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

static void test_compact_keeps_recent_turns(void)
{
    struct conversation conv;
    char summary[1024];
    int removed;

    TEST_START("compact_keeps_recent_turns");
    conv_init(&conv, "system");
    conv_add_user_text(&conv, "turn1 old");
    conv_add_user_text(&conv, "turn2 old");
    conv_add_user_text(&conv, "turn3 old");
    conv_add_user_text(&conv, "turn4 keep");
    conv_add_user_text(&conv, "turn5 keep");

    removed = conv_compact(&conv, 2, "unfinished focus", summary, sizeof(summary));
    ASSERT(removed == 3, "removed count");
    ASSERT(conv.turn_count == 2, "turn count");
    ASSERT(strstr(summary, "turn1 old") != NULL, "summary has old turn");
    ASSERT(strstr(summary, "unfinished focus") != NULL, "summary has focus");
    ASSERT(strstr(conv.system_prompt, "Compact Summary") != NULL,
           "system prompt has compact");
    ASSERT(strcmp(conv.turns[0].blocks[0].text.text, "turn4 keep") == 0,
           "kept turn4");
    ASSERT(strcmp(conv.turns[1].blocks[0].text.text, "turn5 keep") == 0,
           "kept turn5");
    TEST_PASS("compact_keeps_recent_turns");
}

int main(void)
{
    printf("=== compaction tests ===\n\n");
    test_compact_keeps_recent_turns();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
