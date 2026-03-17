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

/* Mock provider pointing at local test server (10.0.2.2:4443) */
static struct api_endpoint mock_ep;
static struct api_header mock_hdrs[4];
static struct llm_provider mock_prov;

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

/* ---- Main ---- */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

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

    /* Run scenarios */
    test_scenario_immediate();
    test_scenario_one_tool();
    test_scenario_two_tools();
    test_scenario_max_steps();

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
