/*
 * tool_get_system_info.c - get_system_info tool implementation
 *
 * Returns basic system information about the Sodex kernel.
 */

#include <agent/tool_handlers.h>
#include <json.h>
#include <string.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_GET_SYSTEM_INFO[] =
    "{\"type\":\"object\",\"properties\":{}}";

int tool_get_system_info(const char *input_json, int input_len,
                         char *result_buf, int result_cap)
{
    struct json_writer jw;

    (void)input_json;
    (void)input_len;

    if (!result_buf)
        return -1;

    debug_printf("[TOOL get_system_info] gathering info\n");

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);

    jw_key(&jw, "kernel");
    jw_string(&jw, "sodex");

    jw_key(&jw, "arch");
    jw_string(&jw, "i486");

    jw_key(&jw, "bits");
    jw_int(&jw, 32);

    jw_key(&jw, "page_offset");
    jw_string(&jw, "0xC0000000");

    jw_key(&jw, "filesystem");
    jw_string(&jw, "ext3");

    jw_key(&jw, "boot_device");
#ifdef FDC_DEVICE
    jw_string(&jw, "floppy");
#else
    jw_string(&jw, "usb");
#endif

    jw_key(&jw, "features");
    jw_array_start(&jw);
    jw_string(&jw, "paging");
    jw_string(&jw, "protected_mode");
    jw_string(&jw, "ext3fs");
    jw_string(&jw, "process_management");
    jw_string(&jw, "tcp_ip");
    jw_string(&jw, "pci");
    jw_array_end(&jw);

    jw_object_end(&jw);

    return jw_finish(&jw);
}
