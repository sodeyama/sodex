#include <ime.h>
#include <ime_conversion.h>
#include <ime_dictionary.h>
#include <stddef.h>
#include <string.h>
#include <utf8.h>

#define IME_SEGMENT_INF_COST 0x3fffffff
#define IME_SEGMENT_MAX_CHARS 16
#define IME_SEGMENT_DICT_BASE_COST 4000
#define IME_SEGMENT_DICT_CHAR_REWARD 6000
#define IME_SEGMENT_FALLBACK_BASE_COST 12000
#define IME_SEGMENT_FALLBACK_CHAR_COST 3000

static int ime_build_char_offsets(const char *reading, int reading_len,
                                  int *out_offsets, int out_cap);
static int ime_copy_reading_slice(const struct ime_state *state,
                                  int start_byte, int end_byte,
                                  char *out, int out_cap);
static int ime_clause_lookup_allowed(const struct ime_state *state,
                                     int clause_chars);
static int ime_reset_shared_candidates(struct ime_state *state);
static int ime_copy_clause_selection(struct ime_clause *clause,
                                     const char *text);
static int ime_load_clause_candidates(struct ime_state *state, int clause_index);
static int ime_focus_clause(struct ime_state *state, int clause_index);
static int ime_segment_dictionary_cost(int best_cost, int clause_chars);
static int ime_is_common_particle(const char *reading, int clause_chars);
static int ime_segment_fallback_cost(const char *reading, int clause_chars);
static int ime_apply_clause_boundaries(struct ime_state *state,
                                       const int *char_offsets,
                                       const int *starts,
                                       const int *ends,
                                       int clause_count,
                                       int focused_clause);

int ime_conversion_active(const struct ime_state *state)
{
  if (state == NULL)
    return 0;
  return state->conversion_active;
}

int ime_start_conversion(struct ime_state *state)
{
  int char_offsets[IME_READING_MAX];
  int best_costs[IME_READING_MAX];
  int prev_chars[IME_READING_MAX];
  int starts[IME_CLAUSE_MAX];
  int ends[IME_CLAUSE_MAX];
  int char_count;
  int start_char;
  int clause_count = 0;
  int end_char;

  if (state == NULL || state->mode != IME_MODE_HIRAGANA ||
      state->reading_len <= 0)
    return 0;

  char_count = ime_build_char_offsets(state->reading, state->reading_len,
                                      char_offsets, IME_READING_MAX);
  if (char_count <= 0)
    return 0;

  best_costs[0] = 0;
  prev_chars[0] = -1;
  for (end_char = 1; end_char <= char_count; end_char++) {
    best_costs[end_char] = IME_SEGMENT_INF_COST;
    prev_chars[end_char] = -1;
  }

  for (start_char = 0; start_char < char_count; start_char++) {
    int max_end_char;

    if (best_costs[start_char] >= IME_SEGMENT_INF_COST)
      continue;

    max_end_char = start_char + IME_SEGMENT_MAX_CHARS;
    if (max_end_char > char_count)
      max_end_char = char_count;
    for (end_char = start_char + 1; end_char <= max_end_char; end_char++) {
      char clause_reading[IME_READING_MAX];
      int clause_chars = end_char - start_char;
      int clause_len = char_offsets[end_char] - char_offsets[start_char];
      int clause_best_cost = 0;
      int candidate_count = 0;
      int edge_cost;
      int total_cost;

      if (clause_len <= 0 || clause_len >= IME_READING_MAX)
        continue;
      memcpy(clause_reading, state->reading + char_offsets[start_char],
             (size_t)clause_len);
      clause_reading[clause_len] = '\0';

      if (ime_clause_lookup_allowed(state, clause_chars) != 0 &&
          ime_dictionary_lookup_with_cost(clause_reading,
                                          state->candidate_storage,
                                          sizeof(state->candidate_storage),
                                          state->candidates,
                                          IME_CANDIDATE_MAX,
                                          &candidate_count,
                                          &clause_best_cost) == 0 &&
          candidate_count > 0) {
        edge_cost = ime_segment_dictionary_cost(clause_best_cost,
                                                clause_chars);
        total_cost = best_costs[start_char] + edge_cost;
        if (total_cost < best_costs[end_char] ||
            (total_cost == best_costs[end_char] &&
             (prev_chars[end_char] < 0 ||
              clause_chars > end_char - prev_chars[end_char]))) {
          best_costs[end_char] = total_cost;
          prev_chars[end_char] = start_char;
        }
      }

      edge_cost = ime_segment_fallback_cost(clause_reading, clause_chars);
      total_cost = best_costs[start_char] + edge_cost;
      if (total_cost < best_costs[end_char] ||
          (total_cost == best_costs[end_char] &&
           (prev_chars[end_char] < 0 ||
            clause_chars > end_char - prev_chars[end_char]))) {
        best_costs[end_char] = total_cost;
        prev_chars[end_char] = start_char;
      }
    }
  }

  if (prev_chars[char_count] < 0)
    return 0;

  end_char = char_count;
  while (end_char > 0) {
    start_char = prev_chars[end_char];

    if (start_char < 0)
      return 0;
    if (clause_count >= IME_CLAUSE_MAX) {
      starts[0] = 0;
      ends[0] = char_count;
      clause_count = 1;
      end_char = 0;
      break;
    }
    starts[clause_count] = start_char;
    ends[clause_count] = end_char;
    clause_count++;
    end_char = start_char;
  }
  if (end_char != 0)
    return 0;

  {
    int normalized_starts[IME_CLAUSE_MAX];
    int normalized_ends[IME_CLAUSE_MAX];
    int i;

    for (i = 0; i < clause_count; i++) {
      normalized_starts[i] = starts[clause_count - 1 - i];
      normalized_ends[i] = ends[clause_count - 1 - i];
    }
    return ime_apply_clause_boundaries(state, char_offsets,
                                       normalized_starts, normalized_ends,
                                       clause_count, 0);
  }
}

