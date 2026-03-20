#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

static void utt_head_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: head [-n count] [-c count] [-q|-v] [file ...]\n");
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

    if (utt_is_help_option(argv[i])) {
      utt_head_print_usage();
      return 0;
    } else if (strcmp(argv[i], "--") == 0) {
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_head_main(argc, argv);
}
#endif
