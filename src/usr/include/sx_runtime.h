#ifndef _USR_SX_RUNTIME_H
#define _USR_SX_RUNTIME_H

#include <sx_parser.h>

enum sx_value_kind {
  SX_VALUE_NONE = 0,
  SX_VALUE_STRING = 1,
  SX_VALUE_BOOL = 2,
  SX_VALUE_I32 = 3,
  SX_VALUE_BYTES = 4,
  SX_VALUE_LIST = 5,
  SX_VALUE_MAP = 6,
  SX_VALUE_RESULT = 7
};

struct sx_value {
  enum sx_value_kind kind;
  char text[SX_TEXT_MAX];
  int data_len;
  int bool_value;
  int int_value;
};

struct sx_binding {
  char name[SX_NAME_MAX];
  struct sx_value value;
  int scope_depth;
};

typedef int (*sx_output_fn)(void *ctx, const char *text, int len);

struct sx_pipe_handle {
  int active;
  int read_fd;
  int write_fd;
};

struct sx_list_handle {
  int active;
  int count;
  struct sx_value items[SX_MAX_LIST_ITEMS];
};

struct sx_map_entry {
  int used;
  char key[SX_TEXT_MAX];
  struct sx_value value;
};

struct sx_map_handle {
  int active;
  int count;
  struct sx_map_entry entries[SX_MAX_MAP_ITEMS];
};

struct sx_result_handle {
  int active;
  int ok;
  struct sx_value value;
  char error[SX_TEXT_MAX];
};

struct sx_runtime_limits {
  int max_bindings;
  int max_scope_depth;
  int max_call_depth;
  int max_loop_iterations;
};

struct sx_runtime {
  struct sx_binding bindings[SX_MAX_BINDINGS];
  int binding_count;
  int scope_depth;
  int call_depth;
  char call_stack[SX_MAX_CALL_DEPTH][SX_NAME_MAX];
  int error_call_depth;
  char error_call_stack[SX_MAX_CALL_DEPTH][SX_NAME_MAX];
  int inside_function;
  int loop_depth;
  sx_output_fn output;
  void *output_ctx;
  int argc;
  char argv[SX_MAX_RUNTIME_ARGS][SX_TEXT_MAX];
  struct sx_pipe_handle pipes[SX_MAX_PIPE_HANDLES];
  struct sx_list_handle lists[SX_MAX_LIST_HANDLES];
  struct sx_map_handle maps[SX_MAX_MAP_HANDLES];
  struct sx_result_handle results[SX_MAX_RESULT_HANDLES];
  struct sx_runtime_limits limits;
};

void sx_runtime_init(struct sx_runtime *runtime);
void sx_runtime_dispose(struct sx_runtime *runtime);
void sx_runtime_default_limits(struct sx_runtime_limits *limits);
int sx_runtime_set_limits(struct sx_runtime *runtime,
                          const struct sx_runtime_limits *limits);
void sx_runtime_reset_session(struct sx_runtime *runtime);
void sx_runtime_set_output(struct sx_runtime *runtime,
                           sx_output_fn output, void *ctx);
int sx_runtime_set_argv(struct sx_runtime *runtime,
                        int argc, char *const argv[]);
int sx_runtime_check_program(struct sx_runtime *runtime,
                             const struct sx_program *program,
                             struct sx_diagnostic *diag);
int sx_runtime_execute_program(struct sx_runtime *runtime,
                               const struct sx_program *program,
                               struct sx_diagnostic *diag);
int sx_runtime_format_stack_trace(const struct sx_runtime *runtime,
                                  char *buf, int cap);

#endif
