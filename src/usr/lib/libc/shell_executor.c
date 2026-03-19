#include <shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>
#include <debug.h>

struct shell_expanded_command {
  char *argv[SHELL_MAX_ARGS];
  char argv_storage[SHELL_MAX_ARGS][SHELL_WORD_SIZE];
  int argc;
  char assign_name[SHELL_MAX_ASSIGNMENTS][SHELL_VAR_NAME_MAX];
  char assign_value[SHELL_MAX_ASSIGNMENTS][SHELL_VAR_VALUE_MAX];
  int assignment_count;
  char input_path[SHELL_WORD_SIZE];
  char output_path[SHELL_WORD_SIZE];
  int has_input;
  int has_output;
  int append_output;
};

static int shell_write_text(const char *text);

static void shell_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0)
    src = "";

  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static int shell_is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int shell_is_name_start(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    return 1;
  if (ch >= 'A' && ch <= 'Z')
    return 1;
  return ch == '_';
}

static int shell_is_name_char(char ch)
{
  if (shell_is_name_start(ch))
    return 1;
  return shell_is_digit(ch);
}

static int shell_parse_assignment(const char *text, char *name, int name_cap,
                                  char *value, int value_cap)
{
  int i;

  if (text == 0 || shell_is_name_start(text[0]) == 0)
    return -1;

  for (i = 1; text[i] != '\0'; i++) {
    if (text[i] == '=')
      break;
    if (shell_is_name_char(text[i]) == 0)
      return -1;
  }
  if (text[i] != '=')
    return -1;

  if (name != 0 && name_cap > 0) {
    int j;
    for (j = 0; j < i && j < name_cap - 1; j++)
      name[j] = text[j];
    name[j] = '\0';
  }
  if (value != 0 && value_cap > 0)
    shell_copy_text(value, value_cap, text + i + 1);
  return 0;
}

