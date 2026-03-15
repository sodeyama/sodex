#include <ime.h>
#include <stddef.h>
#include <string.h>
#include <utf8.h>

struct ime_mapping {
  const char *romaji;
  u_int32_t codepoints[2];
  int count;
};

static void ime_clear_candidates(struct ime_state *state);
static int ime_append_bytes(char *out, int out_len, int out_cap,
                            const char *src, int src_len);
static int ime_append_reading_bytes(struct ime_state *state,
                                    const char *src, int src_len, int chars);
static int ime_emit_codepoint(struct ime_state *state, u_int32_t codepoint,
                              char *out, int out_len, int out_cap);
static int ime_emit_ascii(char ch, char *out, int out_len, int out_cap);
static int ime_process_preedit(struct ime_state *state, int force,
                               char *out, int out_cap);
static int ime_is_alpha(char ch);
static int ime_is_vowel(char ch);
static int ime_is_consonant(char ch);
static int ime_is_sokuon_target(char ch);
static char ime_lower(char ch);
static int ime_has_prefix(const char *prefix, int prefix_len);
static const struct ime_mapping *ime_find_mapping(const char *text, int len,
                                                  int *matched_len);
static void ime_consume_preedit(struct ime_state *state, int consumed);
static u_int32_t ime_mode_codepoint(enum ime_mode mode, u_int32_t codepoint);
static int ime_match_prefix(const char *a, const char *b, int len);

