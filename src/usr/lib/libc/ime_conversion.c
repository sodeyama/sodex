#include <ime.h>
#include <ime_conversion.h>
#include <ime_dictionary.h>
#include <stddef.h>
#include <string.h>

int ime_conversion_active(const struct ime_state *state)
{
  if (state == NULL)
    return 0;
  return state->conversion_active;
}

int ime_start_conversion(struct ime_state *state)
{
  const char *const *candidates = NULL;
  int candidate_count = 0;
  int i;

  if (state == NULL || state->mode != IME_MODE_HIRAGANA ||
      state->reading_len <= 0)
    return 0;
  if (ime_dictionary_lookup(state->reading, &candidates, &candidate_count) < 0)
    return 0;
  if (candidate_count <= 0)
    return 0;
  if (candidate_count > IME_CANDIDATE_MAX)
    candidate_count = IME_CANDIDATE_MAX;

  for (i = 0; i < candidate_count; i++)
    state->candidates[i] = candidates[i];
  for (; i < IME_CANDIDATE_MAX; i++)
    state->candidates[i] = NULL;
  state->candidate_count = candidate_count;
  state->candidate_index = 0;
  state->conversion_active = 1;
  return 1;
}

int ime_select_next_candidate(struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0 || state->candidate_count <= 1)
    return 0;
  state->candidate_index = (state->candidate_index + 1) % state->candidate_count;
  return 1;
}

int ime_select_prev_candidate(struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0 || state->candidate_count <= 1)
    return 0;
  state->candidate_index--;
  if (state->candidate_index < 0)
    state->candidate_index = state->candidate_count - 1;
  return 1;
}

void ime_cancel_conversion(struct ime_state *state)
{
  int i;

  if (state == NULL)
    return;
  for (i = 0; i < IME_CANDIDATE_MAX; i++)
    state->candidates[i] = NULL;
  state->candidate_count = 0;
  state->candidate_index = 0;
  state->conversion_active = 0;
}

const char *ime_current_candidate(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0 ||
      state->candidate_index < 0 ||
      state->candidate_index >= state->candidate_count)
    return "";
  if (state->candidates[state->candidate_index] == NULL)
    return "";
  return state->candidates[state->candidate_index];
}

int ime_candidate_count(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0)
    return 0;
  return state->candidate_count;
}

int ime_candidate_index(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0)
    return -1;
  return state->candidate_index;
}

int ime_commit_conversion(struct ime_state *state, char *out, int out_cap,
                          int *replace_chars)
{
  const char *candidate;
  int len;

  if (state == NULL || out == NULL || out_cap <= 0 ||
      state->conversion_active == 0)
    return 0;

  candidate = ime_current_candidate(state);
  if (candidate[0] == '\0')
    return -1;

  len = (int)strlen(candidate);
  if (len > out_cap)
    return -1;

  memcpy(out, candidate, (size_t)len);
  if (replace_chars != NULL)
    *replace_chars = state->reading_chars;
  ime_reset_segment(state);
  return len;
}
