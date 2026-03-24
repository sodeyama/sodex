#ifndef _AGENT_TERM_COMMAND_BLOCK_H
#define _AGENT_TERM_COMMAND_BLOCK_H

#define TERM_COMMAND_BLOCK_TEXT_MAX 256
#define TERM_COMMAND_BLOCK_SUMMARY_MAX 256

enum term_command_class {
  TERM_COMMAND_CLASS_NONE = 0,
  TERM_COMMAND_CLASS_READ_ONLY,
  TERM_COMMAND_CLASS_WRITE,
  TERM_COMMAND_CLASS_PROCESS,
  TERM_COMMAND_CLASS_NETWORK,
  TERM_COMMAND_CLASS_COUNT
};

enum term_command_state {
  TERM_COMMAND_STATE_NONE = 0,
  TERM_COMMAND_STATE_PENDING,
  TERM_COMMAND_STATE_RUNNING,
  TERM_COMMAND_STATE_DONE,
  TERM_COMMAND_STATE_DENIED
};

struct term_command_block {
  int active;
  enum term_command_class command_class;
  enum term_command_state state;
  int exit_code;
  char command[TERM_COMMAND_BLOCK_TEXT_MAX];
  char summary[TERM_COMMAND_BLOCK_SUMMARY_MAX];
};

void term_command_block_init(struct term_command_block *block);
enum term_command_class term_command_block_classify(const char *command);
const char *term_command_block_class_name(enum term_command_class command_class);
const char *term_command_block_state_name(enum term_command_state state);
void term_command_block_clear(struct term_command_block *block);
void term_command_block_set_proposal(struct term_command_block *block,
                                     const char *command);
void term_command_block_mark_running(struct term_command_block *block);
void term_command_block_mark_done(struct term_command_block *block,
                                  int exit_code,
                                  const char *summary);
void term_command_block_mark_denied(struct term_command_block *block,
                                    const char *summary);
int term_command_block_format(const struct term_command_block *block,
                              char *out, int out_cap);

#endif
