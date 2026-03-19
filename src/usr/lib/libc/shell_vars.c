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

void shell_state_init(struct shell_state *state, int interactive)
{
  if (state == 0)
    return;

  memset(state, 0, sizeof(*state));
  state->interactive = interactive;
  state->next_job_id = 1;
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
