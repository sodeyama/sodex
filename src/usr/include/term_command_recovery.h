#ifndef _USR_TERM_COMMAND_RECOVERY_H
#define _USR_TERM_COMMAND_RECOVERY_H

#include <shell.h>

#define TERM_COMMAND_RECOVERY_TEXT_MAX 512

enum term_command_recovery_kind {
  TERM_COMMAND_RECOVERY_NONE = 0,
  TERM_COMMAND_RECOVERY_SUGGEST = 1,
  TERM_COMMAND_RECOVERY_HINT = 2
};

struct term_command_recovery_result {
  int kind;
  int auto_apply;
  int destructive;
  char replacement[TERM_COMMAND_RECOVERY_TEXT_MAX];
  char display[TERM_COMMAND_RECOVERY_TEXT_MAX];
  char reason[32];
};

void term_command_recovery_reset(struct term_command_recovery_result *result);
int term_command_recovery_build(const struct shell_state *state,
                                const char *command_text,
                                int status,
                                struct term_command_recovery_result *result);
int term_command_recovery_write(const struct term_command_recovery_result *result);

#endif /* _USR_TERM_COMMAND_RECOVERY_H */
