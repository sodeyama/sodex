#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <shell.h>
#include <fs.h>

static struct shell_state g_shell_state;
static int shell_append_input(char **buf, int *buf_size, int *len,
                              const char *line, int line_len, int add_newline);

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
    char *command_buf = (char *)malloc(512);
    struct shell_program *probe_program;
    int buf_size = 256;
    int command_buf_size = 512;
    int command_len = 0;

    if (buf == 0 || command_buf == 0)
      return 1;
    probe_program = (struct shell_program *)malloc(sizeof(*probe_program));
    if (probe_program == 0)
      return 1;
    memset(buf, 0, (size_t)buf_size);
    memset(command_buf, 0, (size_t)command_buf_size);
    state->interactive = 1;
    while (1) {
      int add_newline = 0;
      int len;
      int parse_status;

      shell_reap_background(state);
      if (command_len > 0)
        write(STDOUT_FILENO, "> ", 2);
      else
        write(STDOUT_FILENO, "sh> ", 4);
      len = read_line(&buf, &buf_size);
      if (len < 0)
        break;
      if (len > 0 && buf[len - 1] == '\n') {
        len--;
        add_newline = 1;
        buf[len] = '\0';
      }
      if (shell_append_input(&command_buf, &command_buf_size, &command_len,
                             buf, len, add_newline) < 0) {
        write(STDERR_FILENO, "sh: out of memory\n", 18);
        break;
      }

      parse_status = shell_parse_program(command_buf, command_len, probe_program);
      if (parse_status == SHELL_PARSE_INCOMPLETE)
        continue;
      if (parse_status < 0) {
        write(STDERR_FILENO, "sh: parse error\n", 16);
        command_len = 0;
        command_buf[0] = '\0';
        continue;
      }

      status = shell_execute_string(state, command_buf);
      if (status == 2)
        write(STDERR_FILENO, "sh: parse error\n", 16);
      if (state->exit_requested != 0)
        break;
      command_len = 0;
      command_buf[0] = '\0';
    }
    free(buf);
    free(command_buf);
    free(probe_program);
  }

  exit(status);
  return status;
}

static int shell_append_input(char **buf, int *buf_size, int *len,
                              const char *line, int line_len, int add_newline)
{
  int need;

  if (buf == 0 || *buf == 0 || buf_size == 0 || len == 0)
    return -1;

  need = *len + line_len + (add_newline != 0 ? 1 : 0) + 1;
  if (need > *buf_size) {
    int next_size = *buf_size;
    char *next_buf;

    while (next_size < need)
      next_size *= 2;
    next_buf = (char *)malloc((size_t)next_size);
    if (next_buf == 0)
      return -1;
    memset(next_buf, 0, (size_t)next_size);
    memcpy(next_buf, *buf, (size_t)(*len));
    free(*buf);
    *buf = next_buf;
    *buf_size = next_size;
  }

  if (line_len > 0)
    memcpy(*buf + *len, line, (size_t)line_len);
  *len += line_len;
  if (add_newline != 0)
    (*buf)[(*len)++] = '\n';
  (*buf)[*len] = '\0';
  return 0;
}