int ime_select_next_candidate(struct ime_state *state)
{
  struct ime_clause *clause;

  if (state == NULL || state->conversion_active == 0 || state->candidate_count <= 1 ||
      state->focused_clause < 0 || state->focused_clause >= state->clause_count)
    return 0;

  state->candidate_index = (state->candidate_index + 1) % state->candidate_count;
  clause = &state->clauses[state->focused_clause];
  clause->selected_index = state->candidate_index;
  clause->candidate_count = state->candidate_count;
  if (ime_copy_clause_selection(clause,
                                state->candidates[state->candidate_index]) < 0)
    return 0;
  return 1;
}

int ime_select_prev_candidate(struct ime_state *state)
{
  struct ime_clause *clause;

  if (state == NULL || state->conversion_active == 0 || state->candidate_count <= 1 ||
      state->focused_clause < 0 || state->focused_clause >= state->clause_count)
    return 0;

  state->candidate_index--;
  if (state->candidate_index < 0)
    state->candidate_index = state->candidate_count - 1;
  clause = &state->clauses[state->focused_clause];
  clause->selected_index = state->candidate_index;
  clause->candidate_count = state->candidate_count;
  if (ime_copy_clause_selection(clause,
                                state->candidates[state->candidate_index]) < 0)
    return 0;
  return 1;
}

void ime_cancel_conversion(struct ime_state *state)
{
  if (state == NULL)
    return;

  memset(state->clauses, 0, sizeof(state->clauses));
  state->clause_count = 0;
  state->focused_clause = 0;
  ime_reset_shared_candidates(state);
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

int ime_clause_count(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0)
    return 0;
  return state->clause_count;
}

int ime_focused_clause_index(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0)
    return -1;
  return state->focused_clause;
}

int ime_focus_next_clause(struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0)
    return 0;
  return ime_focus_clause(state, state->focused_clause + 1);
}

int ime_focus_prev_clause(struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0)
    return 0;
  return ime_focus_clause(state, state->focused_clause - 1);
}

