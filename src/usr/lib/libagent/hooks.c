/*
 * hooks.c - Tool execution hook system
 *
 * Provides a simple event-based hook mechanism for tool execution.
 * Handlers are registered per event type and invoked when events fire.
 * Any handler returning HOOK_BLOCK will block the operation.
 */

#include <agent/hooks.h>
#include <string.h>

#ifndef TEST_BUILD
#include <debug.h>
#endif

/* Per-event handler lists */
static hook_handler_fn s_handlers[HOOK_EVENT_COUNT][HOOKS_MAX_HANDLERS];
static int s_handler_count[HOOK_EVENT_COUNT];

void hooks_init(void)
{
    memset(s_handlers, 0, sizeof(s_handlers));
    memset(s_handler_count, 0, sizeof(s_handler_count));
}

int hooks_register(enum hook_event event, hook_handler_fn handler)
{
    if (!handler)
        return -1;
    if (event < 0 || event >= HOOK_EVENT_COUNT)
        return -1;
    if (s_handler_count[event] >= HOOKS_MAX_HANDLERS) {
#ifndef TEST_BUILD
        debug_printf("[HOOKS] handler limit reached for event %d\n", event);
#endif
        return -1;
    }

    s_handlers[event][s_handler_count[event]] = handler;
    s_handler_count[event]++;
    return 0;
}

int hooks_fire(const struct hook_context *ctx, struct hook_response *response)
{
    int i, ret;
    enum hook_event event;

    if (!ctx || !response)
        return -1;

    event = ctx->event;
    if (event < 0 || event >= HOOK_EVENT_COUNT)
        return -1;

    /* Default: allow */
    response->decision = HOOK_CONTINUE;
    response->message[0] = '\0';

    for (i = 0; i < s_handler_count[event]; i++) {
        hook_handler_fn fn = s_handlers[event][i];
        if (!fn)
            continue;

        ret = fn(ctx, response);
        if (ret < 0) {
#ifndef TEST_BUILD
            debug_printf("[HOOKS] handler %d for event %d returned error\n",
                         i, event);
#endif
            continue;
        }

        /* If any handler blocks, stop immediately */
        if (response->decision == HOOK_BLOCK) {
#ifndef TEST_BUILD
            debug_printf("[HOOKS] event %d blocked by handler %d: %s\n",
                         event, i, response->message);
#endif
            return 1; /* blocked */
        }
    }

    return 0; /* all handlers passed */
}
