#ifndef _USR_SHELL_H
#define _USR_SHELL_H

#include <sys/types.h>

#define SHELL_MAX_ARGS 24
#define SHELL_MAX_ASSIGNMENTS 8
#define SHELL_MAX_COMMANDS 8
#define SHELL_MAX_PIPELINES 32
#define SHELL_MAX_REDIRECTIONS 16
#define SHELL_MAX_TOKENS 256
#define SHELL_STORAGE_SIZE 8192
#define SHELL_WORD_SIZE 512
#define SHELL_MAX_VARS 64
#define SHELL_VAR_NAME_MAX 32
#define SHELL_VAR_VALUE_MAX 256
#define SHELL_MAX_PARAMS 16
#define SHELL_MAX_BG_PIDS 16
#define SHELL_JOB_TEXT_MAX 160
#define SHELL_MAX_LISTS 32
#define SHELL_MAX_LIST_ITEMS 32
#define SHELL_MAX_NODES 64
#define SHELL_MAX_FOR_WORDS 24
#define SHELL_MAX_IF_ELIFS 8

#define SHELL_PARSE_ERROR (-1)
#define SHELL_PARSE_INCOMPLETE (-2)

enum shell_next_type {
  SHELL_NEXT_END = 0,
  SHELL_NEXT_SEQ = 1,
  SHELL_NEXT_AND = 2,
  SHELL_NEXT_OR = 3,
  SHELL_NEXT_BACKGROUND = 4
};

enum shell_redirection_type {
  SHELL_REDIR_INPUT = 0,
  SHELL_REDIR_OUTPUT = 1,
  SHELL_REDIR_APPEND = 2,
  SHELL_REDIR_DUP = 3
};

enum shell_node_type {
  SHELL_NODE_PIPELINE = 0,
  SHELL_NODE_IF = 1,
  SHELL_NODE_FOR = 2,
  SHELL_NODE_WHILE = 3,
  SHELL_NODE_UNTIL = 4
};

enum shell_loop_control {
  SHELL_LOOP_NONE = 0,
  SHELL_LOOP_BREAK = 1,
  SHELL_LOOP_CONTINUE = 2
};

struct shell_redirection {
  enum shell_redirection_type type;
  int fd;
  int target_fd;
  char *path;
};

struct shell_command {
  char *argv[SHELL_MAX_ARGS];
  int argc;
  char *assignments[SHELL_MAX_ASSIGNMENTS];
  int assignment_count;
  struct shell_redirection redirections[SHELL_MAX_REDIRECTIONS];
  int redirection_count;
};

struct shell_pipeline {
  struct shell_command commands[SHELL_MAX_COMMANDS];
  int command_count;
  enum shell_next_type next_type;
};

struct shell_list_item {
  int node_index;
  enum shell_next_type next_type;
};

struct shell_list {
  struct shell_list_item items[SHELL_MAX_LIST_ITEMS];
  int item_count;
};

struct shell_if_clause {
  int cond_list_index;
  int body_list_index;
};

struct shell_if_node {
  int cond_list_index;
  int then_list_index;
  struct shell_if_clause elifs[SHELL_MAX_IF_ELIFS];
  int elif_count;
  int else_list_index;
  int has_else;
};

struct shell_for_node {
  char *name;
  char *words[SHELL_MAX_FOR_WORDS];
  int word_count;
  int body_list_index;
  int implicit_params;
};

struct shell_loop_node {
  int cond_list_index;
  int body_list_index;
};

struct shell_node {
  enum shell_node_type type;
  union {
    int pipeline_index;
    struct shell_if_node if_node;
    struct shell_for_node for_node;
    struct shell_loop_node loop_node;
  } data;
};

struct shell_program {
  struct shell_pipeline pipelines[SHELL_MAX_PIPELINES];
  int pipeline_count;
  struct shell_node nodes[SHELL_MAX_NODES];
  int node_count;
  struct shell_list lists[SHELL_MAX_LISTS];
  int list_count;
  int root_list_index;
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
  int loop_depth;
  int loop_control;
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
