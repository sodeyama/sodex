#include <key.h>
#include <string.h>

static char KeyMapASCII[256] = {
  KEY_NULL, KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6,
  KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_POW, KEY_BACK, KEY_TAB,
  KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I,
  KEY_O, KEY_P, KEY_ATTO, KEY_LEFTBIGCACCO, KEY_ENTER, KEY_BACKSLASH,
  KEY_A, KEY_S,
  KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_PLUS,
  KEY_ASTA, KEY_NULL, KEY_LEFTSHIFT, KEY_RIGHTBIGCACCO, KEY_Z, KEY_X, KEY_C,
  KEY_V,
  KEY_B, KEY_N, KEY_M, KEY_SMALL, KEY_DOT, KEY_SLASH, KEY_RIGHTSHIFT,
  KEY_NULL,
  KEY_LEFTALT, KEY_SPACE, KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4,
  KEY_F5,
  KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_F11,
  KEY_F12, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL
};

static char ShiftKeyMapASCII[256] = {
  KEY_NULL, KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6,
  KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_POW, KEY_BACK, KEY_TAB,
  KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I,
  KEY_O, KEY_P, KEY_ATTO, KEY_LEFTBIGCACCO, KEY_ENTER, KEY_BACKSLASH,
  KEY_A, KEY_S,
  KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_PLUS,
  KEY_ASTA, KEY_NULL, KEY_LEFTSHIFT, KEY_RIGHTBIGCACCO, KEY_Z, KEY_X, KEY_C,
  KEY_V,
  KEY_B, KEY_N, KEY_M, KEY_SMALL, KEY_BIG, KEY_SLASH, KEY_RIGHTSHIFT,
  KEY_NULL,
  KEY_LEFTALT, KEY_SPACE, KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4,
  KEY_F5,
  KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_F11,
  KEY_F12, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL,
  KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
  KEY_NULL
};

struct key_state {
  struct key_event events[KEY_EVENT_BUF];
  int event_head;
  int event_tail;
  char stdin_buf[KEY_BUF];
  int stdin_head;
  int stdin_tail;
  u_int8_t modifiers;
  int extended_prefix;
};

PRIVATE struct key_state g_key_state;

PRIVATE int ring_next(int index, int size)
{
  return (index + 1) % size;
}

PRIVATE char translate_scancode(u_int8_t scancode, u_int8_t modifiers)
{
  char translated;

  if (modifiers & KEY_MOD_SHIFT) {
    translated = ShiftKeyMapASCII[scancode];
    if (translated >= 'a' && translated <= 'z') {
      translated -= ('a' - 'A');
    }
    return translated;
  }
  return KeyMapASCII[scancode];
}

PRIVATE int is_modifier_scancode(u_int8_t scancode)
{
  return scancode == KEY_SCANCODE_LEFT_SHIFT ||
         scancode == KEY_SCANCODE_RIGHT_SHIFT ||
         scancode == KEY_SCANCODE_LEFT_CTRL ||
         scancode == KEY_SCANCODE_LEFT_ALT;
}

PRIVATE void push_stdin_char(char c)
{
  int next;

  if (c == KEY_NULL) {
    return;
  }

  next = ring_next(g_key_state.stdin_tail, KEY_BUF);
  if (next == g_key_state.stdin_head) {
    g_key_state.stdin_head = ring_next(g_key_state.stdin_head, KEY_BUF);
  }

  g_key_state.stdin_buf[g_key_state.stdin_tail] = c;
  g_key_state.stdin_tail = next;
}

PRIVATE void push_key_event(const struct key_event *event)
{
  int next;

  next = ring_next(g_key_state.event_tail, KEY_EVENT_BUF);
  if (next == g_key_state.event_head) {
    g_key_state.event_head = ring_next(g_key_state.event_head, KEY_EVENT_BUF);
  }

  memcpy(&g_key_state.events[g_key_state.event_tail], event,
         sizeof(struct key_event));
  g_key_state.event_tail = next;
}

