#include <unix_text_tool_lib.h>
#include <unix_text_tools.h>

static void utt_tee_print_usage(void)
{
  utt_write_text(STDOUT_FILENO,
                 "usage: tee [-a] [-i] [file ...]\n");
}

int unix_tee_main(int argc, char **argv)
{
  int append = 0;
  int file_start = argc;
  int *fds = 0;
  int fd_count = 0;
  int i;

  for (i = 1; i < argc; i++) {
    if (utt_is_help_option(argv[i])) {
      utt_tee_print_usage();
      return 0;
    } else if (strcmp(argv[i], "--") == 0) {
      file_start = i + 1;
      break;
    } else if (strcmp(argv[i], "-a") == 0 ||
               utt_match_long_option(argv[i], "append", 0)) {
      append = 1;
    } else if (strcmp(argv[i], "-i") == 0 ||
               utt_match_long_option(argv[i], "ignore-interrupts", 0)) {
      continue;
    } else if (argv[i][0] == '-') {
      return utt_print_error("tee", "unsupported option", argv[i]);
    } else {
      file_start = i;
      break;
    }
  }
  if (file_start == argc)
    file_start = i;

  if (argc - file_start > 0) {
    fds = (int *)malloc(sizeof(int) * (size_t)(argc - file_start));
    if (fds == 0)
      return utt_print_error("tee", "out of memory", "");
  }

  for (i = file_start; i < argc; i++) {
    int flags = O_WRONLY | O_CREAT;

    if (append != 0)
      flags |= O_APPEND;
    else
      flags |= O_TRUNC;
    fds[fd_count] = open(argv[i], flags, 0644);
    if (fds[fd_count] < 0) {
      free(fds);
      return utt_print_error("tee", "open failed", argv[i]);
    }
    fd_count++;
  }

  while (1) {
    char buf[UTT_IO_BUF_SIZE];
    int read_len = (int)read(STDIN_FILENO, buf, sizeof(buf));

    if (read_len < 0) {
      for (i = 0; i < fd_count; i++)
        close(fds[i]);
      free(fds);
      return utt_print_error("tee", "read failed", "");
    }
    if (read_len == 0)
      break;
    utt_write_raw(STDOUT_FILENO, buf, read_len);
    for (i = 0; i < fd_count; i++)
      utt_write_raw(fds[i], buf, read_len);
  }

  for (i = 0; i < fd_count; i++)
    close(fds[i]);
  free(fds);
  return 0;
}

#ifndef TEST_BUILD
int main(int argc, char **argv)
{
  return unix_tee_main(argc, argv);
}
#endif
