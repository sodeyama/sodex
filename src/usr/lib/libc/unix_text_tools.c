#include <unix_text_tools.h>

#ifdef TEST_BUILD
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <fs.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utf8.h>
#endif

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

#define UTT_IO_BUF_SIZE 512
#define UTT_PATH_MAX 512
#define UTT_FIELD_MAX 64
#define UTT_EXPR_MAX 16
#define UTT_STMT_MAX 16
#define UTT_VAR_MAX 16

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

struct utt_find_options {
  int mindepth;
  int maxdepth;
  int has_maxdepth;
  char name_pattern[128];
  char type_filter;
};

struct utt_sort_options {
  int numeric;
  int reverse;
  int unique;
  int has_delim;
  char delim;
  int key_start;
  int key_end;
  const char *output_path;
};

struct utt_uniq_options {
  int show_count;
  int only_repeated;
  int only_unique;
  int skip_fields;
  int skip_chars;
};

struct utt_wc_counts {
  long lines;
  long words;
  long bytes;
};

struct utt_grep_options {
  int fixed;
  int ignore_case;
  int invert;
  int show_line_numbers;
  int count_only;
  int quiet;
  char **patterns;
  int pattern_count;
};

struct utt_cut_options {
  int mode_chars;
  int mode_fields;
  int suppress_no_delim;
  int complement;
  char delim;
  struct utt_range *ranges;
  int range_count;
};

struct utt_sed_command {
  int addr_line;
  int addr_last;
  char type;
  char *find;
  char *replace;
  int global;
};

struct utt_sed_program {
  int suppress_default;
  struct utt_sed_command *commands;
  int command_count;
};

enum utt_awk_expr_type {
  UTT_AWK_EXPR_STRING = 1,
  UTT_AWK_EXPR_FIELD,
  UTT_AWK_EXPR_NR,
  UTT_AWK_EXPR_NF,
  UTT_AWK_EXPR_VAR
};

enum utt_awk_stmt_kind {
  UTT_AWK_STMT_BEGIN = 1,
  UTT_AWK_STMT_MAIN,
  UTT_AWK_STMT_END
};

struct utt_awk_expr {
  int type;
  int field_index;
  char text[128];
};

struct utt_awk_stmt {
  int kind;
  int has_pattern;
  char pattern[128];
  struct utt_awk_expr exprs[UTT_EXPR_MAX];
  int expr_count;
};

struct utt_awk_var {
  char name[32];
  char value[128];
};

struct utt_awk_program {
  char fs[32];
  struct utt_awk_var vars[UTT_VAR_MAX];
  int var_count;
  struct utt_awk_stmt stmts[UTT_STMT_MAX];
  int stmt_count;
};

struct utt_awk_record {
  char *line;
  int len;
  char *fields[UTT_FIELD_MAX];
  int field_lens[UTT_FIELD_MAX];
  int field_count;
  long nr;
};

static int utt_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' ||
         ch == '\r' || ch == '\f' || ch == '\v';
}

