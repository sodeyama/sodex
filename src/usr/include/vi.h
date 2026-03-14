#ifndef _USR_VI_H
#define _USR_VI_H

#include <sys/types.h>

#define VI_MAX_COMMAND 64
#define VI_STATUS_SIZE 80

enum vi_mode {
  VI_MODE_NORMAL = 0,
  VI_MODE_INSERT = 1,
  VI_MODE_COMMAND = 2
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

int vi_buffer_init(struct vi_buffer *buffer);
void vi_buffer_free(struct vi_buffer *buffer);
int vi_buffer_load(struct vi_buffer *buffer, const char *data, int len);
int vi_buffer_insert_char(struct vi_buffer *buffer, char ch);
int vi_buffer_insert_newline(struct vi_buffer *buffer);
int vi_buffer_backspace(struct vi_buffer *buffer);
void vi_buffer_move_left(struct vi_buffer *buffer);
void vi_buffer_move_right(struct vi_buffer *buffer);
void vi_buffer_move_up(struct vi_buffer *buffer);
void vi_buffer_move_down(struct vi_buffer *buffer);
const char *vi_buffer_line_data(const struct vi_buffer *buffer, int row);
int vi_buffer_line_length(const struct vi_buffer *buffer, int row);
void vi_buffer_clear_dirty(struct vi_buffer *buffer);
enum vi_command_kind vi_parse_command(const char *command);

void vi_screen_redraw(const struct vi_buffer *buffer, enum vi_mode mode,
                      const char *path, const char *status,
                      const char *command, int row_offset,
                      int rows, int cols);
void vi_screen_restore(void);

#endif /* _USR_VI_H */
