/*
 * conversation.h - Multi-turn conversation management
 */
#ifndef _AGENT_CONVERSATION_H
#define _AGENT_CONVERSATION_H

#include <agent/claude_adapter.h>
#include <agent/tool_dispatch.h>
#include <json.h>

/* Limits */
#define CONV_MAX_TURNS       32
#define CONV_MAX_BLOCKS       8  /* Max content blocks per turn */
#define CONV_TEXT_BUF       4096 /* Per-block text buffer for conversation history */

/* Content block types (superset of claude_content_type) */
enum conv_block_type {
    CONV_BLOCK_TEXT,
    CONV_BLOCK_TOOL_USE,
    CONV_BLOCK_TOOL_RESULT,
};

/* A content block within a turn */
struct conv_block {
    enum conv_block_type type;
    union {
        struct {
            char text[CONV_TEXT_BUF];
            int text_len;
        } text;
        struct {
            char id[CLAUDE_MAX_TOOL_ID];
            char name[CLAUDE_MAX_TOOL_NAME];
            char input_json[CLAUDE_MAX_TOOL_INPUT];
            int input_json_len;
        } tool_use;
        struct {
            char tool_use_id[CLAUDE_MAX_TOOL_ID];
            char content[CONV_TEXT_BUF];
            int content_len;
            int is_error;
        } tool_result;
    };
};

/* A single turn in the conversation */
struct conv_turn {
    const char *role;  /* "user" or "assistant" */
    struct conv_block blocks[CONV_MAX_BLOCKS];
    int block_count;
};

/* Conversation state */
struct conversation {
    char system_prompt[CONV_TEXT_BUF];
    int system_prompt_len;
    struct conv_turn turns[CONV_MAX_TURNS];
    int turn_count;
    int total_input_tokens;
    int total_output_tokens;
};

/* Token thresholds */
#define CONV_TOKEN_WARNING  150000
#define CONV_TOKEN_LIMIT    190000

/* Initialize conversation */
void conv_init(struct conversation *conv, const char *system_prompt);

/* system prompt に追加テキストを追記する */
int conv_append_system_text(struct conversation *conv,
                             const char *header,
                             const char *text);

/* Add a user text turn */
int conv_add_user_text(struct conversation *conv, const char *text);

/* Add an assistant response (copies blocks from claude_response) */
int conv_add_assistant_response(struct conversation *conv,
                                 const struct claude_response *resp);

/* Add tool results as a user turn (after tool_use from assistant) */
int conv_add_tool_results(struct conversation *conv,
                           const struct tool_dispatch_result *results,
                           int result_count);

/* Build the full messages JSON array for API request.
 * This serializes ALL turns into the messages array format.
 * Returns 0 on success, negative on error. */
int conv_build_messages_json(const struct conversation *conv,
                              struct json_writer *jw);

/* Update token counts from a response */
void conv_update_tokens(struct conversation *conv,
                         const struct claude_response *resp);

/* Check if token limit is approaching */
int conv_check_tokens(const struct conversation *conv);
/* Returns: 0=ok, 1=warning, 2=limit reached */

/* Truncate oldest turns to reduce token count */
int conv_truncate_oldest(struct conversation *conv, int keep_count);

/* 古いターンを要約して recent turn だけ残す */
int conv_compact(struct conversation *conv,
                 int keep_count,
                 const char *focus,
                 char *summary,
                 int summary_cap);

/* Get total token count */
int conv_total_tokens(const struct conversation *conv);

#endif /* _AGENT_CONVERSATION_H */