static int utt_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int utt_is_alpha(char ch)
{
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static int utt_is_alnum(char ch)
{
  return utt_is_alpha(ch) || utt_is_digit(ch);
}

static char utt_tolower_ascii(char ch)
{
  if (ch >= 'A' && ch <= 'Z')
    return (char)(ch - 'A' + 'a');
  return ch;
}

static int utt_strlen_int(const char *text)
{
  return text == 0 ? 0 : (int)strlen(text);
}

static void utt_write_raw(int fd, const char *data, int len)
{
  if (data == 0 || len <= 0)
    return;
  while (len > 0) {
    int written = (int)write(fd, data, (size_t)len);

    if (written <= 0)
      return;
    data += written;
    len -= written;
  }
}

static void utt_write_text(int fd, const char *text)
{
  utt_write_raw(fd, text, utt_strlen_int(text));
}

static int utt_print_error(const char *prog, const char *msg, const char *arg)
{
  utt_write_text(STDERR_FILENO, prog);
  utt_write_text(STDERR_FILENO, ": ");
  utt_write_text(STDERR_FILENO, msg);
  if (arg != 0 && arg[0] != '\0') {
    utt_write_text(STDERR_FILENO, " ");
    utt_write_text(STDERR_FILENO, arg);
  }
  utt_write_text(STDERR_FILENO, "\n");
  return 1;
}

static char *utt_strdup_len(const char *text, int len)
{
  char *copy;

  if (text == 0)
    len = 0;
  if (len < 0)
    len = 0;
  copy = (char *)malloc((size_t)len + 1U);
  if (copy == 0)
    return 0;
  if (text != 0 && len > 0)
    memcpy(copy, text, (size_t)len);
  copy[len] = '\0';
  return copy;
}

static char *utt_strdup_text(const char *text)
{
  return utt_strdup_len(text, utt_strlen_int(text));
}

static int utt_string_reserve(struct utt_string *str, int need)
{
  char *next;
  int next_cap;

  if (str == 0 || need <= str->cap)
    return 0;

  next_cap = str->cap > 0 ? str->cap : 64;
  while (next_cap < need)
    next_cap *= 2;

  next = (char *)malloc((size_t)next_cap);
  if (next == 0)
    return -1;
  if (str->data != 0 && str->len > 0)
    memcpy(next, str->data, (size_t)str->len);
  if (str->data != 0)
    free(str->data);
  str->data = next;
  str->cap = next_cap;
  if (str->len < str->cap)
    str->data[str->len] = '\0';
  return 0;
}

static void utt_string_init(struct utt_string *str)
{
  if (str == 0)
    return;
  memset(str, 0, sizeof(*str));
}

static void utt_string_reset(struct utt_string *str)
{
  if (str == 0 || str->data == 0)
    return;
  str->len = 0;
  str->data[0] = '\0';
}

static void utt_string_free(struct utt_string *str)
{
  if (str == 0)
    return;
  if (str->data != 0)
    free(str->data);
  memset(str, 0, sizeof(*str));
}

static int utt_string_append_len(struct utt_string *str,
                                 const char *text,
                                 int len)
{
  if (str == 0 || (text == 0 && len > 0))
    return -1;
  if (utt_string_reserve(str, str->len + len + 1) < 0)
    return -1;
  if (len > 0)
    memcpy(str->data + str->len, text, (size_t)len);
  str->len += len;
  str->data[str->len] = '\0';
  return 0;
}

static int utt_string_append_text(struct utt_string *str, const char *text)
{
  return utt_string_append_len(str, text, utt_strlen_int(text));
}

static int utt_string_append_char(struct utt_string *str, char ch)
{
  return utt_string_append_len(str, &ch, 1);
}

static int utt_parse_long_value(const char *text, long *value_out)
{
  char *endptr;
  long value;

  if (text == 0 || text[0] == '\0' || value_out == 0)
    return -1;
  value = strtol(text, &endptr, 10);
  if (endptr == text || *endptr != '\0')
    return -1;
  *value_out = value;
  return 0;
}

static int utt_match_long_option(const char *arg,
                                 const char *name,
                                 const char **value_out)
{
  int name_len;

  if (value_out != 0)
    *value_out = 0;
  if (arg == 0 || name == 0 || strncmp(arg, "--", 2) != 0)
    return 0;
  arg += 2;
  name_len = utt_strlen_int(name);
  if (strcmp(arg, name) == 0)
    return 1;
  if (strncmp(arg, name, (size_t)name_len) == 0 && arg[name_len] == '=') {
    if (value_out != 0)
      *value_out = arg + name_len + 1;
    return 1;
  }
  return 0;
}

static int utt_is_stdin_path(const char *path)
{
  return path != 0 && path[0] == '-' && path[1] == '\0';
}

static long utt_count_newlines(const char *text, int len)
{
  long count = 0;
  int i;

  for (i = 0; i < len; i++) {
    if (text[i] == '\n')
      count++;
  }
  return count;
}

static int utt_parse_head_count_spec(const char *text,
                                     long *count_out,
                                     int *all_but_last_out)
{
  long value;

  if (utt_parse_long_value(text, &value) < 0 ||
      count_out == 0 || all_but_last_out == 0)
    return -1;
  *all_but_last_out = value < 0;
  if (value < 0)
    value = -value;
  *count_out = value;
  return 0;
}

static int utt_parse_tail_count_spec(const char *text,
                                     long *count_out,
                                     int *from_start_out)
{
  long value;

  if (utt_parse_long_value(text, &value) < 0 ||
      count_out == 0 || from_start_out == 0)
    return -1;
  *from_start_out = text[0] == '+';
  if (value < 0)
    value = -value;
  *count_out = value;
  return 0;
}

static long utt_parse_long_substr(const char *text, int len)
{
  char buf[64];

  if (text == 0 || len <= 0)
    return 0;
  if (len >= (int)sizeof(buf))
    len = (int)sizeof(buf) - 1;
  memcpy(buf, text, (size_t)len);
  buf[len] = '\0';
  return strtol(buf, (char **)0, 10);
}

static int utt_format_long(char *buf, int cap, long value)
{
  char tmp[32];
  unsigned long mag;
  int len = 0;
  int out = 0;

  if (buf == 0 || cap <= 1)
    return 0;

  if (value < 0) {
    if (out < cap - 1)
      buf[out++] = '-';
    mag = (unsigned long)(-(value + 1)) + 1UL;
  } else {
    mag = (unsigned long)value;
  }

  do {
    tmp[len++] = (char)('0' + (mag % 10UL));
    mag /= 10UL;
  } while (mag > 0 && len < (int)sizeof(tmp));

  while (len > 0 && out < cap - 1)
    buf[out++] = tmp[--len];
  buf[out] = '\0';
  return out;
}

static int utt_buf_append_long(char *buf, int cap, int len, long value)
{
  char num[32];
  int num_len;

  if (buf == 0 || cap <= 0)
    return len;
  if (len < 0)
    len = 0;
  if (len >= cap)
    return len;

  num_len = utt_format_long(num, sizeof(num), value);
  if (len + num_len >= cap)
    num_len = cap - len - 1;
  if (num_len > 0) {
    memcpy(buf + len, num, (size_t)num_len);
    len += num_len;
  }
  buf[len] = '\0';
  return len;
}

static int utt_next_char_end(const char *text, int len, int index)
{
#ifdef TEST_BUILD
  unsigned char ch;

  if (text == 0 || index >= len)
    return len;
  ch = (unsigned char)text[index];
  if ((ch & 0x80U) == 0)
    return index + 1;
  if ((ch & 0xe0U) == 0xc0U && index + 2 <= len)
    return index + 2;
  if ((ch & 0xf0U) == 0xe0U && index + 3 <= len)
    return index + 3;
  if ((ch & 0xf8U) == 0xf0U && index + 4 <= len)
    return index + 4;
  return index + 1;
#else
  return utf8_next_char_end(text, len, index);
#endif
}

static int utt_prev_char_start(const char *text, int len, int index)
{
#ifdef TEST_BUILD
  if (text == 0 || index <= 0)
    return 0;
  index--;
  while (index > 0 &&
         (((unsigned char)text[index]) & 0xc0U) == 0x80U)
    index--;
  return index;
#else
  return utf8_prev_char_start(text, len, index);
#endif
}

static int utt_advance_chars(const char *text, int len, int count)
{
  int index = 0;

  while (count > 0 && index < len) {
    index = utt_next_char_end(text, len, index);
    count--;
  }
  return index;
}

static int utt_read_fd_all(int fd, char **data_out, int *len_out)
{
  struct utt_string buf;
  char chunk[UTT_IO_BUF_SIZE];

  if (data_out == 0 || len_out == 0)
    return -1;

  utt_string_init(&buf);
  while (1) {
    int read_len = (int)read(fd, chunk, sizeof(chunk));

    if (read_len < 0) {
      utt_string_free(&buf);
      return -1;
    }
    if (read_len == 0)
      break;
    if (utt_string_append_len(&buf, chunk, read_len) < 0) {
      utt_string_free(&buf);
      return -1;
    }
  }

  *data_out = buf.data;
  *len_out = buf.len;
  return 0;
}

static int utt_read_path_all(const char *path, char **data_out, int *len_out)
{
  int fd;
  int ret;

  if (path == 0)
    return -1;
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  ret = utt_read_fd_all(fd, data_out, len_out);
  close(fd);
  return ret;
}

static void utt_loaded_text_free(struct utt_loaded_text *text)
{
  if (text == 0)
    return;
  if (text->name != 0)
    free(text->name);
  if (text->raw_data != 0)
    free(text->raw_data);
  if (text->data != 0)
    free(text->data);
  if (text->lines != 0)
    free(text->lines);
  memset(text, 0, sizeof(*text));
}

static int utt_split_lines(char *data, int len,
                           struct utt_text_line **lines_out,
                           int *count_out)
{
  struct utt_text_line *lines;
  int count = 0;
  int i;
  int start = 0;
  int line_index = 0;

  if (lines_out == 0 || count_out == 0)
    return -1;

  if (len <= 0) {
    *lines_out = 0;
    *count_out = 0;
    return 0;
  }

  for (i = 0; i < len; i++) {
    if (data[i] == '\n')
      count++;
  }
  if (len > 0 && data[len - 1] != '\n')
    count++;

  lines = (struct utt_text_line *)malloc(sizeof(*lines) * (size_t)count);
  if (lines == 0)
    return -1;

  for (i = 0; i < len; i++) {
    if (data[i] != '\n')
      continue;
    data[i] = '\0';
    lines[line_index].text = data + start;
    lines[line_index].len = i - start;
    line_index++;
    start = i + 1;
  }

  if (start < len) {
    lines[line_index].text = data + start;
    lines[line_index].len = len - start;
    line_index++;
  }

  *lines_out = lines;
  *count_out = line_index;
  return 0;
}

static int utt_load_text_from_fd(int fd,
                                 const char *name,
                                 struct utt_loaded_text *out)
{
  char *data = 0;
  char *raw_data = 0;
  int len = 0;
  struct utt_text_line *lines = 0;
  int line_count = 0;

  if (out == 0)
    return -1;
  memset(out, 0, sizeof(*out));
  if (utt_read_fd_all(fd, &data, &len) < 0)
    return -1;
  raw_data = utt_strdup_len(data, len);
  if (raw_data == 0) {
    free(data);
    return -1;
  }
  if (utt_split_lines(data, len, &lines, &line_count) < 0) {
    free(raw_data);
    free(data);
    return -1;
  }
  out->name = utt_strdup_text(name != 0 ? name : "-");
  out->data = data;
  out->raw_data = raw_data;
  out->len = len;
  out->lines = lines;
  out->line_count = line_count;
  return 0;
}

static int utt_load_text_from_path(const char *path,
                                   struct utt_loaded_text *out)
{
  int fd;
  int ret;

  if (path == 0 || out == 0)
    return -1;
  if (utt_is_stdin_path(path))
    return utt_load_text_from_fd(STDIN_FILENO, "-", out);
  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;
  ret = utt_load_text_from_fd(fd, path, out);
  close(fd);
  return ret;
}

static void utt_free_texts(struct utt_loaded_text *texts, int count)
{
  int i;

  if (texts == 0)
    return;
  for (i = 0; i < count; i++)
    utt_loaded_text_free(&texts[i]);
  free(texts);
}

static int utt_collect_input_texts(char **files, int file_count,
                                   struct utt_loaded_text **texts_out,
                                   int *count_out)
{
  struct utt_loaded_text *texts;
  int i;

  if (texts_out == 0 || count_out == 0)
    return -1;

  if (file_count <= 0) {
    texts = (struct utt_loaded_text *)malloc(sizeof(*texts));
    if (texts == 0)
      return -1;
    if (utt_load_text_from_fd(STDIN_FILENO, "-", &texts[0]) < 0) {
      free(texts);
      return -1;
    }
    *texts_out = texts;
    *count_out = 1;
    return 0;
  }

  texts = (struct utt_loaded_text *)malloc(sizeof(*texts) * (size_t)file_count);
  if (texts == 0)
    return -1;
  memset(texts, 0, sizeof(*texts) * (size_t)file_count);
  for (i = 0; i < file_count; i++) {
    if (utt_load_text_from_path(files[i], &texts[i]) < 0) {
      utt_free_texts(texts, file_count);
      return -1;
    }
  }
  *texts_out = texts;
  *count_out = file_count;
  return 0;
}

static int utt_collect_line_refs(struct utt_loaded_text *texts,
                                 int text_count,
                                 struct utt_line_ref **refs_out,
                                 int *ref_count_out)
{
  struct utt_line_ref *refs;
  int total = 0;
  int i;
  int j;
  int pos = 0;

  if (refs_out == 0 || ref_count_out == 0)
    return -1;
  for (i = 0; i < text_count; i++)
    total += texts[i].line_count;
  refs = (struct utt_line_ref *)malloc(sizeof(*refs) * (size_t)(total > 0 ? total : 1));
  if (refs == 0)
    return -1;
  for (i = 0; i < text_count; i++) {
    for (j = 0; j < texts[i].line_count; j++) {
      refs[pos].text = texts[i].lines[j].text;
      refs[pos].len = texts[i].lines[j].len;
      pos++;
    }
  }
  *refs_out = refs;
  *ref_count_out = total;
  return 0;
}

static int utt_wildcard_match(const char *pattern, const char *text)
{
  if (pattern == 0 || text == 0)
    return 0;
  if (*pattern == '\0')
    return *text == '\0';
  if (*pattern == '*') {
    while (*pattern == '*')
      pattern++;
    if (*pattern == '\0')
      return 1;
    while (*text != '\0') {
      if (utt_wildcard_match(pattern, text))
        return 1;
      text++;
    }
    return 0;
  }
  if (*pattern == '?') {
    if (*text == '\0')
      return 0;
    return utt_wildcard_match(pattern + 1, text + 1);
  }
  if (*pattern != *text)
    return 0;
  return utt_wildcard_match(pattern + 1, text + 1);
}

static int utt_char_equal(char a, char b, int ignore_case)
{
  if (ignore_case != 0) {
    a = utt_tolower_ascii(a);
    b = utt_tolower_ascii(b);
  }
  return a == b;
}

static int utt_regex_match_here(const char *pattern,
                                const char *text,
                                int ignore_case);

static int utt_regex_match_star(char ch,
                                const char *pattern,
                                const char *text,
                                int ignore_case,
                                int any_char)
{
  do {
    if (utt_regex_match_here(pattern, text, ignore_case))
      return 1;
  } while (*text != '\0' &&
           (any_char != 0 || utt_char_equal(*text, ch, ignore_case)) &&
           *text++ != '\0');
  return 0;
}

static int utt_regex_match_here(const char *pattern,
                                const char *text,
                                int ignore_case)
{
  if (pattern[0] == '\0')
    return 1;
  if (pattern[0] == '$' && pattern[1] == '\0')
    return *text == '\0';
  if (pattern[1] == '*')
    return utt_regex_match_star(pattern[0], pattern + 2, text, ignore_case,
                                pattern[0] == '.');
  if (*text != '\0' &&
      (pattern[0] == '.' || utt_char_equal(pattern[0], *text, ignore_case)))
    return utt_regex_match_here(pattern + 1, text + 1, ignore_case);
  return 0;
}

static int utt_regex_match(const char *pattern,
                           const char *text,
                           int ignore_case)
{
  if (pattern == 0 || text == 0)
    return 0;
  if (pattern[0] == '^')
    return utt_regex_match_here(pattern + 1, text, ignore_case);
  do {
    if (utt_regex_match_here(pattern, text, ignore_case))
      return 1;
  } while (*text++ != '\0');
  return 0;
}

static int utt_contains_substr(const char *haystack,
                               int hay_len,
                               const char *needle,
                               int needle_len,
                               int ignore_case)
{
  int i;
  int j;

  if (needle_len == 0)
    return 1;
  if (haystack == 0 || needle == 0 || hay_len < needle_len)
    return 0;
  for (i = 0; i + needle_len <= hay_len; i++) {
    for (j = 0; j < needle_len; j++) {
      if (!utt_char_equal(haystack[i + j], needle[j], ignore_case))
        break;
    }
    if (j == needle_len)
      return 1;
  }
  return 0;
}

static int utt_parse_range_list(const char *spec,
                                struct utt_range **ranges_out,
                                int *count_out)
{
  struct utt_range *ranges = 0;
  int count = 0;
  int cap = 0;
  const char *p = spec;

  if (ranges_out == 0 || count_out == 0 || spec == 0 || *spec == '\0')
    return -1;

  while (*p != '\0') {
    long start = 0;
    long end = 0;
    int open_start = 0;
    int open_end = 0;
    struct utt_range *next;
    const char *num_start;

    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == '-') {
      open_start = 1;
      p++;
    } else if (utt_is_digit(*p)) {
      num_start = p;
      while (utt_is_digit(*p))
        p++;
      start = utt_parse_long_substr(num_start, (int)(p - num_start));
    } else {
      free(ranges);
      return -1;
    }

    if (*p == '-') {
      p++;
      if (*p == '\0' || *p == ',') {
        open_end = 1;
      } else if (utt_is_digit(*p)) {
        num_start = p;
        while (utt_is_digit(*p))
          p++;
        end = utt_parse_long_substr(num_start, (int)(p - num_start));
      } else {
        free(ranges);
        return -1;
      }
    } else {
      end = start;
    }

    if (count >= cap) {
      int next_cap = cap > 0 ? cap * 2 : 8;

      next = (struct utt_range *)malloc(sizeof(*ranges) * (size_t)next_cap);
      if (next == 0) {
        free(ranges);
        return -1;
      }
      if (ranges != 0) {
        memcpy(next, ranges, sizeof(*ranges) * (size_t)count);
        free(ranges);
      }
      ranges = next;
      cap = next_cap;
    }

    ranges[count].start = open_start ? 1 : (int)start;
    ranges[count].end = open_end ? 0 : (int)end;
    if (ranges[count].start <= 0)
      ranges[count].start = 1;
    if (ranges[count].end != 0 && ranges[count].end < ranges[count].start)
      ranges[count].end = ranges[count].start;
    count++;
    if (*p == ',')
      p++;
  }

  *ranges_out = ranges;
  *count_out = count;
  return 0;
}

static int utt_range_contains(const struct utt_range *ranges,
                              int range_count,
                              int index1)
{
  int i;

  for (i = 0; i < range_count; i++) {
    if (index1 < ranges[i].start)
      continue;
    if (ranges[i].end == 0 || index1 <= ranges[i].end)
      return 1;
  }
  return 0;
}

static void utt_get_whitespace_field(const char *text, int len,
                                     int field_no,
                                     int *start_out,
                                     int *end_out)
{
  int pos = 0;
  int field = 1;
  int start = len;
  int end = len;

  while (pos < len) {
    while (pos < len && utt_is_space(text[pos]))
      pos++;
    if (pos >= len)
      break;
    if (field == field_no) {
      start = pos;
      while (pos < len && !utt_is_space(text[pos]))
        pos++;
      end = pos;
      break;
    }
    while (pos < len && !utt_is_space(text[pos]))
      pos++;
    field++;
  }

  if (start_out != 0)
    *start_out = start;
  if (end_out != 0)
    *end_out = end;
}

static void utt_get_delim_field(const char *text, int len,
                                char delim,
                                int field_no,
                                int *start_out,
                                int *end_out)
{
  int start = 0;
  int field = 1;
  int pos;

  for (pos = 0; pos <= len; pos++) {
    if (pos != len && text[pos] != delim)
      continue;
    if (field == field_no) {
      if (start_out != 0)
        *start_out = start;
      if (end_out != 0)
        *end_out = pos;
      return;
    }
    start = pos + 1;
    field++;
  }

  if (start_out != 0)
    *start_out = len;
  if (end_out != 0)
    *end_out = len;
}

