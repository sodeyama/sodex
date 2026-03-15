#ifndef _USR_IME_CONVERSION_H
#define _USR_IME_CONVERSION_H

#include <ime.h>

int ime_conversion_active(const struct ime_state *state);
int ime_start_conversion(struct ime_state *state);
int ime_select_next_candidate(struct ime_state *state);
int ime_select_prev_candidate(struct ime_state *state);
void ime_cancel_conversion(struct ime_state *state);
const char *ime_current_candidate(const struct ime_state *state);
int ime_candidate_count(const struct ime_state *state);
int ime_candidate_index(const struct ime_state *state);
int ime_candidate_page_start(const struct ime_state *state);
int ime_candidate_page_count(const struct ime_state *state);
int ime_candidate_page_index(const struct ime_state *state);
int ime_commit_conversion(struct ime_state *state, char *out, int out_cap,
                          int *replace_chars);

#endif /* _USR_IME_CONVERSION_H */
