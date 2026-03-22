/*
 * test_agent_autonomy.c - 最新情報系の自律継続テスト
 */

#include <stdio.h>
#include <string.h>
#include "agent/agent.h"
#include "agent/claude_client.h"
#include "agent/llm_provider.h"
#include "agent/tool_dispatch.h"
#include "agent/hooks.h"
#include "agent/permissions.h"
#include "agent/audit.h"

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

enum mock_scenario {
    MOCK_SCENARIO_NONE = 0,
    MOCK_SCENARIO_FRESH_LOOKUP_RETRY,
    MOCK_SCENARIO_PLAIN_END_TURN,
};

static int s_mock_scenario = MOCK_SCENARIO_NONE;
static int s_send_count = 0;
static int s_tool_dispatch_count = 0;

const struct llm_provider provider_claude = {0};

static int conversation_contains_text(const struct conversation *conv,
                                      const char *needle)
{
    int turn_index;

    if (!conv || !needle)
        return 0;
    for (turn_index = 0; turn_index < conv->turn_count; turn_index++) {
        const struct conv_turn *turn = &conv->turns[turn_index];
        int block_index;

        for (block_index = 0; block_index < turn->block_count; block_index++) {
            const struct conv_block *block = &turn->blocks[block_index];

            switch (block->type) {
            case CONV_BLOCK_TEXT:
                if (strstr(block->text.text, needle) != 0)
                    return 1;
                break;
            case CONV_BLOCK_TOOL_RESULT:
                if (strstr(block->tool_result.content, needle) != 0)
                    return 1;
                break;
            default:
                break;
            }
        }
    }
    return 0;
}

void claude_response_init(struct claude_response *resp)
{
    if (!resp)
        return;
    memset(resp, 0, sizeof(*resp));
}

static void set_text_response(struct claude_response *resp, const char *text)
{
    int len;

    claude_response_init(resp);
    resp->stop_reason = CLAUDE_STOP_END_TURN;
    resp->block_count = 1;
    resp->blocks[0].type = CLAUDE_CONTENT_TEXT;
    len = (int)strlen(text);
    if (len >= CLAUDE_MAX_TEXT)
        len = CLAUDE_MAX_TEXT - 1;
    memcpy(resp->blocks[0].text.text, text, (size_t)len);
    resp->blocks[0].text.text[len] = '\0';
    resp->blocks[0].text.text_len = len;
}

static void set_tool_use_response(struct claude_response *resp,
                                  const char *tool_name,
                                  const char *tool_id,
                                  const char *input_json,
                                  const char *text_before)
{
    int text_len;
    int input_len;

    claude_response_init(resp);
    resp->stop_reason = CLAUDE_STOP_TOOL_USE;
    resp->block_count = 2;
    resp->blocks[0].type = CLAUDE_CONTENT_TEXT;
    text_len = (int)strlen(text_before);
    if (text_len >= CLAUDE_MAX_TEXT)
        text_len = CLAUDE_MAX_TEXT - 1;
    memcpy(resp->blocks[0].text.text, text_before, (size_t)text_len);
    resp->blocks[0].text.text[text_len] = '\0';
    resp->blocks[0].text.text_len = text_len;

    resp->blocks[1].type = CLAUDE_CONTENT_TOOL_USE;
    strncpy(resp->blocks[1].tool_use.name, tool_name, CLAUDE_MAX_TOOL_NAME - 1);
    strncpy(resp->blocks[1].tool_use.id, tool_id, CLAUDE_MAX_TOOL_ID - 1);
    input_len = (int)strlen(input_json);
    if (input_len >= CLAUDE_MAX_TOOL_INPUT)
        input_len = CLAUDE_MAX_TOOL_INPUT - 1;
    memcpy(resp->blocks[1].tool_use.input_json, input_json, (size_t)input_len);
    resp->blocks[1].tool_use.input_json[input_len] = '\0';
    resp->blocks[1].tool_use.input_json_len = input_len;
}

int claude_send_conversation_with_key(
    const struct llm_provider *provider,
    const struct conversation *conv,
    int tools_enabled,
    const char *api_key,
    struct claude_response *out)
{
    (void)provider;
    (void)tools_enabled;
    (void)api_key;

    s_send_count++;