static void utt_get_sort_key(const struct utt_sort_options *opts,
                             const char *text,
                             int len,
                             const char **key_text_out,
                             int *key_len_out)
{
  int start = 0;
  int end = len;

  if (opts == 0 || opts->key_start <= 0) {
    *key_text_out = text;
    *key_len_out = len;
    return;
  }

  if (opts->has_delim != 0)
    utt_get_delim_field(text, len, opts->delim, opts->key_start, &start, &end);
  else
    utt_get_whitespace_field(text, len, opts->key_start, &start, &end);

  if (opts->key_end > opts->key_start) {
    int end_start = len;
    int end_end = len;

    if (opts->has_delim != 0)
      utt_get_delim_field(text, len, opts->delim, opts->key_end, &end_start, &end_end);
    else
      utt_get_whitespace_field(text, len, opts->key_end, &end_start, &end_end);
    if (end_end > end)
      end = end_end;
  } else if (opts->key_end == 0) {
    end = len;
  }

  if (start > len)
    start = len;
  if (end < start)
    end = start;
  *key_text_out = text + start;
  *key_len_out = end - start;
}

static int utt_key_compare(const struct utt_sort_options *opts,
                           const struct utt_line_ref *a,
                           const struct utt_line_ref *b)
{
  const char *akey;
  const char *bkey;
  int alen;
  int blen;
  int cmp = 0;
  int i;

  utt_get_sort_key(opts, a->text, a->len, &akey, &alen);
  utt_get_sort_key(opts, b->text, b->len, &bkey, &blen);

  if (opts != 0 && opts->numeric != 0) {
    long av = utt_parse_long_substr(akey, alen);
    long bv = utt_parse_long_substr(bkey, blen);

    if (av < bv)
      cmp = -1;
    else if (av > bv)
      cmp = 1;
    else
      cmp = 0;
  } else {
    int common = alen < blen ? alen : blen;

    for (i = 0; i < common; i++) {
      if (akey[i] < bkey[i]) {
        cmp = -1;
        break;
      }
      if (akey[i] > bkey[i]) {
        cmp = 1;
        break;
      }
    }
    if (cmp == 0) {
      if (alen < blen)
        cmp = -1;
      else if (alen > blen)
        cmp = 1;
    }
  }

  if (cmp == 0) {
    int common = a->len < b->len ? a->len : b->len;

    for (i = 0; i < common; i++) {
      if (a->text[i] < b->text[i]) {
        cmp = -1;
        break;
      }
      if (a->text[i] > b->text[i]) {
        cmp = 1;
        break;
      }
    }
    if (cmp == 0) {
      if (a->len < b->len)
        cmp = -1;
      else if (a->len > b->len)
        cmp = 1;
    }
  }

  if (opts != 0 && opts->reverse != 0)
    cmp = -cmp;
  return cmp;
}

static void utt_sort_lines(struct utt_line_ref *refs,
                           int count,
                           const struct utt_sort_options *opts)
{
  int i;

  for (i = 1; i < count; i++) {
    struct utt_line_ref tmp = refs[i];
    int j = i - 1;

    while (j >= 0 && utt_key_compare(opts, &refs[j], &tmp) > 0) {
      refs[j + 1] = refs[j];
      j--;
    }
    refs[j + 1] = tmp;
  }
}

static int utt_line_equal(const struct utt_line_ref *a,
                          const struct utt_line_ref *b)
{
  if (a->len != b->len)
    return 0;
  return memcmp(a->text, b->text, (size_t)a->len) == 0;
}

static int utt_print_line_ref(const struct utt_line_ref *line)
{
  utt_write_raw(STDOUT_FILENO, line->text, line->len);
  utt_write_text(STDOUT_FILENO, "\n");
  return 0;
}

static int utt_is_dir_path(const char *path)
{
#ifdef TEST_BUILD
  struct stat st;

  if (stat(path, &st) < 0)
    return 0;
  return S_ISDIR(st.st_mode);
#else
  int fd = open(path, O_RDONLY | O_DIRECTORY, 0);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
#endif
}

static int utt_path_basename(const char *path)
{
  int len = utt_strlen_int(path);
  int pos = len - 1;

  while (pos >= 0 && path[pos] == '/')
    pos--;
  while (pos >= 0 && path[pos] != '/')
    pos--;
  return pos + 1;
}

static int utt_find_name_match(const char *path, const struct utt_find_options *opts)
{
  const char *base;

  if (opts->name_pattern[0] == '\0')
    return 1;
  base = path + utt_path_basename(path);
  return utt_wildcard_match(opts->name_pattern, base);
}

static int utt_find_type_match(int is_dir, const struct utt_find_options *opts)
{
  if (opts->type_filter == '\0')
    return 1;
  if (opts->type_filter == 'd')
    return is_dir != 0;
  if (opts->type_filter == 'f')
    return is_dir == 0;
  return 0;
}

static int utt_find_depth_match(int depth, const struct utt_find_options *opts)
{
  if (depth < opts->mindepth)
    return 0;
  if (opts->has_maxdepth != 0 && depth > opts->maxdepth)
    return 0;
  return 1;
}

static int utt_path_join(char *buf, int cap, const char *left, const char *right)
{
  int len = 0;
  int left_len = utt_strlen_int(left);

  if (buf == 0 || cap <= 1 || left == 0 || right == 0)
    return -1;
  buf[0] = '\0';
  if (left_len == 0)
    left = ".";
  if (utt_strlen_int(left) >= cap - 1)
    return -1;
  strcpy(buf, left);
  len = utt_strlen_int(buf);
  if (len > 0 && buf[len - 1] != '/') {
    if (len + 1 >= cap)
      return -1;
    buf[len++] = '/';
    buf[len] = '\0';
  }
  if (len + utt_strlen_int(right) >= cap)
    return -1;
  strcat(buf, right);
  return 0;
}

static int utt_find_walk(const char *path,
                         int depth,
                         const struct utt_find_options *opts);

static int utt_find_walk_dir(const char *path,
                             int depth,
                             const struct utt_find_options *opts)
{
#ifdef TEST_BUILD
  DIR *dirp;
  struct dirent *de;

  dirp = opendir(path);
  if (dirp == 0)
    return utt_print_error("find", "open failed", path);
  while ((de = readdir(dirp)) != 0) {
    char child[UTT_PATH_MAX];

    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    if (utt_path_join(child, sizeof(child), path, de->d_name) < 0)
      continue;
    if (utt_find_walk(child, depth + 1, opts) != 0) {
      closedir(dirp);
      return 1;
    }
  }
  closedir(dirp);
#else
  struct {
    unsigned int inode;
    unsigned short rec_len;
    unsigned char name_len;
    unsigned char file_type;
    char name[255];
  } *de;
  char *data = 0;
  int len = 0;
  int offset = 0;

  if (utt_read_path_all(path, &data, &len) < 0)
    return utt_print_error("find", "open failed", path);
  while (offset < len) {
    char name[256];
    char child[UTT_PATH_MAX];
    int nlen;

    de = (void *)(data + offset);
    if (de->rec_len == 0 || offset + de->rec_len > len)
      break;
    if (de->inode == 0 || de->name_len == 0) {
      offset += de->rec_len;
      continue;
    }
    nlen = (int)de->name_len;
    if (nlen >= (int)sizeof(name))
      nlen = (int)sizeof(name) - 1;
    memcpy(name, de->name, (size_t)nlen);
    name[nlen] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      offset += de->rec_len;
      continue;
    }
    if (utt_path_join(child, sizeof(child), path, name) == 0) {
      if (utt_find_walk(child, depth + 1, opts) != 0) {
        free(data);
        return 1;
      }
    }
    offset += de->rec_len;
  }
  free(data);
#endif
  return 0;
}

static int utt_find_walk(const char *path,
                         int depth,
                         const struct utt_find_options *opts)
{
  int is_dir = utt_is_dir_path(path);

  if (utt_find_depth_match(depth, opts) &&
      utt_find_name_match(path, opts) &&
      utt_find_type_match(is_dir, opts)) {
    utt_write_text(STDOUT_FILENO, path);
    utt_write_text(STDOUT_FILENO, "\n");
  }

  if (!is_dir)
    return 0;
  if (opts->has_maxdepth != 0 && depth >= opts->maxdepth)
    return 0;
  return utt_find_walk_dir(path, depth, opts);
}

static int utt_match_grep_patterns(const struct utt_grep_options *opts,
                                   const char *line,
                                   int len)
{
  int i;

  for (i = 0; i < opts->pattern_count; i++) {
    int matched;
    int plen = utt_strlen_int(opts->patterns[i]);

    if (opts->fixed != 0)
      matched = utt_contains_substr(line, len, opts->patterns[i], plen,
                                    opts->ignore_case);
    else
      matched = utt_regex_match(opts->patterns[i], line, opts->ignore_case);
    if (matched != 0)
      return opts->invert != 0 ? 0 : 1;
  }
  return opts->invert != 0 ? 1 : 0;
}

static int utt_grep_add_pattern(struct utt_grep_options *opts, const char *pattern)
{
  char **next;
  int j;

  next = (char **)malloc(sizeof(char *) * (size_t)(opts->pattern_count + 1));
  if (next == 0)
    return -1;
  for (j = 0; j < opts->pattern_count; j++)
    next[j] = opts->patterns[j];
  next[opts->pattern_count] = (char *)pattern;
  if (opts->patterns != 0)
    free(opts->patterns);
  opts->patterns = next;
  opts->pattern_count++;
  return 0;
}

static int utt_uniq_key_start(const struct utt_line_ref *line,
                              const struct utt_uniq_options *opts)
{
  int pos = 0;
  int field;

  for (field = 0; field < opts->skip_fields && pos < line->len; field++) {
    while (pos < line->len && utt_is_space(line->text[pos]))
      pos++;
    while (pos < line->len && !utt_is_space(line->text[pos]))
      pos++;
  }
  while (pos < line->len && utt_is_space(line->text[pos]))
    pos++;
  pos += utt_advance_chars(line->text + pos, line->len - pos, opts->skip_chars);
  if (pos > line->len)
    pos = line->len;
  return pos;
}

static int utt_uniq_lines_equal(const struct utt_line_ref *a,
                                const struct utt_line_ref *b,
                                const struct utt_uniq_options *opts)
{
  int astart = utt_uniq_key_start(a, opts);
  int bstart = utt_uniq_key_start(b, opts);
  int alen = a->len - astart;
  int blen = b->len - bstart;

  if (alen != blen)
    return 0;
  return memcmp(a->text + astart, b->text + bstart, (size_t)alen) == 0;
}

static int utt_expand_tr_class(const char *name,
                               unsigned char *out,
                               int *len_io)
{
  int len = *len_io;
  int ch;

  if (strcmp(name, "lower") == 0) {
    for (ch = 'a'; ch <= 'z'; ch++)
      out[len++] = (unsigned char)ch;
  } else if (strcmp(name, "upper") == 0) {
    for (ch = 'A'; ch <= 'Z'; ch++)
      out[len++] = (unsigned char)ch;
  } else if (strcmp(name, "digit") == 0) {
    for (ch = '0'; ch <= '9'; ch++)
      out[len++] = (unsigned char)ch;
  } else if (strcmp(name, "space") == 0) {
    out[len++] = ' ';
    out[len++] = '\t';
    out[len++] = '\n';
    out[len++] = '\r';
    out[len++] = '\f';
    out[len++] = '\v';
  } else if (strcmp(name, "alpha") == 0) {
    for (ch = 'A'; ch <= 'Z'; ch++)
      out[len++] = (unsigned char)ch;
    for (ch = 'a'; ch <= 'z'; ch++)
      out[len++] = (unsigned char)ch;
  } else {
    return -1;
  }
  *len_io = len;
  return 0;
}

