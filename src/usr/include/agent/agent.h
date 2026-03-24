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
#include <agent/permissions.h>
#include <agent/term_command_block.h>

#define AGENT_DEFAULT_MAX_STEPS     10
#define AGENT_DEFAULT_MAX_TOKENS  4096
/* 共有指示と memory を同時に載せる余白を確保する */
#define AGENT_MAX_SYSTEM_PROMPT   8192
#define AGENT_MAX_RESPONSE        8192

/* Stop conditions */
enum agent_stop_condition {
    AGENT_STOP_NONE = 0,
    AGENT_STOP_END_TURN,          /* Claude finished naturally */
    AGENT_STOP_MAX_STEPS,         /* Max step count reached */
    AGENT_STOP_SPECIFIC_TOOL,     /* Terminal tool called */
    AGENT_STOP_ERROR,             /* Unrecoverable error */
    AGENT_STOP_TOKEN_LIMIT,       /* Token budget exceeded */
    AGENT_STOP_APPROVAL_REQUIRED, /* shell command proposal を承認待ち */
};

enum agent_event_type {
    AGENT_EVENT_STEP_START = 0,
    AGENT_EVENT_TOOL_START,
    AGENT_EVENT_TOOL_FINISH,
};

/* Forward declaration */
struct llm_provider;

struct agent_event {
    enum agent_event_type type;
    int  step;
    const char *tool_name;
    const char *tool_input_json;
    int  tool_input_len;
    const char *tool_result_json;
    int  tool_result_len;
    int  tool_is_error;
};

typedef void (*agent_event_fn)(const struct agent_event *event,
                               void *userdata);

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
    int  current_turn_requires_tool;
    int  current_turn_used_tool;
};

/* Agent execution result */
struct agent_result {
    enum agent_stop_condition stop_reason;
    char final_text[AGENT_MAX_RESPONSE];
    int  final_text_len;
    char proposed_command[TERM_COMMAND_BLOCK_TEXT_MAX];
    enum term_command_class proposed_command_class;
    int  steps_executed;
    int  total_input_tokens;
    int  total_output_tokens;
    int  total_tool_calls;
};

struct agent_text_layout {
    int wrap_cols;
    int current_col;
    int skip_leading_space;
};

/* Initialize agent config with defaults */
void agent_config_init(struct agent_config *config);

/* Load system prompt from /etc/agent/system_prompt.txt (fallback to hardcoded) */
int agent_load_config(struct agent_config *config);

/* 会話状態を初期化する */
void agent_state_init(struct agent_state *state,
                       const struct agent_config *config);

/* 既存会話にユーザー入力を足して 1 ターン分進める */
int agent_run_turn(
    const struct agent_config *config,
    struct agent_state *state,
    const char *user_prompt,
    struct agent_result *result
);

/* Main agent loop: prompt -> API -> tools -> repeat until done */
int agent_run(
    const struct agent_config *config,
    const char *initial_prompt,
    struct agent_result *result
);

/* 指定 cwd の直近セッションを解決する */
int agent_resume_latest_for_cwd(const char *cwd,
                                char *session_id_out,
                                int session_id_cap);

/* 実行中 turn の permission mode を一時 override する */
void agent_set_permission_mode_override(int enabled,
                                        enum permission_mode mode);
int agent_get_permission_mode_override(enum permission_mode *mode_out);
void agent_set_shell_proposal_mode(int enabled);

/* audit に紐づける session id を設定する */
void agent_set_active_session_id(const char *session_id);

/* CLI へ進捗イベントを通知する */
void agent_set_event_callback(agent_event_fn callback, void *userdata);

/* テキストを CLI 向けに折り返して整形する */
void agent_text_layout_init(struct agent_text_layout *layout, int wrap_cols);
int agent_text_layout_format(struct agent_text_layout *layout,
                             const char *text, int text_len,
                             char *out, int out_cap);

/* tool 結果 JSON の簡易参照 */
int agent_tool_result_copy_string_field(const char *result_json, int result_len,
                                        const char *key,
                                        char *out, int out_cap);
int agent_tool_result_get_exit_code(const char *result_json, int result_len,
                                    int *exit_code);
int agent_tool_result_is_failure(const char *result_json, int result_len,
                                 int is_error);
int agent_tool_result_same_failure(const char *tool_name_a,
                                   const char *result_json_a,
                                   int result_len_a,
                                   int is_error_a,
                                   const char *tool_name_b,
                                   const char *result_json_b,
                                   int result_len_b,
                                   int is_error_b);

/* Single step (for testing) */
int agent_step(
    const struct agent_config *config,
    struct agent_state *state,
    struct claude_response *resp
);

/* Print result summary to serial */
void agent_print_summary(const struct agent_result *result);

#endif /* _AGENT_AGENT_H */
