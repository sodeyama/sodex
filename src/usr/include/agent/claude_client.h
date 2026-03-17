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

/* Retry configuration */
#define CLAUDE_MAX_RETRIES      3
#define CLAUDE_INITIAL_WAIT_MS  1000
#define CLAUDE_MAX_WAIT_MS      30000

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

#endif /* _AGENT_CLAUDE_CLIENT_H */
