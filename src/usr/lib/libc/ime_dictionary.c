#include <ime_dict_blob.h>
#include <ime_dictionary.h>
#include <stddef.h>
#include <string.h>

#define IME_DICTIONARY_DEFAULT_BLOB_PATH "/usr/bin/ime_dictionary.blob"

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

static const struct ime_dictionary_entry ime_dictionary_fallback[] = {
  {"にほんご", ime_candidates_nihongo, 1},
  {"かんじ", ime_candidates_kanji, 2},
  {"へんかん", ime_candidates_henkan, 1},
  {"あい", ime_candidates_ai, 2}
};

static struct ime_dict_blob_context ime_blob_context;
static struct ime_dictionary_metrics ime_dictionary_metrics;
static char ime_dictionary_blob_path[IME_DICT_BLOB_PATH_MAX] =
  IME_DICTIONARY_DEFAULT_BLOB_PATH;
static int ime_dictionary_blob_state;
static int ime_dictionary_runtime_ready;

static void ime_dictionary_prepare_runtime(void);
static int ime_dictionary_copy_candidates(const char *const *candidates,
                                          int candidate_count,
                                          char *storage, int storage_cap,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int *out_count);
static int ime_dictionary_lookup_fallback(const char *reading,
                                          char *storage, int storage_cap,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int *out_count);

int ime_dictionary_lookup(const char *reading,
                          char *storage, int storage_cap,
                          const char **out_candidates, int candidate_cap,
                          int *out_count)
{
  int lookup_result;

  if (reading == NULL || storage == NULL || storage_cap <= 0 ||
      out_candidates == NULL || candidate_cap <= 0 || out_count == NULL)
    return -1;

  ime_dictionary_prepare_runtime();
  ime_dictionary_metrics.lookups++;
  ime_dictionary_metrics.last_source = IME_DICTIONARY_SOURCE_NONE;
  *out_count = 0;

  if (ime_dictionary_blob_state == 0) {
    if (ime_dict_blob_open(&ime_blob_context, ime_dictionary_blob_path) == 0)
      ime_dictionary_blob_state = 1;
    else
      ime_dictionary_blob_state = -1;
  }

  if (ime_dictionary_blob_state > 0) {
    lookup_result = ime_dict_blob_lookup(&ime_blob_context, reading,
                                         storage, storage_cap,
                                         out_candidates, candidate_cap,
                                         out_count);
    if (lookup_result > 0) {
      ime_dictionary_metrics.blob_lookups++;
      ime_dictionary_metrics.last_source = IME_DICTIONARY_SOURCE_BLOB;
      return 0;
    }
    if (lookup_result < 0) {
      ime_dict_blob_close(&ime_blob_context);
      ime_dictionary_blob_state = -1;
    } else {
      return -1;
    }
  }

  if (ime_dictionary_lookup_fallback(reading, storage, storage_cap,
                                     out_candidates, candidate_cap,
                                     out_count) == 0) {
    ime_dictionary_metrics.fallback_lookups++;
    ime_dictionary_metrics.last_source = IME_DICTIONARY_SOURCE_FALLBACK;
    return 0;
  }

  return -1;
}

void ime_dictionary_reset_runtime(void)
{
  ime_dict_blob_close(&ime_blob_context);
  ime_dict_blob_init(&ime_blob_context);
  memset(&ime_dictionary_metrics, 0, sizeof(ime_dictionary_metrics));
  ime_dictionary_metrics.memory_budget = ime_dict_blob_memory_budget();
  ime_dictionary_metrics.last_source = IME_DICTIONARY_SOURCE_NONE;
  ime_dictionary_blob_state = 0;
  ime_dictionary_runtime_ready = 1;
}

void ime_dictionary_get_metrics(struct ime_dictionary_metrics *out_metrics)
{
  if (out_metrics == NULL)
    return;
  ime_dictionary_prepare_runtime();
  *out_metrics = ime_dictionary_metrics;
}

int ime_dictionary_last_source(void)
{
  ime_dictionary_prepare_runtime();
  return ime_dictionary_metrics.last_source;
}

#ifdef TEST_BUILD
int ime_dictionary_set_blob_path(const char *path)
{
  size_t len;

  if (path == NULL)
    return -1;
  len = strlen(path);
  if (len == 0 || len >= sizeof(ime_dictionary_blob_path))
    return -1;
  memcpy(ime_dictionary_blob_path, path, len + 1);
  ime_dictionary_reset_runtime();
  return 0;
}
#endif

static void ime_dictionary_prepare_runtime(void)
{
  if (ime_dictionary_runtime_ready != 0)
    return;
  ime_dictionary_reset_runtime();
}

static int ime_dictionary_copy_candidates(const char *const *candidates,
                                          int candidate_count,
                                          char *storage, int storage_cap,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int *out_count)
{
  int i;
  int offset = 0;

  if (candidates == NULL || storage == NULL || storage_cap <= 0 ||
      out_candidates == NULL || candidate_cap <= 0 || out_count == NULL ||
      candidate_count <= 0 || candidate_count > candidate_cap)
    return -1;

  for (i = 0; i < candidate_count; i++) {
    int len;

    if (candidates[i] == NULL)
      return -1;
    len = (int)strlen(candidates[i]) + 1;
    if (offset + len > storage_cap)
      return -1;
    memcpy(storage + offset, candidates[i], (size_t)len);
    out_candidates[i] = storage + offset;
    offset += len;
  }
  if (offset < storage_cap)
    storage[offset] = '\0';
  for (; i < candidate_cap; i++)
    out_candidates[i] = NULL;
  *out_count = candidate_count;
  return 0;
}

static int ime_dictionary_lookup_fallback(const char *reading,
                                          char *storage, int storage_cap,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int *out_count)
{
  unsigned int i;

  if (reading == NULL)
    return -1;

  for (i = 0; i < sizeof(ime_dictionary_fallback) /
                  sizeof(ime_dictionary_fallback[0]); i++) {
    if (strcmp(ime_dictionary_fallback[i].reading, reading) != 0)
      continue;
    return ime_dictionary_copy_candidates(ime_dictionary_fallback[i].candidates,
                                          ime_dictionary_fallback[i].candidate_count,
                                          storage, storage_cap,
                                          out_candidates, candidate_cap,
                                          out_count);
  }

  return -1;
}
