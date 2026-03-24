/*
 * term_command_block.c - shell command proposal block
 */

#include <agent/term_command_block.h>
#include <string.h>
#include <stdio.h>

static void block_copy_text(char *dst, int cap, const char *src)
{
  int len;

  if (!dst || cap <= 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  len = (int)strlen(src);
  if (len >= cap)
    len = cap - 1;
  memcpy(dst, src, (size_t)len);
  dst[len] = '\0';
}

static const char *block_trim(const char *text)
{
  if (!text)
    return "";
  while (*text == ' ' || *text == '\t' || *text == '\n')
    text++;
  return text;
}

static int block_starts_with(const char *text, const char *prefix)
{
  int len;

  if (!text || !prefix)
    return 0;
  len = (int)strlen(prefix);
  return strncmp(text, prefix, (size_t)len) == 0;
}

static int block_contains_any(const char *text, const char *const *needles)
{
  int i;

  if (!text || !needles)
    return 0;
  for (i = 0; needles[i] != 0; i++) {
    if (strstr(text, needles[i]) != 0)
      return 1;
  }
  return 0;
}

void term_command_block_init(struct term_command_block *block)
{
  if (!block)
    return;
  memset(block, 0, sizeof(*block));
}

const char *term_command_block_class_name(enum term_command_class command_class)
{
  switch (command_class) {
  case TERM_COMMAND_CLASS_READ_ONLY:
    return "read";
  case TERM_COMMAND_CLASS_WRITE:
    return "write";
  case TERM_COMMAND_CLASS_PROCESS:
    return "process";
  case TERM_COMMAND_CLASS_NETWORK:
    return "network";
  default:
    return "-";
  }
}

const char *term_command_block_state_name(enum term_command_state state)
{
  switch (state) {
  case TERM_COMMAND_STATE_PENDING:
    return "pending";
  case TERM_COMMAND_STATE_RUNNING:
    return "running";
  case TERM_COMMAND_STATE_DONE:
    return "done";
  case TERM_COMMAND_STATE_DENIED:
    return "denied";
  default:
    return "-";
  }
}

enum term_command_class term_command_block_classify(const char *command)
{
  static const char *read_only_prefixes[] = {
    "ls", "cat ", "grep ", "find ", "pwd", "head ", "tail ", "wc ",
    "sort ", "uniq ", "diff ", "cut ", "awk ", "sed ", "readlink ",
    "stat ", "ps", 0
  };
  static const char *write_prefixes[] = {
    "rm ", "mv ", "cp ", "touch ", "mkdir ", "rmdir ", "chmod ",
    "chown ", "tee ", "echo ", "printf ", "make", "git add ",
    "git commit", "git checkout ", "git switch ", 0
  };
  static const char *process_prefixes[] = {
    "kill ", "killall ", "fg", "bg", "wait", 0
  };
  static const char *network_prefixes[] = {
    "curl ", "webfetch ", "websearch ", "ping ", "dig ", "ssh ",
    "scp ", "nc ", 0
  };
  const char *trimmed;
  int i;

  trimmed = block_trim(command);
  if (trimmed[0] == '\0')
    return TERM_COMMAND_CLASS_NONE;

  if (strstr(trimmed, " >") != 0 || strstr(trimmed, ">>") != 0 ||
      strstr(trimmed, "| tee ") != 0) {
    return TERM_COMMAND_CLASS_WRITE;
  }

  for (i = 0; read_only_prefixes[i] != 0; i++) {
    if (block_starts_with(trimmed, read_only_prefixes[i]))
      return TERM_COMMAND_CLASS_READ_ONLY;
  }
  for (i = 0; process_prefixes[i] != 0; i++) {
    if (block_starts_with(trimmed, process_prefixes[i]))
      return TERM_COMMAND_CLASS_PROCESS;
  }
  for (i = 0; network_prefixes[i] != 0; i++) {
    if (block_starts_with(trimmed, network_prefixes[i]))
      return TERM_COMMAND_CLASS_NETWORK;
  }
  for (i = 0; write_prefixes[i] != 0; i++) {
    if (block_starts_with(trimmed, write_prefixes[i]))
      return TERM_COMMAND_CLASS_WRITE;
  }
  if (block_contains_any(trimmed, network_prefixes))
    return TERM_COMMAND_CLASS_NETWORK;
  return TERM_COMMAND_CLASS_WRITE;
}

void term_command_block_clear(struct term_command_block *block)
{
  if (!block)
    return;
  memset(block, 0, sizeof(*block));
}

void term_command_block_set_proposal(struct term_command_block *block,
                                     const char *command)
{
  if (!block)
    return;
  term_command_block_clear(block);
  block->active = 1;
  block->state = TERM_COMMAND_STATE_PENDING;
  block->command_class = term_command_block_classify(command);
  block_copy_text(block->command, sizeof(block->command), block_trim(command));
}

void term_command_block_mark_running(struct term_command_block *block)
{
  if (!block)
    return;
  block->active = 1;
  block->state = TERM_COMMAND_STATE_RUNNING;
}

void term_command_block_mark_done(struct term_command_block *block,
                                  int exit_code,
                                  const char *summary)
{
  if (!block)
    return;
  block->active = 1;
  block->state = TERM_COMMAND_STATE_DONE;
  block->exit_code = exit_code;
  block_copy_text(block->summary, sizeof(block->summary), summary);
}

void term_command_block_mark_denied(struct term_command_block *block,
                                    const char *summary)
{
  if (!block)
    return;
  block->active = 1;
  block->state = TERM_COMMAND_STATE_DENIED;
  block_copy_text(block->summary, sizeof(block->summary), summary);
}

int term_command_block_format(const struct term_command_block *block,
                              char *out, int out_cap)
{
  int pos = 0;

  if (!block || !out || out_cap <= 0)
    return -1;
  if (block->active == 0)
    return 0;

  out[0] = '\0';
  pos += snprintf(out + pos, (size_t)(out_cap - pos),
                  "proposal=%s class=%s\n",
                  term_command_block_state_name(block->state),
                  term_command_block_class_name(block->command_class));
  if (pos < out_cap) {
    pos += snprintf(out + pos, (size_t)(out_cap - pos),
                    "cmd: %s\n", block->command[0] ? block->command : "-");
  }
  if (block->state == TERM_COMMAND_STATE_PENDING && pos < out_cap) {
    pos += snprintf(out + pos, (size_t)(out_cap - pos),
                    "approval: /approve once | /approve session | /deny\n");
  } else if (block->summary[0] != '\0' && pos < out_cap) {
    pos += snprintf(out + pos, (size_t)(out_cap - pos),
                    "result: %s\n", block->summary);
  }
  if (block->state == TERM_COMMAND_STATE_DONE && pos < out_cap) {
    pos += snprintf(out + pos, (size_t)(out_cap - pos),
                    "exit=%d\n", block->exit_code);
  }
  if (pos >= out_cap)
    pos = out_cap - 1;
  out[pos] = '\0';
  return pos;
}
