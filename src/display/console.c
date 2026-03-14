#include <display/console.h>

PRIVATE void console_flush(struct console_state *state)
{
  if (state == NULL || state->backend == NULL || state->backend->ops == NULL) {
    return;
  }
  if (state->backend->ops->flush != NULL) {
    state->backend->ops->flush(state->backend);
  }
}

PRIVATE int console_clamp(int value, int min, int max)
{
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

PUBLIC void console_init(struct console_state *state,
                         struct display_backend *backend,
                         char color)
{
  if (state == NULL) {
    return;
  }
  state->backend = backend;
  state->color = color;
  console_reset(state);
}

PUBLIC void console_reset(struct console_state *state)
{
  if (state == NULL) {
    return;
  }
  state->cursor_x = 0;
  state->cursor_y = 0;
  state->prompt_x = 0;
  state->prompt_y = 0;
}

PUBLIC void console_clear(struct console_state *state)
{
  if (state == NULL || state->backend == NULL || state->backend->ops == NULL) {
    return;
  }
  if (state->backend->ops->clear != NULL) {
    state->backend->ops->clear(state->backend, state->color);
  }
  console_flush(state);
}

PUBLIC void console_scroll_up(struct console_state *state)
{
  if (state == NULL || state->backend == NULL || state->backend->ops == NULL) {
    return;
  }
  if (state->backend->ops->scroll_up != NULL) {
    state->backend->ops->scroll_up(state->backend, state->color);
  }
  if (state->backend->rows > 0) {
    state->cursor_y = state->backend->rows - 1;
  }
  if (state->prompt_y > 0) {
    state->prompt_y--;
  }
  console_flush(state);
}

PUBLIC void console_set_cursor(struct console_state *state, int x, int y)
{
  if (state == NULL || state->backend == NULL) {
    return;
  }
  if (state->backend->cols <= 0 || state->backend->rows <= 0) {
    state->cursor_x = 0;
    state->cursor_y = 0;
    return;
  }
  state->cursor_x = console_clamp(x, 0, state->backend->cols - 1);
  state->cursor_y = console_clamp(y, 0, state->backend->rows - 1);
}

PUBLIC void console_set_color(struct console_state *state, char color)
{
  if (state == NULL) {
    return;
  }
  state->color = color;
}

PUBLIC void console_save_prompt(struct console_state *state)
{
  if (state == NULL) {
    return;
  }
  state->prompt_x = state->cursor_x;
  state->prompt_y = state->cursor_y;
}

PUBLIC void console_putc_at(struct console_state *state, int x, int y, char c)
{
  if (state == NULL || state->backend == NULL || state->backend->ops == NULL) {
    return;
  }
  if (state->backend->ops->put_cell == NULL) {
    return;
  }
  if (x < 0 || y < 0 || x >= state->backend->cols || y >= state->backend->rows) {
    return;
  }
  state->backend->ops->put_cell(state->backend, x, y, state->color, c);
}

PUBLIC void console_write_char(struct console_state *state, char c)
{
  if (state == NULL || state->backend == NULL) {
    return;
  }
  if (state->backend->cols <= 0 || state->backend->rows <= 0) {
    return;
  }

  if (c == '\n') {
    state->cursor_x = 0;
    if (state->cursor_y + 1 >= state->backend->rows) {
      console_scroll_up(state);
    } else {
      state->cursor_y++;
      console_flush(state);
    }
    return;
  }

  if (c == 0x08) {
    if (state->cursor_y > state->prompt_y ||
        (state->cursor_y == state->prompt_y &&
         state->cursor_x > state->prompt_x)) {
      if (state->cursor_x > 0) {
        state->cursor_x--;
      }
      console_putc_at(state, state->cursor_x, state->cursor_y, 0);
      console_flush(state);
    }
    return;
  }

  console_putc_at(state, state->cursor_x, state->cursor_y, c);

  if (state->cursor_x >= state->backend->cols - 1) {
    state->cursor_y++;
    if (state->cursor_y >= state->backend->rows) {
      console_scroll_up(state);
    }
  }
  state->cursor_x = (state->cursor_x + 1) % state->backend->cols;
  console_flush(state);
}

PUBLIC void console_write(struct console_state *state, const char *str)
{
  const char *p;

  if (str == NULL) {
    return;
  }

  for (p = str; *p != '\0'; ++p) {
    console_write_char(state, *p);
  }
}
