/*
 * tool_list_dir.c - list_dir tool implementation
 *
 * Lists directory contents. Since Sodex may have limited directory
 * listing support, this implementation attempts to read directory
 * entries and falls back to an informative error.
 */

#include <agent/tool_handlers.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <fs.h>
#include <stdlib.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_LIST_DIR[] =
    "{\"type\":\"object\","
    "\"properties\":{\"path\":{\"type\":\"string\","
    "\"description\":\"Directory path\"}},"
    "\"required\":[\"path\"]}";

/*
 * Directory entry structure for reading raw directory data.
 * Matches the ext3 directory entry format used in Sodex.
 */
struct dir_entry {
    unsigned int   inode;
    unsigned short rec_len;
    unsigned char  name_len;
    unsigned char  file_type;
    char           name[255];
};

/* ext3 file types */
#define EXT3_FT_UNKNOWN   0
#define EXT3_FT_REG_FILE  1
#define EXT3_FT_DIR       2
#define EXT3_FT_CHRDEV    3
#define EXT3_FT_BLKDEV    4
#define EXT3_FT_FIFO      5
#define EXT3_FT_SOCK      6
#define EXT3_FT_SYMLINK   7

#define DIR_BUF_SIZE      4096
#define MAX_DIR_ENTRIES   64

int tool_list_dir(const char *input_json, int input_len,
                  char *result_buf, int result_cap)
{
    struct json_parser jp;
    struct json_token tokens[32];
    int ntokens;
    int tok;
    char path[256];
    int fd;
    static char dir_buf[DIR_BUF_SIZE];
    int bytes_read;
    struct json_writer jw;
    int offset;
    int entry_count;

    if (!input_json || !result_buf)
        return -1;

    /* Parse input JSON */
    json_init(&jp);
    ntokens = json_parse(&jp, input_json, input_len, tokens, 32);
    if (ntokens < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"invalid input JSON\"}");
    }

    tok = json_find_key(input_json, tokens, ntokens, 0, "path");
    if (tok < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"missing required field: path\"}");
    }
    if (json_token_str(input_json, &tokens[tok], path, sizeof(path)) < 0) {
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"path too long\"}");
    }

    debug_printf("[TOOL list_dir] listing: %s\n", path);

    /* Try to open the directory and read raw directory entries */
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        debug_printf("[TOOL list_dir] open failed: %s\n", path);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"cannot open directory: %s\"}", path);
    }

    bytes_read = read(fd, dir_buf, DIR_BUF_SIZE);
    close(fd);

    if (bytes_read <= 0) {
        /* Directory read not supported or empty */
        debug_printf("[TOOL list_dir] read returned %d for: %s\n",
                     bytes_read, path);
        return snprintf(result_buf, result_cap,
                        "{\"error\":\"directory listing not supported "
                        "or empty directory: %s\"}", path);
    }

    /* Parse directory entries from raw buffer */
    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "path");
    jw_string(&jw, path);
    jw_key(&jw, "entries");
    jw_array_start(&jw);

    offset = 0;
    entry_count = 0;
    while (offset < bytes_read && entry_count < MAX_DIR_ENTRIES) {
        struct dir_entry *de = (struct dir_entry *)(dir_buf + offset);
        char name_buf[256];
        const char *type_str;

        /* Validate entry */
        if (de->rec_len == 0 || de->rec_len < 8)
            break;
        if (offset + de->rec_len > bytes_read)
            break;

        /* Skip deleted entries (inode == 0) */
        if (de->inode != 0 && de->name_len > 0) {
            /* Copy name (not null-terminated in dir entry) */
            int nlen = de->name_len;
            if (nlen > 255)
                nlen = 255;
            memcpy(name_buf, de->name, nlen);
            name_buf[nlen] = '\0';

            /* Determine type string */
            switch (de->file_type) {
            case EXT3_FT_DIR:
                type_str = "dir";
                break;
            case EXT3_FT_REG_FILE:
                type_str = "file";
                break;
            case EXT3_FT_SYMLINK:
                type_str = "symlink";
                break;
            default:
                type_str = "other";
                break;
            }

            jw_object_start(&jw);
            jw_key(&jw, "name");
            jw_string(&jw, name_buf);
            jw_key(&jw, "type");
            jw_string(&jw, type_str);
            jw_object_end(&jw);

            entry_count++;
        }

        offset += de->rec_len;
    }

    jw_array_end(&jw);
    jw_key(&jw, "count");
    jw_int(&jw, entry_count);
    jw_object_end(&jw);

    debug_printf("[TOOL list_dir] found %d entries in: %s\n",
                 entry_count, path);

    return jw_finish(&jw);
}
