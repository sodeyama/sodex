#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

static void utt_diff_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: diff [-q] [-u] file1 file2\n");
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

    if (utt_is_help_option(argv[i])) {
      utt_diff_print_usage();
      return 0;
    }
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

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_diff_main(argc, argv);
}
#endif
