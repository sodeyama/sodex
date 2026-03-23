/*
 * agent_integ.c - Agent integration test command (Plan 18)
 *
 * Runs multi-turn agent loop scenarios against a mock Claude server
 * to verify the full agent pipeline: conversation management, tool
 * dispatch, and stop condition handling.
 *
 * Scenarios:
 *   1. Immediate completion (no tools)
 *   2. One tool use (read_file) then completion
 *   3. Two-tool chain (list_dir -> read_file -> done)
 *   4. Max steps (always returns tool_use -> hits max_steps)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <entropy.h>
#include <agent/agent.h>
#include <agent/tool_handlers.h>
#include <agent/claude_adapter.h>
#include <agent/llm_provider.h>
#include <agent/claude_client.h>
#include <agent/api_config.h>
#include <agent/session.h>
#include <fs.h>

#define TEST_PASS(name) do { \
    debug_printf("[AGENT-INTEG] %s ... PASS\n", name); \
    printf("[AGENT-INTEG] %s ... PASS\n", name); \
    passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    debug_printf("[AGENT-INTEG] %s ... FAIL (%s)\n", name, reason); \
    printf("[AGENT-INTEG] %s ... FAIL (%s)\n", name, reason); \
    failed++; \
} while(0)

static int passed = 0;
static int failed = 0;

static struct agent_state s_turn_state;
static struct agent_state s_resume_state;
static struct agent_state s_continue_state;
static struct session_meta s_session_meta;

/* Mock provider pointing at local test server (10.0.2.2:4443) */
static struct api_endpoint mock_ep;
static struct api_header mock_hdrs[4];
static struct llm_provider mock_prov;

extern int chdir(char *path);

static void restore_default_home(void)
{
    if (chdir("/home/user") == 0)
        return;
    chdir("/");
}

static void init_mock_provider(void)
{
    mock_ep.host = "10.0.2.2";
    mock_ep.path = "/v1/messages";
    mock_ep.port = 4443;

    mock_hdrs[0].name  = "content-type";
    mock_hdrs[0].value = "application/json";
    mock_hdrs[1].name  = "anthropic-version";
    mock_hdrs[1].value = "2023-06-01";
    mock_hdrs[2].name  = "x-api-key";
    mock_hdrs[2].value = "test-key-mock";
    mock_hdrs[3].name  = (const char *)0;
    mock_hdrs[3].value = (const char *)0;

    mock_prov.name          = "mock-claude";
    mock_prov.endpoint      = &mock_ep;
    mock_prov.headers       = mock_hdrs;
    mock_prov.header_count  = 3;
    mock_prov.build_request    = claude_build_request;
    mock_prov.parse_sse_event  = claude_parse_sse_event;
    mock_prov.parse_response   = claude_parse_response;
}

/* Static config/result to avoid stack overflow (agent_config has 4KB prompt) */
static struct agent_config s_config;
static struct agent_result s_result;

static unsigned int test_hash_path(const char *path)
{
    unsigned int hash = 5381U;

    while (path && *path) {
        hash = ((hash << 5) + hash) ^ (unsigned int)(unsigned char)(*path);
        path++;
    }
    return hash;
}

