#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

struct utt_cut_options {
  int mode_chars;
  int mode_fields;
  int suppress_no_delim;
  int complement;
  char delim;
  struct utt_range *ranges;
  int range_count;
};

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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_cut_main(argc, argv);
}
#endif
