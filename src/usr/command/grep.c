#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

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

static void utt_grep_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: grep [-F] [-i] [-v] [-n] [-c] [-q] "
                 "[-e pattern]... pattern [file ...]\n");
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

    if (utt_is_help_option(argv[i])) {
      utt_grep_print_usage();
      return 0;
    }
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_grep_main(argc, argv);
}
#endif