static const struct ime_mapping ime_table[] = {
  {"a", {0x3042U}, 1}, {"i", {0x3044U}, 1}, {"u", {0x3046U}, 1},
  {"e", {0x3048U}, 1}, {"o", {0x304aU}, 1},
  {"ka", {0x304bU}, 1}, {"ki", {0x304dU}, 1}, {"ku", {0x304fU}, 1},
  {"ke", {0x3051U}, 1}, {"ko", {0x3053U}, 1},
  {"kya", {0x304dU, 0x3083U}, 2}, {"kyu", {0x304dU, 0x3085U}, 2},
  {"kyo", {0x304dU, 0x3087U}, 2},
  {"ga", {0x304cU}, 1}, {"gi", {0x304eU}, 1}, {"gu", {0x3050U}, 1},
  {"ge", {0x3052U}, 1}, {"go", {0x3054U}, 1},
  {"gya", {0x304eU, 0x3083U}, 2}, {"gyu", {0x304eU, 0x3085U}, 2},
  {"gyo", {0x304eU, 0x3087U}, 2},
  {"sa", {0x3055U}, 1}, {"si", {0x3057U}, 1}, {"shi", {0x3057U}, 1},
  {"su", {0x3059U}, 1}, {"se", {0x305bU}, 1}, {"so", {0x305dU}, 1},
  {"sya", {0x3057U, 0x3083U}, 2}, {"syu", {0x3057U, 0x3085U}, 2},
  {"syo", {0x3057U, 0x3087U}, 2}, {"sha", {0x3057U, 0x3083U}, 2},
  {"shu", {0x3057U, 0x3085U}, 2}, {"sho", {0x3057U, 0x3087U}, 2},
  {"za", {0x3056U}, 1}, {"zi", {0x3058U}, 1}, {"ji", {0x3058U}, 1},
  {"zu", {0x305aU}, 1}, {"ze", {0x305cU}, 1}, {"zo", {0x305eU}, 1},
  {"ja", {0x3058U, 0x3083U}, 2}, {"ju", {0x3058U, 0x3085U}, 2},
  {"jo", {0x3058U, 0x3087U}, 2}, {"jya", {0x3058U, 0x3083U}, 2},
  {"jyu", {0x3058U, 0x3085U}, 2}, {"jyo", {0x3058U, 0x3087U}, 2},
  {"ta", {0x305fU}, 1}, {"ti", {0x3061U}, 1}, {"chi", {0x3061U}, 1},
  {"tu", {0x3064U}, 1}, {"tsu", {0x3064U}, 1}, {"te", {0x3066U}, 1},
  {"to", {0x3068U}, 1}, {"tya", {0x3061U, 0x3083U}, 2},
  {"tyu", {0x3061U, 0x3085U}, 2}, {"tyo", {0x3061U, 0x3087U}, 2},
  {"cha", {0x3061U, 0x3083U}, 2}, {"chu", {0x3061U, 0x3085U}, 2},
  {"cho", {0x3061U, 0x3087U}, 2},
  {"da", {0x3060U}, 1}, {"di", {0x3062U}, 1}, {"du", {0x3065U}, 1},
  {"de", {0x3067U}, 1}, {"do", {0x3069U}, 1},
  {"na", {0x306aU}, 1}, {"ni", {0x306bU}, 1}, {"nu", {0x306cU}, 1},
  {"ne", {0x306dU}, 1}, {"no", {0x306eU}, 1},
  {"nya", {0x306bU, 0x3083U}, 2}, {"nyu", {0x306bU, 0x3085U}, 2},
  {"nyo", {0x306bU, 0x3087U}, 2},
  {"ha", {0x306fU}, 1}, {"hi", {0x3072U}, 1}, {"hu", {0x3075U}, 1},
  {"fu", {0x3075U}, 1}, {"he", {0x3078U}, 1}, {"ho", {0x307bU}, 1},
  {"hya", {0x3072U, 0x3083U}, 2}, {"hyu", {0x3072U, 0x3085U}, 2},
  {"hyo", {0x3072U, 0x3087U}, 2},
  {"fa", {0x3075U, 0x3041U}, 2}, {"fi", {0x3075U, 0x3043U}, 2},
  {"fe", {0x3075U, 0x3047U}, 2}, {"fo", {0x3075U, 0x3049U}, 2},
  {"ba", {0x3070U}, 1}, {"bi", {0x3073U}, 1}, {"bu", {0x3076U}, 1},
  {"be", {0x3079U}, 1}, {"bo", {0x307cU}, 1},
  {"bya", {0x3073U, 0x3083U}, 2}, {"byu", {0x3073U, 0x3085U}, 2},
  {"byo", {0x3073U, 0x3087U}, 2},
  {"pa", {0x3071U}, 1}, {"pi", {0x3074U}, 1}, {"pu", {0x3077U}, 1},
  {"pe", {0x307aU}, 1}, {"po", {0x307dU}, 1},
  {"pya", {0x3074U, 0x3083U}, 2}, {"pyu", {0x3074U, 0x3085U}, 2},
  {"pyo", {0x3074U, 0x3087U}, 2},
  {"ma", {0x307eU}, 1}, {"mi", {0x307fU}, 1}, {"mu", {0x3080U}, 1},
  {"me", {0x3081U}, 1}, {"mo", {0x3082U}, 1},
  {"mya", {0x307fU, 0x3083U}, 2}, {"myu", {0x307fU, 0x3085U}, 2},
  {"myo", {0x307fU, 0x3087U}, 2},
  {"ya", {0x3084U}, 1}, {"yu", {0x3086U}, 1}, {"yo", {0x3088U}, 1},
  {"ra", {0x3089U}, 1}, {"ri", {0x308aU}, 1}, {"ru", {0x308bU}, 1},
  {"re", {0x308cU}, 1}, {"ro", {0x308dU}, 1},
  {"rya", {0x308aU, 0x3083U}, 2}, {"ryu", {0x308aU, 0x3085U}, 2},
  {"ryo", {0x308aU, 0x3087U}, 2},
  {"wa", {0x308fU}, 1}, {"wo", {0x3092U}, 1}
};

void ime_init(struct ime_state *state)
{
  if (state == NULL)
    return;

  memset(state, 0, sizeof(*state));
  state->mode = IME_MODE_LATIN;
}

