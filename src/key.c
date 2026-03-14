#include <key.h>
#include <string.h>

/* QEMU cocoa から届く標準的な配列に合わせて US keymap を既定にする。 */
static char KeyMapASCII[256] = {
  [0x01] = KEY_ESC,
  [0x02] = '1',
  [0x03] = '2',
  [0x04] = '3',
  [0x05] = '4',
  [0x06] = '5',
  [0x07] = '6',
  [0x08] = '7',
  [0x09] = '8',
  [0x0A] = '9',
  [0x0B] = '0',
  [0x0C] = '-',
  [0x0D] = '=',
  [0x0E] = KEY_BACK,
  [0x0F] = KEY_TAB,
  [0x10] = KEY_Q,
  [0x11] = KEY_W,
  [0x12] = KEY_E,
  [0x13] = KEY_R,
  [0x14] = KEY_T,
  [0x15] = KEY_Y,
  [0x16] = KEY_U,
  [0x17] = KEY_I,
  [0x18] = KEY_O,
  [0x19] = KEY_P,
  [0x1A] = '[',
  [0x1B] = ']',
  [0x1C] = KEY_ENTER,
  [0x1E] = KEY_A,
  [0x1F] = KEY_S,
  [0x20] = KEY_D,
  [0x21] = KEY_F,
  [0x22] = KEY_G,
  [0x23] = KEY_H,
  [0x24] = KEY_J,
  [0x25] = KEY_K,
  [0x26] = KEY_L,
  [0x27] = ';',
  [0x28] = '\'',
  [0x29] = '`',
  [0x2B] = '\\',
  [0x2C] = KEY_Z,
  [0x2D] = KEY_X,
  [0x2E] = KEY_C,
  [0x2F] = KEY_V,
  [0x30] = KEY_B,
  [0x31] = KEY_N,
  [0x32] = KEY_M,
  [0x33] = ',',
  [0x34] = '.',
  [0x35] = '/',
  [0x37] = '*',
  [0x39] = KEY_SPACE,
};

static char ShiftKeyMapASCII[256] = {
  [0x01] = KEY_ESC,
  [0x02] = '!',
  [0x03] = '@',
  [0x04] = '#',
  [0x05] = '$',
  [0x06] = '%',
  [0x07] = '^',
  [0x08] = '&',
  [0x09] = '*',
  [0x0A] = '(',
  [0x0B] = ')',
  [0x0C] = '_',
  [0x0D] = '+',
  [0x0E] = KEY_BACK,
  [0x0F] = KEY_TAB,
  [0x10] = KEY_Q,
  [0x11] = KEY_W,
  [0x12] = KEY_E,
  [0x13] = KEY_R,
  [0x14] = KEY_T,
  [0x15] = KEY_Y,
  [0x16] = KEY_U,
  [0x17] = KEY_I,
  [0x18] = KEY_O,
  [0x19] = KEY_P,
  [0x1A] = '{',
  [0x1B] = '}',
  [0x1C] = KEY_ENTER,
  [0x1E] = KEY_A,
  [0x1F] = KEY_S,
  [0x20] = KEY_D,
  [0x21] = KEY_F,
  [0x22] = KEY_G,
  [0x23] = KEY_H,
  [0x24] = KEY_J,
  [0x25] = KEY_K,
  [0x26] = KEY_L,
  [0x27] = ':',
  [0x28] = '"',
  [0x29] = '~',
  [0x2B] = '|',
  [0x2C] = KEY_Z,
  [0x2D] = KEY_X,
  [0x2E] = KEY_C,
  [0x2F] = KEY_V,
  [0x30] = KEY_B,
  [0x31] = KEY_N,
  [0x32] = KEY_M,
  [0x33] = '<',
  [0x34] = '>',
  [0x35] = '?',
  [0x37] = '*',
  [0x39] = KEY_SPACE,
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