int ime_expand_clause_right(struct ime_state *state)
{
  int starts[IME_CLAUSE_MAX];
  int ends[IME_CLAUSE_MAX];
  int char_offsets[IME_READING_MAX];
  int clause_count;
  int focused;
  int i;

  if (state == NULL || state->conversion_active == 0 || state->clause_count <= 1)
    return 0;

  clause_count = state->clause_count;
  focused = state->focused_clause;
  if (focused < 0 || focused >= clause_count - 1)
    return 0;

  for (i = 0; i < clause_count; i++) {
    starts[i] = state->clauses[i].start_char;
    ends[i] = state->clauses[i].end_char;
  }

  ends[focused]++;
  starts[focused + 1]++;
  if (starts[focused + 1] >= ends[focused + 1]) {
    for (i = focused + 1; i < clause_count - 1; i++) {
      starts[i] = starts[i + 1];
      ends[i] = ends[i + 1];
    }
    clause_count--;
  }

  if (ime_build_char_offsets(state->reading, state->reading_len,
                             char_offsets, IME_READING_MAX) <= 0)
    return 0;
  return ime_apply_clause_boundaries(state, char_offsets,
                                     starts, ends,
                                     clause_count, focused);
}

int ime_expand_clause_left(struct ime_state *state)
{
  int starts[IME_CLAUSE_MAX];
  int ends[IME_CLAUSE_MAX];
  int char_offsets[IME_READING_MAX];
  int clause_count;
  int focused;
  int i;

  if (state == NULL || state->conversion_active == 0 || state->clause_count <= 1)
    return 0;

  clause_count = state->clause_count;
  focused = state->focused_clause;
  if (focused <= 0 || focused >= clause_count)
    return 0;

  for (i = 0; i < clause_count; i++) {
    starts[i] = state->clauses[i].start_char;
    ends[i] = state->clauses[i].end_char;
  }

  starts[focused]--;
  ends[focused - 1]--;
  if (starts[focused - 1] >= ends[focused - 1]) {
    for (i = focused - 1; i < clause_count - 1; i++) {
      starts[i] = starts[i + 1];
      ends[i] = ends[i + 1];
    }
    clause_count--;
    focused--;
  }

  if (ime_build_char_offsets(state->reading, state->reading_len,
                             char_offsets, IME_READING_MAX) <= 0)
    return 0;
  return ime_apply_clause_boundaries(state, char_offsets,
                                     starts, ends,
                                     clause_count, focused);
}

int ime_copy_clause_reading(const struct ime_state *state, int clause_index,
                            char *out, int out_cap)
{
  if (state == NULL || out == NULL || out_cap <= 0 ||
      state->conversion_active == 0 ||
      clause_index < 0 || clause_index >= state->clause_count)
    return 0;
  return ime_copy_reading_slice(state,
                                state->clauses[clause_index].start_byte,
                                state->clauses[clause_index].end_byte,
                                out, out_cap);
}

int ime_clause_start_char(const struct ime_state *state, int clause_index)
{
  if (state == NULL || state->conversion_active == 0 ||
      clause_index < 0 || clause_index >= state->clause_count)
    return -1;
  return state->clauses[clause_index].start_char;
}

int ime_clause_end_char(const struct ime_state *state, int clause_index)
{
  if (state == NULL || state->conversion_active == 0 ||
      clause_index < 0 || clause_index >= state->clause_count)
    return -1;
  return state->clauses[clause_index].end_char;
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

int ime_candidate_page_start(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0 ||
      state->candidate_count <= 0 || state->candidate_index < 0)
    return 0;
  return (state->candidate_index / IME_CANDIDATE_PAGE_SIZE) *
         IME_CANDIDATE_PAGE_SIZE;
}

int ime_candidate_page_count(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0 ||
      state->candidate_count <= 0)
    return 0;
  return (state->candidate_count + IME_CANDIDATE_PAGE_SIZE - 1) /
         IME_CANDIDATE_PAGE_SIZE;
}

