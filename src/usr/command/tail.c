#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

static void utt_tail_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: tail [-n count] [-c count] [-q|-v] [file ...]\n");
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

    if (utt_is_help_option(argv[i])) {
      utt_tail_print_usage();
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_tail_main(argc, argv);
}
#endif
