/*
 * hooks.h - Tool execution hook system
 */
#ifndef _AGENT_HOOKS_H
#define _AGENT_HOOKS_H

#define HOOKS_MAX_HANDLERS  16

enum hook_event {
    HOOK_PRE_TOOL_USE,
    HOOK_POST_TOOL_USE,
    HOOK_POST_TOOL_FAILURE,
    HOOK_AGENT_START,
    HOOK_AGENT_STOP,
    HOOK_STEP_START,
    HOOK_STEP_END,
    HOOK_EVENT_COUNT,
};

struct hook_context {
    enum hook_event event;
    const char *tool_name;
    const char *tool_input_json;
    int tool_input_len;
    const char *tool_result_json;
    int tool_result_len;
    int tool_is_error;
    int step_number;
};

enum hook_decision {
    HOOK_CONTINUE,
    HOOK_BLOCK,
};

struct hook_response {
    enum hook_decision decision;
    char message[256];
};

typedef int (*hook_handler_fn)(const struct hook_context *ctx,
                                struct hook_response *response);

void hooks_init(void);
int hooks_register(enum hook_event event, hook_handler_fn handler);
int hooks_fire(const struct hook_context *ctx, struct hook_response *response);

#endif /* _AGENT_HOOKS_H */