static unsigned char utt_parse_escape(const char **pp)
{
  const char *p = *pp;

  if (*p != '\\')
    return (unsigned char)*p;
  p++;
  if (*p == 'n') {
    *pp = p;
    return (unsigned char)'\n';
  }
  if (*p == 't') {
    *pp = p;
    return (unsigned char)'\t';
  }
  if (*p == 'r') {
    *pp = p;
    return (unsigned char)'\r';
  }
  if (*p == '0') {
    *pp = p;
    return (unsigned char)'\0';
  }
  *pp = p;
  return (unsigned char)*p;
}

static int utt_expand_tr_set(const char *spec,
                             unsigned char *out,
                             int *len_out)
{
  const char *p = spec;
  int len = 0;

  if (spec == 0 || out == 0 || len_out == 0)
    return -1;
  while (*p != '\0') {
    unsigned char start;

    if (p[0] == '[' && p[1] == ':' ) {
      char name[32];
      int pos = 0;

      p += 2;
      while (*p != '\0' &&
             !(*p == ':' && p[1] == ']') &&
             pos < (int)sizeof(name) - 1) {
        name[pos++] = *p++;
      }
      name[pos] = '\0';
      if (*p != ':' || p[1] != ']')
        return -1;
      if (utt_expand_tr_class(name, out, &len) < 0)
        return -1;
      p += 2;
      continue;
    }

    start = utt_parse_escape(&p);
    if (p[1] == '-' && p[2] != '\0') {
      unsigned char end;
      int ch;

      p += 2;
      end = utt_parse_escape(&p);
      if (start <= end) {
        for (ch = (int)start; ch <= (int)end; ch++)
          out[len++] = (unsigned char)ch;
      } else {
        for (ch = (int)start; ch >= (int)end; ch--)
          out[len++] = (unsigned char)ch;
      }
    } else {
      out[len++] = start;
    }
    if (*p != '\0')
      p++;
  }

  *len_out = len;
  return 0;
}

static void utt_fill_set_table(unsigned char table[256],
                               const unsigned char *items,
                               int count)
{
  int i;

  memset(table, 0, 256);
  for (i = 0; i < count; i++)
    table[items[i]] = 1;
}

static int utt_diff_equal_lines(struct utt_loaded_text *a,
                                struct utt_loaded_text *b)
{
  int i;

  if (a->line_count != b->line_count)
    return 0;
  for (i = 0; i < a->line_count; i++) {
    if (a->lines[i].len != b->lines[i].len)
      return 0;
    if (memcmp(a->lines[i].text, b->lines[i].text,
               (size_t)a->lines[i].len) != 0)
      return 0;
  }
  return 1;
}

int unix_find_main(int argc, char **argv)
{
  struct utt_find_options opts;
  char *paths[32];
  int path_count = 0;
  int i;

  memset(&opts, 0, sizeof(opts));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if ((strcmp(argv[i], "-name") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "name", &value)) {
      const char *pattern = value != 0 ? value : argv[++i];
      strcpy(opts.name_pattern, pattern);
    } else if ((strcmp(argv[i], "-type") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "type", &value)) {
      const char *type = value != 0 ? value : argv[++i];
      opts.type_filter = type[0];
    } else if ((strcmp(argv[i], "-maxdepth") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "maxdepth", &value)) {
      const char *depth = value != 0 ? value : argv[++i];
      opts.maxdepth = atoi(depth);
      opts.has_maxdepth = 1;
    } else if ((strcmp(argv[i], "-mindepth") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "mindepth", &value)) {
      const char *depth = value != 0 ? value : argv[++i];
      opts.mindepth = atoi(depth);
    } else if (strcmp(argv[i], "-print") == 0 ||
               utt_match_long_option(argv[i], "print", 0)) {
      continue;
    } else if (argv[i][0] == '-') {
      return utt_print_error("find", "unsupported option", argv[i]);
    } else {
      if (path_count < (int)(sizeof(paths) / sizeof(paths[0])))
        paths[path_count++] = argv[i];
    }
  }

  for (; i < argc; i++) {
    if (path_count < (int)(sizeof(paths) / sizeof(paths[0])))
      paths[path_count++] = argv[i];
  }

  if (path_count == 0) {
    char *default_path = ".";
    return utt_find_walk(default_path, 0, &opts);
  }

  for (i = 0; i < path_count; i++) {
    if (utt_find_walk(paths[i], 0, &opts) != 0)
      return 1;
  }
  return 0;
}

int unix_sort_main(int argc, char **argv)
{
  struct utt_sort_options opts;
  struct utt_loaded_text *texts = 0;
  struct utt_line_ref *refs = 0;
  int text_count = 0;
  int ref_count = 0;
  int i;
  int file_start = argc;
  int out_fd = STDOUT_FILENO;
  int saved_stdout = -1;

  memset(&opts, 0, sizeof(opts));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    }
    if (strcmp(argv[i], "-n") == 0) {
      opts.numeric = 1;
    } else if (utt_match_long_option(argv[i], "numeric-sort", 0)) {
      opts.numeric = 1;
    } else if (strcmp(argv[i], "-r") == 0) {
      opts.reverse = 1;
    } else if (utt_match_long_option(argv[i], "reverse", 0)) {
      opts.reverse = 1;
    } else if (strcmp(argv[i], "-u") == 0) {
      opts.unique = 1;
    } else if (utt_match_long_option(argv[i], "unique", 0)) {
      opts.unique = 1;
    } else if ((strcmp(argv[i], "-o") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "output", &value)) {
      opts.output_path = value != 0 ? value : argv[++i];
    } else if ((strcmp(argv[i], "-t") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "field-separator", &value)) {
      const char *delim = value != 0 ? value : argv[++i];
      opts.has_delim = 1;
      opts.delim = delim[0];
    } else if ((strcmp(argv[i], "-k") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "key", &value)) {
      const char *spec = value != 0 ? value : argv[++i];
      opts.key_start = atoi(spec);
      while (*spec != '\0' && *spec != ',')
        spec++;
      if (*spec == ',')
        opts.key_end = atoi(spec + 1);
    } else if (argv[i][0] == '-') {
      return utt_print_error("sort", "unsupported option", argv[i]);
    } else {
      file_start = i;
      break;
    }
  }

  if (utt_collect_input_texts(argv + file_start, argc - file_start,
                              &texts, &text_count) < 0)
    return utt_print_error("sort", "read failed", "");
  if (utt_collect_line_refs(texts, text_count, &refs, &ref_count) < 0) {
    utt_free_texts(texts, text_count);
    return utt_print_error("sort", "out of memory", "");
  }

  utt_sort_lines(refs, ref_count, &opts);

  if (opts.output_path != 0) {
    out_fd = open(opts.output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
      free(refs);
      utt_free_texts(texts, text_count);
      return utt_print_error("sort", "open failed", opts.output_path);
    }
    saved_stdout = dup(STDOUT_FILENO);
    close(STDOUT_FILENO);
    dup(out_fd);
    close(out_fd);
  }

  for (i = 0; i < ref_count; i++) {
    if (opts.unique != 0 && i > 0 &&
        utt_key_compare(&opts, &refs[i - 1], &refs[i]) == 0)
      continue;
    utt_print_line_ref(&refs[i]);
  }

  if (saved_stdout >= 0) {
    close(STDOUT_FILENO);
    dup(saved_stdout);
    close(saved_stdout);
  }

  free(refs);
  utt_free_texts(texts, text_count);
  return 0;
}

int unix_uniq_main(int argc, char **argv)
{
  struct utt_uniq_options opts;
  struct utt_loaded_text *texts = 0;
  struct utt_line_ref *refs = 0;
  int text_count = 0;
  int ref_count = 0;
  int i;

  memset(&opts, 0, sizeof(opts));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    }
    if (strcmp(argv[i], "-c") == 0) {
      opts.show_count = 1;
    } else if (utt_match_long_option(argv[i], "count", 0)) {
      opts.show_count = 1;
    } else if (strcmp(argv[i], "-d") == 0) {
      opts.only_repeated = 1;
    } else if (utt_match_long_option(argv[i], "repeated", 0)) {
      opts.only_repeated = 1;
    } else if (strcmp(argv[i], "-u") == 0) {
      opts.only_unique = 1;
    } else if (utt_match_long_option(argv[i], "unique", 0)) {
      opts.only_unique = 1;
    } else if ((strcmp(argv[i], "-f") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "skip-fields", &value)) {
      const char *count = value != 0 ? value : argv[++i];
      opts.skip_fields = atoi(count);
    } else if ((strcmp(argv[i], "-s") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "skip-chars", &value)) {
      const char *count = value != 0 ? value : argv[++i];
      opts.skip_chars = atoi(count);
    } else if (argv[i][0] == '-') {
      return utt_print_error("uniq", "unsupported option", argv[i]);
    } else {
      break;
    }
  }

  if (utt_collect_input_texts(argv + i, argc - i, &texts, &text_count) < 0)
    return utt_print_error("uniq", "read failed", "");
  if (utt_collect_line_refs(texts, text_count, &refs, &ref_count) < 0) {
    utt_free_texts(texts, text_count);
    return utt_print_error("uniq", "out of memory", "");
  }

  i = 0;
  while (i < ref_count) {
    int run = 1;
    int j = i + 1;

    while (j < ref_count) {
      if (utt_uniq_lines_equal(&refs[i], &refs[j], &opts) == 0)
        break;
      run++;
      j++;
    }

    if ((opts.only_repeated != 0 && run < 2) ||
        (opts.only_unique != 0 && run > 1)) {
      i = j;
      continue;
    }

    if (opts.show_count != 0) {
      char buf[32];
      int len = snprintf(buf, sizeof(buf), "%d ", run);
      utt_write_raw(STDOUT_FILENO, buf, len);
    }
    utt_print_line_ref(&refs[i]);
    i = j;
  }

  free(refs);
  utt_free_texts(texts, text_count);
  return 0;
}

int unix_wc_main(int argc, char **argv)
{
  int show_lines = 0;
  int show_words = 0;
  int show_bytes = 0;
  int i;
  int file_start = argc;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  struct utt_wc_counts total;

  memset(&total, 0, sizeof(total));
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    }
    if (strcmp(argv[i], "-l") == 0) {
      show_lines = 1;
    } else if (utt_match_long_option(argv[i], "lines", 0)) {
      show_lines = 1;
    } else if (strcmp(argv[i], "-w") == 0) {
      show_words = 1;
    } else if (utt_match_long_option(argv[i], "words", 0)) {
      show_words = 1;
    } else if (strcmp(argv[i], "-c") == 0) {
      show_bytes = 1;
    } else if (utt_match_long_option(argv[i], "bytes", 0)) {
      show_bytes = 1;
    } else if (argv[i][0] == '-') {
      return utt_print_error("wc", "unsupported option", argv[i]);
    } else {
      file_start = i;
      break;
    }
  }
  if (show_lines == 0 && show_words == 0 && show_bytes == 0) {
    show_lines = 1;
    show_words = 1;
    show_bytes = 1;
  }

  if (utt_collect_input_texts(argv + file_start, argc - file_start,
                              &texts, &text_count) < 0)
    return utt_print_error("wc", "read failed", "");

  for (i = 0; i < text_count; i++) {
    struct utt_wc_counts counts;
    int pos;
    int in_word = 0;
    char buf[128];
    int len = 0;

    memset(&counts, 0, sizeof(counts));
    counts.lines = utt_count_newlines(texts[i].raw_data, texts[i].len);
    counts.bytes = texts[i].len;
    for (pos = 0; pos < texts[i].len; pos++) {
      if (utt_is_space(texts[i].raw_data[pos])) {
        in_word = 0;
      } else if (in_word == 0) {
        counts.words++;
        in_word = 1;
      }
    }

    if (show_lines) {
      len = utt_buf_append_long(buf, sizeof(buf), len, counts.lines);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " ");
    }
    if (show_words) {
      len = utt_buf_append_long(buf, sizeof(buf), len, counts.words);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " ");
    }
    if (show_bytes) {
      len = utt_buf_append_long(buf, sizeof(buf), len, counts.bytes);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " ");
    }
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%s\n", texts[i].name);
    utt_write_raw(STDOUT_FILENO, buf, len);

    total.lines += counts.lines;
    total.words += counts.words;
    total.bytes += counts.bytes;
  }

  if (text_count > 1) {
    char buf[128];
    int len = 0;

    if (show_lines) {
      len = utt_buf_append_long(buf, sizeof(buf), len, total.lines);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " ");
    }
    if (show_words) {
      len = utt_buf_append_long(buf, sizeof(buf), len, total.words);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " ");
    }
    if (show_bytes) {
      len = utt_buf_append_long(buf, sizeof(buf), len, total.bytes);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " ");
    }
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "total\n");
    utt_write_raw(STDOUT_FILENO, buf, len);
  }

  utt_free_texts(texts, text_count);
  return 0;
}

