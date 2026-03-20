#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

struct utt_uniq_options {
  int show_count;
  int only_repeated;
  int only_unique;
  int skip_fields;
  int skip_chars;
};

static void utt_uniq_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: uniq [-c] [-d] [-u] [-f fields] [-s chars] [file ...]\n");
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

    if (utt_is_help_option(argv[i])) {
      utt_uniq_print_usage();
      return 0;
    }
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_uniq_main(argc, argv);
}
#endif