    if (s_mock_scenario == MOCK_SCENARIO_FRESH_LOOKUP_RETRY) {
        if (s_send_count == 1) {
            set_text_response(out,
                              "東京の天気を調べます。まずwebsearchで最新の天気情報を検索します。");
            return 0;
        }
        if (s_send_count == 2) {
            if (!conversation_contains_text(conv,
                                            "前の応答は調査方針の説明だけで")) {
                set_text_response(out, "retry prompt missing");
                return 0;
            }
            set_tool_use_response(out,
                                  "fetch_url",
                                  "toolu_retry_1",
                                  "{\"url\":\"http://127.0.0.1:18081/weather/tokyo\"}",
                                  "Confirming current weather.");
            return 0;
        }
        if (s_send_count == 3) {
            if (!conversation_contains_text(conv, "Tokyo Weather 2026-03-19") ||
                !conversation_contains_text(conv, "http://127.0.0.1:18081/weather/tokyo")) {
                set_text_response(out, "tool result missing source");
                return 0;
            }
            set_text_response(out,
                              "Current weather verified with source http://127.0.0.1:18081/weather/tokyo");
            return 0;
        }
    }

    set_text_response(out, "Plain completion.");
    return 0;
}

int claude_send_conversation(
    const struct llm_provider *provider,
    const struct conversation *conv,
    int tools_enabled,
    struct claude_response *out)
{
    return claude_send_conversation_with_key(provider,
                                             conv,
                                             tools_enabled,
                                             (const char *)0,
                                             out);
}

int claude_send_message(
    const struct llm_provider *provider,
    const char *user_message,
    struct claude_response *out)
{
    (void)provider;
    (void)user_message;
    set_text_response(out, "unused");
    return 0;
}

int claude_send_message_with_key(
    const struct llm_provider *provider,
    const char *user_message,
    const char *api_key,
    struct claude_response *out)
{
    (void)api_key;
    return claude_send_message(provider, user_message, out);
}

int claude_send_raw_request(
    const struct llm_provider *provider,
    const char *request_json,
    int request_json_len,
    const char *api_key,
    struct claude_response *out)
{
    (void)provider;
    (void)request_json;
    (void)request_json_len;
    (void)api_key;
    set_text_response(out, "unused");
    return 0;
}

void claude_client_set_text_stream_callback(claude_stream_text_fn callback,
                                            void *userdata)
{
    (void)callback;
    (void)userdata;
}

void tool_init(void)
{
}

int tool_count(void)
{
    return 1;
}

int tool_dispatch(const struct claude_tool_use *tu,
                  struct tool_dispatch_result *out)
{
    const char *json =
        "{\"url\":\"http://127.0.0.1:18081/weather/tokyo\","
        "\"title\":\"Tokyo Weather 2026-03-19\","
        "\"content\":\"Sunny 18C\"}";
    int len;

    if (!tu || !out)
        return -1;
    s_tool_dispatch_count++;
    memset(out, 0, sizeof(*out));
    strncpy(out->tool_use_id, tu->id, sizeof(out->tool_use_id) - 1);
    len = (int)strlen(json);
    if (len >= TOOL_RESULT_BUF)
        len = TOOL_RESULT_BUF - 1;
    memcpy(out->result_json, json, (size_t)len);
    out->result_json[len] = '\0';
    out->result_len = len;
    out->is_error = 0;
    return 0;
}

int tool_dispatch_all(const struct claude_response *resp,
                      struct tool_dispatch_result *results, int max_results)
{
    (void)resp;
    (void)results;
    (void)max_results;
    return 0;
}

int tool_build_definitions(struct json_writer *jw)
{
    (void)jw;
    return 0;
}

void tool_stats_reset(struct tool_stats *stats)
{
    if (stats)
        memset(stats, 0, sizeof(*stats));
}

void tool_stats_record(struct tool_stats *stats,
                       const char *tool_name,
                       int is_error, int elapsed_ticks)
{
    (void)stats;
    (void)tool_name;
    (void)is_error;
    (void)elapsed_ticks;
}

void tool_stats_print(const struct tool_stats *stats)
{
    (void)stats;
}

void hooks_init(void)
{
}

int hooks_register(enum hook_event event, hook_handler_fn handler)
{
    (void)event;
    (void)handler;
    return 0;
}

int hooks_fire(const struct hook_context *ctx, struct hook_response *response)
{
    (void)ctx;
    if (response)
        memset(response, 0, sizeof(*response));
    return 0;
}

void perm_init(struct permission_policy *policy)
{
    if (policy)
        memset(policy, 0, sizeof(*policy));
}

void perm_set_default(struct permission_policy *policy)
{
    perm_init(policy);
}

