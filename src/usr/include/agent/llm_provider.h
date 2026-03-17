/*
 * llm_provider.h - LLM provider abstraction interface
 *
 * Function pointer table for LLM-agnostic agent loop.
 * Currently only Claude is implemented.
 */

#ifndef _AGENT_LLM_PROVIDER_H
#define _AGENT_LLM_PROVIDER_H

#include <agent/api_config.h>
#include <agent/claude_adapter.h>
#include <json.h>
#include <sse_parser.h>

struct llm_provider {
    const char *name;
    const struct api_endpoint *endpoint;
    const struct api_header *headers;
    int header_count;

    int (*build_request)(
        struct json_writer *jw,
        const char *model,
        const struct claude_message *msgs, int msg_count,
        const char *system_prompt,
        int max_tokens,
        int stream
    );

    int (*parse_sse_event)(
        const struct sse_event *event,
        struct claude_response *state
    );

    int (*parse_response)(
        const char *json_str, int json_len,
        struct claude_response *out
    );
};

/* Claude provider instance */
extern const struct llm_provider provider_claude;

#endif /* _AGENT_LLM_PROVIDER_H */