int ime_candidate_page_index(const struct ime_state *state)
{
  if (state == NULL || state->conversion_active == 0 ||
      state->candidate_index < 0)
    return -1;
  return state->candidate_index / IME_CANDIDATE_PAGE_SIZE;
}

int ime_commit_conversion(struct ime_state *state, char *out, int out_cap,
                          int *replace_chars)
{
  int out_len = 0;
  int i;

  if (state == NULL || out == NULL || out_cap <= 0 ||
      state->conversion_active == 0)
    return 0;

  for (i = 0; i < state->clause_count; i++) {
    int len;

    if (state->clauses[i].selected[0] == '\0')
      return -1;
    len = (int)strlen(state->clauses[i].selected);
    if (out_len + len > out_cap)
      return -1;
    memcpy(out + out_len, state->clauses[i].selected, (size_t)len);
    out_len += len;
  }

  if (replace_chars != NULL)
    *replace_chars = state->reading_chars;
  ime_reset_segment(state);
  return out_len;
}

static int ime_build_char_offsets(const char *reading, int reading_len,
                                  int *out_offsets, int out_cap)
{
  int char_count = 0;
  int index = 0;

  if (reading == NULL || out_offsets == NULL || out_cap <= 1 || reading_len <= 0)
    return 0;

  out_offsets[0] = 0;
  while (index < reading_len) {
    int next = utf8_next_char_end(reading, reading_len, index);

    if (next <= index || char_count + 1 >= out_cap)
      return -1;
    char_count++;
    out_offsets[char_count] = next;
    index = next;
  }

  return char_count;
}

static int ime_copy_reading_slice(const struct ime_state *state,
                                  int start_byte, int end_byte,
                                  char *out, int out_cap)
{
  int len;

  if (state == NULL || out == NULL || out_cap <= 0 ||
      start_byte < 0 || end_byte < start_byte ||
      end_byte > state->reading_len)
    return 0;

  len = end_byte - start_byte;
  if (len >= out_cap)
    return 0;
  memcpy(out, state->reading + start_byte, (size_t)len);
  out[len] = '\0';
  return len;
}

static int ime_clause_lookup_allowed(const struct ime_state *state,
                                     int clause_chars)
{
  if (state == NULL || clause_chars <= 0)
    return 0;
  if (state->reading_chars > 1 && clause_chars == 1)
    return 0;
  return 1;
}

static int ime_reset_shared_candidates(struct ime_state *state)
{
  int i;

  if (state == NULL)
    return -1;
  memset(state->candidate_storage, 0, sizeof(state->candidate_storage));
  for (i = 0; i < IME_CANDIDATE_MAX; i++)
    state->candidates[i] = NULL;
  state->candidate_count = 0;
  state->candidate_index = 0;
  return 0;
}

static int ime_copy_clause_selection(struct ime_clause *clause,
                                     const char *text)
{
  int len;

  if (clause == NULL || text == NULL)
    return -1;
  len = (int)strlen(text);
  if (len >= (int)sizeof(clause->selected))
    return -1;
  memcpy(clause->selected, text, (size_t)len + 1U);
  return 0;
}