int perm_check_tool(const struct permission_policy *policy,
                    const char *tool_name, const char *input_json, int input_len)
{
    (void)policy;
    (void)tool_name;
    (void)input_json;
    (void)input_len;
    return 1;
}

int perm_load_policy(struct permission_policy *policy, const char *path)
{
    (void)policy;
    (void)path;
    return 0;
}

int audit_init(void)
{
    return 0;
}

int audit_log(const struct audit_entry *entry)
{
    (void)entry;
    return 0;
}

int audit_read_last(struct audit_entry *entries, int max_entries, int *count)
{
    (void)entries;
    (void)max_entries;
    if (count)
        *count = 0;
    return 0;
}

int audit_rotate(int max_size)
{
    (void)max_size;
    return 0;
}

static void reset_mocks(enum mock_scenario scenario)
{
    s_mock_scenario = scenario;
    s_send_count = 0;
    s_tool_dispatch_count = 0;
}

static void test_fresh_lookup_retries_after_text_only_end_turn(void)
{
    struct agent_config config;
    struct agent_state state;
    struct agent_result result;

    TEST_START("fresh_lookup_retries_after_text_only_end_turn");
    reset_mocks(MOCK_SCENARIO_FRESH_LOOKUP_RETRY);

    agent_config_init(&config);
    config.api_key = "test-key";
    config.provider = &provider_claude;
    config.max_steps = 5;
    agent_state_init(&state, &config);

    ASSERT(agent_run_turn(&config, &state, "東京の天気しらべて", &result) == 0,
           "agent_run_turn should succeed");
    ASSERT(result.stop_reason == AGENT_STOP_END_TURN,
           "stop reason should be end_turn");
    ASSERT(result.steps_executed == 3, "should take three steps");
    ASSERT(result.total_tool_calls == 1, "should execute one tool");
    ASSERT(s_send_count == 3, "provider should be called three times");
    ASSERT(s_tool_dispatch_count == 1, "tool should dispatch once");
    ASSERT(strstr(result.final_text,
                  "http://127.0.0.1:18081/weather/tokyo") != 0,
           "final text should include source");
    ASSERT(conversation_contains_text(&state.conv,
                                      "前の応答は調査方針の説明だけで") == 1,
           "conversation should contain retry prompt");
    TEST_PASS("fresh_lookup_retries_after_text_only_end_turn");
}

static void test_plain_prompt_does_not_force_retry(void)
{
    struct agent_config config;
    struct agent_state state;
    struct agent_result result;

    TEST_START("plain_prompt_does_not_force_retry");
    reset_mocks(MOCK_SCENARIO_PLAIN_END_TURN);

    agent_config_init(&config);
    config.api_key = "test-key";
    config.provider = &provider_claude;
    config.max_steps = 5;
    agent_state_init(&state, &config);

    ASSERT(agent_run_turn(&config, &state, "自己紹介して", &result) == 0,
           "agent_run_turn should succeed");
    ASSERT(result.stop_reason == AGENT_STOP_END_TURN,
           "stop reason should be end_turn");
    ASSERT(result.steps_executed == 1, "plain prompt should finish in one step");
    ASSERT(result.total_tool_calls == 0, "plain prompt should not use tools");
    ASSERT(s_send_count == 1, "plain prompt should call provider once");
    ASSERT(conversation_contains_text(&state.conv,
                                      "前の応答は調査方針の説明だけで") == 0,
           "plain prompt should not add retry prompt");
    TEST_PASS("plain_prompt_does_not_force_retry");
}

static void test_system_prompt_mentions_text_commands(void)
{
    struct agent_config config;

    TEST_START("system_prompt_mentions_text_commands");
    agent_config_init(&config);
    ASSERT(strstr(config.system_prompt, "find") != 0,
           "system prompt should mention find");
    ASSERT(strstr(config.system_prompt, "grep") != 0,
           "system prompt should mention grep");
    ASSERT(strstr(config.system_prompt, "tee") != 0,
           "system prompt should mention tee");
    ASSERT(strstr(config.system_prompt, "rename_path") != 0,
           "system prompt should mention rename_path");
    ASSERT(strstr(config.system_prompt, "list_dir") != 0,
           "system prompt should mention list_dir");
    TEST_PASS("system_prompt_mentions_text_commands");
}

int main(void)
{
    printf("=== agent autonomy tests ===\n\n");
    test_fresh_lookup_retries_after_text_only_end_turn();
    test_plain_prompt_does_not_force_retry();
    test_system_prompt_mentions_text_commands();
    printf("\n=== RESULT: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