void ime_set_mode(struct ime_state *state, enum ime_mode mode)
{
  if (state == NULL)
    return;

  if (mode < IME_MODE_LATIN || mode > IME_MODE_KATAKANA)
    return;
  state->mode = mode;
}

void ime_cycle_mode(struct ime_state *state)
{
  if (state == NULL)
    return;

  if (state->mode == IME_MODE_LATIN)
    state->mode = IME_MODE_HIRAGANA;
  else if (state->mode == IME_MODE_HIRAGANA)
    state->mode = IME_MODE_KATAKANA;
  else
    state->mode = IME_MODE_LATIN;
}

void ime_cycle_mode_reverse(struct ime_state *state)
{
  if (state == NULL)
    return;

  if (state->mode == IME_MODE_LATIN)
    state->mode = IME_MODE_KATAKANA;
  else if (state->mode == IME_MODE_HIRAGANA)
    state->mode = IME_MODE_LATIN;
  else
    state->mode = IME_MODE_HIRAGANA;
}

const char *ime_mode_label(const struct ime_state *state)
{
  if (state == NULL)
    return "LATN";
  if (state->mode == IME_MODE_HIRAGANA)
    return "HIRA";
  if (state->mode == IME_MODE_KATAKANA)
    return "KATA";
  return "LATN";
}

const char *ime_preedit(const struct ime_state *state)
{
  if (state == NULL)
    return "";
  return state->preedit;
}

const char *ime_reading(const struct ime_state *state)
{
  if (state == NULL)
    return "";
  return state->reading;
}

int ime_reading_chars(const struct ime_state *state)
{
  if (state == NULL)
    return 0;
  return state->reading_chars;
}

void ime_reset_segment(struct ime_state *state)
{
  if (state == NULL)
    return;

  memset(state->reading, 0, sizeof(state->reading));
  state->reading_len = 0;
  state->reading_chars = 0;
  ime_clear_candidates(state);
}

int ime_drop_last_reading_char(struct ime_state *state)
{
  int start;

  if (state == NULL || state->reading_len <= 0 || state->reading_chars <= 0)
    return 0;

  start = utf8_prev_char_start(state->reading, state->reading_len,
                               state->reading_len);
  if (start < 0)
    start = 0;
  state->reading_len = start;
  state->reading[state->reading_len] = '\0';
  state->reading_chars--;
  if (state->reading_chars < 0)
    state->reading_chars = 0;
  ime_clear_candidates(state);
  return 1;
}

int ime_feed_ascii(struct ime_state *state, char ch, char *out, int out_cap)
{
  int out_len = 0;

  if (state == NULL || out == NULL || out_cap <= 0)
    return -1;

  if (state->mode == IME_MODE_LATIN) {
    ime_reset_segment(state);
    out[0] = ch;
    return 1;
  }

  if (ime_is_alpha(ch) != 0 || ch == '\'') {
    if (state->preedit_len >= IME_PREEDIT_MAX - 1) {
      out_len = ime_process_preedit(state, 1, out, out_cap);
      if (out_len < 0)
        return -1;
    }
    if (state->preedit_len < IME_PREEDIT_MAX - 1) {
      state->preedit[state->preedit_len++] = ime_lower(ch);
      state->preedit[state->preedit_len] = '\0';
    }
    return out_len + ime_process_preedit(state, 0, out + out_len,
                                         out_cap - out_len);
  }

  out_len = ime_process_preedit(state, 1, out, out_cap);
  if (out_len < 0)
    return -1;
  if (ime_emit_ascii(ch, out, out_len, out_cap) < 0)
    return -1;
  ime_reset_segment(state);
  return out_len + 1;
}

int ime_backspace(struct ime_state *state)
{
  if (state == NULL || state->preedit_len <= 0)
    return 0;

  state->preedit_len--;
  state->preedit[state->preedit_len] = '\0';
  return 1;
}

