#ifndef _USR_IME_DICT_BLOB_H
#define _USR_IME_DICT_BLOB_H

#include <sys/types.h>

#define IME_DICT_BLOB_VERSION 3U
#define IME_DICT_BLOB_BUCKET_COUNT_MAX 16384
#define IME_DICT_BLOB_CACHE_BLOCK_SIZE 4096
#define IME_DICT_BLOB_CACHE_SLOTS 8
#define IME_DICT_BLOB_PATH_MAX 256

struct ime_dict_blob_header {
  char magic[4];
  u_int32_t version;
  u_int32_t bucket_count;
  u_int32_t entry_count;
  u_int32_t bucket_offset;
  u_int32_t entry_offset;
  u_int32_t data_offset;
  u_int32_t file_size;
};

struct ime_dict_blob_metrics {
  u_int32_t lookups;
  u_int32_t cache_hits;
  u_int32_t cache_misses;
  u_int32_t bytes_read;
};

struct ime_dict_blob_cache_entry {
  int valid;
  u_int32_t block_index;
  u_int32_t age;
  u_int8_t data[IME_DICT_BLOB_CACHE_BLOCK_SIZE];
};

struct ime_dict_blob_context {
  int fd;
  int ready;
  char path[IME_DICT_BLOB_PATH_MAX];
  struct ime_dict_blob_header header;
  u_int32_t bucket_offsets[IME_DICT_BLOB_BUCKET_COUNT_MAX + 1];
  struct ime_dict_blob_metrics metrics;
  u_int32_t tick;
  struct ime_dict_blob_cache_entry cache[IME_DICT_BLOB_CACHE_SLOTS];
};

void ime_dict_blob_init(struct ime_dict_blob_context *ctx);
int ime_dict_blob_open(struct ime_dict_blob_context *ctx, const char *path);
void ime_dict_blob_close(struct ime_dict_blob_context *ctx);
int ime_dict_blob_lookup(struct ime_dict_blob_context *ctx,
                         const char *reading,
                         char *storage, int storage_cap,
                         const char **out_candidates, int candidate_cap,
                         int *out_count);
int ime_dict_blob_lookup_with_cost(struct ime_dict_blob_context *ctx,
                                   const char *reading,
                                   char *storage, int storage_cap,
                                   const char **out_candidates,
                                   int candidate_cap,
                                   int *out_count,
                                   int *out_best_cost);
const struct ime_dict_blob_metrics *
ime_dict_blob_get_metrics(const struct ime_dict_blob_context *ctx);
u_int32_t ime_dict_blob_memory_budget(void);

#endif /* _USR_IME_DICT_BLOB_H */
