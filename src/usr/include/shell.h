#ifndef _USR_SHELL_H
#define _USR_SHELL_H

#include <sys/types.h>

#define SHELL_MAX_ARGS 24
#define SHELL_MAX_ASSIGNMENTS 8
#define SHELL_MAX_COMMANDS 8
#define SHELL_MAX_PIPELINES 32
#define SHELL_MAX_TOKENS 256
#define SHELL_STORAGE_SIZE 4096
#define SHELL_WORD_SIZE 512
#define SHELL_MAX_VARS 64
#define SHELL_VAR_NAME_MAX 32
#define SHELL_VAR_VALUE_MAX 256
#define SHELL_MAX_PARAMS 16
#define SHELL_MAX_BG_PIDS 16
#define SHELL_JOB_TEXT_MAX 160

enum shell_next_type {
  SHELL_NEXT_END = 0,
  SHELL_NEXT_SEQ = 1,
  SHELL_NEXT_AND = 2,
  SHELL_NEXT_OR = 3,
  SHELL_NEXT_BACKGROUND = 4
};

struct shell_command {
  char *argv[SHELL_MAX_ARGS];
  int argc;
  char *assignments[SHELL_MAX_ASSIGNMENTS];
  int assignment_count;
  char *input_path;
  char *output_path;
  int append_output;
};

struct shell_pipeline {
  struct shell_command commands[SHELL_MAX_COMMANDS];
  int command_count;
  enum shell_next_type next_type;
};

struct shell_program {
  struct shell_pipeline pipelines[SHELL_MAX_PIPELINES];
  int pipeline_count;
  char storage[SHELL_STORAGE_SIZE];
};

struct shell_var {
  char name[SHELL_VAR_NAME_MAX];
  char value[SHELL_VAR_VALUE_MAX];
  int exported;
};

struct shell_job {
  int id;
  pid_t pid;
  char command[SHELL_JOB_TEXT_MAX];
};

struct shell_state {
  int interactive;
  int last_status;
  pid_t last_background_pid;
  int exit_requested;
  int exit_status;
  char script_name[SHELL_VAR_VALUE_MAX];
  char param_storage[SHELL_MAX_PARAMS][SHELL_VAR_VALUE_MAX];
  int param_count;
  struct shell_var vars[SHELL_MAX_VARS];
  int var_count;
  pid_t background_pids[SHELL_MAX_BG_PIDS];
  int background_count;
  struct shell_job jobs[SHELL_MAX_BG_PIDS];
  int job_count;
  int next_job_id;
};

void shell_state_init(struct shell_state *state, int interactive);
void shell_state_set_script(struct shell_state *state, const char *name,
                            int argc, char **argv);
int shell_var_set(struct shell_state *state, const char *name,
                  const char *value, int exported);
const char *shell_var_get(const struct shell_state *state, const char *name);

int shell_parse_program(const char *text, int len, struct shell_program *program);
int shell_execute_program(struct shell_state *state,
                          const struct shell_program *program);
int shell_execute_string(struct shell_state *state, const char *text);
int shell_execute_buffer(struct shell_state *state, const char *name,
                         const char *text, int argc, char **argv,
                         int sourced);
int shell_execute_file(struct shell_state *state, const char *path,
                       int argc, char **argv, int sourced);
void shell_reap_background(struct shell_state *state);

#endif /* _USR_SHELL_H */