static void utt_print_header_if_needed(const char *name,
                                       int index,
                                       int total,
                                       int quiet,
                                       int verbose)
{
  if (quiet != 0)
    return;
  if (verbose == 0 && total <= 1)
    return;
  if (index > 0)
    utt_write_text(STDOUT_FILENO, "\n");
  utt_write_text(STDOUT_FILENO, "==> ");
  utt_write_text(STDOUT_FILENO, name);
  utt_write_text(STDOUT_FILENO, " <==\n");
}

int unix_head_main(int argc, char **argv)
{
  long count_lines = 10;
  long count_bytes = -1;
  int lines_all_but_last = 0;
  int bytes_all_but_last = 0;
  int quiet_headers = 0;
  int verbose_headers = 0;
  int i;
  int file_start = argc;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;

  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    } else if (strcmp(argv[i], "-q") == 0 ||
               utt_match_long_option(argv[i], "quiet", 0) ||
               utt_match_long_option(argv[i], "silent", 0)) {
      quiet_headers = 1;
      verbose_headers = 0;
    } else if (strcmp(argv[i], "-v") == 0 ||
               utt_match_long_option(argv[i], "verbose", 0)) {
      verbose_headers = 1;
      quiet_headers = 0;
    } else if ((strcmp(argv[i], "-n") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "lines", &value)) {
      if (utt_parse_head_count_spec(value != 0 ? value : argv[++i],
                                    &count_lines,
                                    &lines_all_but_last) < 0)
        return utt_print_error("head", "bad count", value != 0 ? value : argv[i]);
      count_bytes = -1;
      bytes_all_but_last = 0;
    } else if ((strcmp(argv[i], "-c") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "bytes", &value)) {
      if (utt_parse_head_count_spec(value != 0 ? value : argv[++i],
                                    &count_bytes,
                                    &bytes_all_but_last) < 0)
        return utt_print_error("head", "bad count", value != 0 ? value : argv[i]);
    } else if (i == 1 && argv[i][0] == '-' && utt_is_digit(argv[i][1])) {
      if (utt_parse_head_count_spec(argv[i] + 1,
                                    &count_lines,
                                    &lines_all_but_last) < 0)
        return utt_print_error("head", "bad count", argv[i]);
      count_bytes = -1;
      bytes_all_but_last = 0;
    } else if (argv[i][0] == '-') {
      return utt_print_error("head", "unsupported option", argv[i]);
    } else {
      file_start = i;
      break;
    }
  }

  if (utt_collect_input_texts(argv + file_start, argc - file_start,
                              &texts, &text_count) < 0)
    return utt_print_error("head", "read failed", "");

  for (i = 0; i < text_count; i++) {
    int j;

    utt_print_header_if_needed(texts[i].name, i, text_count,
                               quiet_headers, verbose_headers);
    if (count_bytes >= 0) {
      int bytes = 0;

      if (bytes_all_but_last != 0)
        bytes = texts[i].len - (int)count_bytes;
      else if (count_bytes < texts[i].len)
        bytes = (int)count_bytes;
      else
        bytes = texts[i].len;
      if (bytes < 0)
        bytes = 0;
      utt_write_raw(STDOUT_FILENO, texts[i].raw_data, bytes);
      if (bytes > 0 && texts[i].raw_data[bytes - 1] != '\n')
        utt_write_text(STDOUT_FILENO, "\n");
      continue;
    }
    {
      long line_limit = count_lines;

      if (lines_all_but_last != 0)
        line_limit = texts[i].line_count - line_limit;
      if (line_limit < 0)
        line_limit = 0;
      for (j = 0; j < texts[i].line_count && j < line_limit; j++)
        utt_print_line_ref((struct utt_line_ref *)&texts[i].lines[j]);
    }
  }

  utt_free_texts(texts, text_count);
  return 0;
}

int unix_tail_main(int argc, char **argv)
{
  long count_lines = 10;
  long count_bytes = -1;
  int lines_from_start = 0;
  int bytes_from_start = 0;
  int quiet_headers = 0;
  int verbose_headers = 0;
  int i;
  int file_start = argc;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;

  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    } else if (strcmp(argv[i], "-q") == 0 ||
               utt_match_long_option(argv[i], "quiet", 0) ||
               utt_match_long_option(argv[i], "silent", 0)) {
      quiet_headers = 1;
      verbose_headers = 0;
    } else if (strcmp(argv[i], "-v") == 0 ||
               utt_match_long_option(argv[i], "verbose", 0)) {
      verbose_headers = 1;
      quiet_headers = 0;
    } else if ((strcmp(argv[i], "-n") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "lines", &value)) {
      if (utt_parse_tail_count_spec(value != 0 ? value : argv[++i],
                                    &count_lines,
                                    &lines_from_start) < 0)
        return utt_print_error("tail", "bad count", value != 0 ? value : argv[i]);
      count_bytes = -1;
      bytes_from_start = 0;
    } else if ((strcmp(argv[i], "-c") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "bytes", &value)) {
      if (utt_parse_tail_count_spec(value != 0 ? value : argv[++i],
                                    &count_bytes,
                                    &bytes_from_start) < 0)
        return utt_print_error("tail", "bad count", value != 0 ? value : argv[i]);
    } else if (i == 1 &&
               (argv[i][0] == '+' || argv[i][0] == '-') &&
               utt_is_digit(argv[i][1])) {
      if (utt_parse_tail_count_spec(argv[i],
                                    &count_lines,
                                    &lines_from_start) < 0)
        return utt_print_error("tail", "bad count", argv[i]);
      count_bytes = -1;
      bytes_from_start = 0;
    } else if (argv[i][0] == '-') {
      return utt_print_error("tail", "unsupported option", argv[i]);
    } else {
      file_start = i;
      break;
    }
  }

  if (utt_collect_input_texts(argv + file_start, argc - file_start,
                              &texts, &text_count) < 0)
    return utt_print_error("tail", "read failed", "");

  for (i = 0; i < text_count; i++) {
    int j;
    int start;

    utt_print_header_if_needed(texts[i].name, i, text_count,
                               quiet_headers, verbose_headers);
    if (count_bytes >= 0) {
      int start_byte;

      if (bytes_from_start != 0)
        start_byte = count_bytes <= 1 ? 0 : (int)count_bytes - 1;
      else
        start_byte = texts[i].len - (int)count_bytes;
      if (start_byte < 0)
        start_byte = 0;
      if (start_byte > texts[i].len)
        start_byte = texts[i].len;
      utt_write_raw(STDOUT_FILENO, texts[i].raw_data + start_byte,
                    texts[i].len - start_byte);
      if (texts[i].len > 0 && texts[i].raw_data[texts[i].len - 1] != '\n')
        utt_write_text(STDOUT_FILENO, "\n");
      continue;
    }
    if (lines_from_start != 0)
      start = count_lines <= 1 ? 0 : (int)count_lines - 1;
    else
      start = texts[i].line_count - (int)count_lines;
    if (start < 0)
      start = 0;
    if (start > texts[i].line_count)
      start = texts[i].line_count;
    for (j = start; j < texts[i].line_count; j++)
      utt_print_line_ref((struct utt_line_ref *)&texts[i].lines[j]);
  }

  utt_free_texts(texts, text_count);
  return 0;
}

int unix_grep_main(int argc, char **argv)
{
  struct utt_grep_options opts;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  int i;
  int file_start = argc;
  int selected_any = 0;

  memset(&opts, 0, sizeof(opts));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    }
    if (strcmp(argv[i], "-F") == 0) {
      opts.fixed = 1;
    } else if (utt_match_long_option(argv[i], "fixed-strings", 0)) {
      opts.fixed = 1;
    } else if (strcmp(argv[i], "-i") == 0) {
      opts.ignore_case = 1;
    } else if (utt_match_long_option(argv[i], "ignore-case", 0)) {
      opts.ignore_case = 1;
    } else if (strcmp(argv[i], "-v") == 0) {
      opts.invert = 1;
    } else if (utt_match_long_option(argv[i], "invert-match", 0)) {
      opts.invert = 1;
    } else if (strcmp(argv[i], "-n") == 0) {
      opts.show_line_numbers = 1;
    } else if (utt_match_long_option(argv[i], "line-number", 0)) {
      opts.show_line_numbers = 1;
    } else if (strcmp(argv[i], "-c") == 0) {
      opts.count_only = 1;
    } else if (utt_match_long_option(argv[i], "count", 0)) {
      opts.count_only = 1;
    } else if (strcmp(argv[i], "-q") == 0) {
      opts.quiet = 1;
    } else if (utt_match_long_option(argv[i], "quiet", 0)) {
      opts.quiet = 1;
    } else if ((strcmp(argv[i], "-e") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "regexp", &value)) {
      const char *pattern = value != 0 ? value : argv[++i];

      if (utt_grep_add_pattern(&opts, pattern) < 0)
        return utt_print_error("grep", "out of memory", "");
    } else if (argv[i][0] == '-') {
      return utt_print_error("grep", "unsupported option", argv[i]);
    } else {
      if (opts.pattern_count == 0) {
        if (utt_grep_add_pattern(&opts, argv[i]) < 0)
          return utt_print_error("grep", "out of memory", "");
        file_start = i + 1;
      } else {
        file_start = i;
      }
      break;
    }
  }

  if (opts.pattern_count == 0)
    return utt_print_error("grep", "missing pattern", "");
  if (utt_collect_input_texts(argv + file_start, argc - file_start,
                              &texts, &text_count) < 0) {
    free(opts.patterns);
    return utt_print_error("grep", "read failed", "");
  }

  for (i = 0; i < text_count; i++) {
    int j;
    int match_count = 0;

    for (j = 0; j < texts[i].line_count; j++) {
      int matched = utt_match_grep_patterns(&opts,
                                            texts[i].lines[j].text,
                                            texts[i].lines[j].len);

      if (matched == 0)
        continue;
      selected_any = 1;
      match_count++;
      if (opts.quiet != 0)
        continue;
      if (opts.count_only != 0)
        continue;
      if (text_count > 1) {
        utt_write_text(STDOUT_FILENO, texts[i].name);
        utt_write_text(STDOUT_FILENO, ":");
      }
      if (opts.show_line_numbers != 0) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%d:", j + 1);
        utt_write_raw(STDOUT_FILENO, buf, len);
      }
      utt_write_raw(STDOUT_FILENO, texts[i].lines[j].text, texts[i].lines[j].len);
      utt_write_text(STDOUT_FILENO, "\n");
    }

    if (opts.count_only != 0 && opts.quiet == 0) {
      char buf[64];
      int len = 0;

      if (text_count > 1)
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%s:", texts[i].name);
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%d\n", match_count);
      utt_write_raw(STDOUT_FILENO, buf, len);
    }
  }

  free(opts.patterns);
  utt_free_texts(texts, text_count);
  return selected_any ? 0 : 1;
}

