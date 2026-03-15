#include <ime_dictionary.h>
#include <stddef.h>
#include <string.h>

struct ime_dictionary_entry {
  const char *reading;
  const char *const *candidates;
  int candidate_count;
};

static const char *const ime_candidates_nihongo[] = {
  "日本語"
};

static const char *const ime_candidates_kanji[] = {
  "漢字",
  "感じ"
};

static const char *const ime_candidates_henkan[] = {
  "変換"
};

static const char *const ime_candidates_ai[] = {
  "愛",
  "藍"
};

static const struct ime_dictionary_entry ime_dictionary[] = {
  {"にほんご", ime_candidates_nihongo, 1},
  {"かんじ", ime_candidates_kanji, 2},
  {"へんかん", ime_candidates_henkan, 1},
  {"あい", ime_candidates_ai, 2}
};

int ime_dictionary_lookup(const char *reading,
                          const char *const **out_candidates,
                          int *out_count)
{
  unsigned int i;

  if (reading == NULL || out_candidates == NULL || out_count == NULL)
    return -1;

  *out_candidates = NULL;
  *out_count = 0;

  for (i = 0; i < sizeof(ime_dictionary) / sizeof(ime_dictionary[0]); i++) {
    if (strcmp(ime_dictionary[i].reading, reading) == 0) {
      *out_candidates = ime_dictionary[i].candidates;
      *out_count = ime_dictionary[i].candidate_count;
      return 0;
    }
  }

  return -1;
}
