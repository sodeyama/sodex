#ifndef _USR_IME_H
#define _USR_IME_H

#include <sys/types.h>

#define IME_PREEDIT_MAX 16

enum ime_mode {
  IME_MODE_LATIN = 0,
  IME_MODE_HIRAGANA = 1,
  IME_MODE_KATAKANA = 2
};

struct ime_state {
  enum ime_mode mode;
  char preedit[IME_PREEDIT_MAX];
  int preedit_len;
};

void ime_init(struct ime_state *state);
void ime_cycle_mode(struct ime_state *state);
void ime_cycle_mode_reverse(struct ime_state *state);
const char *ime_mode_label(const struct ime_state *state);
const char *ime_preedit(const struct ime_state *state);
int ime_feed_ascii(struct ime_state *state, char ch, char *out, int out_cap);
int ime_backspace(struct ime_state *state);
int ime_flush(struct ime_state *state, char *out, int out_cap);

#endif /* _USR_IME_H */