static int write_text_file(const char *path, const char *text)
{
    int fd;
    int len;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    len = strlen(text);
    if (write(fd, text, (size_t)len) != len) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int read_text_file(const char *path, char *buf, int cap)
{
    int fd;
    int nread;

    if (!path || !buf || cap <= 1)
        return -1;
    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    nread = read(fd, buf, (size_t)(cap - 1));
    close(fd);
    if (nread < 0)
        return -1;
    buf[nread] = '\0';
    return nread;
}

static int path_exists(const char *path)
{
    int fd;

    if (!path)
        return 0;
    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static int persist_turns(const char *session_id,
                         const struct agent_state *state,
                         int start_turn)
{
    int i;

    for (i = start_turn; i < state->conv.turn_count; i++) {
        if (session_append_turn(session_id, &state->conv.turns[i], 0, 0) < 0)
            return -1;
    }
    return 0;
}

/* ----- Scenario 1: Immediate completion (no tools) ----- */
static void test_scenario_immediate(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 1: immediate completion\n");
    printf("[AGENT-INTEG] scenario 1: immediate completion\n");

    ret = agent_run(&s_config, "test_immediate", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 1 && s_result.total_tool_calls == 0) {
            TEST_PASS("scenario_1_immediate");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "steps=%d (exp 1), tools=%d (exp 0)",
                    s_result.steps_executed, s_result.total_tool_calls);
            TEST_FAIL("scenario_1_immediate", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_1_immediate", msg);
    }
}

/* ----- Scenario 2: One tool use (read_file) ----- */
static void test_scenario_one_tool(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 2: one tool use\n");
    printf("[AGENT-INTEG] scenario 2: one tool use\n");

    ret = agent_run(&s_config, "test_one_tool", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 2 && s_result.total_tool_calls == 1) {
            TEST_PASS("scenario_2_one_tool");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "steps=%d (exp 2), tools=%d (exp 1)",
                    s_result.steps_executed, s_result.total_tool_calls);
            TEST_FAIL("scenario_2_one_tool", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_2_one_tool", msg);
    }
}

/* ----- Scenario 3: Two-tool chain (list_dir -> read_file -> done) ----- */
static void test_scenario_two_tools(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 10;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 3: two-tool chain\n");
    printf("[AGENT-INTEG] scenario 3: two-tool chain\n");

    ret = agent_run(&s_config, "test_two_tools", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 3 && s_result.total_tool_calls == 2) {
            TEST_PASS("scenario_3_two_tools");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "steps=%d (exp 3), tools=%d (exp 2)",
                    s_result.steps_executed, s_result.total_tool_calls);
            TEST_FAIL("scenario_3_two_tools", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_3_two_tools", msg);
    }
}

/* ----- Scenario 4: Max steps (always returns tool_use) ----- */
static void test_scenario_max_steps(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 3;  /* Low limit to trigger max_steps quickly */
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 4: max steps\n");
    printf("[AGENT-INTEG] scenario 4: max steps\n");

    ret = agent_run(&s_config, "test_max_steps", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_MAX_STEPS) {
        if (s_result.steps_executed == 3 && s_result.total_tool_calls == 3) {
            TEST_PASS("scenario_4_max_steps");
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "steps=%d (exp 3), tools=%d (exp 3)",
                    s_result.steps_executed, s_result.total_tool_calls);
            TEST_FAIL("scenario_4_max_steps", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_4_max_steps", msg);
    }
}

/* ----- Scenario 5: multi-turn memory within one REPL session ----- */
static void test_scenario_repl_memory(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    agent_state_init(&s_turn_state, &s_config);

    ret = agent_run_turn(&s_config, &s_turn_state,
                         "test_repl_turn1", &s_result);
    if (ret != 0 || s_result.stop_reason != AGENT_STOP_END_TURN) {
        TEST_FAIL("scenario_5_repl_memory", "turn1 failed");
        return;
    }

    ret = agent_run_turn(&s_config, &s_turn_state,
                         "test_repl_turn2", &s_result);
    if (ret == 0 &&
        s_result.stop_reason == AGENT_STOP_END_TURN &&
        strstr(s_result.final_text, "remembers turn1") != 0 &&
        s_turn_state.conv.turn_count >= 4) {
        TEST_PASS("scenario_5_repl_memory");
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d stop=%d text=%s",
                 ret, s_result.stop_reason, s_result.final_text);
        TEST_FAIL("scenario_5_repl_memory", msg);
    }
}

/* ----- Scenario 6: session persistence and resume ----- */
static void test_scenario_session_resume(void)
{
    int ret;
    int start_turn;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    agent_state_init(&s_turn_state, &s_config);
    if (session_create(&s_session_meta, s_config.model, "/") < 0) {
        TEST_FAIL("scenario_6_session_resume", "session_create failed");
        return;
    }

    start_turn = s_turn_state.conv.turn_count;
    ret = agent_run_turn(&s_config, &s_turn_state,
                         "test_session_resume_a", &s_result);
    if (ret != 0 || s_result.stop_reason != AGENT_STOP_END_TURN) {
        TEST_FAIL("scenario_6_session_resume", "turnA failed");
        return;
    }
    if (persist_turns(s_session_meta.id, &s_turn_state, start_turn) < 0) {
        TEST_FAIL("scenario_6_session_resume", "persist failed");
        return;
    }

    agent_state_init(&s_resume_state, &s_config);
    if (session_load(s_session_meta.id, &s_resume_state.conv) < 0) {
        TEST_FAIL("scenario_6_session_resume", "session_load failed");
        return;
    }

    ret = agent_run_turn(&s_config, &s_resume_state,
                         "test_session_resume_b", &s_result);
    if (ret == 0 &&
        s_result.stop_reason == AGENT_STOP_END_TURN &&
        strstr(s_result.final_text, "Resumed session remembered.") != 0 &&
        s_resume_state.conv.turn_count >= 4) {
        TEST_PASS("scenario_6_session_resume");
    } else {
        char msg[160];
        snprintf(msg, sizeof(msg), "ret=%d stop=%d text=%s turns=%d",
                 ret, s_result.stop_reason, s_result.final_text,
                 s_resume_state.conv.turn_count);
        TEST_FAIL("scenario_6_session_resume", msg);
    }
}

/* ----- Scenario 7: CLAUDE.md and memory loader ----- */
static void test_scenario_memory_loader(void)
{
    char workspace_path[128];
    unsigned int workspace_hash;

    mkdir("/tmp", 0755);
    mkdir("/tmp/agent_cfg_test", 0755);
    mkdir("/var", 0755);
    mkdir("/var/agent", 0755);
    mkdir("/var/agent/memory", 0755);

    write_text_file("/tmp/AGENTS.md", "parent_agent_marker\n");
    write_text_file("/tmp/agent_cfg_test/CLAUDE.md", "project_scope_marker\n");
    write_text_file("/var/agent/memory/global.md", "global_memory_marker\n");
    workspace_hash = test_hash_path("/tmp/agent_cfg_test");
    snprintf(workspace_path, sizeof(workspace_path),
             "/var/agent/memory/%08x.md", workspace_hash);
    write_text_file(workspace_path, "workspace_memory_marker\n");

    if (chdir("/tmp/agent_cfg_test") < 0) {
        TEST_FAIL("scenario_7_memory_loader", "chdir failed");
        return;
    }

    agent_config_init(&s_config);
    agent_load_config(&s_config);

    if (strstr(s_config.system_prompt, "project_scope_marker") != 0 &&
        strstr(s_config.system_prompt, "workspace_memory_marker") != 0 &&
        strstr(s_config.system_prompt, "global_memory_marker") != 0 &&
        strstr(s_config.system_prompt, "parent_agent_marker") != 0 &&
        strstr(s_config.system_prompt, "eshell") != 0) {
        TEST_PASS("scenario_7_memory_loader");
    } else {
        TEST_FAIL("scenario_7_memory_loader", "markers missing");
    }

    restore_default_home();
}

/* ----- Scenario 8: continue + compact flow ----- */
static void test_scenario_continue_and_compact(void)
{
    char session_id[SESSION_ID_LEN + 1];
    char summary[512];
    int ret;
    int start_turn;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    agent_state_init(&s_turn_state, &s_config);
    if (session_create(&s_session_meta, s_config.model, "/tmp/repl_flow") < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "session_create failed");
        return;
    }

    start_turn = s_turn_state.conv.turn_count;
    ret = agent_run_turn(&s_config, &s_turn_state,
                         "test_repl_turn1", &s_result);
    if (ret != 0 || s_result.stop_reason != AGENT_STOP_END_TURN) {
        TEST_FAIL("scenario_8_continue_and_compact", "turn1 failed");
        return;
    }
    if (persist_turns(s_session_meta.id, &s_turn_state, start_turn) < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "persist turn1 failed");
        return;
    }

    start_turn = s_turn_state.conv.turn_count;
    ret = agent_run_turn(&s_config, &s_turn_state,
                         "test_repl_turn2", &s_result);
    if (ret != 0 || s_result.stop_reason != AGENT_STOP_END_TURN) {
        TEST_FAIL("scenario_8_continue_and_compact", "turn2 failed");
        return;
    }
    if (persist_turns(s_session_meta.id, &s_turn_state, start_turn) < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "persist turn2 failed");
        return;
    }

    memset(session_id, 0, sizeof(session_id));
    if (agent_resume_latest_for_cwd("/tmp/repl_flow",
                                    session_id, sizeof(session_id)) < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "continue lookup failed");
        return;
    }
    if (strcmp(session_id, s_session_meta.id) != 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "continue picked wrong id");
        return;
    }

    agent_state_init(&s_continue_state, &s_config);
    if (session_load(session_id, &s_continue_state.conv) < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "session_load failed");
        return;
    }

    ret = agent_run_turn(&s_config, &s_continue_state,
                         "test_repl_turn2", &s_result);
    if (ret != 0 ||
        s_result.stop_reason != AGENT_STOP_END_TURN ||
        strstr(s_result.final_text, "remembers turn1") == 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "continue turn failed");
        return;
    }

    if (conv_compact(&s_continue_state.conv, 4, "continue flow",
                     summary, sizeof(summary)) < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "compact failed");
        return;
    }
    if (session_append_compact(session_id, summary, 0, 1) < 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "append compact failed");
        return;
    }
    if (s_continue_state.conv.turn_count != 4 ||
        strstr(summary, "test_repl_turn1") == 0 ||
        strstr(s_continue_state.conv.system_prompt, "Compact Summary") == 0) {
        TEST_FAIL("scenario_8_continue_and_compact", "compact state mismatch");
        return;
    }

    TEST_PASS("scenario_8_continue_and_compact");
}

