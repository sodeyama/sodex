/*
 * agent.h - Autonomous agent loop
 *
 * Implements the core agent loop: prompt -> API -> tool execution -> repeat
 * until completion or max steps reached.
 */
#ifndef _AGENT_AGENT_H
#define _AGENT_AGENT_H

#include <agent/conversation.h>
#include <agent/claude_adapter.h>

#define AGENT_DEFAULT_MAX_STEPS     10
#define AGENT_DEFAULT_MAX_TOKENS  4096
#define AGENT_MAX_SYSTEM_PROMPT   4096
#define AGENT_MAX_RESPONSE        8192

/* Stop conditions */
enum agent_stop_condition {
    AGENT_STOP_NONE = 0,
    AGENT_STOP_END_TURN,          /* Claude finished naturally */
    AGENT_STOP_MAX_STEPS,         /* Max step count reached */
    AGENT_STOP_SPECIFIC_TOOL,     /* Terminal tool called */
    AGENT_STOP_ERROR,             /* Unrecoverable error */
    AGENT_STOP_TOKEN_LIMIT,       /* Token budget exceeded */
};

/* Forward declaration */
struct llm_provider;

/* Agent configuration */
struct agent_config {
    const char *model;
    char system_prompt[AGENT_MAX_SYSTEM_PROMPT];
    int  system_prompt_len;
    int  max_steps;
    int  max_tokens_per_turn;
    const char *terminal_tool;    /* NULL = disabled */
    const char *api_key;          /* NULL = use default */
    const struct llm_provider *provider; /* NULL = use provider_claude */
};

/* Agent runtime state */
struct agent_state {
    struct conversation conv;
    int  current_step;
    enum agent_stop_condition stop_reason;
    int  total_api_calls;
    int  total_tool_executions;
    int  total_errors;
};

/* Agent execution result */
struct agent_result {
    enum agent_stop_condition stop_reason;
    char final_text[AGENT_MAX_RESPONSE];
    int  final_text_len;
    int  steps_executed;
    int  total_input_tokens;
    int  total_output_tokens;
    int  total_tool_calls;
};

/* Initialize agent config with defaults */
void agent_config_init(struct agent_config *config);

/* Load system prompt from /etc/agent/system_prompt.txt (fallback to hardcoded) */
int agent_load_config(struct agent_config *config);

/* Main agent loop: prompt -> API -> tools -> repeat until done */
int agent_run(
    const struct agent_config *config,
    const char *initial_prompt,
    struct agent_result *result
);

/* Single step (for testing) */
int agent_step(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp
);

/* Print result summary to serial */
void agent_print_summary(const struct agent_result *result);

#endif /* _AGENT_AGENT_H */
