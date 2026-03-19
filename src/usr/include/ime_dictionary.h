#ifndef _USR_IME_DICTIONARY_H
#define _USR_IME_DICTIONARY_H

#include <sys/types.h>

enum ime_dictionary_source {
  IME_DICTIONARY_SOURCE_NONE = 0,
  IME_DICTIONARY_SOURCE_BLOB = 1,
  IME_DICTIONARY_SOURCE_FALLBACK = 2
};

struct ime_dictionary_metrics {
  u_int32_t lookups;
  u_int32_t blob_lookups;
  u_int32_t fallback_lookups;
  u_int32_t result_cache_hits;
  u_int32_t result_cache_misses;
  u_int32_t memory_budget;
  int last_source;
};

int ime_dictionary_lookup(const char *reading,
                          char *storage, int storage_cap,
                          const char **out_candidates, int candidate_cap,
                          int *out_count);
int ime_dictionary_lookup_with_cost(const char *reading,
                                    char *storage, int storage_cap,
                                    const char **out_candidates,
                                    int candidate_cap,
                                    int *out_count,
                                    int *out_best_cost);
void ime_dictionary_reset_runtime(void);
void ime_dictionary_get_metrics(struct ime_dictionary_metrics *out_metrics);
int ime_dictionary_last_source(void);

#ifdef TEST_BUILD
int ime_dictionary_set_blob_path(const char *path);
#endif

#endif /* _USR_IME_DICTIONARY_H */
