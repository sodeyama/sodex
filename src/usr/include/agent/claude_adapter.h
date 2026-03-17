/*
 * claude_adapter.h - Claude Messages API adapter
 *
 * Converts between internal representation and Claude API JSON format.
 * Handles both streaming (SSE) and non-streaming responses.
 */

#ifndef _AGENT_CLAUDE_ADAPTER_H
#define _AGENT_CLAUDE_ADAPTER_H

#include <json.h>
#include <sse_parser.h>

/* ---- Limits ---- */
#define CLAUDE_MAX_TOOL_NAME   64
#define CLAUDE_MAX_TOOL_ID     64
#define CLAUDE_MAX_TOOL_INPUT 4096
#define CLAUDE_MAX_TEXT       8192
#define CLAUDE_MAX_TOOLS        8
#define CLAUDE_MAX_BLOCKS       8

/* ---- Content types ---- */
enum claude_content_type {
    CLAUDE_CONTENT_TEXT,
    CLAUDE_CONTENT_TOOL_USE,
};

/* ---- Tool use block ---- */
struct claude_tool_use {
    char id[CLAUDE_MAX_TOOL_ID];
    char name[CLAUDE_MAX_TOOL_NAME];
    char input_json[CLAUDE_MAX_TOOL_INPUT];
    int  input_json_len;
};

/* ---- Content block ---- */
struct claude_content_block {
    enum claude_content_type type;
    union {
        struct {
            char text[CLAUDE_MAX_TEXT];
            int  text_len;
        } text;
        struct claude_tool_use tool_use;
    };
};

/* ---- Stop reasons ---- */
enum claude_stop_reason {
    CLAUDE_STOP_NONE = 0,
    CLAUDE_STOP_END_TURN,
    CLAUDE_STOP_TOOL_USE,
    CLAUDE_STOP_MAX_TOKENS,
    CLAUDE_STOP_ERROR,
};

/* ---- Response (accumulated from SSE events) ---- */
struct claude_response {
    char id[64];
    char model[64];
    enum claude_stop_reason stop_reason;
    struct claude_content_block blocks[CLAUDE_MAX_BLOCKS];
    int block_count;
    int input_tokens;
    int output_tokens;
    int current_block_index;  /* Tracks which block is being built */
};

/* ---- Message (conversation history) ---- */
struct claude_message {
    const char *role;       /* "user", "assistant" */
    const char *content;    /* Text content (simple version) */
};

/* ---- Error codes ---- */
#define CLAUDE_OK                0
#define CLAUDE_ERR_BUF_OVERFLOW (-1)
#define CLAUDE_ERR_JSON_PARSE   (-2)
#define CLAUDE_ERR_MISSING_FIELD (-3)
#define CLAUDE_ERR_UNKNOWN_EVENT (-4)
#define CLAUDE_ERR_HTTP         (-5)
#define CLAUDE_ERR_CONNECT      (-6)
#define CLAUDE_ERR_TIMEOUT      (-7)
#define CLAUDE_ERR_API          (-8)

/* ---- Adapter API ---- */

/* Build Claude Messages API request JSON.
 * jw: json writer (caller provides buffer).
 * Returns 0 on success, negative on error. */
int claude_build_request(
    struct json_writer *jw,
    const char *model,
    const struct claude_message *msgs, int msg_count,
    const char *system_prompt,
    int max_tokens,
    int stream
);

/* Parse non-streaming response JSON into claude_response.
 * Returns 0 on success, negative on error. */
int claude_parse_response(
    const char *json_str, int json_len,
    struct claude_response *out
);

/* Parse one SSE event and accumulate into response state.
 * Returns 0 on success, 1 when message_stop received, negative on error. */
int claude_parse_sse_event(
    const struct sse_event *event,
    struct claude_response *state
);

/* Check if response requires a tool call. */
int claude_needs_tool_call(const struct claude_response *resp);

/* Build tool_result message JSON for returning tool output.
 * Returns 0 on success, negative on error. */
int claude_build_tool_result(
    struct json_writer *jw,
    const char *tool_use_id,
    const char *result_json, int result_json_len,
    int is_error
);

/* Initialize response structure. */
void claude_response_init(struct claude_response *resp);

#endif /* _AGENT_CLAUDE_ADAPTER_H */
