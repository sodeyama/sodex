#ifndef _USR_IME_CONVERSION_H
#define _USR_IME_CONVERSION_H

#include <ime.h>

int ime_conversion_active(const struct ime_state *state);
int ime_start_conversion(struct ime_state *state);
int ime_select_next_candidate(struct ime_state *state);
int ime_select_prev_candidate(struct ime_state *state);
void ime_cancel_conversion(struct ime_state *state);
const char *ime_current_candidate(const struct ime_state *state);
int ime_clause_count(const struct ime_state *state);
int ime_focused_clause_index(const struct ime_state *state);
int ime_focus_next_clause(struct ime_state *state);
int ime_focus_prev_clause(struct ime_state *state);
int ime_expand_clause_right(struct ime_state *state);
int ime_expand_clause_left(struct ime_state *state);
int ime_copy_clause_reading(const struct ime_state *state, int clause_index,
                            char *out, int out_cap);
int ime_clause_start_char(const struct ime_state *state, int clause_index);
int ime_clause_end_char(const struct ime_state *state, int clause_index);
int ime_candidate_count(const struct ime_state *state);
int ime_candidate_index(const struct ime_state *state);
int ime_candidate_page_start(const struct ime_state *state);
int ime_candidate_page_count(const struct ime_state *state);
int ime_candidate_page_index(const struct ime_state *state);
int ime_commit_conversion(struct ime_state *state, char *out, int out_cap,
                          int *replace_chars);

#endif /* _USR_IME_CONVERSION_H */
