/*
 * tool_init.c - Register all built-in tools
 *
 * Called once at startup to populate the tool registry with
 * all available tool handlers.
 */

#include <agent/tool_handlers.h>
#include <agent/tool_registry.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
#include <stdio.h>
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

void tool_init(void)
{
    tool_registry_init();

    tool_register("read_file",
                  "Read a file from the filesystem",
                  TOOL_SCHEMA_READ_FILE,
                  tool_read_file);

    tool_register("write_file",
                  "Write content to a file",
                  TOOL_SCHEMA_WRITE_FILE,
                  tool_write_file);

    tool_register("list_dir",
                  "List directory contents",
                  TOOL_SCHEMA_LIST_DIR,
                  tool_list_dir);

    tool_register("get_system_info",
                  "Get system information",
                  TOOL_SCHEMA_GET_SYSTEM_INFO,
                  tool_get_system_info);

    tool_register("run_command",
                  "Run a shell command",
                  TOOL_SCHEMA_RUN_COMMAND,
                  tool_run_command);

    debug_printf("[TOOL] initialized %d built-in tools\n", tool_count());
}
