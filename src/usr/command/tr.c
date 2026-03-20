#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

static void utt_tr_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: tr [-d] [-s] [-c] string1 [string2]\n");
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

    if (p[0] == '[' && p[1] == ':') {
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
    if (utt_is_help_option(argv[i])) {
      utt_tr_print_usage();
      return 0;
    } else if (strcmp(argv[i], "--") == 0) {
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_tr_main(argc, argv);
}
#endif