static int ime_load_clause_candidates(struct ime_state *state, int clause_index)
{
  struct ime_clause *clause;
  char clause_reading[IME_READING_MAX];
  int clause_chars;
  int candidate_count = 0;
  int best_cost = 0;

  if (state == NULL || clause_index < 0 || clause_index >= state->clause_count)
    return 0;

  clause = &state->clauses[clause_index];
  clause_chars = clause->end_char - clause->start_char;
  if (ime_copy_reading_slice(state, clause->start_byte, clause->end_byte,
                             clause_reading, sizeof(clause_reading)) <= 0)
    return 0;

  ime_reset_shared_candidates(state);
  if (ime_clause_lookup_allowed(state, clause_chars) != 0 &&
      ime_dictionary_lookup_with_cost(clause_reading,
                                      state->candidate_storage,
                                      sizeof(state->candidate_storage),
                                      state->candidates, IME_CANDIDATE_MAX,
                                      &candidate_count,
                                      &best_cost) == 0 &&
      candidate_count > 0) {
    if (clause->selected_index < 0 || clause->selected_index >= candidate_count)
      clause->selected_index = 0;
    state->candidate_count = candidate_count;
    state->candidate_index = clause->selected_index;
    clause->candidate_count = candidate_count;
    if (ime_copy_clause_selection(clause,
                                  state->candidates[state->candidate_index]) < 0)
      return 0;
    (void)best_cost;
    return 1;
  }

  if ((int)strlen(clause_reading) + 1 > (int)sizeof(state->candidate_storage))
    return 0;
  memcpy(state->candidate_storage, clause_reading,
         strlen(clause_reading) + 1U);
  state->candidates[0] = state->candidate_storage;
  state->candidate_count = 1;
  state->candidate_index = 0;
  clause->selected_index = 0;
  clause->candidate_count = 1;
  if (ime_copy_clause_selection(clause, clause_reading) < 0)
    return 0;
  return 1;
}

static int ime_focus_clause(struct ime_state *state, int clause_index)
{
  if (state == NULL || state->conversion_active == 0 ||
      clause_index < 0 || clause_index >= state->clause_count)
    return 0;

  state->focused_clause = clause_index;
  return ime_load_clause_candidates(state, clause_index);
}

static int ime_segment_dictionary_cost(int best_cost, int clause_chars)
{
  return best_cost + IME_SEGMENT_DICT_BASE_COST -
         clause_chars * IME_SEGMENT_DICT_CHAR_REWARD;
}

static int ime_is_common_particle(const char *reading, int clause_chars)
{
  static const char *const particles[] = {
    "は", "が", "を", "に", "へ", "と", "で", "も", "や", "の",
    "ね", "よ", "か", "な"
  };
  unsigned int i;

  if (reading == NULL || clause_chars != 1)
    return 0;

  for (i = 0; i < sizeof(particles) / sizeof(particles[0]); i++) {
    if (strcmp(reading, particles[i]) == 0)
      return 1;
  }
  return 0;
}

static int ime_segment_fallback_cost(const char *reading, int clause_chars)
{
  if (ime_is_common_particle(reading, clause_chars) != 0)
    return -6000;
  return IME_SEGMENT_FALLBACK_BASE_COST +
         clause_chars * IME_SEGMENT_FALLBACK_CHAR_COST;
}

static int ime_apply_clause_boundaries(struct ime_state *state,
                                       const int *char_offsets,
                                       const int *starts,
                                       const int *ends,
                                       int clause_count,
                                       int focused_clause)
{
  int i;

  if (state == NULL || char_offsets == NULL || starts == NULL || ends == NULL ||
      clause_count <= 0 || clause_count > IME_CLAUSE_MAX)
    return 0;

  memset(state->clauses, 0, sizeof(state->clauses));
  for (i = 0; i < clause_count; i++) {
    if (starts[i] < 0 || ends[i] <= starts[i] ||
        ends[i] > state->reading_chars)
      return 0;
    state->clauses[i].start_char = starts[i];
    state->clauses[i].end_char = ends[i];
    state->clauses[i].start_byte = char_offsets[starts[i]];
    state->clauses[i].end_byte = char_offsets[ends[i]];
    state->clauses[i].selected_index = 0;
    state->clauses[i].candidate_count = 0;
  }

  state->clause_count = clause_count;
  state->focused_clause = 0;
  state->conversion_active = 1;
  ime_reset_shared_candidates(state);

  for (i = 0; i < clause_count; i++) {
    if (ime_load_clause_candidates(state, i) == 0)
      return 0;
  }

  if (focused_clause < 0)
    focused_clause = 0;
  if (focused_clause >= clause_count)
    focused_clause = clause_count - 1;
  return ime_focus_clause(state, focused_clause);
}