static void shell_status_text(int value, char *buf, int cap)
{
  char tmp[16];
  int len = 0;
  int negative = 0;

  if (cap <= 0 || buf == 0)
    return;
  if (value == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  if (value < 0) {
    negative = 1;
    value = -value;
  }
  while (value > 0 && len < (int)sizeof(tmp)) {
    tmp[len++] = (char)('0' + (value % 10));
    value /= 10;
  }
  if (negative && len < (int)sizeof(tmp))
    tmp[len++] = '-';
  if (len >= cap)
    len = cap - 1;
  while (len > 0) {
    *buf++ = tmp[--len];
  }
  *buf = '\0';
}

static const char *shell_param_value(const struct shell_state *state, int index)
{
  if (state == 0)
    return "";
  if (index == 0)
    return state->script_name;
  if (index < 0 || index > state->param_count)
    return "";
  return state->param_storage[index - 1];
}

static void shell_join_params(const struct shell_state *state,
                              char *buf, int cap)
{
  int i;
  int len = 0;

  if (buf == 0 || cap <= 0)
    return;
  buf[0] = '\0';
  if (state == 0)
    return;

  for (i = 0; i < state->param_count; i++) {
    const char *text = state->param_storage[i];
    int j;

    if (i > 0 && len < cap - 1)
      buf[len++] = ' ';
    for (j = 0; text[j] != '\0' && len < cap - 1; j++)
      buf[len++] = text[j];
  }
  buf[len] = '\0';
}

static void shell_append_text(char *dst, int cap, int *len, const char *src)
{
  int i;

  if (dst == 0 || len == 0 || cap <= 0 || src == 0)
    return;

  for (i = 0; src[i] != '\0' && *len < cap - 1; i++)
    dst[(*len)++] = src[i];
  dst[*len] = '\0';
}

static int shell_expand_word(const struct shell_state *state, const char *raw,
                             char *out, int cap)
{
  int len = 0;
  char quote = '\0';
  int i;

  if (raw == 0 || out == 0 || cap <= 0)
    return -1;

  out[0] = '\0';
  for (i = 0; raw[i] != '\0'; i++) {
    char ch = raw[i];

    if (quote == '\'') {
      if (ch == '\'') {
        quote = '\0';
        continue;
      }
      if (len >= cap - 1)
        return -1;
      out[len++] = ch;
      out[len] = '\0';
      continue;
    }

    if (ch == '\'' && quote == '\0') {
      quote = '\'';
      continue;
    }
    if (ch == '"' && quote == '\0') {
      quote = '"';
      continue;
    }
    if (ch == '"' && quote == '"') {
      quote = '\0';
      continue;
    }
    if (ch == '\\') {
      char next = raw[i + 1];
      if (next == '\0')
        continue;
      if (quote == '"' && next != '"' && next != '$' && next != '\\') {
        if (len >= cap - 1)
          return -1;
        out[len++] = ch;
        out[len] = '\0';
        continue;
      }
      if (len >= cap - 1)
        return -1;
      out[len++] = next;
      out[len] = '\0';
      i++;
      continue;
    }
    if (ch == '$') {
      char value[SHELL_VAR_VALUE_MAX];
      int j = i + 1;

      value[0] = '\0';
      if (raw[j] == '?') {
        shell_status_text(state->last_status, value, sizeof(value));
        i = j;
      } else if (raw[j] == '!') {
        shell_status_text((int)state->last_background_pid,
                          value, sizeof(value));
        i = j;
      } else if (raw[j] == '@' || raw[j] == '*') {
        shell_join_params(state, value, sizeof(value));
        i = j;
      } else if (shell_is_digit(raw[j])) {
        int index = 0;
        while (shell_is_digit(raw[j])) {
          index = index * 10 + (raw[j] - '0');
          j++;
        }
        shell_copy_text(value, sizeof(value), shell_param_value(state, index));
        i = j - 1;
      } else if (shell_is_name_start(raw[j])) {
        char name[SHELL_VAR_NAME_MAX];
        int name_len = 0;

        while (shell_is_name_char(raw[j]) && name_len < SHELL_VAR_NAME_MAX - 1)
          name[name_len++] = raw[j++];
        name[name_len] = '\0';
        shell_copy_text(value, sizeof(value), shell_var_get(state, name));
        i = j - 1;
      } else {
        value[0] = '$';
        value[1] = '\0';
      }

      shell_append_text(out, cap, &len, value);
      continue;
    }
    if (len >= cap - 1)
      return -1;
    out[len++] = ch;
    out[len] = '\0';
  }

  if (quote != '\0')
    return -1;
  return 0;
}

static int shell_expand_command(const struct shell_state *state,
                                const struct shell_command *command,
                                struct shell_expanded_command *out)
{
  int i;

  if (command == 0 || out == 0)
    return -1;

  memset(out, 0, sizeof(*out));
  out->append_output = command->append_output;
  for (i = 0; i < command->assignment_count; i++) {
    char text[SHELL_WORD_SIZE];

    if (i >= SHELL_MAX_ASSIGNMENTS)
      return -1;
    if (shell_expand_word(state, command->assignments[i],
                          text, sizeof(text)) < 0)
      return -1;
    if (shell_parse_assignment(text,
                               out->assign_name[i], sizeof(out->assign_name[i]),
                               out->assign_value[i], sizeof(out->assign_value[i])) < 0)
      return -1;
    out->assignment_count++;
  }

  for (i = 0; i < command->argc; i++) {
    if (i >= SHELL_MAX_ARGS - 1)
      return -1;
    if (shell_expand_word(state, command->argv[i],
                          out->argv_storage[i], sizeof(out->argv_storage[i])) < 0)
      return -1;
    out->argv[i] = out->argv_storage[i];
    out->argc++;
  }
  out->argv[out->argc] = 0;

  if (command->input_path != 0) {
    if (shell_expand_word(state, command->input_path,
                          out->input_path, sizeof(out->input_path)) < 0)
      return -1;
    out->has_input = 1;
  }
  if (command->output_path != 0) {
    if (shell_expand_word(state, command->output_path,
                          out->output_path, sizeof(out->output_path)) < 0)
      return -1;
    out->has_output = 1;
  }

  return 0;
}

static int shell_swap_fd(int target_fd, int next_fd)
{
  int saved_fd;
  int new_fd;

  saved_fd = dup(target_fd);
  if (saved_fd < 0)
    return -1;
  close(target_fd);
  new_fd = dup(next_fd);
  if (new_fd != target_fd)
    return -1;
  return saved_fd;
}

static void shell_restore_fd(int target_fd, int saved_fd)
{
  if (saved_fd < 0)
    return;
  close(target_fd);
  dup(saved_fd);
  close(saved_fd);
}

static void shell_append_job_text(char *dst, int cap, int *len, const char *src)
{
  int i;

  if (dst == 0 || len == 0 || cap <= 0 || src == 0)
    return;

  for (i = 0; src[i] != '\0' && *len < cap - 1; i++)
    dst[(*len)++] = src[i];
  dst[*len] = '\0';
}

static void shell_describe_command(char *dst, int cap, int *len,
                                   struct shell_expanded_command *command,
                                   int add_pipe)
{
  int i;

  if (command == 0)
    return;

  for (i = 0; i < command->argc; i++) {
    if (*len > 0 && *len < cap - 1)
      dst[(*len)++] = ' ';
    shell_append_job_text(dst, cap, len, command->argv[i]);
  }
  if (command->has_input) {
    if (*len > 0 && *len < cap - 1)
      dst[(*len)++] = ' ';
    shell_append_job_text(dst, cap, len, "< ");
    shell_append_job_text(dst, cap, len, command->input_path);
  }
  if (command->has_output) {
    if (*len > 0 && *len < cap - 1)
      dst[(*len)++] = ' ';
    if (command->append_output)
      shell_append_job_text(dst, cap, len, ">> ");
    else
      shell_append_job_text(dst, cap, len, "> ");
    shell_append_job_text(dst, cap, len, command->output_path);
  }
  if (add_pipe != 0)
    shell_append_job_text(dst, cap, len, " |");
  dst[*len] = '\0';
}

static void shell_bg_remove(struct shell_state *state, pid_t pid)
{
  int i;

  if (state == 0 || pid < 0)
    return;
  for (i = 0; i < state->background_count; i++) {
    if (state->background_pids[i] != pid)
      continue;
    for (; i + 1 < state->background_count; i++)
      state->background_pids[i] = state->background_pids[i + 1];
    state->background_count--;
    break;
  }
  for (i = 0; i < state->job_count; i++) {
    if (state->jobs[i].pid != pid)
      continue;
    for (; i + 1 < state->job_count; i++)
      state->jobs[i] = state->jobs[i + 1];
    state->job_count--;
    break;
  }
  if (state->background_count > 0)
    state->last_background_pid = state->background_pids[state->background_count - 1];
  else
    state->last_background_pid = 0;
}

static int shell_job_find_pid(const struct shell_state *state, pid_t pid)
{
  int i;

  if (state == 0 || pid < 0)
    return -1;
  for (i = 0; i < state->job_count; i++) {
    if (state->jobs[i].pid == pid)
      return i;
  }
  return -1;
}

static int shell_job_find_id(const struct shell_state *state, int id)
{
  int i;

  if (state == 0 || id <= 0)
    return -1;
  for (i = 0; i < state->job_count; i++) {
    if (state->jobs[i].id == id)
      return i;
  }
  return -1;
}

static void shell_bg_add(struct shell_state *state, pid_t pid, const char *command)
{
  int job_id = 0;
  char buf[32];

  if (state == 0 || pid < 0)
    return;
  if (state->background_count < SHELL_MAX_BG_PIDS)
    state->background_pids[state->background_count++] = pid;
  if (state->job_count < SHELL_MAX_BG_PIDS) {
    int index = state->job_count++;

    if (state->next_job_id <= 0)
      state->next_job_id = 1;
    state->jobs[index].id = state->next_job_id++;
    state->jobs[index].pid = pid;
    shell_copy_text(state->jobs[index].command,
                    sizeof(state->jobs[index].command),
                    command != 0 && command[0] != '\0' ? command : "job");
    job_id = state->jobs[index].id;
  }
  state->last_background_pid = pid;
  if (state->interactive != 0 && job_id > 0) {
    shell_write_text("[");
    shell_status_text(job_id, buf, sizeof(buf));
    shell_write_text(buf);
    shell_write_text("] ");
    shell_status_text((int)pid, buf, sizeof(buf));
    shell_write_text(buf);
    shell_write_text("\n");
  }
}

static void shell_write_job(struct shell_job *job)
{
  char buf[32];

  if (job == 0)
    return;

  shell_write_text("[");
  shell_status_text(job->id, buf, sizeof(buf));
  shell_write_text(buf);
  shell_write_text("] ");
  shell_status_text((int)job->pid, buf, sizeof(buf));
  shell_write_text(buf);
  shell_write_text(" ");
  shell_write_text(job->command);
  shell_write_text("\n");
}

static struct shell_job *shell_resolve_job(struct shell_state *state,
                                           struct shell_expanded_command *command)
{
  int index = -1;

  if (state == 0)
    return 0;

  if (command == 0 || command->argc <= 1) {
    if (state->last_background_pid > 0)
      index = shell_job_find_pid(state, state->last_background_pid);
  } else if (command->argv[1][0] == '%') {
    index = shell_job_find_id(state, atoi(command->argv[1] + 1));
  } else {
    index = shell_job_find_pid(state, (pid_t)atoi(command->argv[1]));
  }

  if (index < 0)
    return 0;
  return &state->jobs[index];
}

void shell_reap_background(struct shell_state *state)
{
  int status;
  pid_t pid;

  if (state == 0)
    return;

  while (1) {
    pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0)
      break;
    shell_bg_remove(state, pid);
  }
}

