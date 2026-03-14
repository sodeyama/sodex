#ifndef _DISPLAY_CONSOLE_H
#define _DISPLAY_CONSOLE_H

#include <sodex/const.h>
#include <display_backend.h>

struct console_state {
  struct display_backend *backend;
  int cursor_x;
  int cursor_y;
  int prompt_x;
  int prompt_y;
  char color;
};

PUBLIC void console_init(struct console_state *state,
                         struct display_backend *backend,
                         char color);
PUBLIC void console_reset(struct console_state *state);
PUBLIC void console_clear(struct console_state *state);
PUBLIC void console_scroll_up(struct console_state *state);
PUBLIC void console_set_cursor(struct console_state *state, int x, int y);
PUBLIC void console_set_color(struct console_state *state, char color);
PUBLIC void console_save_prompt(struct console_state *state);
PUBLIC void console_putc_at(struct console_state *state, int x, int y, char c);
PUBLIC void console_write_char(struct console_state *state, char c);
PUBLIC void console_write(struct console_state *state, const char *str);

#endif /* _DISPLAY_CONSOLE_H */
