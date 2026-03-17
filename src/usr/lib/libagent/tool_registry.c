/*
 * tool_registry.c - Tool registration and lookup
 *
 * Maintains a static array of tool definitions that can be
 * registered at init time and looked up by name during dispatch.
 */

#include <agent/tool_registry.h>
#include <string.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
#include <stdio.h>
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

/* Static registry storage */
static struct tool_def registry[TOOL_MAX_TOOLS];
static int registry_count;

void tool_registry_init(void)
{
    memset(registry, 0, sizeof(registry));
    registry_count = 0;
    debug_printf("[TOOL] registry initialized\n");
}

int tool_register(const char *name, const char *description,
                  const char *input_schema_json, tool_handler_fn handler)
{
    struct tool_def *td;

    if (!name || !handler)
        return -1;

    if (registry_count >= TOOL_MAX_TOOLS) {
        debug_printf("[TOOL] registry full, cannot register '%s'\n", name);
        return -1;
    }

    td = &registry[registry_count];

    /* Copy name */
    strncpy(td->name, name, TOOL_MAX_NAME - 1);
    td->name[TOOL_MAX_NAME - 1] = '\0';

    /* Copy description */
    if (description) {
        strncpy(td->description, description, TOOL_MAX_DESC - 1);
        td->description[TOOL_MAX_DESC - 1] = '\0';
    } else {
        td->description[0] = '\0';
    }

    td->input_schema_json = input_schema_json;
    td->handler = handler;

    registry_count++;
    debug_printf("[TOOL] registered '%s' (%d/%d)\n",
                 name, registry_count, TOOL_MAX_TOOLS);
    return 0;
}

const struct tool_def *tool_find(const char *name)
{
    int i;

    if (!name)
        return (const struct tool_def *)0;

    for (i = 0; i < registry_count; i++) {
        if (strcmp(registry[i].name, name) == 0)
            return &registry[i];
    }
    return (const struct tool_def *)0;
}

int tool_list(const struct tool_def **out, int max)
{
    int i;
    int count;

    if (!out || max <= 0)
        return 0;

    count = registry_count < max ? registry_count : max;
    for (i = 0; i < count; i++)
        out[i] = &registry[i];

    return count;
}

int tool_count(void)
{
    return registry_count;
}
