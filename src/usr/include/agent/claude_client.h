/*
 * claude_client.h - High-level Claude API client
 *
 * Integrates DNS, TLS, HTTP, SSE, and Claude adapter into
 * a single function call for sending messages and receiving responses.
 */

#ifndef _AGENT_CLAUDE_CLIENT_H
#define _AGENT_CLAUDE_CLIENT_H

#include <agent/llm_provider.h>
#include <agent/claude_adapter.h>
#include <agent/conversation.h>

typedef void (*claude_stream_text_fn)(const char *text,
                                      int text_len,
                                      void *userdata);

/* Retry configuration */
#define CLAUDE_MAX_RETRIES      3
#define CLAUDE_INITIAL_WAIT_MS  1000
#define CLAUDE_MAX_WAIT_MS      30000

/* request JSON の固定領域。
 * 複数 tool を往復する調査ターンで会話が膨らみやすいので、
 * compact 前でも十分持てるよう大きめに確保する。 */
#define CLAUDE_SINGLE_REQUEST_BUF        16384
#define CLAUDE_CONVERSATION_REQUEST_BUF 1048576

/*
 * Send a simple text message and receive streaming response.
 * provider: LLM provider (use &provider_claude).
 * user_message: the text prompt.
 * out: filled with parsed response on success.
 * Returns 0 on success, negative on error.
 */
int claude_send_message(
    const struct llm_provider *provider,
    const char *user_message,
    struct claude_response *out
);

/*
 * Send message with API key override.
 * api_key: NULL to use default from provider headers.
 */
int claude_send_message_with_key(
    const struct llm_provider *provider,
    const char *user_message,
    const char *api_key,
    struct claude_response *out
);

/*
 * Send full conversation and receive streaming response.
 * conv: conversation with all turns
 * tools_enabled: if 1, include tool definitions in request
 * out: filled with response
 * Returns 0 on success, negative on error.
 */
int claude_send_conversation(
    const struct llm_provider *provider,
    const struct conversation *conv,
    int tools_enabled,
    struct claude_response *out
);

/*
 * Send full conversation with API key override.
 * api_key: NULL to use default from provider headers.
 */
int claude_send_conversation_with_key(
    const struct llm_provider *provider,
    const struct conversation *conv,
    int tools_enabled,
    const char *api_key,
    struct claude_response *out
);

/*
 * Send raw request JSON (for multi-turn agent loop).
 * request_json: complete JSON body to POST.
 * request_json_len: length of request_json.
 * api_key: NULL to use default from provider headers.
 * out: filled with parsed response on success.
 * Returns 0 on success, negative on error.
 */
int claude_send_raw_request(
    const struct llm_provider *provider,
    const char *request_json, int request_json_len,
    const char *api_key,
    struct claude_response *out
);

void claude_client_set_text_stream_callback(claude_stream_text_fn callback,
                                            void *userdata);

#endif /* _AGENT_CLAUDE_CLIENT_H */