int unix_cut_main(int argc, char **argv)
{
  struct utt_cut_options opts;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  int i;

  memset(&opts, 0, sizeof(opts));
  opts.delim = '\t';
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if ((strcmp(argv[i], "-c") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "characters", &value)) {
      const char *list = value != 0 ? value : argv[++i];
      opts.mode_chars = 1;
      if (utt_parse_range_list(list, &opts.ranges, &opts.range_count) < 0)
        return utt_print_error("cut", "bad list", list);
    } else if ((strcmp(argv[i], "-f") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "fields", &value)) {
      const char *list = value != 0 ? value : argv[++i];
      opts.mode_fields = 1;
      if (utt_parse_range_list(list, &opts.ranges, &opts.range_count) < 0)
        return utt_print_error("cut", "bad list", list);
    } else if ((strcmp(argv[i], "-d") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "delimiter", &value)) {
      const char *delim = value != 0 ? value : argv[++i];
      opts.delim = delim[0];
    } else if (strcmp(argv[i], "-s") == 0) {
      opts.suppress_no_delim = 1;
    } else if (utt_match_long_option(argv[i], "only-delimited", 0)) {
      opts.suppress_no_delim = 1;
    } else if (utt_match_long_option(argv[i], "complement", 0)) {
      opts.complement = 1;
    } else if (argv[i][0] == '-') {
      return utt_print_error("cut", "unsupported option", argv[i]);
    } else {
      break;
    }
  }

  if ((opts.mode_chars == 0 && opts.mode_fields == 0) ||
      (opts.mode_chars != 0 && opts.mode_fields != 0))
    return utt_print_error("cut", "missing -c or -f", "");
  if (utt_collect_input_texts(argv + i, argc - i, &texts, &text_count) < 0) {
    free(opts.ranges);
    return utt_print_error("cut", "read failed", "");
  }

  for (i = 0; i < text_count; i++) {
    int j;

    for (j = 0; j < texts[i].line_count; j++) {
      struct utt_string out;

      utt_string_init(&out);
      if (opts.mode_chars != 0) {
        int pos = 0;
        int char_index = 1;

        while (pos < texts[i].lines[j].len) {
          int next = utt_next_char_end(texts[i].lines[j].text,
                                       texts[i].lines[j].len, pos);
          int selected = utt_range_contains(opts.ranges, opts.range_count,
                                            char_index);

          if (opts.complement != 0)
            selected = !selected;
          if (selected)
            utt_string_append_len(&out, texts[i].lines[j].text + pos, next - pos);
          pos = next;
          char_index++;
        }
      } else {
        int start = 0;
        int pos2;
        int field = 1;
        int saw_delim = 0;
        int first_written = 1;

        for (pos2 = 0; pos2 <= texts[i].lines[j].len; pos2++) {
          if (pos2 != texts[i].lines[j].len && texts[i].lines[j].text[pos2] != opts.delim)
            continue;
          if (pos2 != texts[i].lines[j].len)
            saw_delim = 1;
          {
            int selected = utt_range_contains(opts.ranges, opts.range_count, field);

            if (opts.complement != 0)
              selected = !selected;
            if (selected) {
            if (!first_written)
              utt_string_append_char(&out, opts.delim);
            utt_string_append_len(&out, texts[i].lines[j].text + start, pos2 - start);
            first_written = 0;
            }
          }
          start = pos2 + 1;
          field++;
        }
        if (!saw_delim) {
          if (opts.suppress_no_delim != 0) {
            utt_string_free(&out);
            continue;
          }
          utt_string_reset(&out);
          utt_string_append_len(&out, texts[i].lines[j].text, texts[i].lines[j].len);
        }
      }

      utt_write_raw(STDOUT_FILENO, out.data != 0 ? out.data : "", out.len);
      utt_write_text(STDOUT_FILENO, "\n");
      utt_string_free(&out);
    }
  }

  free(opts.ranges);
  utt_free_texts(texts, text_count);
  return 0;
}

int unix_tr_main(int argc, char **argv)
{
  int delete_mode = 0;
  int squeeze_mode = 0;
  int complement = 0;
  const char *set1_spec = 0;
  const char *set2_spec = 0;
  unsigned char set1_items[256];
  unsigned char set2_items[256];
  unsigned char set1_table[256];
  unsigned char squeeze_table[256];
  int set1_len = 0;
  int set2_len = 0;
  int i;
  int prev_valid = 0;
  unsigned char prev_out = 0;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if (strcmp(argv[i], "-d") == 0 ||
               utt_match_long_option(argv[i], "delete", 0))
      delete_mode = 1;
    else if (strcmp(argv[i], "-s") == 0 ||
             utt_match_long_option(argv[i], "squeeze-repeats", 0))
      squeeze_mode = 1;
    else if (strcmp(argv[i], "-c") == 0 ||
             utt_match_long_option(argv[i], "complement", 0))
      complement = 1;
    else if (argv[i][0] == '-')
      return utt_print_error("tr", "unsupported option", argv[i]);
    else
      break;
  }

  if (i >= argc)
    return utt_print_error("tr", "missing string1", "");
  set1_spec = argv[i++];
  if (delete_mode == 0 && squeeze_mode == 0) {
    if (i >= argc)
      return utt_print_error("tr", "missing string2", "");
  }
  if (i < argc)
    set2_spec = argv[i++];
  if (i < argc)
    return utt_print_error("tr", "too many operands", argv[i]);

  if (delete_mode != 0 && squeeze_mode == 0)
    set2_spec = 0;
  if (delete_mode == 0 && squeeze_mode != 0 && set2_spec == 0)
    set2_spec = set1_spec;

  if (utt_expand_tr_set(set1_spec, set1_items, &set1_len) < 0)
    return utt_print_error("tr", "bad set", set1_spec);
  utt_fill_set_table(set1_table, set1_items, set1_len);
  if (complement != 0) {
    for (i = 0; i < 256; i++)
      set1_table[i] = (unsigned char)(set1_table[i] == 0 ? 1 : 0);
  }

  memset(squeeze_table, 0, sizeof(squeeze_table));
  if (set2_spec != 0 && utt_expand_tr_set(set2_spec, set2_items, &set2_len) < 0)
    return utt_print_error("tr", "bad set", set2_spec);

  if (squeeze_mode != 0) {
    if (delete_mode != 0 || set2_spec == 0)
      utt_fill_set_table(squeeze_table, set1_items, set1_len);
    else
      utt_fill_set_table(squeeze_table, set2_items, set2_len);
  }

  while (1) {
    unsigned char inbuf[UTT_IO_BUF_SIZE];
    int read_len = (int)read(STDIN_FILENO, inbuf, sizeof(inbuf));

    if (read_len < 0)
      return utt_print_error("tr", "read failed", "");
    if (read_len == 0)
      break;
    for (i = 0; i < read_len; i++) {
      unsigned char ch = inbuf[i];
      unsigned char outch = ch;

      if (delete_mode != 0 && set1_table[ch] != 0)
        continue;
      if (delete_mode == 0 && set1_table[ch] != 0 && set2_len > 0) {
        int pos = 0;

        while (pos < set1_len && set1_items[pos] != ch)
          pos++;
        if (pos >= set2_len)
          pos = set2_len - 1;
        if (pos >= 0)
          outch = set2_items[pos];
      }
      if (squeeze_mode != 0 && prev_valid != 0 &&
          prev_out == outch && squeeze_table[outch] != 0)
        continue;
      utt_write_raw(STDOUT_FILENO, (char *)&outch, 1);
      prev_out = outch;
      prev_valid = 1;
    }
  }

  return 0;
}

int unix_diff_main(int argc, char **argv)
{
  int quiet = 0;
  int unified = 0;
  struct utt_loaded_text a;
  struct utt_loaded_text b;
  int i;

  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    }
    if (strcmp(argv[i], "-q") == 0 ||
        utt_match_long_option(argv[i], "brief", 0) ||
        utt_match_long_option(argv[i], "quiet", 0))
      quiet = 1;
    else if (strcmp(argv[i], "-u") == 0 ||
             utt_match_long_option(argv[i], "unified", &value))
      unified = 1;
    else if (argv[i][0] == '-')
      return utt_print_error("diff", "unsupported option", argv[i]);
    else
      break;
  }
  if (argc - i < 2)
    return utt_print_error("diff", "need two files", "");

  if (utt_load_text_from_path(argv[i], &a) < 0)
    return utt_print_error("diff", "open failed", argv[i]);
  if (utt_load_text_from_path(argv[i + 1], &b) < 0) {
    utt_loaded_text_free(&a);
    return utt_print_error("diff", "open failed", argv[i + 1]);
  }

  if (utt_diff_equal_lines(&a, &b) != 0) {
    utt_loaded_text_free(&a);
    utt_loaded_text_free(&b);
    return 0;
  }

  if (quiet != 0) {
    utt_write_text(STDOUT_FILENO, "Files ");
    utt_write_text(STDOUT_FILENO, argv[i]);
    utt_write_text(STDOUT_FILENO, " and ");
    utt_write_text(STDOUT_FILENO, argv[i + 1]);
    utt_write_text(STDOUT_FILENO, " differ\n");
    utt_loaded_text_free(&a);
    utt_loaded_text_free(&b);
    return 1;
  }

  if (unified == 0)
    unified = 1;
  if (unified != 0) {
    char header[128];
    int j;
    int len = snprintf(header, sizeof(header),
                       "--- %s\n+++ %s\n@@ -1,%d +1,%d @@\n",
                       argv[i], argv[i + 1],
                       a.line_count, b.line_count);
    utt_write_raw(STDOUT_FILENO, header, len);
    for (j = 0; j < a.line_count; j++) {
      utt_write_text(STDOUT_FILENO, "-");
      utt_write_raw(STDOUT_FILENO, a.lines[j].text, a.lines[j].len);
      utt_write_text(STDOUT_FILENO, "\n");
    }
    for (j = 0; j < b.line_count; j++) {
      utt_write_text(STDOUT_FILENO, "+");
      utt_write_raw(STDOUT_FILENO, b.lines[j].text, b.lines[j].len);
      utt_write_text(STDOUT_FILENO, "\n");
    }
  }

  utt_loaded_text_free(&a);
  utt_loaded_text_free(&b);
  return 1;
}

int unix_tee_main(int argc, char **argv)
{
  int append = 0;
  int file_start = argc;
  int *fds = 0;
  int fd_count = 0;
  int i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    } else if (strcmp(argv[i], "-a") == 0 ||
               utt_match_long_option(argv[i], "append", 0)) {
      append = 1;
    } else if (strcmp(argv[i], "-i") == 0 ||
               utt_match_long_option(argv[i], "ignore-interrupts", 0)) {
      continue;
    } else if (argv[i][0] == '-') {
      return utt_print_error("tee", "unsupported option", argv[i]);
    } else {
      file_start = i;
      break;
    }
  }
  if (file_start == argc)
    file_start = i;

  if (argc - file_start > 0) {
    fds = (int *)malloc(sizeof(int) * (size_t)(argc - file_start));
    if (fds == 0)
      return utt_print_error("tee", "out of memory", "");
  }

  for (i = file_start; i < argc; i++) {
    int flags = O_WRONLY | O_CREAT;

    if (append != 0)
      flags |= O_APPEND;
    else
      flags |= O_TRUNC;
    fds[fd_count] = open(argv[i], flags, 0644);
    if (fds[fd_count] < 0) {
      free(fds);
      return utt_print_error("tee", "open failed", argv[i]);
    }
    fd_count++;
  }

  while (1) {
    char buf[UTT_IO_BUF_SIZE];
    int read_len = (int)read(STDIN_FILENO, buf, sizeof(buf));

    if (read_len < 0) {
      for (i = 0; i < fd_count; i++)
        close(fds[i]);
      free(fds);
      return utt_print_error("tee", "read failed", "");
    }
    if (read_len == 0)
      break;
    utt_write_raw(STDOUT_FILENO, buf, read_len);
    for (i = 0; i < fd_count; i++)
      utt_write_raw(fds[i], buf, read_len);
  }

  for (i = 0; i < fd_count; i++)
    close(fds[i]);
  free(fds);
  return 0;
}