static int shell_write_text(const char *text)
{
  if (text == 0)
    return 0;
  return (int)write(STDOUT_FILENO, text, strlen(text));
}

static void shell_debug_audit(struct shell_expanded_command *command)
{
  char buf[SHELL_WORD_SIZE];
  int len = 0;
  int i;

  if (command == 0 || command->argc <= 1)
    return;
  if (strcmp(command->argv[1], "AUDIT") != 0)
    return;

  for (i = 1; i < command->argc && len < SHELL_WORD_SIZE - 1; i++) {
    int j;
    const char *text = command->argv[i];

    if (i > 1 && len < SHELL_WORD_SIZE - 1)
      buf[len++] = ' ';
    for (j = 0; text[j] != '\0' && len < SHELL_WORD_SIZE - 1; j++)
      buf[len++] = text[j];
  }
  if (len < SHELL_WORD_SIZE - 1)
    buf[len++] = '\n';
  buf[len] = '\0';
  debug_write(buf, (size_t)len);
}

static int shell_builtin_echo(struct shell_expanded_command *command)
{
  int i;

  for (i = 1; i < command->argc; i++) {
    if (i > 1)
      shell_write_text(" ");
    shell_write_text(command->argv[i]);
  }
  shell_write_text("\n");
  shell_debug_audit(command);
  return 0;
}

