#ifndef _USR_SHELL_COMPLETION_H
#define _USR_SHELL_COMPLETION_H

#include <sys/types.h>

#define SHELL_COMPLETION_LINE_MAX 256
#define SHELL_COMPLETION_PATH_MAX 512
#define SHELL_COMPLETION_NAME_MAX 256
#define SHELL_COMPLETION_CANDIDATE_MAX 16
#define SHELL_COMPLETION_OVERLAY_MAX 160

struct shell_completion_candidate {
  char name[SHELL_COMPLETION_NAME_MAX];
  int is_dir;
};

struct shell_completion_state {
  pid_t shell_pid;
  char line[SHELL_COMPLETION_LINE_MAX];
  int line_len;
  int sync_valid;
  char cwd[SHELL_COMPLETION_PATH_MAX];
  int cwd_valid;
  char prompt_line[SHELL_COMPLETION_LINE_MAX];
  int prompt_len;
  int completion_active;
  int candidate_count;
  int candidate_index;
  int token_raw_start;
  int token_raw_len;
  char original_token_raw[SHELL_COMPLETION_LINE_MAX];
  int original_token_raw_len;
  char current_token_raw[SHELL_COMPLETION_LINE_MAX];
  int current_token_raw_len;
  int current_token_raw_chars;
  char typed_dir_logical[SHELL_COMPLETION_LINE_MAX];
  char prefix_logical[SHELL_COMPLETION_LINE_MAX];
  char quote_char;
  struct shell_completion_candidate candidates[SHELL_COMPLETION_CANDIDATE_MAX];
};

void shell_completion_state_init(struct shell_completion_state *state);
void shell_completion_state_set_shell_pid(struct shell_completion_state *state,
                                          pid_t shell_pid);
void shell_completion_state_reset(struct shell_completion_state *state);
void shell_completion_state_invalidate(struct shell_completion_state *state);
int shell_completion_state_valid(const struct shell_completion_state *state);
int shell_completion_state_can_track(const struct shell_completion_state *state,
                                     pid_t foreground_pid,
                                     u_int32_t lflag);
int shell_completion_state_can_complete(const struct shell_completion_state *state,
                                        pid_t foreground_pid,
                                        u_int32_t lflag,
                                        int ime_busy);
int shell_completion_state_feed_input(struct shell_completion_state *state,
                                      const char *buf, int len);
void shell_completion_state_observe_output(struct shell_completion_state *state,
                                           const char *buf, int len,
                                           pid_t foreground_pid);
int shell_completion_state_active(const struct shell_completion_state *state);
void shell_completion_state_finish_completion(struct shell_completion_state *state);
int shell_completion_state_complete(struct shell_completion_state *state,
                                    int reverse,
                                    char *out, int out_cap);
int shell_completion_state_cancel_completion(struct shell_completion_state *state,
                                             char *out, int out_cap);
int shell_completion_state_overlay_text(const struct shell_completion_state *state,
                                        char *buf, int cap);
const char *shell_completion_state_line(const struct shell_completion_state *state);
int shell_completion_state_line_len(const struct shell_completion_state *state);

#endif /* _USR_SHELL_COMPLETION_H */
