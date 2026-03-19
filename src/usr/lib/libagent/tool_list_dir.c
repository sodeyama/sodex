/*
 * tool_list_dir.c - list_dir tool implementation
 */

#include <agent/tool_handlers.h>
#include <agent/path_utils.h>
#include <json.h>
#include <string.h>
#include <stdio.h>
#include <fs.h>
#include <stdlib.h>

#ifdef TEST_BUILD
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <debug.h>
#endif

#ifdef TEST_BUILD
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

const char TOOL_SCHEMA_LIST_DIR[] =
    "{\"type\":\"object\","
    "\"properties\":{\"path\":{\"type\":\"string\","
    "\"description\":\"Directory path. Relative paths use the current directory\"}},"
    "\"required\":[\"path\"]}";

#define DIR_BUF_SIZE      4096
#define MAX_DIR_ENTRIES   64

#ifndef TEST_BUILD
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
#define EXT3_FT_REG_FILE  1
#define EXT3_FT_DIR       2
#define EXT3_FT_SYMLINK   7
#endif

static int is_dot_entry(const char *name)
{
    if (!name)
        return 1;
    if (strcmp(name, ".") == 0)
        return 1;
    return strcmp(name, "..") == 0;
}

static void write_entry_json(struct json_writer *jw,
                             const char *name,
                             const char *full_path,
                             const char *type_str,
                             int size)
{
    jw_object_start(jw);
    jw_key(jw, "name");
    jw_string(jw, name);
    jw_key(jw, "path");
    jw_string(jw, full_path);
    jw_key(jw, "type");
    jw_string(jw, type_str);
    jw_key(jw, "size");
    jw_int(jw, size);
    jw_object_end(jw);
}

#ifndef TEST_BUILD
static int guest_entry_size(const char *full_path, const char *type_str)
{
    int fd;
    int size = -1;

    if (!full_path || !type_str)
        return -1;
    if (strcmp(type_str, "file") != 0)
        return -1;

    fd = open(full_path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    size = (int)lseek(fd, 0, SEEK_END);
    close(fd);
    return size;
}
#endif

int tool_list_dir(const char *input_json, int input_len,
                  char *result_buf, int result_cap)
{
    char path[AGENT_PATH_MAX];
    struct json_writer jw;
    int entry_count = 0;
    int truncated = 0;

    if (!input_json || !result_buf)
        return -1;

    if (agent_json_get_normalized_path(input_json, input_len,
                                       "path", path, sizeof(path)) < 0) {
        return agent_write_error_json(result_buf, result_cap,
                                      "invalid_path",
                                      "path must resolve to a normalized location",
                                      (const char *)0);
    }

    debug_printf("[TOOL list_dir] listing: %s\n", path);

    jw_init(&jw, result_buf, result_cap);
    jw_object_start(&jw);
    jw_key(&jw, "path");
    jw_string(&jw, path);
    jw_key(&jw, "entries");
    jw_array_start(&jw);

#ifdef TEST_BUILD
    {
        DIR *dirp;
        struct dirent *de;

        dirp = opendir(path);
        if (!dirp) {
            return agent_write_error_json(result_buf, result_cap,
                                          "not_found",
                                          "cannot open directory",
                                          path);
        }

        while ((de = readdir(dirp)) != (struct dirent *)0) {
            char full_path[PATHNAME_MAX];
            struct stat st;
            const char *type_str = "other";
            int size = -1;

            if (is_dot_entry(de->d_name))
                continue;
            if (entry_count >= MAX_DIR_ENTRIES) {
                truncated = 1;
                break;
            }
            if (agent_path_join(path, de->d_name,
                                full_path, sizeof(full_path)) < 0)
                continue;
            if (stat(full_path, &st) == 0) {
                if (S_ISDIR(st.st_mode))
                    type_str = "dir";
                else if (S_ISREG(st.st_mode))
                    type_str = "file";
                else if (S_ISLNK(st.st_mode))
                    type_str = "symlink";
                size = (int)st.st_size;
            }
            write_entry_json(&jw, de->d_name, full_path, type_str, size);
            entry_count++;
        }
        closedir(dirp);
    }
#else
    {
        int fd;
        static char dir_buf[DIR_BUF_SIZE];
        int bytes_read;
        int offset = 0;

        fd = open(path, O_RDONLY, 0);
        if (fd < 0) {
            return agent_write_error_json(result_buf, result_cap,
                                          "not_found",
                                          "cannot open directory",
                                          path);
        }

        bytes_read = read(fd, dir_buf, DIR_BUF_SIZE);
        close(fd);

        if (bytes_read <= 0) {
            return agent_write_error_json(result_buf, result_cap,
                                          "io_error",
                                          "directory read failed",
                                          path);
        }

        while (offset < bytes_read && entry_count < MAX_DIR_ENTRIES) {
            struct dir_entry *de = (struct dir_entry *)(dir_buf + offset);
            char name_buf[256];
            char full_path[PATHNAME_MAX];
            const char *type_str = "other";
            int size = -1;
            int nlen;

            if (de->rec_len == 0 || de->rec_len < 8)
                break;
            if (offset + de->rec_len > bytes_read)
                break;

            if (de->inode == 0 || de->name_len == 0) {
                offset += de->rec_len;
                continue;
            }

            nlen = de->name_len;
            if (nlen > 255)
                nlen = 255;
            memcpy(name_buf, de->name, (size_t)nlen);
            name_buf[nlen] = '\0';

            if (is_dot_entry(name_buf)) {
                offset += de->rec_len;
                continue;
            }

            if (agent_path_join(path, name_buf,
                                full_path, sizeof(full_path)) < 0) {
                offset += de->rec_len;
                continue;
            }

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

            size = guest_entry_size(full_path, type_str);
            write_entry_json(&jw, name_buf, full_path, type_str, size);
            entry_count++;
            offset += de->rec_len;
        }

        if (offset < bytes_read)
            truncated = 1;
    }
#endif

    jw_array_end(&jw);
    jw_key(&jw, "count");
    jw_int(&jw, entry_count);
    jw_key(&jw, "truncated");
    jw_bool(&jw, truncated);
    jw_object_end(&jw);

    debug_printf("[TOOL list_dir] found %d entries in: %s\n",
                 entry_count, path);
    return jw_finish(&jw);
}