static int shell_builtin_cd(struct shell_state *state,
                            struct shell_expanded_command *command)
{
  const char *path = "/";

  if (state != 0) {
    const char *home = shell_var_get(state, "HOME");

    if (home != 0 && home[0] != '\0')
      path = home;
  }
  if (command->argc > 1)
    path = command->argv[1];
  if (chdir((char *)path) < 0) {
    shell_write_text("sh: cd failed\n");
    return 1;
  }
  return 0;
}

static int shell_builtin_export(struct shell_state *state,
                                struct shell_expanded_command *command)
{
  int i;

  for (i = 1; i < command->argc; i++) {
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VALUE_MAX];

    if (shell_parse_assignment(command->argv[i], name, sizeof(name),
                               value, sizeof(value)) == 0) {
      if (shell_var_set(state, name, value, 1) < 0)
        return 1;
      continue;
    }
    if (shell_var_set(state, command->argv[i],
                      shell_var_get(state, command->argv[i]), 1) < 0)
      return 1;
  }
  return 0;
}

static int shell_builtin_set(struct shell_state *state,
                             struct shell_expanded_command *command)
{
  int i;

  if (command->argc <= 1) {
    for (i = 0; i < state->var_count; i++) {
      shell_write_text(state->vars[i].name);
      shell_write_text("=");
      shell_write_text(state->vars[i].value);
      shell_write_text("\n");
    }
    return 0;
  }

  for (i = 1; i < command->argc; i++) {
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VALUE_MAX];

    if (shell_parse_assignment(command->argv[i], name, sizeof(name),
                               value, sizeof(value)) < 0)
      return 1;
    if (shell_var_set(state, name, value, -1) < 0)
      return 1;
  }
  return 0;
}

