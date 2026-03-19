#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <shell.h>
#include <fs.h>

static struct shell_state g_shell_state;

static int read_line(char **buf, int *buf_size)
{
  int read_len;

  read_len = (int)read(STDIN_FILENO, *buf, (size_t)(*buf_size));
  if (read_len <= 0)
    return -1;
  if (read_len >= *buf_size)
    read_len = *buf_size - 1;
  (*buf)[read_len] = '\0';
  return read_len;
}

int main(int argc, char **argv)
{
  struct shell_state *state = &g_shell_state;
  int status = 0;

  shell_state_init(state, 0);
  if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
    status = shell_execute_buffer(state,
                                  argc >= 4 ? argv[3] : "sh",
                                  argv[2],
                                  argc >= 4 ? argc - 4 : 0,
                                  argc >= 4 ? &argv[4] : 0,
                                  0);
  } else if (argc >= 2) {
    status = shell_execute_file(state, argv[1],
                                argc - 2, &argv[2], 0);
  } else {
    char *buf = (char *)malloc(256);
    int buf_size = 256;

    if (buf == 0)
      return 1;
    memset(buf, 0, (size_t)buf_size);
    state->interactive = 1;
    while (1) {
      int len;

      shell_reap_background(state);
      write(STDOUT_FILENO, "sh> ", 4);
      len = read_line(&buf, &buf_size);
      if (len < 0)
        break;
      status = shell_execute_string(state, buf);
      if (status == 2)
        write(STDERR_FILENO, "sh: parse error\n", 16);
      if (state->exit_requested != 0)
        break;
    }
    free(buf);
  }

  exit(status);
  return status;
}
