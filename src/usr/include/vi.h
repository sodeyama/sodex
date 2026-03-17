#ifndef _USR_VI_H
#define _USR_VI_H

#include <sys/types.h>

#define VI_MAX_COMMAND 64
#define VI_STATUS_SIZE 80

enum vi_mode {
  VI_MODE_NORMAL = 0,
  VI_MODE_INSERT = 1,
  VI_MODE_COMMAND = 2,
  VI_MODE_SEARCH = 3,
  VI_MODE_VISUAL = 4,
  VI_MODE_VISUAL_LINE = 5
};

enum vi_command_kind {
  VI_COMMAND_INVALID = -1,
  VI_COMMAND_NONE = 0,
  VI_COMMAND_WRITE = 1,
  VI_COMMAND_QUIT = 2,
  VI_COMMAND_WRITE_QUIT = 3
};

struct vi_line {
  char *data;
  int len;
  int cap;
};

struct vi_buffer {
  struct vi_line *lines;
  int line_count;
  int line_cap;
  int cursor_row;
  int cursor_col;
  int dirty;
};

struct vi_visual_state {
  int active;
  int linewise;
  int start_row;
  int start_col;
  int end_row;
  int end_col;
};

int vi_buffer_init(struct vi_buffer *buffer);
void vi_buffer_free(struct vi_buffer *buffer);
int vi_buffer_load(struct vi_buffer *buffer, const char *data, int len);
int vi_buffer_insert_char(struct vi_buffer *buffer, char ch);
int vi_buffer_insert_newline(struct vi_buffer *buffer);
int vi_buffer_backspace(struct vi_buffer *buffer);
int vi_buffer_delete_char(struct vi_buffer *buffer);
int vi_buffer_delete_prev_char(struct vi_buffer *buffer);
int vi_buffer_delete_to_line_start(struct vi_buffer *buffer);
int vi_buffer_delete_to_line_end(struct vi_buffer *buffer);
int vi_buffer_delete_line(struct vi_buffer *buffer);
int vi_buffer_delete_word_forward(struct vi_buffer *buffer);
int vi_buffer_delete_word_backward(struct vi_buffer *buffer);
int vi_buffer_delete_word_end(struct vi_buffer *buffer);
int vi_buffer_open_line_below(struct vi_buffer *buffer);
int vi_buffer_open_line_above(struct vi_buffer *buffer);
int vi_buffer_delete_range(struct vi_buffer *buffer,
                           int start_row, int start_col,
                           int end_row, int end_col);
int vi_buffer_delete_lines(struct vi_buffer *buffer,
                           int start_row, int end_row);
void vi_buffer_move_left(struct vi_buffer *buffer);
void vi_buffer_move_right(struct vi_buffer *buffer);
void vi_buffer_move_up(struct vi_buffer *buffer);
void vi_buffer_move_down(struct vi_buffer *buffer);
void vi_buffer_move_line_start(struct vi_buffer *buffer);
void vi_buffer_move_line_first_nonblank(struct vi_buffer *buffer);
void vi_buffer_move_line_last_char(struct vi_buffer *buffer);
void vi_buffer_move_line_end(struct vi_buffer *buffer);
void vi_buffer_move_first_line(struct vi_buffer *buffer);
void vi_buffer_move_last_line(struct vi_buffer *buffer);
void vi_buffer_move_word_forward(struct vi_buffer *buffer);
void vi_buffer_move_word_backward(struct vi_buffer *buffer);
void vi_buffer_move_word_end(struct vi_buffer *buffer);
int vi_buffer_next_char_col(const struct vi_buffer *buffer, int row, int col);
int vi_buffer_prev_char_col(const struct vi_buffer *buffer, int row, int col);
const char *vi_buffer_line_data(const struct vi_buffer *buffer, int row);
int vi_buffer_line_length(const struct vi_buffer *buffer, int row);
int vi_buffer_line_display_width(const struct vi_buffer *buffer, int row);
int vi_buffer_line_bytes_for_width(const struct vi_buffer *buffer, int row, int cols);
int vi_buffer_cursor_display_col(const struct vi_buffer *buffer);
int vi_buffer_find_forward(const struct vi_buffer *buffer, const char *needle,
                           int start_row, int start_col,
                           int *out_row, int *out_col);
int vi_buffer_find_backward(const struct vi_buffer *buffer, const char *needle,
                            int start_row, int start_col,
                            int *out_row, int *out_col);
int vi_buffer_serialize(const struct vi_buffer *buffer, char **out_data, int *out_len);
void vi_buffer_clear_dirty(struct vi_buffer *buffer);
enum vi_command_kind vi_parse_command(const char *command);

void vi_screen_enter(void);
void vi_screen_redraw(const struct vi_buffer *buffer, enum vi_mode mode,
                      const char *path, const char *status,
                      const char *command, char command_prefix,
                      const struct vi_visual_state *visual,
                      int row_offset, int rows, int cols);
void vi_screen_restore(void);
#ifdef TEST_BUILD
void vi_screen_test_reset_state(void);
#endif

#endif /* _USR_VI_H */
