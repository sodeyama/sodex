/*
 * agent_loop.c - Autonomous agent loop implementation
 *
 * Core loop: prompt -> Claude API -> tool execution -> repeat until done.
 * Uses claude_send_conversation() for multi-turn conversations with
 * tool definitions, and tool_dispatch() for executing tool calls.
 */

#include <agent/agent.h>
#include <agent/tool_dispatch.h>
#include <agent/tool_handlers.h>
#include <agent/claude_client.h>
#include <agent/llm_provider.h>
#include <agent/tool_registry.h>
#include <json.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#include <stdlib.h>  /* for open/read/close */
#include <fs.h>      /* for O_RDONLY */
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

/* Default system prompt (hardcoded fallback) */
static const char DEFAULT_SYSTEM_PROMPT[] =
    "You are Sodex OS system agent. Sodex is a custom i486 OS kernel. "
    "You can use tools to read/write files, list directories, get system info, "
    "and run commands. Be concise and helpful.";

void agent_config_init(struct agent_config *config)
{
    memset(config, 0, sizeof(*config));
    config->model = "claude-sonnet-4-20250514";
    config->max_steps = AGENT_DEFAULT_MAX_STEPS;
    config->max_tokens_per_turn = AGENT_DEFAULT_MAX_TOKENS;
    config->terminal_tool = (const char *)0;
    config->api_key = (const char *)0;
    config->provider = (const struct llm_provider *)0;

    /* Copy default system prompt */
    {
        int len = strlen(DEFAULT_SYSTEM_PROMPT);
        if (len >= AGENT_MAX_SYSTEM_PROMPT)
            len = AGENT_MAX_SYSTEM_PROMPT - 1;
        memcpy(config->system_prompt, DEFAULT_SYSTEM_PROMPT, len);
        config->system_prompt[len] = '\0';
        config->system_prompt_len = len;
    }
}

int agent_load_config(struct agent_config *config)
{
#ifndef TEST_BUILD
    /* Try to read /etc/agent/system_prompt.txt */
    int fd = open("/etc/agent/system_prompt.txt", O_RDONLY, 0);
    if (fd >= 0) {
        int n = read(fd, config->system_prompt, AGENT_MAX_SYSTEM_PROMPT - 1);
        close(fd);
        if (n > 0) {
            config->system_prompt[n] = '\0';
            config->system_prompt_len = n;
            debug_printf("[AGENT] loaded system prompt: %d bytes\n", n);
            return 0;
        }
    }
    debug_printf("[AGENT] using default system prompt\n");
#endif
    return 0;
}

/* Extract final text from the last response */
static void extract_final_text(const struct claude_response *resp,
                                struct agent_result *result)
{
    int i;
    result->final_text[0] = '\0';
    result->final_text_len = 0;

    for (i = 0; i < resp->block_count; i++) {
        if (resp->blocks[i].type == CLAUDE_CONTENT_TEXT) {
            int len = resp->blocks[i].text.text_len;
            if (len >= AGENT_MAX_RESPONSE)
                len = AGENT_MAX_RESPONSE - 1;
            memcpy(result->final_text, resp->blocks[i].text.text, len);
            result->final_text[len] = '\0';
            result->final_text_len = len;
            break;  /* Take the first text block */
        }
    }
}

/* Fill common result fields from state */
static void fill_result(struct agent_result *result,
                         const struct agent_state *state,
                         enum agent_stop_condition reason,
                         int steps)
{
    result->stop_reason = reason;
    result->steps_executed = steps;
    result->total_input_tokens = state->conv.total_input_tokens;
    result->total_output_tokens = state->conv.total_output_tokens;
    result->total_tool_calls = state->total_tool_executions;
}

/* Send the current conversation to Claude via the existing API */
static int send_conversation(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp)
{
    int tc = tool_count();
    const struct llm_provider *prov;

    prov = config->provider ? config->provider : &provider_claude;

    claude_response_init(resp);
    return claude_send_conversation(prov, &state->conv,
                                     (tc > 0) ? 1 : 0, resp);
}

int agent_step(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp)
{
    claude_response_init(resp);
    return send_conversation(config, state, resp);
}

int agent_run(
    const struct agent_config *config,
    const char *initial_prompt,
    struct agent_result *result)
{
    /* These structs are too large for the stack (~1MB+).
     * Use static storage. Not reentrant, but fine for this OS. */
    static struct agent_state state;
    static struct claude_response resp;
    int step;
    int ret;

    if (!config || !initial_prompt || !result)
        return -1;

    /* Initialize */
    memset(&state, 0, sizeof(state));
    memset(result, 0, sizeof(*result));
    conv_init(&state.conv, config->system_prompt);
    tool_init();

    /* Add initial user prompt */
    conv_add_user_text(&state.conv, initial_prompt);

    debug_printf("[AGENT] === Agent Run Start ===\n");
    debug_printf("[AGENT] model=%s, max_steps=%d\n",
                config->model, config->max_steps);
    debug_printf("[AGENT] system_prompt=%d bytes, tools=%d registered\n",
                config->system_prompt_len, tool_count());
    debug_printf("[AGENT] prompt: %.80s%s\n", initial_prompt,
                strlen(initial_prompt) > 80 ? "..." : "");

