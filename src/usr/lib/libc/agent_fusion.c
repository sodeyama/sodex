#include <agent_fusion.h>
#include <string.h>

static void fusion_copy_text(char *dst, int cap, const char *src)
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

static int fusion_is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int fusion_tokenize(const char *text,
                           char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                           char *argv[AGENT_FUSION_MAX_ARGS + 1],
                           int start_index)
{
  int index = 0;
  int argc = start_index;

  if (text == 0 || storage == 0 || argv == 0)
    return -1;

  while (text[index] != '\0') {
    int out_len = 0;
    char quote = '\0';

    while (fusion_is_space(text[index]))
      index++;
    if (text[index] == '\0')
      break;
    if (argc >= AGENT_FUSION_MAX_ARGS)
      return -1;

    while (text[index] != '\0') {
      char ch = text[index];

      if (quote == '\0' && fusion_is_space(ch))
        break;
      if (quote == '\0' && (ch == '\'' || ch == '"')) {
        quote = ch;
        index++;
        continue;
      }
      if (quote != '\0' && ch == quote) {
        quote = '\0';
        index++;
        continue;
      }
      if (ch == '\\' && text[index + 1] != '\0') {
        index++;
        ch = text[index];
      }
      if (out_len >= AGENT_FUSION_TEXT_MAX - 1)
        return -1;
      storage[argc][out_len++] = ch;
      index++;
    }

    if (quote != '\0')
      return -1;
    storage[argc][out_len] = '\0';
    argv[argc] = storage[argc];
    argc++;
  }

  argv[argc] = 0;
  return argc;
}

static int fusion_is_direct_mode(const char *text)
{
  static const char *prefixes[] = {
    "run", "-p", "--continue", "--resume", "sessions", "audit", "memory"
  };
  int i;

  if (text == 0 || text[0] == '\0')
    return 0;

  for (i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); i++) {
    int len = (int)strlen(prefixes[i]);

    if (strncmp(text, prefixes[i], (size_t)len) != 0)
      continue;
    if (text[len] == '\0' || fusion_is_space(text[len]))
      return 1;
  }
  return 0;
}

static int fusion_build_argv_impl(const char *input, int force_agent,
                                  char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                                  char *argv[AGENT_FUSION_MAX_ARGS + 1])
{
  char trimmed[AGENT_FUSION_TEXT_MAX];
  int start = 0;
  int end;
  int len = 0;

  if (input == 0 || storage == 0 || argv == 0)
    return -1;

  while (fusion_is_space(input[start]))
    start++;
  if (input[start] == '@') {
    start++;
    while (fusion_is_space(input[start]))
      start++;
  } else if (force_agent == 0) {
    return 0;
  }
  if (input[start] == '\0')
    return -1;

  end = start + (int)strlen(input + start);
  while (end > start && fusion_is_space(input[end - 1]))
    end--;
  while (start < end && len < AGENT_FUSION_TEXT_MAX - 1)
    trimmed[len++] = input[start++];
  trimmed[len] = '\0';
  if (trimmed[0] == '\0')
    return -1;

  fusion_copy_text(storage[0], AGENT_FUSION_TEXT_MAX, "agent");
  argv[0] = storage[0];

  if (fusion_is_direct_mode(trimmed) != 0)
    return fusion_tokenize(trimmed, storage, argv, 1);

  fusion_copy_text(storage[1], AGENT_FUSION_TEXT_MAX, "run");
  fusion_copy_text(storage[2], AGENT_FUSION_TEXT_MAX, trimmed);
  argv[1] = storage[1];
  argv[2] = storage[2];
  argv[3] = 0;
  return 3;
}

int agent_fusion_enabled(int argc, char **argv)
{
  int i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--agent-fusion") == 0)
      return 1;
  }
  return 0;
}

int agent_fusion_parse_mode_text(const char *text, int *mode_out)
{
  if (text == 0 || mode_out == 0)
    return -1;
  if (strcmp(text, "auto") == 0) {
    *mode_out = AGENT_FUSION_MODE_AUTO;
    return 0;
  }
  if (strcmp(text, "shell") == 0) {
    *mode_out = AGENT_FUSION_MODE_SHELL;
    return 0;
  }
  if (strcmp(text, "agent") == 0) {
    *mode_out = AGENT_FUSION_MODE_AGENT;
    return 0;
  }
  return -1;
}

const char *agent_fusion_mode_name(int mode)
{
  if (mode == AGENT_FUSION_MODE_SHELL)
    return "shell";
  if (mode == AGENT_FUSION_MODE_AGENT)
    return "agent";
  return "auto";
}

int agent_fusion_mode_from_argv(int argc, char **argv, int fallback_mode)
{
  int i;

  for (i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--agent-mode=", 13) == 0) {
      int mode;

      if (agent_fusion_parse_mode_text(argv[i] + 13, &mode) == 0)
        return mode;
    }
  }
  return fallback_mode;
}

int agent_fusion_build_argv(const char *input,
                            char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                            char *argv[AGENT_FUSION_MAX_ARGS + 1])
{
  return fusion_build_argv_impl(input, 0, storage, argv);
}

int agent_fusion_build_mode_argv(const char *input, int force_agent,
                                 char storage[AGENT_FUSION_MAX_ARGS][AGENT_FUSION_TEXT_MAX],
                                 char *argv[AGENT_FUSION_MAX_ARGS + 1])
{
  return fusion_build_argv_impl(input, force_agent, storage, argv);
}
