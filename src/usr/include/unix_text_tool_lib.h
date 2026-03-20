#ifndef _USR_UNIX_TEXT_TOOL_LIB_H
#define _USR_UNIX_TEXT_TOOL_LIB_H

#ifdef TEST_BUILD
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#else
#include <fs.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utf8.h>
#endif

#define UTT_IO_BUF_SIZE 512
#define UTT_PATH_MAX 512

struct utt_string {
  char *data;
  int len;
  int cap;
};

struct utt_text_line {
  char *text;
  int len;
};

struct utt_loaded_text {
  char *name;
  char *data;
  char *raw_data;
  int len;
  struct utt_text_line *lines;
  int line_count;
};

struct utt_line_ref {
  char *text;
  int len;
};

struct utt_range {
  int start;
  int end;
};

int utt_is_space(char ch);
int utt_is_digit(char ch);
int utt_is_alpha(char ch);
int utt_is_alnum(char ch);
char utt_tolower_ascii(char ch);
int utt_strlen_int(const char *text);
void utt_write_raw(int fd, const char *data, int len);
void utt_write_text(int fd, const char *text);
int utt_print_error(const char *prog, const char *msg, const char *arg);
char *utt_strdup_len(const char *text, int len);
char *utt_strdup_text(const char *text);
int utt_string_reserve(struct utt_string *str, int need);
void utt_string_init(struct utt_string *str);
void utt_string_reset(struct utt_string *str);
void utt_string_free(struct utt_string *str);
int utt_string_append_len(struct utt_string *str, const char *text, int len);
int utt_string_append_text(struct utt_string *str, const char *text);
int utt_string_append_char(struct utt_string *str, char ch);
int utt_parse_long_value(const char *text, long *value_out);
int utt_match_long_option(const char *arg,
                          const char *name,
                          const char **value_out);
int utt_is_help_option(const char *arg);
int utt_is_stdin_path(const char *path);
long utt_count_newlines(const char *text, int len);
int utt_parse_head_count_spec(const char *text,
                              long *count_out,
                              int *all_but_last_out);
int utt_parse_tail_count_spec(const char *text,
                              long *count_out,
                              int *from_start_out);
long utt_parse_long_substr(const char *text, int len);
int utt_format_long(char *buf, int cap, long value);
int utt_buf_append_long(char *buf, int cap, int len, long value);
int utt_next_char_end(const char *text, int len, int index);
int utt_prev_char_start(const char *text, int len, int index);
int utt_advance_chars(const char *text, int len, int count);
int utt_read_fd_all(int fd, char **data_out, int *len_out);
int utt_read_path_all(const char *path, char **data_out, int *len_out);
void utt_loaded_text_free(struct utt_loaded_text *text);
int utt_split_lines(char *data, int len,
                    struct utt_text_line **lines_out,
                    int *count_out);
int utt_load_text_from_fd(int fd,
                          const char *name,
                          struct utt_loaded_text *out);
int utt_load_text_from_path(const char *path,
                            struct utt_loaded_text *out);
void utt_free_texts(struct utt_loaded_text *texts, int count);
int utt_collect_input_texts(char **files, int file_count,
                            struct utt_loaded_text **texts_out,
                            int *count_out);
int utt_collect_line_refs(struct utt_loaded_text *texts,
                          int text_count,
                          struct utt_line_ref **refs_out,
                          int *ref_count_out);
int utt_wildcard_match(const char *pattern, const char *text);
int utt_char_equal(char a, char b, int ignore_case);
int utt_regex_match(const char *pattern,
                    const char *text,
                    int ignore_case);
int utt_contains_substr(const char *haystack,
                        int hay_len,
                        const char *needle,
                        int needle_len,
                        int ignore_case);
int utt_parse_range_list(const char *spec,
                         struct utt_range **ranges_out,
                         int *count_out);
int utt_range_contains(const struct utt_range *ranges,
                       int range_count,
                       int index1);
void utt_get_whitespace_field(const char *text, int len,
                              int field_no,
                              int *start_out,
                              int *end_out);
void utt_get_delim_field(const char *text, int len,
                         char delim,
                         int field_no,
                         int *start_out,
                         int *end_out);
int utt_print_line_ref(const struct utt_line_ref *line);
void utt_print_header_if_needed(const char *name,
                                int index,
                                int total,
                                int quiet,
                                int verbose);

#endif /* _USR_UNIX_TEXT_TOOL_LIB_H */
