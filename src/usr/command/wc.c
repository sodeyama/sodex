#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

struct utt_wc_counts {
  long lines;
  long words;
  long bytes;
};

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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_wc_main(argc, argv);
}
#endif