PRIVATE void update_modifier_state(u_int8_t scancode, int is_release, int is_extended)
{
  if (is_extended == FALSE) {
    if (scancode == KEY_SCANCODE_LEFT_SHIFT ||
        scancode == KEY_SCANCODE_RIGHT_SHIFT) {
      if (is_release) {
        g_key_state.modifiers &= ~KEY_MOD_SHIFT;
      } else {
        g_key_state.modifiers |= KEY_MOD_SHIFT;
      }
    } else if (scancode == KEY_SCANCODE_LEFT_CTRL) {
      if (is_release) {
        g_key_state.modifiers &= ~KEY_MOD_CTRL;
      } else {
        g_key_state.modifiers |= KEY_MOD_CTRL;
      }
    } else if (scancode == KEY_SCANCODE_LEFT_ALT) {
      if (is_release) {
        g_key_state.modifiers &= ~KEY_MOD_ALT;
      } else {
        g_key_state.modifiers |= KEY_MOD_ALT;
      }
    }
  } else if (scancode == KEY_SCANCODE_LEFT_CTRL || scancode == KEY_SCANCODE_LEFT_ALT) {
    if (is_release) {
      if (scancode == KEY_SCANCODE_LEFT_CTRL) {
        g_key_state.modifiers &= ~KEY_MOD_CTRL;
      } else {
        g_key_state.modifiers &= ~KEY_MOD_ALT;
      }
    } else {
      if (scancode == KEY_SCANCODE_LEFT_CTRL) {
        g_key_state.modifiers |= KEY_MOD_CTRL;
      } else {
        g_key_state.modifiers |= KEY_MOD_ALT;
      }
    }
  }
}

PUBLIC void init_key()
{
  memset(&g_key_state, 0, sizeof(struct key_state));
}

PUBLIC char get_keymap(u_int8_t c)
{
  return KeyMapASCII[c];
}

PUBLIC char get_shiftkeymap(u_int8_t c)
{
  return ShiftKeyMapASCII[c];
}

PUBLIC char key_handle_scancode(u_int8_t raw)
{
  struct key_event event;
  u_int8_t scancode;
  int is_release;
  int is_extended;

  if (raw == KEY_SCANCODE_EXTENDED) {
    g_key_state.extended_prefix = TRUE;
    return KEY_NULL;
  }

  is_extended = g_key_state.extended_prefix;
  g_key_state.extended_prefix = FALSE;
  is_release = (raw & KEY_SCANCODE_RELEASE) != 0;
  scancode = raw & 0x7f;

  update_modifier_state(scancode, is_release, is_extended);

  memset(&event, 0, sizeof(struct key_event));
  event.scancode = scancode;
  event.modifiers = g_key_state.modifiers;
  if (is_release) {
    event.flags |= KEY_EVENT_RELEASE;
  }
  if (is_extended) {
    event.flags |= KEY_EVENT_EXTENDED;
  }
  if (is_release == FALSE &&
      is_extended == FALSE &&
      is_modifier_scancode(scancode) == FALSE) {
    event.ascii = translate_scancode(scancode, g_key_state.modifiers);
  }

  push_key_event(&event);

  if (is_release == FALSE &&
      event.ascii != KEY_NULL &&
      event.ascii != KEY_SHIFT) {
    push_stdin_char(event.ascii);
    return event.ascii;
  }

  return KEY_NULL;
}

PUBLIC int key_pop_event(struct key_event *event)
{
  if (event == NULL) {
    return FALSE;
  }
  if (g_key_state.event_head == g_key_state.event_tail) {
    return FALSE;
  }

  memcpy(event, &g_key_state.events[g_key_state.event_head],
         sizeof(struct key_event));
  g_key_state.event_head = ring_next(g_key_state.event_head, KEY_EVENT_BUF);
  return TRUE;
}

PUBLIC char set_stdin(u_int8_t c)
{
  char mapped = get_keymap(c);

  if (mapped != KEY_NULL && mapped != KEY_SHIFT) {
    push_stdin_char(mapped);
  }
  return mapped;
}

PUBLIC char* get_stdin(char* tobuf)
{
  int count = 0;

  if (tobuf == NULL) {
    return NULL;
  }
  if (g_key_state.stdin_head == g_key_state.stdin_tail) {
    return NULL;
  }

  while (g_key_state.stdin_head != g_key_state.stdin_tail && count < KEY_BUF) {
    tobuf[count++] = g_key_state.stdin_buf[g_key_state.stdin_head];
    g_key_state.stdin_head = ring_next(g_key_state.stdin_head, KEY_BUF);
  }
  tobuf[count] = '\0';
  return tobuf;
}
