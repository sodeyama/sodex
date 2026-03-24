#include <shell.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fs.h>

#ifdef TEST_BUILD
#include <unistd.h>
#include <fcntl.h>
#endif

static int shell_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return -1;
  if (src == 0)
    src = "";

  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
  return 0;
}

static void shell_write_error_text(const char *text)
{
  if (text == 0)
    return;
  write(STDERR_FILENO, text, strlen(text));
}

static char *shell_read_fd_all(int fd, int *out_len)
{
  char *buf;
  int cap = 512;
  int len = 0;

  if (out_len == 0)
    return 0;

  buf = (char *)malloc((size_t)cap);
  if (buf == 0)
    return 0;

  while (1) {
    int read_len;
    char *next;

    if (len >= cap - 1) {
      int next_cap = cap * 2;
      int copy_len = len;

      next = (char *)malloc((size_t)next_cap);
      if (next == 0) {
        free(buf);
        return 0;
      }
      memset(next, 0, (size_t)next_cap);
      memcpy(next, buf, (size_t)copy_len);
      free(buf);
      buf = next;
      cap = next_cap;
    }

    read_len = (int)read(fd, buf + len, (size_t)(cap - len - 1));
    if (read_len <= 0)
      break;
    len += read_len;
  }

  buf[len] = '\0';
  *out_len = len;
  return buf;
}

int shell_execute_buffer(struct shell_state *state, const char *name,
                         const char *text, int argc, char **argv,
                         int sourced)
{
  struct shell_program *program;
  char saved_name[SHELL_VAR_VALUE_MAX];
  char saved_params[SHELL_MAX_PARAMS][SHELL_VAR_VALUE_MAX];
  int saved_param_count;
  int status;

  if (state == 0 || text == 0)
    return 1;
  shell_state_clear_last_error(state);

  program = (struct shell_program *)malloc(sizeof(*program));
  if (program == 0) {
    shell_state_set_last_error(state, "sh: out of memory\n");
    shell_write_error_text("sh: out of memory\n");
    return 1;
  }

  shell_copy_text(saved_name, sizeof(saved_name), state->script_name);
  saved_param_count = state->param_count;
  memcpy(saved_params, state->param_storage, sizeof(saved_params));
  if (sourced == 0)
    shell_state_set_script(state, name, argc, argv);
  else if (argv != 0)
    shell_state_set_script(state, saved_name, argc, argv);

  if (shell_parse_program(text, (int)strlen(text), program) < 0) {
    shell_state_set_script(state, saved_name, saved_param_count, 0);
    state->param_count = saved_param_count;
    memcpy(state->param_storage, saved_params, sizeof(saved_params));
    state->last_status = 2;
    shell_state_set_last_error(state, "sh: parse error\n");
    shell_write_error_text("sh: parse error\n");
    free(program);
    return 2;
  }

  status = shell_execute_program(state, program);
  if (sourced != 0) {
    shell_copy_text(state->script_name, sizeof(state->script_name), saved_name);
    state->param_count = saved_param_count;
    memcpy(state->param_storage, saved_params, sizeof(saved_params));
  }
  free(program);
  return status;
}

int shell_execute_file(struct shell_state *state, const char *path,
                       int argc, char **argv, int sourced)
{
  char *text;
  int fd;
  int len = 0;
  int status;

  if (state == 0 || path == 0)
    return 1;
  shell_state_clear_last_error(state);

  fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    shell_state_set_last_error(state, "sh: cannot open script\n");
    shell_write_error_text("sh: cannot open script\n");
    return 1;
  }

  text = shell_read_fd_all(fd, &len);
  close(fd);
  if (text == 0) {
    shell_state_set_last_error(state, "sh: out of memory\n");
    shell_write_error_text("sh: out of memory\n");
    return 1;
  }

  status = shell_execute_buffer(state, path, text, argc, argv, sourced);
  free(text);
  return status;
}