static void utt_sed_free_program(struct utt_sed_program *prog)
{
  int i;

  if (prog == 0 || prog->commands == 0)
    return;
  for (i = 0; i < prog->command_count; i++) {
    if (prog->commands[i].find != 0)
      free(prog->commands[i].find);
    if (prog->commands[i].replace != 0)
      free(prog->commands[i].replace);
  }
  free(prog->commands);
  prog->commands = 0;
  prog->command_count = 0;
}

static int utt_sed_add_command(struct utt_sed_program *prog,
                               struct utt_sed_command *cmd)
{
  struct utt_sed_command *next;

  next = (struct utt_sed_command *)malloc(sizeof(*next) *
                                          (size_t)(prog->command_count + 1));
  if (next == 0)
    return -1;
  if (prog->commands != 0) {
    memcpy(next, prog->commands,
           sizeof(*next) * (size_t)prog->command_count);
    free(prog->commands);
  }
  next[prog->command_count] = *cmd;
  prog->commands = next;
  prog->command_count++;
  return 0;
}

static const char *utt_sed_skip_ws(const char *p)
{
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';')
    p++;
  return p;
}

static int utt_sed_parse_piece(const char *piece, struct utt_sed_program *prog)
{
  const char *p = utt_sed_skip_ws(piece);
  struct utt_sed_command cmd;
  char delim;
  const char *start;
  const char *end;

  memset(&cmd, 0, sizeof(cmd));
  if (*p == '\0')
    return 0;
  if (utt_is_digit(*p)) {
    cmd.addr_line = atoi(p);
    while (utt_is_digit(*p))
      p++;
  } else if (*p == '$') {
    cmd.addr_last = 1;
    p++;
  }
  if (*p == 'p' || *p == 'd' || *p == 'q') {
    cmd.type = *p;
    return utt_sed_add_command(prog, &cmd);
  }
  if (*p != 's')
    return -1;
  cmd.type = 's';
  p++;
  delim = *p++;
  start = p;
  while (*p != '\0' && *p != delim) {
    if (*p == '\\' && p[1] != '\0')
      p += 2;
    else
      p++;
  }
  if (*p != delim)
    return -1;
  end = p;
  cmd.find = utt_strdup_len(start, (int)(end - start));
  p++;
  start = p;
  while (*p != '\0' && *p != delim) {
    if (*p == '\\' && p[1] != '\0')
      p += 2;
    else
      p++;
  }
  if (*p != delim) {
    if (cmd.find != 0)
      free(cmd.find);
    return -1;
  }
  end = p;
  cmd.replace = utt_strdup_len(start, (int)(end - start));
  p++;
  while (*p != '\0') {
    if (*p == 'g')
      cmd.global = 1;
    else if (*p != ' ' && *p != '\t')
      return -1;
    p++;
  }
  return utt_sed_add_command(prog, &cmd);
}

static int utt_sed_parse_script(const char *script, struct utt_sed_program *prog)
{
  const char *p = script;
  const char *start = script;
  char quote = '\0';

  while (*p != '\0') {
    if (quote == '\0' && (*p == ';' || *p == '\n')) {
      char *piece = utt_strdup_len(start, (int)(p - start));
      int ret;

      if (piece == 0)
        return -1;
      ret = utt_sed_parse_piece(piece, prog);
      free(piece);
      if (ret < 0)
        return -1;
      p++;
      start = p;
      continue;
    }
    if (*p == '\\' && p[1] != '\0') {
      p += 2;
      continue;
    }
    if (*p == '\'' || *p == '"') {
      if (quote == *p)
        quote = '\0';
      else if (quote == '\0')
        quote = *p;
    }
    p++;
  }
  if (p > start) {
    char *piece = utt_strdup_len(start, (int)(p - start));
    int ret;

    if (piece == 0)
      return -1;
    ret = utt_sed_parse_piece(piece, prog);
    free(piece);
    if (ret < 0)
      return -1;
  }
  return 0;
}

static int utt_sed_add_script_file(const char *path,
                                   struct utt_sed_program *prog)
{
  char *data = 0;
  int len = 0;
  int ret;

  if (utt_read_path_all(path, &data, &len) < 0)
    return -1;
  ret = utt_sed_parse_script(data, prog);
  free(data);
  return ret;
}

static int utt_sed_addr_match(const struct utt_sed_command *cmd,
                              int line_no,
                              int is_last)
{
  if (cmd->addr_line > 0)
    return line_no == cmd->addr_line;
  if (cmd->addr_last != 0)
    return is_last != 0;
  return 1;
}

static int utt_sed_substitute(struct utt_string *line,
                              const struct utt_sed_command *cmd)
{
  struct utt_string out;
  int pos = 0;
  int copy_from = 0;
  int found = 0;
  int find_len = utt_strlen_int(cmd->find);
  int repl_len = utt_strlen_int(cmd->replace);
  char *orig_data = line->data;
  int orig_len = line->len;

  if (find_len <= 0)
    return 0;
  utt_string_init(&out);
  while (pos <= orig_len - find_len) {
    if (memcmp(orig_data + pos, cmd->find, (size_t)find_len) == 0) {
      utt_string_append_len(&out, orig_data + copy_from, pos - copy_from);
      utt_string_append_len(&out, cmd->replace, repl_len);
      pos += find_len;
      copy_from = pos;
      found = 1;
      if (cmd->global == 0)
        break;
      continue;
    }
    if (found != 0 && cmd->global == 0)
      break;
    pos++;
  }
  if (found == 0) {
    utt_string_free(&out);
    return 0;
  }
  utt_string_append_len(&out, orig_data + copy_from, orig_len - copy_from);
  free(orig_data);
  *line = out;
  return 1;
}

int unix_sed_main(int argc, char **argv)
{
  struct utt_sed_program prog;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  int i;

  memset(&prog, 0, sizeof(prog));
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if (strcmp(argv[i], "-n") == 0 ||
               utt_match_long_option(argv[i], "quiet", 0) ||
               utt_match_long_option(argv[i], "silent", 0)) {
      prog.suppress_default = 1;
    } else if ((strcmp(argv[i], "-e") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "expression", &value)) {
      const char *script = value != 0 ? value : argv[++i];

      if (utt_sed_parse_script(script, &prog) < 0) {
        utt_sed_free_program(&prog);
        return utt_print_error("sed", "bad script", script);
      }
    } else if ((strcmp(argv[i], "-f") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "file", &value)) {
      const char *path = value != 0 ? value : argv[++i];

      if (utt_sed_add_script_file(path, &prog) < 0) {
        utt_sed_free_program(&prog);
        return utt_print_error("sed", "bad script", path);
      }
    } else if (argv[i][0] == '-') {
      utt_sed_free_program(&prog);
      return utt_print_error("sed", "unsupported option", argv[i]);
    } else {
      if (prog.command_count == 0) {
        if (utt_sed_parse_script(argv[i], &prog) < 0) {
          utt_sed_free_program(&prog);
          return utt_print_error("sed", "bad script", argv[i]);
        }
        i++;
      }
      break;
    }
  }

  if (prog.command_count == 0)
    return utt_print_error("sed", "missing script", "");
  if (utt_collect_input_texts(argv + i, argc - i, &texts, &text_count) < 0) {
    utt_sed_free_program(&prog);
    return utt_print_error("sed", "read failed", "");
  }

  for (i = 0; i < text_count; i++) {
    int j;

    for (j = 0; j < texts[i].line_count; j++) {
      struct utt_string line;
      int cmd_index;
      int deleted = 0;
      int quit_after = 0;

      utt_string_init(&line);
      utt_string_append_len(&line, texts[i].lines[j].text, texts[i].lines[j].len);
      for (cmd_index = 0; cmd_index < prog.command_count; cmd_index++) {
        struct utt_sed_command *cmd = &prog.commands[cmd_index];

        if (!utt_sed_addr_match(cmd, j + 1, j == texts[i].line_count - 1))
          continue;
        if (cmd->type == 's')
          utt_sed_substitute(&line, cmd);
        else if (cmd->type == 'p') {
          utt_write_raw(STDOUT_FILENO, line.data, line.len);
          utt_write_text(STDOUT_FILENO, "\n");
        } else if (cmd->type == 'd') {
          deleted = 1;
          break;
        } else if (cmd->type == 'q') {
          quit_after = 1;
          break;
        }
      }
      if (deleted == 0 && prog.suppress_default == 0) {
        utt_write_raw(STDOUT_FILENO, line.data, line.len);
        utt_write_text(STDOUT_FILENO, "\n");
      }
      utt_string_free(&line);
      if (quit_after != 0) {
        utt_free_texts(texts, text_count);
        utt_sed_free_program(&prog);
        return 0;
      }
    }
  }

  utt_free_texts(texts, text_count);
  utt_sed_free_program(&prog);
  return 0;
}

static int utt_append_program_text(struct utt_string *prog_buf, const char *text)
{
  if (prog_buf == 0 || text == 0)
    return -1;
  if (prog_buf->len > 0 && prog_buf->data[prog_buf->len - 1] != '\n') {
    if (utt_string_append_char(prog_buf, '\n') < 0)
      return -1;
  }
  return utt_string_append_text(prog_buf, text);
}

static int utt_append_program_file(struct utt_string *prog_buf, const char *path)
{
  char *data = 0;
  int len = 0;
  int ret = 0;

  if (utt_read_path_all(path, &data, &len) < 0)
    return -1;
  if (prog_buf->len > 0 && prog_buf->data[prog_buf->len - 1] != '\n') {
    if (utt_string_append_char(prog_buf, '\n') < 0)
      ret = -1;
  }
  if (ret == 0 && utt_string_append_len(prog_buf, data, len) < 0)
    ret = -1;
  free(data);
  return ret;
}

static int utt_awk_add_assignment(struct utt_awk_program *prog, const char *assign)
{
  const char *eq;
  int name_len;

  if (prog == 0 || assign == 0 || prog->var_count >= UTT_VAR_MAX)
    return -1;
  eq = strchr(assign, '=');
  if (eq == 0)
    return -1;
  name_len = (int)(eq - assign);
  if (name_len <= 0 ||
      name_len >= (int)sizeof(prog->vars[prog->var_count].name))
    return -1;
  memcpy(prog->vars[prog->var_count].name, assign, (size_t)name_len);
  prog->vars[prog->var_count].name[name_len] = '\0';
  strncpy(prog->vars[prog->var_count].value, eq + 1,
          sizeof(prog->vars[prog->var_count].value) - 1);
  prog->var_count++;
  return 0;
}

static const char *utt_awk_skip_space(const char *p)
{
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';')
    p++;
  return p;
}

