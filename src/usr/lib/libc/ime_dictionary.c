#include <ime_dict_blob.h>
#include <ime.h>
#include <ime_dictionary.h>
#include <stddef.h>
#include <string.h>

#define IME_DICTIONARY_DEFAULT_BLOB_PATH "/usr/bin/ime_dictionary.blob"
#define IME_DICTIONARY_RESULT_CACHE_SLOTS 4

struct ime_dictionary_entry {
  const char *reading;
  const char *const *candidates;
  int candidate_count;
};

struct ime_dictionary_cache_entry {
  int valid;
  int source;
  u_int32_t age;
  int candidate_count;
  char reading[IME_READING_MAX];
  char storage[IME_CANDIDATE_STORAGE_MAX];
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
static u_int32_t ime_dictionary_cache_tick;
static struct ime_dictionary_cache_entry
  ime_dictionary_cache[IME_DICTIONARY_RESULT_CACHE_SLOTS];

static void ime_dictionary_prepare_runtime(void);
static u_int32_t ime_dictionary_memory_budget(void);
static int ime_dictionary_copy_candidates(const char *const *candidates,
                                          int candidate_count,
                                          char *storage, int storage_cap,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int *out_count);
static int ime_dictionary_copy_cached_entry(
    const struct ime_dictionary_cache_entry *entry,
    char *storage, int storage_cap,
    const char **out_candidates, int candidate_cap,
    int *out_count);
static int ime_dictionary_cache_lookup(const char *reading,
                                       char *storage, int storage_cap,
                                       const char **out_candidates,
                                       int candidate_cap,
                                       int *out_count,
                                       int *out_source);
static void ime_dictionary_cache_store(const char *reading,
                                       const char *storage,
                                       int candidate_count,
                                       int source);
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
  int cache_source = IME_DICTIONARY_SOURCE_NONE;
  int lookup_result;

  if (reading == NULL || storage == NULL || storage_cap <= 0 ||
      out_candidates == NULL || candidate_cap <= 0 || out_count == NULL)
    return -1;

  ime_dictionary_prepare_runtime();
  ime_dictionary_metrics.lookups++;
  ime_dictionary_metrics.last_source = IME_DICTIONARY_SOURCE_NONE;
  *out_count = 0;

  if (ime_dictionary_cache_lookup(reading, storage, storage_cap,
                                  out_candidates, candidate_cap,
                                  out_count, &cache_source) == 0) {
    ime_dictionary_metrics.result_cache_hits++;
    ime_dictionary_metrics.last_source = cache_source;
    if (cache_source == IME_DICTIONARY_SOURCE_BLOB)
      ime_dictionary_metrics.blob_lookups++;
    else if (cache_source == IME_DICTIONARY_SOURCE_FALLBACK)
      ime_dictionary_metrics.fallback_lookups++;
    return 0;
  }
  ime_dictionary_metrics.result_cache_misses++;

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
      ime_dictionary_cache_store(reading, storage, *out_count,
                                 IME_DICTIONARY_SOURCE_BLOB);
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
    ime_dictionary_cache_store(reading, storage, *out_count,
                               IME_DICTIONARY_SOURCE_FALLBACK);
    return 0;
  }

  return -1;
}

void ime_dictionary_reset_runtime(void)
{
  ime_dict_blob_close(&ime_blob_context);
  ime_dict_blob_init(&ime_blob_context);
  memset(ime_dictionary_cache, 0, sizeof(ime_dictionary_cache));
  ime_dictionary_cache_tick = 0;
  memset(&ime_dictionary_metrics, 0, sizeof(ime_dictionary_metrics));
  ime_dictionary_metrics.memory_budget = ime_dictionary_memory_budget();
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

static u_int32_t ime_dictionary_memory_budget(void)
{
  return ime_dict_blob_memory_budget() +
         (u_int32_t)sizeof(ime_dictionary_cache);
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

static int ime_dictionary_copy_cached_entry(
    const struct ime_dictionary_cache_entry *entry,
    char *storage, int storage_cap,
    const char **out_candidates, int candidate_cap,
    int *out_count)
{
  const char *cursor;
  int i;
  int offset = 0;

  if (entry == NULL || entry->valid == 0 || storage == NULL || storage_cap <= 0 ||
      out_candidates == NULL || candidate_cap <= 0 || out_count == NULL ||
      entry->candidate_count <= 0 || entry->candidate_count > candidate_cap)
    return -1;

  cursor = entry->storage;
  for (i = 0; i < entry->candidate_count; i++) {
    int len;

    len = (int)strlen(cursor) + 1;
    if (len <= 1 || offset + len > storage_cap)
      return -1;
    memcpy(storage + offset, cursor, (size_t)len);
    out_candidates[i] = storage + offset;
    offset += len;
    cursor += len;
  }
  if (offset < storage_cap)
    storage[offset] = '\0';
  for (; i < candidate_cap; i++)
    out_candidates[i] = NULL;
  *out_count = entry->candidate_count;
  return 0;
}

static int ime_dictionary_cache_lookup(const char *reading,
                                       char *storage, int storage_cap,
                                       const char **out_candidates,
                                       int candidate_cap,
                                       int *out_count,
                                       int *out_source)
{
  int i;

  if (out_source != NULL)
    *out_source = IME_DICTIONARY_SOURCE_NONE;
  if (reading == NULL)
    return -1;

  for (i = 0; i < IME_DICTIONARY_RESULT_CACHE_SLOTS; i++) {
    if (ime_dictionary_cache[i].valid == 0)
      continue;
    if (strcmp(ime_dictionary_cache[i].reading, reading) != 0)
      continue;
    ime_dictionary_cache[i].age = ++ime_dictionary_cache_tick;
    if (out_source != NULL)
      *out_source = ime_dictionary_cache[i].source;
    return ime_dictionary_copy_cached_entry(&ime_dictionary_cache[i],
                                            storage, storage_cap,
                                            out_candidates, candidate_cap,
                                            out_count);
  }
  return -1;
}

static void ime_dictionary_cache_store(const char *reading,
                                       const char *storage,
                                       int candidate_count,
                                       int source)
{
  struct ime_dictionary_cache_entry *slot = NULL;
  size_t reading_len;
  int i;
  int offset = 0;
  const char *cursor;

  if (reading == NULL || storage == NULL || candidate_count <= 0 ||
      candidate_count > IME_CANDIDATE_MAX)
    return;

  reading_len = strlen(reading);
  if (reading_len == 0 || reading_len >= sizeof(ime_dictionary_cache[0].reading))
    return;

  for (i = 0; i < IME_DICTIONARY_RESULT_CACHE_SLOTS; i++) {
    if (ime_dictionary_cache[i].valid == 0) {
      slot = &ime_dictionary_cache[i];
      break;
    }
    if (slot == NULL || ime_dictionary_cache[i].age < slot->age)
      slot = &ime_dictionary_cache[i];
  }
  if (slot == NULL)
    return;

  memset(slot, 0, sizeof(*slot));
  memcpy(slot->reading, reading, reading_len + 1);
  cursor = storage;
  for (i = 0; i < candidate_count; i++) {
    int len = (int)strlen(cursor) + 1;

    if (len <= 1 || offset + len > (int)sizeof(slot->storage))
      return;
    memcpy(slot->storage + offset, cursor, (size_t)len);
    offset += len;
    cursor += len;
  }

  slot->valid = 1;
  slot->source = source;
  slot->age = ++ime_dictionary_cache_tick;
  slot->candidate_count = candidate_count;
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