static int shell_builtin_wait(struct shell_state *state,
                              struct shell_expanded_command *command)
{
  int status = 0;
  int i;

  if (command->argc <= 1) {
    while (state->background_count > 0) {
      pid_t pid = state->background_pids[0];

      if (waitpid(pid, &status, 0) < 0) {
        shell_bg_remove(state, pid);
        continue;
      }
      shell_bg_remove(state, pid);
    }
    return status;
  }

  for (i = 1; i < command->argc; i++) {
    pid_t pid = (pid_t)atoi(command->argv[i]);

    if (waitpid(pid, &status, 0) < 0)
      return 1;
    shell_bg_remove(state, pid);
  }
  return status;
}

static int shell_builtin_jobs(struct shell_state *state)
{
  int i;

  if (state == 0)
    return 1;
  for (i = 0; i < state->job_count; i++)
    shell_write_job(&state->jobs[i]);
  return 0;
}

static int shell_builtin_fg(struct shell_state *state,
                            struct shell_expanded_command *command)
{
  struct shell_job *job;
  pid_t pid;
  int status = 0;

  job = shell_resolve_job(state, command);
  if (job == 0) {
    shell_write_text("sh: fg: job not found\n");
    return 1;
  }

  pid = job->pid;
  if (kill(pid, SIGCONT) < 0) {
    shell_bg_remove(state, pid);
    return 1;
  }
  set_foreground_pid(STDIN_FILENO, pid);
  if (waitpid(pid, &status, 0) < 0)
    status = 1;
  set_foreground_pid(STDIN_FILENO, 0);
  shell_bg_remove(state, pid);
  return status;
}

static int shell_builtin_bg(struct shell_state *state,
                            struct shell_expanded_command *command)
{
  struct shell_job *job;

  job = shell_resolve_job(state, command);
  if (job == 0) {
    shell_write_text("sh: bg: job not found\n");
    return 1;
  }
  if (kill(job->pid, SIGCONT) < 0) {
    shell_bg_remove(state, job->pid);
    return 1;
  }
  return 0;
}

static int shell_builtin_dot(struct shell_state *state,
                             struct shell_expanded_command *command)
{
  if (command->argc < 2)
    return 1;
  return shell_execute_file(state, command->argv[1],
                            command->argc - 2, &command->argv[2], 1);
}

static int shell_builtin_exit(struct shell_state *state,
                              struct shell_expanded_command *command)
{
  int status = state->last_status;

  if (command->argc > 1)
    status = atoi(command->argv[1]);
  state->exit_requested = 1;
  state->exit_status = status;
  return status;
}

/* ---- builtin [ / test ---- */

static int test_file_exists(const char *path)
{
  int fd = open(path, O_RDONLY, 0);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static int test_is_nonempty_string(const char *s)
{
  return s != 0 && s[0] != '\0';
}

/*
 * Evaluate a single test primary.  Returns 1 for true, 0 for false.
 * *pos is advanced past the consumed tokens.
 */
static int test_eval_primary(int argc, char **argv, int *pos)
{
  const char *tok;

  if (*pos >= argc)
    return 0;

  tok = argv[*pos];

  /* unary: ! expr */
  if (strcmp(tok, "!") == 0) {
    (*pos)++;
    return !test_eval_primary(argc, argv, pos);
  }

  /* -f FILE */
  if (strcmp(tok, "-f") == 0 || strcmp(tok, "-e") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_file_exists(argv[*pos - 1]);
  }

  /* -d DIR (use open; good enough for this OS) */
  if (strcmp(tok, "-d") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_file_exists(argv[*pos - 1]);
  }

  /* -n STRING */
  if (strcmp(tok, "-n") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 0;
    (*pos)++;
    return test_is_nonempty_string(argv[*pos - 1]);
  }

  /* -z STRING */
  if (strcmp(tok, "-z") == 0) {
    (*pos)++;
    if (*pos >= argc)
      return 1;
    (*pos)++;
    return !test_is_nonempty_string(argv[*pos - 1]);
  }

  /* binary: STRING = STRING  /  STRING != STRING */
  if (*pos + 2 < argc) {
    const char *op = argv[*pos + 1];

    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
      int r = strcmp(argv[*pos], argv[*pos + 2]) == 0;
      *pos += 3;
      return r;
    }
    if (strcmp(op, "!=") == 0) {
      int r = strcmp(argv[*pos], argv[*pos + 2]) != 0;
      *pos += 3;
      return r;
    }
  }

  /* bare string — true if non-empty */
  (*pos)++;
  return test_is_nonempty_string(tok);
}