static int utt_awk_parse_expr(const char *token, struct utt_awk_expr *expr)
{
  int len;

  memset(expr, 0, sizeof(*expr));
  len = utt_strlen_int(token);
  while (len > 0 && utt_is_space(token[len - 1]))
    len--;
  while (*token != '\0' && utt_is_space(*token)) {
    token++;
    len--;
  }
  if (len <= 0)
    return 0;
  if ((token[0] == '"' && token[len - 1] == '"') ||
      (token[0] == '\'' && token[len - 1] == '\'')) {
    expr->type = UTT_AWK_EXPR_STRING;
    if (len - 2 >= (int)sizeof(expr->text))
      len = (int)sizeof(expr->text) - 1 + 2;
    memcpy(expr->text, token + 1, (size_t)(len - 2));
    expr->text[len - 2] = '\0';
    return 0;
  }
  if (token[0] == '$' && utt_is_digit(token[1])) {
    expr->type = UTT_AWK_EXPR_FIELD;
    expr->field_index = atoi(token + 1);
    return 0;
  }
  if (len == 2 && token[0] == 'N' && token[1] == 'R') {
    expr->type = UTT_AWK_EXPR_NR;
    return 0;
  }
  if (len == 2 && token[0] == 'N' && token[1] == 'F') {
    expr->type = UTT_AWK_EXPR_NF;
    return 0;
  }
  expr->type = UTT_AWK_EXPR_VAR;
  if (len >= (int)sizeof(expr->text))
    len = (int)sizeof(expr->text) - 1;
  memcpy(expr->text, token, (size_t)len);
  expr->text[len] = '\0';
  return 0;
}

static int utt_awk_parse_print_list(const char *body, struct utt_awk_stmt *stmt)
{
  const char *p = body;

  if (strncmp(p, "print", 5) != 0)
    return -1;
  p += 5;
  p = utt_awk_skip_space(p);
  if (*p == '\0')
    return 0;
  while (*p != '\0') {
    char token[128];
    int tlen = 0;
    char quote = '\0';

    while (*p != '\0') {
      if (quote == '\0' && *p == ',')
        break;
      if ((*p == '"' || *p == '\'') && (quote == '\0' || quote == *p))
        quote = (quote == '\0') ? *p : '\0';
      if (tlen < (int)sizeof(token) - 1)
        token[tlen++] = *p;
      p++;
    }
    token[tlen] = '\0';
    if (stmt->expr_count >= UTT_EXPR_MAX)
      return -1;
    if (utt_awk_parse_expr(token, &stmt->exprs[stmt->expr_count]) < 0)
      return -1;
    stmt->expr_count++;
    if (*p == ',')
      p++;
    p = utt_awk_skip_space(p);
  }
  return 0;
}

static int utt_awk_parse_program(const char *text, struct utt_awk_program *prog)
{
  const char *p = text;

  while (*(p = utt_awk_skip_space(p)) != '\0') {
    struct utt_awk_stmt stmt;
    const char *body_start;
    const char *body_end;
    int body_len;

    if (prog->stmt_count >= UTT_STMT_MAX)
      return -1;
    memset(&stmt, 0, sizeof(stmt));
    if (strncmp(p, "BEGIN", 5) == 0 && !utt_is_alnum(p[5])) {
      stmt.kind = UTT_AWK_STMT_BEGIN;
      p += 5;
    } else if (strncmp(p, "END", 3) == 0 && !utt_is_alnum(p[3])) {
      stmt.kind = UTT_AWK_STMT_END;
      p += 3;
    } else {
      stmt.kind = UTT_AWK_STMT_MAIN;
      if (*p == '/') {
        int len = 0;

        p++;
        while (*p != '\0' && *p != '/' && len < (int)sizeof(stmt.pattern) - 1)
          stmt.pattern[len++] = *p++;
        stmt.pattern[len] = '\0';
        if (*p != '/')
          return -1;
        stmt.has_pattern = 1;
        p++;
      }
    }

    p = utt_awk_skip_space(p);
    if (*p != '{')
      return -1;
    p++;
    body_start = p;
    while (*p != '\0' && *p != '}')
      p++;
    if (*p != '}')
      return -1;
    body_end = p;
    body_len = (int)(body_end - body_start);
    {
      char *body = utt_strdup_len(body_start, body_len);
      int ret;

      if (body == 0)
        return -1;
      ret = utt_awk_parse_print_list(utt_awk_skip_space(body), &stmt);
      free(body);
      if (ret < 0)
        return -1;
    }
    prog->stmts[prog->stmt_count++] = stmt;
    p++;
  }
  return 0;
}

static const char *utt_awk_lookup_var(const struct utt_awk_program *prog,
                                      const char *name)
{
  int i;

  for (i = 0; i < prog->var_count; i++) {
    if (strcmp(prog->vars[i].name, name) == 0)
      return prog->vars[i].value;
  }
  return "";
}

static void utt_awk_split_fields(struct utt_awk_program *prog,
                                 struct utt_awk_record *record)
{
  int pos = 0;
  int fs_len = utt_strlen_int(prog->fs);

  record->field_count = 0;
  if (fs_len == 0 || (fs_len == 1 && prog->fs[0] == ' ')) {
    while (pos < record->len) {
      int start;

      while (pos < record->len && utt_is_space(record->line[pos]))
        pos++;
      if (pos >= record->len)
        break;
      start = pos;
      while (pos < record->len && !utt_is_space(record->line[pos]))
        pos++;
      if (record->field_count < UTT_FIELD_MAX) {
        record->fields[record->field_count] = record->line + start;
        record->field_lens[record->field_count] = pos - start;
        record->field_count++;
      }
    }
    return;
  }

  while (pos <= record->len) {
    int next = pos;

    while (next + fs_len <= record->len &&
           memcmp(record->line + next, prog->fs, (size_t)fs_len) != 0)
      next++;
    if (record->field_count < UTT_FIELD_MAX) {
      record->fields[record->field_count] = record->line + pos;
      if (next + fs_len <= record->len)
        record->field_lens[record->field_count] = next - pos;
      else
        record->field_lens[record->field_count] = record->len - pos;
      record->field_count++;
    }
    if (next + fs_len > record->len)
      break;
    pos = next + fs_len;
  }
}

static int utt_awk_stmt_match(const struct utt_awk_stmt *stmt,
                              const struct utt_awk_record *record)
{
  if (stmt->kind != UTT_AWK_STMT_MAIN)
    return 0;
  if (stmt->has_pattern == 0)
    return 1;
  return utt_regex_match(stmt->pattern, record->line, 0);
}

static void utt_awk_emit_expr(const struct utt_awk_program *prog,
                              const struct utt_awk_record *record,
                              const struct utt_awk_expr *expr)
{
  char buf[32];
  int len = 0;

  if (expr->type == UTT_AWK_EXPR_STRING) {
    utt_write_text(STDOUT_FILENO, expr->text);
  } else if (expr->type == UTT_AWK_EXPR_FIELD) {
    int index = expr->field_index - 1;

    if (index >= 0 && index < record->field_count)
      utt_write_raw(STDOUT_FILENO,
                    record->fields[index],
                    record->field_lens[index]);
  } else if (expr->type == UTT_AWK_EXPR_NR) {
    len = utt_format_long(buf, sizeof(buf), record->nr);
    utt_write_raw(STDOUT_FILENO, buf, len);
  } else if (expr->type == UTT_AWK_EXPR_NF) {
    len = snprintf(buf, sizeof(buf), "%d", record->field_count);
    utt_write_raw(STDOUT_FILENO, buf, len);
  } else if (expr->type == UTT_AWK_EXPR_VAR) {
    utt_write_text(STDOUT_FILENO, utt_awk_lookup_var(prog, expr->text));
  }
}

static void utt_awk_run_stmt(const struct utt_awk_program *prog,
                             const struct utt_awk_stmt *stmt,
                             const struct utt_awk_record *record)
{
  int i;

  if (stmt->expr_count == 0) {
    utt_write_raw(STDOUT_FILENO, record->line, record->len);
    utt_write_text(STDOUT_FILENO, "\n");
    return;
  }
  for (i = 0; i < stmt->expr_count; i++) {
    if (i > 0)
      utt_write_text(STDOUT_FILENO, " ");
    utt_awk_emit_expr(prog, record, &stmt->exprs[i]);
  }
  utt_write_text(STDOUT_FILENO, "\n");
}

int unix_awk_main(int argc, char **argv)
{
  struct utt_awk_program prog;
  struct utt_string program_buf;
  struct utt_loaded_text *texts = 0;
  int text_count = 0;
  int i;
  const char *program_text = 0;
  long nr = 0;

  memset(&prog, 0, sizeof(prog));
  utt_string_init(&program_buf);
  strcpy(prog.fs, " ");
  for (i = 1; i < argc; i++) {
    const char *value = 0;

    if (strcmp(argv[i], "--") == 0) {
      i++;
      break;
    } else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
      strncpy(prog.fs, argv[++i], sizeof(prog.fs) - 1);
    } else if (strncmp(argv[i], "-F", 2) == 0 && argv[i][2] != '\0') {
      strncpy(prog.fs, argv[i] + 2, sizeof(prog.fs) - 1);
    } else if (utt_match_long_option(argv[i], "field-separator", &value)) {
      const char *fs = value != 0 ? value : argv[++i];
      strncpy(prog.fs, fs, sizeof(prog.fs) - 1);
    } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
      if (utt_awk_add_assignment(&prog, argv[++i]) < 0) {
        utt_string_free(&program_buf);
        return utt_print_error("awk", "bad -v", argv[i]);
      }
    } else if (utt_match_long_option(argv[i], "assign", &value)) {
      const char *assign = value != 0 ? value : argv[++i];

      if (utt_awk_add_assignment(&prog, assign) < 0) {
        utt_string_free(&program_buf);
        return utt_print_error("awk", "bad -v", assign);
      }
    } else if ((strcmp(argv[i], "-f") == 0 && i + 1 < argc) ||
               utt_match_long_option(argv[i], "file", &value)) {
      const char *path = value != 0 ? value : argv[++i];

      if (utt_append_program_file(&program_buf, path) < 0) {
        utt_string_free(&program_buf);
        return utt_print_error("awk", "bad program", path);
      }
    } else if (argv[i][0] == '-') {
      utt_string_free(&program_buf);
      return utt_print_error("awk", "unsupported option", argv[i]);
    } else {
      if (program_buf.len == 0) {
        if (utt_append_program_text(&program_buf, argv[i++]) < 0) {
          utt_string_free(&program_buf);
          return utt_print_error("awk", "out of memory", "");
        }
      }
      break;
    }
  }

  if (program_buf.len > 0)
    program_text = program_buf.data;
  if (program_text == 0) {
    utt_string_free(&program_buf);
    return utt_print_error("awk", "missing program", "");
  }
  if (utt_awk_parse_program(program_text, &prog) < 0) {
    utt_string_free(&program_buf);
    return utt_print_error("awk", "bad program", program_text);
  }
  if (utt_collect_input_texts(argv + i, argc - i, &texts, &text_count) < 0) {
    utt_string_free(&program_buf);
    return utt_print_error("awk", "read failed", "");
  }

  for (i = 0; i < prog.stmt_count; i++) {
    struct utt_awk_record empty;

    if (prog.stmts[i].kind != UTT_AWK_STMT_BEGIN)
      continue;
    memset(&empty, 0, sizeof(empty));
    utt_awk_run_stmt(&prog, &prog.stmts[i], &empty);
  }

  for (i = 0; i < text_count; i++) {
    int j;

    for (j = 0; j < texts[i].line_count; j++) {
      struct utt_awk_record record;
      int stmt_index;

      memset(&record, 0, sizeof(record));
      record.line = texts[i].lines[j].text;
      record.len = texts[i].lines[j].len;
      record.nr = ++nr;
      utt_awk_split_fields(&prog, &record);

      for (stmt_index = 0; stmt_index < prog.stmt_count; stmt_index++) {
        if (utt_awk_stmt_match(&prog.stmts[stmt_index], &record))
          utt_awk_run_stmt(&prog, &prog.stmts[stmt_index], &record);
      }
    }
  }

  for (i = 0; i < prog.stmt_count; i++) {
    struct utt_awk_record endrec;

    if (prog.stmts[i].kind != UTT_AWK_STMT_END)
      continue;
    memset(&endrec, 0, sizeof(endrec));
    endrec.nr = nr;
    utt_awk_run_stmt(&prog, &prog.stmts[i], &endrec);
  }

  utt_free_texts(texts, text_count);
  utt_string_free(&program_buf);
  return 0;
}
