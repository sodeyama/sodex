#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

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

static void utt_sort_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: sort [-n] [-r] [-u] [-o path] [-t delim] "
                 "[-k start[,end]] [file ...]\n");
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

    if (utt_is_help_option(argv[i])) {
      utt_sort_print_usage();
      return 0;
    }
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_sort_main(argc, argv);
}
#endif