/* ----- Scenario 9: permission block and recovery ----- */
static void test_scenario_perm_blocked(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 9: permission block and recovery\n");
    printf("[AGENT-INTEG] scenario 9: permission block and recovery\n");

    ret = agent_run(&s_config, "test_perm_blocked", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        /* Should have: 1 blocked write + 1 allowed write + final text = 3 steps,
         * but the blocked tool also counts as a tool_call + the retry.
         * total_errors >= 1 means at least one tool was blocked */
        if (s_result.final_text_len > 0 &&
            strstr(s_result.final_text, "recovery succeeded") != 0) {
            TEST_PASS("scenario_9_perm_blocked");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "steps=%d tools=%d errors=%d text=%.80s",
                     s_result.steps_executed,
                     s_result.total_tool_calls,
                     0, /* errors not directly in result, just check final text */
                     s_result.final_text);
            TEST_FAIL("scenario_9_perm_blocked", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_9_perm_blocked", msg);
    }
}

/* ----- Scenario 10: fetch_url tool for weather source ----- */
static void test_scenario_write_readback(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 10: write/readback\n");
    printf("[AGENT-INTEG] scenario 10: write/readback\n");

    ret = agent_run(&s_config, "test_write_readback", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 3 &&
            s_result.total_tool_calls == 2 &&
            strstr(s_result.final_text, "Write/readback succeeded.") != 0) {
            TEST_PASS("scenario_10_write_readback");
        } else {
            char msg[192];
            snprintf(msg, sizeof(msg), "steps=%d tools=%d text=%s",
                     s_result.steps_executed, s_result.total_tool_calls,
                     s_result.final_text);
            TEST_FAIL("scenario_10_write_readback", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_10_write_readback", msg);
    }
}

/* ----- Scenario 11: fetch_url tool for weather source ----- */
static void test_scenario_fetch_url_weather(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 11: fetch_url weather\n");
    printf("[AGENT-INTEG] scenario 11: fetch_url weather\n");

    ret = agent_run(&s_config, "test_fetch_url_weather", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 2 &&
            s_result.total_tool_calls == 1 &&
            strstr(s_result.final_text, "http://127.0.0.1:18081/weather/tokyo") != 0) {
            TEST_PASS("scenario_11_fetch_url_weather");
        } else {
            char msg[192];
            snprintf(msg, sizeof(msg), "steps=%d tools=%d text=%s",
                     s_result.steps_executed, s_result.total_tool_calls,
                     s_result.final_text);
            TEST_FAIL("scenario_11_fetch_url_weather", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_11_fetch_url_weather", msg);
    }
}

static void test_scenario_sxi_workflow(void)
{
    static char buf[256];
    int ret;

    mkdir("/tmp", 0755);
    mkdir("/tmp/agent_sxi_case", 0755);
    if (chdir("/tmp/agent_sxi_case") < 0) {
        TEST_FAIL("scenario_12_sxi_workflow", "chdir failed");
        return;
    }

    agent_config_init(&s_config);
    s_config.max_steps = 8;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 12: sxi workflow\n");
    printf("[AGENT-INTEG] scenario 12: sxi workflow\n");

    ret = agent_run(&s_config, "test_sxi_workflow", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 5 &&
            s_result.total_tool_calls == 4 &&
            strstr(s_result.final_text, "sxi workflow succeeded.") != 0 &&
            path_exists("agent_sxi_workflow.sx") &&
            read_text_file("agent_sxi_workflow.sx", buf, sizeof(buf)) > 0 &&
            strstr(buf, "SXI_AGENT_OK") != 0) {
            TEST_PASS("scenario_12_sxi_workflow");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "steps=%d tools=%d text=%s",
                     s_result.steps_executed, s_result.total_tool_calls,
                     s_result.final_text);
            TEST_FAIL("scenario_12_sxi_workflow", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_12_sxi_workflow", msg);
    }

    restore_default_home();
}

/* ----- Scenario 13: sorted rename completes with narrow tools ----- */
static void test_scenario_sorted_prefix_rename(void)
{
    static char buf[64];
    int ret;

    mkdir("/tmp", 0755);
    mkdir("/tmp/agent_rename_case", 0755);
    if (chdir("/tmp/agent_rename_case") < 0) {
        TEST_FAIL("scenario_12_sorted_prefix_rename", "chdir failed");
        return;
    }
    if (write_text_file("file_c.txt", "ccc\n") < 0 ||
        write_text_file("file_a.txt", "aaa\n") < 0 ||
        write_text_file("file_b.txt", "bbb\n") < 0 ||
        write_text_file("document.md", "doc\n") < 0) {
        TEST_FAIL("scenario_12_sorted_prefix_rename", "fixture write failed");
        restore_default_home();
        return;
    }

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 13: sorted prefix rename\n");
    printf("[AGENT-INTEG] scenario 13: sorted prefix rename\n");

    ret = agent_run(&s_config, "test_sorted_prefix_rename", &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 4 &&
            s_result.total_tool_calls == 6 &&
            strstr(s_result.final_text, "Sorted prefix rename succeeded.") != 0 &&
            path_exists("01_document.md") &&
            path_exists("02_file_a.txt") &&
            path_exists("03_file_b.txt") &&
            path_exists("04_file_c.txt") &&
            !path_exists("document.md") &&
            !path_exists("file_a.txt") &&
            !path_exists("file_b.txt") &&
            !path_exists("file_c.txt") &&
            read_text_file("02_file_a.txt", buf, sizeof(buf)) > 0 &&
            strcmp(buf, "aaa\n") == 0) {
            TEST_PASS("scenario_13_sorted_prefix_rename");
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "steps=%d tools=%d text=%s",
                     s_result.steps_executed, s_result.total_tool_calls,
                     s_result.final_text);
            TEST_FAIL("scenario_13_sorted_prefix_rename", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_13_sorted_prefix_rename", msg);
    }

    restore_default_home();
}

/* ----- Scenario 14: current-info prompt is forced through tools ----- */
static void test_scenario_current_weather_requires_tool(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 14: current weather requires tool\n");
    printf("[AGENT-INTEG] scenario 14: current weather requires tool\n");

    ret = agent_run(&s_config,
                    "今日の天気を教えて test_current_weather_requires_tool",
                    &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 2 &&
            s_result.total_tool_calls == 1 &&
            strstr(s_result.final_text, "http://127.0.0.1:18081/weather/tokyo") != 0) {
            TEST_PASS("scenario_14_current_weather_requires_tool");
        } else {
            char msg[192];
            snprintf(msg, sizeof(msg), "steps=%d tools=%d text=%s",
                     s_result.steps_executed, s_result.total_tool_calls,
                     s_result.final_text);
            TEST_FAIL("scenario_14_current_weather_requires_tool", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_14_current_weather_requires_tool", msg);
    }
}

/* ----- Scenario 15: text-only planning must retry through tools ----- */
static void test_scenario_current_weather_retry_after_text_only(void)
{
    int ret;

    agent_config_init(&s_config);
    s_config.max_steps = 5;
    s_config.api_key = "test-key-mock";
    s_config.provider = &mock_prov;

    debug_printf("[AGENT-INTEG] scenario 15: retry after text-only plan\n");
    printf("[AGENT-INTEG] scenario 15: retry after text-only plan\n");

    ret = agent_run(&s_config,
                    "東京の天気しらべて test_current_weather_retry_after_text_only",
                    &s_result);

    if (ret == 0 && s_result.stop_reason == AGENT_STOP_END_TURN) {
        if (s_result.steps_executed == 3 &&
            s_result.total_tool_calls == 1 &&
            strstr(s_result.final_text, "http://127.0.0.1:18081/weather/tokyo") != 0) {
            TEST_PASS("scenario_15_current_weather_retry_after_text_only");
        } else {
            char msg[192];
            snprintf(msg, sizeof(msg), "steps=%d tools=%d text=%s",
                     s_result.steps_executed, s_result.total_tool_calls,
                     s_result.final_text);
            TEST_FAIL("scenario_15_current_weather_retry_after_text_only", msg);
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ret=%d, stop=%d", ret, s_result.stop_reason);
        TEST_FAIL("scenario_15_current_weather_retry_after_text_only", msg);
    }
}

/* ---- Main ---- */
int main(int argc, char *argv[])
{
    debug_printf("[AGENT-INTEG] === Agent Integration Test Start ===\n");
    printf("[AGENT-INTEG] === Agent Integration Test Start ===\n");

    /* Initialize entropy and PRNG (needed for TLS) */
    entropy_init();
    entropy_collect_jitter(512);
    if (prng_init() < 0) {
        debug_printf("[AGENT-INTEG] PRNG init failed\n");
        printf("[AGENT-INTEG] PRNG init failed\n");
    }

    /* Initialize tools (registers built-in handlers) */
    tool_init();

    /* Set up mock provider */
    init_mock_provider();
    restore_default_home();

    if (argc >= 2 && strcmp(argv[1], "sxi-workflow") == 0) {
        test_scenario_sxi_workflow();
        goto summary;
    }

    /* Run scenarios */
    test_scenario_immediate();
    test_scenario_one_tool();
    test_scenario_two_tools();
    test_scenario_max_steps();
    test_scenario_repl_memory();
    test_scenario_session_resume();
    test_scenario_memory_loader();
    test_scenario_continue_and_compact();
    test_scenario_perm_blocked();
    test_scenario_write_readback();
    test_scenario_fetch_url_weather();
    test_scenario_sxi_workflow();
    test_scenario_sorted_prefix_rename();
    test_scenario_current_weather_requires_tool();
    test_scenario_current_weather_retry_after_text_only();

summary:
    /* Summary */
    debug_printf("[AGENT-INTEG] === RESULT: %d/%d passed ===\n",
                passed, passed + failed);
    printf("[AGENT-INTEG] === RESULT: %d/%d passed ===\n",
          passed, passed + failed);

    if (failed == 0) {
        debug_printf("[AGENT-INTEG] ALL TESTS PASSED\n");
        printf("[AGENT-INTEG] ALL TESTS PASSED\n");
    }

    exit(failed == 0 ? 0 : 1);
    return 0;
}