static int shell_builtin_test(struct shell_expanded_command *command)
{
  int is_bracket;
  int argc;
  int pos;
  int result;

  is_bracket = (strcmp(command->argv[0], "[") == 0);

  argc = command->argc;
  /* strip trailing ] for [ ... ] syntax */
  if (is_bracket) {
    if (argc < 2 || strcmp(command->argv[argc - 1], "]") != 0)
      return 2; /* syntax error */
    argc--;
  }

  pos = 1; /* skip argv[0] which is "[" or "test" */
  result = test_eval_primary(argc, command->argv, &pos);

  return result ? 0 : 1;
}

static int shell_apply_assignments(struct shell_state *state,
                                   const struct shell_expanded_command *command)
{
  int i;

  for (i = 0; i < command->assignment_count; i++) {
    if (shell_var_set(state, command->assign_name[i],
                      command->assign_value[i], -1) < 0)
      return 1;
  }
  return 0;
}

static int shell_builtin_run(struct shell_state *state,
                             struct shell_expanded_command *command)
{
  const char *name;

  if (command->argc <= 0 || command->argv[0] == 0)
    return shell_apply_assignments(state, command);

  if (shell_apply_assignments(state, command) != 0)
    return 1;

  name = command->argv[0];
  if (strcmp(name, "cd") == 0)
    return shell_builtin_cd(state, command);
  if (strcmp(name, "exit") == 0)
    return shell_builtin_exit(state, command);
  if (strcmp(name, "export") == 0)
    return shell_builtin_export(state, command);
  if (strcmp(name, "set") == 0)
    return shell_builtin_set(state, command);
  if (strcmp(name, ".") == 0)
    return shell_builtin_dot(state, command);
  if (strcmp(name, "wait") == 0)
    return shell_builtin_wait(state, command);
  if (strcmp(name, "jobs") == 0)
    return shell_builtin_jobs(state);
  if (strcmp(name, "fg") == 0)
    return shell_builtin_fg(state, command);
  if (strcmp(name, "bg") == 0)
    return shell_builtin_bg(state, command);
  if (strcmp(name, "trap") == 0)
    return 0;
  if (strcmp(name, "echo") == 0)
    return shell_builtin_echo(command);
  if (strcmp(name, "true") == 0)
    return 0;
  if (strcmp(name, "false") == 0)
    return 1;
  if (strcmp(name, "[") == 0 || strcmp(name, "test") == 0)
    return shell_builtin_test(command);
  return -1;
}

static pid_t shell_spawn_script_fallback(struct shell_expanded_command *command)
{
  char *argv[SHELL_MAX_ARGS + 2];
  int i;

  argv[0] = "sh";
  argv[1] = command->argv[0];
  for (i = 1; i < command->argc && i < SHELL_MAX_ARGS; i++)
    argv[i + 1] = command->argv[i];
  argv[i + 1] = 0;
  return execve("/usr/bin/sh", argv, 0);
}

static pid_t shell_spawn_external(struct shell_state *state,
                                  struct shell_expanded_command *command)
{
  pid_t pid;
  int fd;

  (void)state;
  pid = execve(command->argv[0], command->argv, 0);
  if (pid >= 0)
    return pid;

  if (strchr(command->argv[0], '/') == 0)
    return -1;

  fd = open(command->argv[0], O_RDONLY, 0);
  if (fd < 0)
    return -1;
  close(fd);
  return shell_spawn_script_fallback(command);
}