int ime_flush(struct ime_state *state, char *out, int out_cap)
{
  if (state == NULL || out == NULL || out_cap <= 0)
    return -1;
  return ime_process_preedit(state, 1, out, out_cap);
}

static void ime_clear_candidates(struct ime_state *state)
{
  int i;

  if (state == NULL)
    return;
  memset(state->candidate_storage, 0, sizeof(state->candidate_storage));
  for (i = 0; i < IME_CANDIDATE_MAX; i++)
    state->candidates[i] = NULL;
  state->candidate_count = 0;
  state->candidate_index = 0;
  state->conversion_active = 0;
}

static int ime_append_bytes(char *out, int out_len, int out_cap,
                            const char *src, int src_len)
{
  if (out_len + src_len > out_cap)
    return -1;
  memcpy(out + out_len, src, src_len);
  return out_len + src_len;
}

static int ime_append_reading_bytes(struct ime_state *state,
                                    const char *src, int src_len, int chars)
{
  if (state == NULL || src == NULL || src_len < 0 || chars < 0)
    return -1;
  if (state->reading_len + src_len >= IME_READING_MAX)
    return -1;

  memcpy(state->reading + state->reading_len, src, (size_t)src_len);
  state->reading_len += src_len;
  state->reading[state->reading_len] = '\0';
  state->reading_chars += chars;
  ime_clear_candidates(state);
  return 0;
}

static int ime_emit_codepoint(struct ime_state *state, u_int32_t codepoint,
                              char *out, int out_len, int out_cap)
{
  char encoded[4];
  int encoded_len;

  codepoint = ime_mode_codepoint(state->mode, codepoint);
  encoded_len = utf8_encode(codepoint, encoded);
  if (encoded_len < 0)
    return -1;
  out_len = ime_append_bytes(out, out_len, out_cap, encoded, encoded_len);
  if (out_len < 0)
    return -1;
  if (ime_append_reading_bytes(state, encoded, encoded_len, 1) < 0)
    return -1;
  return out_len;
}

static int ime_emit_ascii(char ch, char *out, int out_len, int out_cap)
{
  if (out_len + 1 > out_cap)
    return -1;
  out[out_len] = ch;
  return out_len + 1;
}

static int ime_process_preedit(struct ime_state *state, int force,
                               char *out, int out_cap)
{
  int out_len = 0;

  while (state->preedit_len > 0) {
    const struct ime_mapping *mapping;
    int matched_len = 0;

    if (state->preedit_len >= 2 &&
        state->preedit[0] == state->preedit[1] &&
        ime_is_sokuon_target(state->preedit[0]) != 0) {
      out_len = ime_emit_codepoint(state, 0x3063U, out, out_len, out_cap);
      if (out_len < 0)
        return -1;
      ime_consume_preedit(state, 1);
      continue;
    }

    if (state->preedit[0] == 'n') {
      if (state->preedit_len >= 2 && state->preedit[1] == '\'') {
        out_len = ime_emit_codepoint(state, 0x3093U, out, out_len, out_cap);
        if (out_len < 0)
          return -1;
        ime_consume_preedit(state, 2);
        continue;
      }
      if (state->preedit_len >= 2 && state->preedit[1] == 'n') {
        if (state->preedit_len >= 3 || force != 0) {
          out_len = ime_emit_codepoint(state, 0x3093U, out, out_len, out_cap);
          if (out_len < 0)
            return -1;
          ime_consume_preedit(state, force != 0 && state->preedit_len == 2 ? 2 : 1);
          continue;
        }
      } else if (state->preedit_len >= 2 &&
                 ime_is_consonant(state->preedit[1]) != 0 &&
                 state->preedit[1] != 'y') {
        out_len = ime_emit_codepoint(state, 0x3093U, out, out_len, out_cap);
        if (out_len < 0)
          return -1;
        ime_consume_preedit(state, 1);
        continue;
      } else if (force != 0 && state->preedit_len == 1) {
        out_len = ime_emit_codepoint(state, 0x3093U, out, out_len, out_cap);
        if (out_len < 0)
          return -1;
        ime_consume_preedit(state, 1);
        continue;
      }
    }

    mapping = ime_find_mapping(state->preedit, state->preedit_len, &matched_len);
    if (mapping != NULL) {
      int i;

      for (i = 0; i < mapping->count; i++) {
        out_len = ime_emit_codepoint(state, mapping->codepoints[i],
                                     out, out_len, out_cap);
        if (out_len < 0)
          return -1;
      }
      ime_consume_preedit(state, matched_len);
      continue;
    }

    if (force == 0 && ime_has_prefix(state->preedit, state->preedit_len) != 0)
      break;

    out_len = ime_emit_ascii(state->preedit[0], out, out_len, out_cap);
    if (out_len < 0)
      return -1;
    ime_consume_preedit(state, 1);
  }

  return out_len;
}

