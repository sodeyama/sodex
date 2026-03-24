#include <shell.h>
#include <string.h>

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

static int shell_name_char(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    return 1;
  if (ch >= 'A' && ch <= 'Z')
    return 1;
  if (ch >= '0' && ch <= '9')
    return 1;
  return ch == '_';
}

static int shell_name_valid(const char *name)
{
  int i;

  if (name == 0 || name[0] == '\0')
    return 0;
  if ((name[0] >= '0' && name[0] <= '9'))
    return 0;

  for (i = 0; name[i] != '\0'; i++) {
    if (shell_name_char(name[i]) == 0)
      return 0;
  }
  return 1;
}

static int shell_find_var(const struct shell_state *state, const char *name)
{
  int i;

  if (state == 0 || name == 0)
    return -1;

  for (i = 0; i < state->var_count; i++) {
    if (strcmp(state->vars[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int shell_find_alias(const struct shell_state *state, const char *name)
{
  int i;

  if (state == 0 || name == 0)
    return -1;

  for (i = 0; i < state->alias_count; i++) {
    if (strcmp(state->aliases[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int shell_trim_command_text(const char *src, char *dst, int cap)
{
  int len;

  if (dst == 0 || cap <= 0)
    return -1;
  dst[0] = '\0';
  if (src == 0)
    return 0;

  shell_copy_text(dst, cap, src);
  len = (int)strlen(dst);
  while (len > 0 &&
         (dst[len - 1] == '\n' || dst[len - 1] == '\r')) {
    len--;
    dst[len] = '\0';
  }
  return len;
}

static int shell_history_find_prefix(const struct shell_state *state,
                                     const char *prefix)
{
  int i;
  int prefix_len;

  if (state == 0 || prefix == 0 || prefix[0] == '\0')
    return -1;

  prefix_len = (int)strlen(prefix);
  for (i = state->history_count - 1; i >= 0; i--) {
    if (strncmp(state->history[i], prefix, (size_t)prefix_len) == 0)
      return i;
  }
  return -1;
}

void shell_state_init(struct shell_state *state, int interactive)
{
  if (state == 0)
    return;

  memset(state, 0, sizeof(*state));
  state->interactive = interactive;
  state->next_job_id = 1;
  state->history_base = 1;
  shell_copy_text(state->script_name, sizeof(state->script_name), "sh");
  shell_var_set(state, "PATH", "/usr/bin", 1);
  shell_var_set(state, "HOME", "/home/user", 1);
  shell_var_set(state, "TERM", "sodex", 1);
}

void shell_state_set_script(struct shell_state *state, const char *name,
                            int argc, char **argv)
{
  int i;

  if (state == 0)
    return;

  shell_copy_text(state->script_name, sizeof(state->script_name),
                  name != 0 ? name : "sh");
  state->param_count = 0;
  if (argv == 0)
    return;

  for (i = 0; i < argc && i < SHELL_MAX_PARAMS; i++) {
    shell_copy_text(state->param_storage[i],
                    sizeof(state->param_storage[i]), argv[i]);
    state->param_count++;
  }
}

int shell_var_set(struct shell_state *state, const char *name,
                  const char *value, int exported)
{
  int index;

  if (state == 0 || shell_name_valid(name) == 0)
    return -1;

  index = shell_find_var(state, name);
  if (index < 0) {
    if (state->var_count >= SHELL_MAX_VARS)
      return -1;
    index = state->var_count++;
    shell_copy_text(state->vars[index].name,
                    sizeof(state->vars[index].name), name);
  }

  shell_copy_text(state->vars[index].value,
                  sizeof(state->vars[index].value),
                  value != 0 ? value : "");
  if (exported >= 0)
    state->vars[index].exported = exported;
  return 0;
}

const char *shell_var_get(const struct shell_state *state, const char *name)
{
  int index;

  if (state == 0 || name == 0)
    return "";

  index = shell_find_var(state, name);
  if (index < 0)
    return "";
  return state->vars[index].value;
}

int shell_alias_set(struct shell_state *state, const char *name,
                    const char *value)
{
  int index;

  if (state == 0 || shell_name_valid(name) == 0)
    return -1;

  index = shell_find_alias(state, name);
  if (index < 0) {
    if (state->alias_count >= SHELL_MAX_ALIASES)
      return -1;
    index = state->alias_count++;
    shell_copy_text(state->aliases[index].name,
                    sizeof(state->aliases[index].name), name);
  }

  shell_copy_text(state->aliases[index].value,
                  sizeof(state->aliases[index].value),
                  value != 0 ? value : "");
  return 0;
}

const char *shell_alias_get(const struct shell_state *state, const char *name)
{
  int index;

  if (state == 0 || name == 0)
    return 0;

  index = shell_find_alias(state, name);
  if (index < 0)
    return 0;
  return state->aliases[index].value;
}

int shell_alias_unset(struct shell_state *state, const char *name)
{
  int index;

  if (state == 0 || name == 0)
    return -1;

  index = shell_find_alias(state, name);
  if (index < 0)
    return -1;

  for (; index + 1 < state->alias_count; index++)
    state->aliases[index] = state->aliases[index + 1];
  state->alias_count--;
  return 0;
}

void shell_alias_clear(struct shell_state *state)
{
  if (state == 0)
    return;
  state->alias_count = 0;
}

int shell_history_add(struct shell_state *state, const char *text)
{
  char trimmed[SHELL_HISTORY_TEXT_MAX];
  int len;

  if (state == 0)
    return -1;

  len = shell_trim_command_text(text, trimmed, sizeof(trimmed));
  if (len < 0)
    return -1;
  if (len == 0)
    return 0;

  if (state->history_count >= SHELL_HISTORY_MAX) {
    int i;

    for (i = 1; i < state->history_count; i++)
      memcpy(state->history[i - 1], state->history[i],
             sizeof(state->history[i - 1]));
    state->history_count = SHELL_HISTORY_MAX - 1;
    state->history_base++;
  }

  shell_copy_text(state->history[state->history_count],
                  sizeof(state->history[state->history_count]),
                  trimmed);
  state->history_count++;
  return 0;
}

int shell_history_expand_line(const struct shell_state *state,
                              const char *input,
                              char *out, int cap)
{
  char trimmed[SHELL_HISTORY_TEXT_MAX];
  int len;
  int index = -1;

  if (out == 0 || cap <= 0)
    return -1;
  out[0] = '\0';
  if (state == 0 || input == 0)
    return 0;

  len = shell_trim_command_text(input, trimmed, sizeof(trimmed));
  if (len < 0)
    return -1;
  if (trimmed[0] != '!')
    return 0;
  if (trimmed[1] == '\0')
    return -1;

  if (strcmp(trimmed, "!!") == 0) {
    if (state->history_count <= 0)
      return -1;
    index = state->history_count - 1;
  } else {
    index = shell_history_find_prefix(state, trimmed + 1);
    if (index < 0)
      return -1;
  }

  if ((int)strlen(state->history[index]) >= cap)
    return -1;
  shell_copy_text(out, cap, state->history[index]);
  return 1;
}

int shell_history_count(const struct shell_state *state)
{
  if (state == 0)
    return 0;
  return state->history_count;
}

const char *shell_history_get(const struct shell_state *state, int index)
{
  if (state == 0 || index < 0 || index >= state->history_count)
    return 0;
  return state->history[index];
}

int shell_history_entry_number(const struct shell_state *state, int index)
{
  if (state == 0 || index < 0 || index >= state->history_count)
    return 0;
  return state->history_base + index;
}

void shell_state_clear_last_error(struct shell_state *state)
{
  if (state == 0)
    return;
  state->last_error_text[0] = '\0';
}

void shell_state_set_last_error(struct shell_state *state, const char *text)
{
  if (state == 0)
    return;
  shell_copy_text(state->last_error_text,
                  sizeof(state->last_error_text),
                  text != 0 ? text : "");
}

const char *shell_state_last_error(const struct shell_state *state)
{
  if (state == 0)
    return "";
  return state->last_error_text;
}