static int shell_command_needs_builtin_parent(struct shell_expanded_command *command)
{
  if (command->argc <= 0)
    return 1;
  if (strcmp(command->argv[0], "cd") == 0)
    return 1;
  if (strcmp(command->argv[0], "exit") == 0)
    return 1;
  if (strcmp(command->argv[0], "export") == 0)
    return 1;
  if (strcmp(command->argv[0], "set") == 0)
    return 1;
  if (strcmp(command->argv[0], ".") == 0)
    return 1;
  if (strcmp(command->argv[0], "wait") == 0)
    return 1;
  if (strcmp(command->argv[0], "jobs") == 0)
    return 1;
  if (strcmp(command->argv[0], "fg") == 0)
    return 1;
  if (strcmp(command->argv[0], "bg") == 0)
    return 1;
  if (strcmp(command->argv[0], "trap") == 0)
    return 1;
  if (strcmp(command->argv[0], "echo") == 0)
    return 1;
  if (strcmp(command->argv[0], "true") == 0)
    return 1;
  if (strcmp(command->argv[0], "false") == 0)
    return 1;
  return 0;
}

static int shell_run_parent_command(struct shell_state *state,
                                    struct shell_expanded_command *command)
{
  int saved_stdin = -1;
  int saved_stdout = -1;
  int input_fd = -1;
  int output_fd = -1;
  int flags;
  int status;

  if (command->has_input) {
    input_fd = open(command->input_path, O_RDONLY, 0);
    if (input_fd < 0)
      return 1;
    saved_stdin = shell_swap_fd(STDIN_FILENO, input_fd);
    close(input_fd);
    if (saved_stdin < 0)
      return 1;
  }

  if (command->has_output) {
    flags = O_CREAT | O_WRONLY;
    if (command->append_output)
      flags |= O_APPEND;
    else
      flags |= O_TRUNC;
    output_fd = open(command->output_path, flags, 0644);
    if (output_fd < 0) {
      shell_restore_fd(STDIN_FILENO, saved_stdin);
      return 1;
    }
    saved_stdout = shell_swap_fd(STDOUT_FILENO, output_fd);
    close(output_fd);
    if (saved_stdout < 0) {
      shell_restore_fd(STDIN_FILENO, saved_stdin);
      return 1;
    }
  }

  status = shell_builtin_run(state, command);
  shell_restore_fd(STDIN_FILENO, saved_stdin);
  shell_restore_fd(STDOUT_FILENO, saved_stdout);
  return status;
}

static int shell_wait_pipeline(struct shell_state *state,
                               pid_t *pids, int pid_count)
{
  int i;
  int status = 0;

  if (pid_count <= 0)
    return 0;

  set_foreground_pid(STDIN_FILENO, pids[pid_count - 1]);
  for (i = 0; i < pid_count; i++) {
    if (waitpid(pids[i], &status, 0) < 0)
      status = 1;
  }
  set_foreground_pid(STDIN_FILENO, 0);
  return status;
}

