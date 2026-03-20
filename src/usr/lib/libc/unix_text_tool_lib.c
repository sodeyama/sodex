#include <unix_text_tool_lib.h>

int utt_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' ||
         ch == '\r' || ch == '\f' || ch == '\v';
}

int utt_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

int utt_is_alpha(char ch)
{
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

int utt_is_alnum(char ch)
{
  return utt_is_alpha(ch) || utt_is_digit(ch);
}

char utt_tolower_ascii(char ch)
{
  if (ch >= 'A' && ch <= 'Z')
    return (char)(ch - 'A' + 'a');
  return ch;
}

int utt_strlen_int(const char *text)
{
  return text == 0 ? 0 : (int)strlen(text);
}

void utt_write_raw(int fd, const char *data, int len)
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

void utt_write_text(int fd, const char *text)
{
  utt_write_raw(fd, text, utt_strlen_int(text));
}

int utt_print_error(const char *prog, const char *msg, const char *arg)
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

char *utt_strdup_len(const char *text, int len)
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

char *utt_strdup_text(const char *text)
{
  return utt_strdup_len(text, utt_strlen_int(text));
}

int utt_string_reserve(struct utt_string *str, int need)
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

void utt_string_init(struct utt_string *str)
{
  if (str == 0)
    return;
  memset(str, 0, sizeof(*str));
}

void utt_string_reset(struct utt_string *str)
{
  if (str == 0 || str->data == 0)
    return;
  str->len = 0;
  str->data[0] = '\0';
}

void utt_string_free(struct utt_string *str)
{
  if (str == 0)
    return;
  if (str->data != 0)
    free(str->data);
  memset(str, 0, sizeof(*str));
}

int utt_string_append_len(struct utt_string *str, const char *text, int len)
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

int utt_string_append_text(struct utt_string *str, const char *text)
{
  return utt_string_append_len(str, text, utt_strlen_int(text));
}

int utt_string_append_char(struct utt_string *str, char ch)
{
  return utt_string_append_len(str, &ch, 1);
}

int utt_parse_long_value(const char *text, long *value_out)
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

int utt_match_long_option(const char *arg,
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

int utt_is_help_option(const char *arg)
{
  return (arg != 0 && strcmp(arg, "-h") == 0) ||
         utt_match_long_option(arg, "help", 0);
}

int utt_is_stdin_path(const char *path)
{
  return path != 0 && path[0] == '-' && path[1] == '\0';
}

long utt_count_newlines(const char *text, int len)
{
  long count = 0;
  int i;

  for (i = 0; i < len; i++) {
    if (text[i] == '\n')
      count++;
  }
  return count;
}

int utt_parse_head_count_spec(const char *text,
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

int utt_parse_tail_count_spec(const char *text,
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

long utt_parse_long_substr(const char *text, int len)
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

int utt_format_long(char *buf, int cap, long value)
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

int utt_buf_append_long(char *buf, int cap, int len, long value)
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

int utt_next_char_end(const char *text, int len, int index)
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

int utt_prev_char_start(const char *text, int len, int index)
{
#ifdef TEST_BUILD
  (void)len;
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

int utt_advance_chars(const char *text, int len, int count)
{
  int index = 0;

  while (count > 0 && index < len) {
    index = utt_next_char_end(text, len, index);
    count--;
  }
  return index;
}

int utt_read_fd_all(int fd, char **data_out, int *len_out)
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

int utt_read_path_all(const char *path, char **data_out, int *len_out)
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

void utt_loaded_text_free(struct utt_loaded_text *text)
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

int utt_split_lines(char *data, int len,
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

int utt_load_text_from_fd(int fd,
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

int utt_load_text_from_path(const char *path,
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

void utt_free_texts(struct utt_loaded_text *texts, int count)
{
  int i;

  if (texts == 0)
    return;
  for (i = 0; i < count; i++)
    utt_loaded_text_free(&texts[i]);
  free(texts);
}

int utt_collect_input_texts(char **files, int file_count,
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

int utt_collect_line_refs(struct utt_loaded_text *texts,
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

int utt_wildcard_match(const char *pattern, const char *text)
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

int utt_char_equal(char a, char b, int ignore_case)
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

int utt_regex_match(const char *pattern,
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

int utt_contains_substr(const char *haystack,
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

int utt_parse_range_list(const char *spec,
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

int utt_range_contains(const struct utt_range *ranges,
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

void utt_get_whitespace_field(const char *text, int len,
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

void utt_get_delim_field(const char *text, int len,
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

int utt_print_line_ref(const struct utt_line_ref *line)
{
  utt_write_raw(STDOUT_FILENO, line->text, line->len);
  utt_write_text(STDOUT_FILENO, "\n");
  return 0;
}

void utt_print_header_if_needed(const char *name,
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
