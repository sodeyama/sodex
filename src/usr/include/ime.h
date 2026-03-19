#ifndef _USR_IME_H
#define _USR_IME_H

#include <sys/types.h>

#define IME_PREEDIT_MAX 16
#define IME_READING_MAX 128
#define IME_CLAUSE_MAX 16
#define IME_CANDIDATE_MAX 16
#define IME_CANDIDATE_PAGE_SIZE 4
#define IME_CANDIDATE_STORAGE_MAX 1024

enum ime_mode {
  IME_MODE_LATIN = 0,
  IME_MODE_HIRAGANA = 1,
  IME_MODE_KATAKANA = 2
};

struct ime_clause {
  int start_byte;
  int end_byte;
  int start_char;
  int end_char;
  int selected_index;
  int candidate_count;
  char selected[IME_CANDIDATE_STORAGE_MAX];
};

struct ime_state {
  enum ime_mode mode;
  char preedit[IME_PREEDIT_MAX];
  int preedit_len;
  char reading[IME_READING_MAX];
  int reading_len;
  int reading_chars;
  struct ime_clause clauses[IME_CLAUSE_MAX];
  int clause_count;
  int focused_clause;
  char candidate_storage[IME_CANDIDATE_STORAGE_MAX];
  const char *candidates[IME_CANDIDATE_MAX];
  int candidate_count;
  int candidate_index;
  int conversion_active;
};

void ime_init(struct ime_state *state);
void ime_set_mode(struct ime_state *state, enum ime_mode mode);
void ime_cycle_mode(struct ime_state *state);
void ime_cycle_mode_reverse(struct ime_state *state);
const char *ime_mode_label(const struct ime_state *state);
const char *ime_preedit(const struct ime_state *state);
const char *ime_reading(const struct ime_state *state);
int ime_reading_chars(const struct ime_state *state);
void ime_reset_segment(struct ime_state *state);
int ime_drop_last_reading_char(struct ime_state *state);
int ime_feed_ascii(struct ime_state *state, char ch, char *out, int out_cap);
int ime_backspace(struct ime_state *state);
int ime_flush(struct ime_state *state, char *out, int out_cap);

#endif /* _USR_IME_H */
