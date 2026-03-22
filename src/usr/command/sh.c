#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <shell.h>
#include <fs.h>

static struct shell_state g_shell_state;
static int shell_ensure_buffer(char **buf, int *buf_size, int need);
static int shell_append_input(char **buf, int *buf_size, int *len,
                              const char *line, int line_len, int add_newline);
static int shell_replace_input(char **buf, int *buf_size, int *len,
                               const char *text);
static int shell_read_input_line(char **buf, int *buf_size,
                                 int *data_pos, int *data_len,
                                 char **line, int *line_len,
                                 int *add_newline);

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
    int input_data_len = 0;
    int input_pos = 0;
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
      char *line;
      int add_newline = 0;
      int line_len;
      int parse_status;
      int read_status;

      shell_reap_background(state);
      if (command_len > 0)
        write(STDOUT_FILENO, "> ", 2);
      else
        write(STDOUT_FILENO, "sh> ", 4);
      read_status = shell_read_input_line(&buf, &buf_size,
                                          &input_pos, &input_data_len,
                                          &line, &line_len, &add_newline);
      if (read_status < 0)
        break;
      if (read_status == 0)
        break;
      if (line_len <= 0 && add_newline == 0)
        continue;
      if (shell_append_input(&command_buf, &command_buf_size, &command_len,
                             line, line_len, add_newline) < 0) {
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
      if (state->interactive != 0) {
        char history_text[SHELL_HISTORY_TEXT_MAX];
        int history_status;

        history_status = shell_history_expand_line(state, command_buf,
                                                   history_text, sizeof(history_text));
        if (history_status < 0) {
          write(STDERR_FILENO, "sh: history not found\n", 22);
          command_len = 0;
          command_buf[0] = '\0';
          continue;
        }
        if (history_status > 0) {
          write(STDOUT_FILENO, history_text, strlen(history_text));
          write(STDOUT_FILENO, "\n", 1);
          if (shell_replace_input(&command_buf, &command_buf_size, &command_len,
                                  history_text) < 0) {
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
        }
        shell_history_add(state, command_buf);
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

static int shell_ensure_buffer(char **buf, int *buf_size, int need)
{
  char *next_buf;
  int next_size;

  if (buf == 0 || *buf == 0 || buf_size == 0)
    return -1;
  if (need <= *buf_size)
    return 0;

  next_size = *buf_size;
  while (next_size < need)
    next_size *= 2;
  next_buf = (char *)malloc((size_t)next_size);
  if (next_buf == 0)
    return -1;
  memset(next_buf, 0, (size_t)next_size);
  memcpy(next_buf, *buf, (size_t)(*buf_size));
  free(*buf);
  *buf = next_buf;
  *buf_size = next_size;
  return 0;
}

static int shell_read_input_line(char **buf, int *buf_size,
                                 int *data_pos, int *data_len,
                                 char **line, int *line_len,
                                 int *add_newline)
{
  if (buf == 0 || *buf == 0 || buf_size == 0 ||
      data_pos == 0 || data_len == 0 ||
      line == 0 || line_len == 0 || add_newline == 0)
    return -1;

  while (1) {
    int i;

    for (i = *data_pos; i < *data_len; i++) {
      if ((*buf)[i] == '\r' || (*buf)[i] == '\n') {
        int consume = 1;

        *line = *buf + *data_pos;
        *line_len = i - *data_pos;
        *add_newline = 1;
        if ((*buf)[i] == '\r' && i + 1 < *data_len && (*buf)[i + 1] == '\n')
          consume = 2;
        *data_pos = i + consume;
        if (*data_pos >= *data_len) {
          *data_pos = 0;
          *data_len = 0;
        }
        return 1;
      }
    }

    if (*data_pos > 0) {
      memmove(*buf, *buf + *data_pos, (size_t)(*data_len - *data_pos));
      *data_len -= *data_pos;
      *data_pos = 0;
      (*buf)[*data_len] = '\0';
    }

    if (*data_len >= *buf_size - 1) {
      if (shell_ensure_buffer(buf, buf_size, *buf_size * 2) < 0)
        return -1;
    }

    i = (int)read(STDIN_FILENO, *buf + *data_len,
                  (size_t)(*buf_size - *data_len - 1));
    if (i < 0)
      return -1;
    if (i == 0) {
      if (*data_len <= 0)
        return 0;
      *line = *buf;
      *line_len = *data_len;
      *add_newline = 0;
      *data_pos = 0;
      *data_len = 0;
      (*buf)[*line_len] = '\0';
      return 1;
    }
    *data_len += i;
    (*buf)[*data_len] = '\0';
  }
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

static int shell_replace_input(char **buf, int *buf_size, int *len,
                               const char *text)
{
  int text_len;

  if (text == 0)
    text = "";
  *len = 0;
  if (*buf != 0)
    (*buf)[0] = '\0';
  text_len = (int)strlen(text);
  return shell_append_input(buf, buf_size, len, text, text_len, 0);
}