static int shell_execute_pipeline(struct shell_state *state,
                                  const struct shell_pipeline *pipeline)
{
  pid_t pids[SHELL_MAX_COMMANDS];
  int pid_count = 0;
  int prev_input_fd = -1;
  char job_text[SHELL_JOB_TEXT_MAX];
  int job_text_len = 0;
  int i;
  int async = (pipeline->next_type == SHELL_NEXT_BACKGROUND);

  memset(job_text, 0, sizeof(job_text));

  for (i = 0; i < pipeline->command_count; i++) {
    struct shell_expanded_command *command;
    int pipefd[2] = {-1, -1};
    int saved_stdin = -1;
    int saved_stdout = -1;
    int input_fd = -1;
    int output_fd = -1;
    int flags;
    pid_t pid;

    command = (struct shell_expanded_command *)malloc(sizeof(*command));
    if (command == 0)
      return 1;

    if (shell_expand_command(state, &pipeline->commands[i], command) < 0) {
      free(command);
      return 1;
    }
    shell_describe_command(job_text, sizeof(job_text), &job_text_len,
                           command, i + 1 < pipeline->command_count);

    if (pipeline->command_count == 1 &&
        async == 0 &&
        shell_command_needs_builtin_parent(command) != 0) {
      int status = shell_run_parent_command(state, command);

      free(command);
      return status;
    }

    if (command->argc > 0 && shell_command_needs_builtin_parent(command) != 0) {
      shell_write_text("sh: builtin in pipeline/background is unsupported\n");
      free(command);
      return 1;
    }

    if (prev_input_fd >= 0 && command->has_input == 0) {
      saved_stdin = shell_swap_fd(STDIN_FILENO, prev_input_fd);
      close(prev_input_fd);
      prev_input_fd = -1;
      if (saved_stdin < 0) {
        free(command);
        return 1;
      }
    } else if (prev_input_fd >= 0) {
      close(prev_input_fd);
      prev_input_fd = -1;
    }

    if (command->has_input) {
      input_fd = open(command->input_path, O_RDONLY, 0);
      if (input_fd < 0) {
        shell_restore_fd(STDIN_FILENO, saved_stdin);
        free(command);
        return 1;
      }
      shell_restore_fd(STDIN_FILENO, saved_stdin);
      saved_stdin = shell_swap_fd(STDIN_FILENO, input_fd);
      close(input_fd);
      if (saved_stdin < 0) {
        free(command);
        return 1;
      }
    }

    if (i + 1 < pipeline->command_count) {
      if (pipe(pipefd) < 0) {
        shell_restore_fd(STDIN_FILENO, saved_stdin);
        free(command);
        return 1;
      }
      saved_stdout = shell_swap_fd(STDOUT_FILENO, pipefd[1]);
      close(pipefd[1]);
      if (saved_stdout < 0) {
        close(pipefd[0]);
        shell_restore_fd(STDIN_FILENO, saved_stdin);
        free(command);
        return 1;
      }
    } else if (command->has_output) {
      flags = O_CREAT | O_WRONLY;
      if (command->append_output)
        flags |= O_APPEND;
      else
        flags |= O_TRUNC;
      output_fd = open(command->output_path, flags, 0644);
      if (output_fd < 0) {
        shell_restore_fd(STDIN_FILENO, saved_stdin);
        free(command);
        return 1;
      }
      saved_stdout = shell_swap_fd(STDOUT_FILENO, output_fd);
      close(output_fd);
      if (saved_stdout < 0) {
        shell_restore_fd(STDIN_FILENO, saved_stdin);
        free(command);
        return 1;
      }
    }

    if (shell_apply_assignments(state, command) != 0) {
      shell_restore_fd(STDIN_FILENO, saved_stdin);
      shell_restore_fd(STDOUT_FILENO, saved_stdout);
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      free(command);
      return 1;
    }

    if (command->argc == 0) {
      shell_restore_fd(STDIN_FILENO, saved_stdin);
      shell_restore_fd(STDOUT_FILENO, saved_stdout);
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      free(command);
      continue;
    }

    pid = shell_spawn_external(state, command);
    shell_restore_fd(STDIN_FILENO, saved_stdin);
    shell_restore_fd(STDOUT_FILENO, saved_stdout);
    if (pid < 0) {
      if (pipefd[0] >= 0)
        close(pipefd[0]);
      shell_write_text("sh: command not found\n");
      free(command);
      return 127;
    }

    if (pipefd[0] >= 0)
      prev_input_fd = pipefd[0];
    pids[pid_count++] = pid;
    free(command);
  }

  if (prev_input_fd >= 0)
    close(prev_input_fd);

  if (async != 0) {
    if (pid_count > 0)
      shell_bg_add(state, pids[pid_count - 1], job_text);
    return 0;
  }
  return shell_wait_pipeline(state, pids, pid_count);
}

int shell_execute_program(struct shell_state *state,
                          const struct shell_program *program)
{
  enum shell_next_type prev_next = SHELL_NEXT_SEQ;
  int status = state->last_status;
  int i;

  if (state == 0 || program == 0)
    return 1;

  for (i = 0; i < program->pipeline_count; i++) {
    int should_run = 1;

    if (prev_next == SHELL_NEXT_AND && status != 0)
      should_run = 0;
    if (prev_next == SHELL_NEXT_OR && status == 0)
      should_run = 0;

    shell_reap_background(state);
    if (should_run != 0)
      status = shell_execute_pipeline(state, &program->pipelines[i]);
    prev_next = program->pipelines[i].next_type;
    state->last_status = status;
    if (state->exit_requested != 0)
      break;
  }

  shell_reap_background(state);
  return status;
}

int shell_execute_string(struct shell_state *state, const char *text)
{
  return shell_execute_buffer(state, state != 0 ? state->script_name : "sh",
                              text, 0, 0, 1);
}