    /* Main loop */
    for (step = 0; step < config->max_steps; step++) {
        state.current_step = step;
        debug_printf("[AGENT] step %d/%d\n", step + 1, config->max_steps);

        /* Brief delay between steps to let TCP/TLS state settle */
        if (step > 0) {
            volatile int delay;
            for (delay = 0; delay < 500000; delay++)
                ;
        }

        /* Send conversation to Claude */
        ret = agent_step(config, &state, &resp);
        if (ret != 0) {
            debug_printf("[AGENT] API error: %d\n", ret);
            fill_result(result, &state, AGENT_STOP_ERROR, step + 1);
            debug_printf("[AGENT] === Agent Run End (error) ===\n");
            return -1;
        }

        state.total_api_calls++;
        conv_update_tokens(&state.conv, &resp);

        /* Add assistant response to conversation */
        conv_add_assistant_response(&state.conv, &resp);

        /* Check stop reason */
        switch (resp.stop_reason) {
        case CLAUDE_STOP_END_TURN:
            /* Natural completion */
            extract_final_text(&resp, result);
            fill_result(result, &state, AGENT_STOP_END_TURN, step + 1);
            debug_printf("[AGENT] completed: %d steps, %d tokens, %d tool calls\n",
                        step + 1,
                        state.conv.total_input_tokens +
                            state.conv.total_output_tokens,
                        state.total_tool_executions);
            agent_print_summary(result);
            debug_printf("[AGENT] === Agent Run End ===\n");
            return 0;

        case CLAUDE_STOP_TOOL_USE: {
            /* Execute all tool_use blocks */
            static struct tool_dispatch_result tool_results[CLAUDE_MAX_BLOCKS];
            int tool_count_exec = 0;
            int i;

            for (i = 0; i < resp.block_count; i++) {
                if (resp.blocks[i].type != CLAUDE_CONTENT_TOOL_USE)
                    continue;

                /* Check terminal tool */
                if (config->terminal_tool &&
                    strcmp(resp.blocks[i].tool_use.name,
                           config->terminal_tool) == 0) {
                    debug_printf("[AGENT] terminal tool '%s' called\n",
                                config->terminal_tool);
                    extract_final_text(&resp, result);
                    fill_result(result, &state,
                                AGENT_STOP_SPECIFIC_TOOL, step + 1);
                    agent_print_summary(result);
                    debug_printf("[AGENT] === Agent Run End ===\n");
                    return 0;
                }

                debug_printf("[AGENT] executing tool: %s\n",
                            resp.blocks[i].tool_use.name);

                tool_dispatch(&resp.blocks[i].tool_use,
                              &tool_results[tool_count_exec]);

                if (tool_results[tool_count_exec].is_error) {
                    state.total_errors++;
                    debug_printf("[AGENT] tool error: %.80s\n",
                                tool_results[tool_count_exec].result_json);
                }

                state.total_tool_executions++;
                tool_count_exec++;

                debug_printf("[AGENT] tool result: %d bytes, is_error=%d\n",
                            tool_results[tool_count_exec - 1].result_len,
                            tool_results[tool_count_exec - 1].is_error);

                if (tool_count_exec >= CLAUDE_MAX_BLOCKS)
                    break;
            }

            /* Add tool results to conversation */
            if (tool_count_exec > 0) {
                conv_add_tool_results(&state.conv,
                                       tool_results, tool_count_exec);
            }

            /* Check token limits */
            if (conv_check_tokens(&state.conv) == 2) {
                debug_printf("[AGENT] token limit reached\n");
                fill_result(result, &state,
                            AGENT_STOP_TOKEN_LIMIT, step + 1);
                agent_print_summary(result);
                debug_printf("[AGENT] === Agent Run End ===\n");
                return -1;
            }

            break;  /* Continue to next step */
        }

        case CLAUDE_STOP_MAX_TOKENS:
            debug_printf("[AGENT] max_tokens in response, continuing...\n");
            /* The response was truncated; loop again to get more */
            break;

        default:
            debug_printf("[AGENT] unexpected stop_reason: %d\n",
                        resp.stop_reason);
            break;
        }
    }

    /* Max steps reached */
    debug_printf("[AGENT] max steps (%d) reached\n", config->max_steps);
    fill_result(result, &state, AGENT_STOP_MAX_STEPS, config->max_steps);

    /* Try to extract any text from the last response */
    extract_final_text(&resp, result);

    agent_print_summary(result);
    debug_printf("[AGENT] === Agent Run End ===\n");
    return 0;
}

void agent_print_summary(const struct agent_result *result)
{
    const char *stop_str;

    if (!result)
        return;

    switch (result->stop_reason) {
    case AGENT_STOP_END_TURN:      stop_str = "end_turn"; break;
    case AGENT_STOP_MAX_STEPS:     stop_str = "max_steps"; break;
    case AGENT_STOP_SPECIFIC_TOOL: stop_str = "terminal_tool"; break;
    case AGENT_STOP_ERROR:         stop_str = "error"; break;
    case AGENT_STOP_TOKEN_LIMIT:   stop_str = "token_limit"; break;
    default:                       stop_str = "unknown"; break;
    }

    debug_printf("[AGENT] Summary: stop=%s, steps=%d, "
                "tokens=%d/%d, tools=%d\n",
                stop_str,
                result->steps_executed,
                result->total_input_tokens,
                result->total_output_tokens,
                result->total_tool_calls);
}