static int ime_is_alpha(char ch)
{
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static int ime_is_vowel(char ch)
{
  return ch == 'a' || ch == 'i' || ch == 'u' || ch == 'e' || ch == 'o';
}

static int ime_is_consonant(char ch)
{
  return ime_is_alpha(ch) != 0 && ime_is_vowel(ch) == 0;
}

static int ime_is_sokuon_target(char ch)
{
  return ime_is_consonant(ch) != 0 && ch != 'n';
}

static char ime_lower(char ch)
{
  if (ch >= 'A' && ch <= 'Z')
    return (char)(ch - 'A' + 'a');
  return ch;
}

static int ime_has_prefix(const char *prefix, int prefix_len)
{
  unsigned int i;

  for (i = 0; i < sizeof(ime_table) / sizeof(ime_table[0]); i++) {
    const char *romaji = ime_table[i].romaji;
    int romaji_len = (int)strlen(romaji);
    if (prefix_len <= romaji_len &&
        ime_match_prefix(prefix, romaji, prefix_len) != 0)
      return 1;
  }
  if (prefix_len == 1 && prefix[0] == 'n')
    return 1;
  if (prefix_len == 2 && prefix[0] == 'n' && prefix[1] == 'n')
    return 1;
  return 0;
}

static const struct ime_mapping *ime_find_mapping(const char *text, int len,
                                                  int *matched_len)
{
  const struct ime_mapping *best = NULL;
  int best_len = 0;
  unsigned int i;

  for (i = 0; i < sizeof(ime_table) / sizeof(ime_table[0]); i++) {
    int romaji_len = (int)strlen(ime_table[i].romaji);

    if (romaji_len > len)
      continue;
    if (ime_match_prefix(text, ime_table[i].romaji, romaji_len) == 0)
      continue;
    if (romaji_len > best_len) {
      best = &ime_table[i];
      best_len = romaji_len;
    }
  }

  if (matched_len != NULL)
    *matched_len = best_len;
  return best;
}

static void ime_consume_preedit(struct ime_state *state, int consumed)
{
  if (consumed >= state->preedit_len) {
    state->preedit_len = 0;
    state->preedit[0] = '\0';
    return;
  }

  {
    int i;
    for (i = consumed; i < state->preedit_len; i++) {
      state->preedit[i - consumed] = state->preedit[i];
    }
    state->preedit_len -= consumed;
  }
  state->preedit[state->preedit_len] = '\0';
}

static u_int32_t ime_mode_codepoint(enum ime_mode mode, u_int32_t codepoint)
{
  if (mode == IME_MODE_KATAKANA &&
      codepoint >= 0x3041U && codepoint <= 0x3096U)
    return codepoint + 0x60U;
  return codepoint;
}

static int ime_match_prefix(const char *a, const char *b, int len)
{
  int i;

  for (i = 0; i < len; i++) {
    if (a[i] != b[i])
      return 0;
  }
  return 1;
}
