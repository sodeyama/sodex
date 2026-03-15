#include <ime_dict_blob.h>
#include <fs.h>
#include <stddef.h>
#include <string.h>

#ifdef TEST_BUILD
#include <fcntl.h>
#include <unistd.h>
#else
#include <stdlib.h>
#endif

#define IME_DICT_BLOB_MAGIC "IMED"
#define IME_DICT_BLOB_ENTRY_SIZE 20

struct ime_dict_blob_entry {
  u_int32_t reading_hash;
  u_int32_t reading_offset;
  u_int32_t candidate_offset;
  u_int16_t reading_len;
  u_int16_t candidate_count;
  u_int32_t candidate_bytes;
};

static int ime_dict_blob_read_raw_all(int fd, u_int32_t offset,
                                      void *buf, int len);
static int ime_dict_blob_read_raw_partial(int fd, u_int32_t offset,
                                          void *buf, int len, int *out_read);
static struct ime_dict_blob_cache_entry *
ime_dict_blob_load_block(struct ime_dict_blob_context *ctx, u_int32_t block_index);
static int ime_dict_blob_read_at(struct ime_dict_blob_context *ctx,
                                 u_int32_t offset, void *buf, int len);
static int ime_dict_blob_read_entry(struct ime_dict_blob_context *ctx,
                                    u_int32_t index,
                                    struct ime_dict_blob_entry *entry);
static int ime_dict_blob_match_reading(struct ime_dict_blob_context *ctx,
                                       const struct ime_dict_blob_entry *entry,
                                       const char *reading,
                                       u_int32_t reading_hash,
                                       int reading_len);
static int ime_dict_blob_parse_candidates(char *storage, int candidate_bytes,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int candidate_count);
static u_int32_t ime_dict_blob_hash(const char *reading);
static int ime_dict_blob_memcmp(const void *a, const void *b, size_t len);

void ime_dict_blob_init(struct ime_dict_blob_context *ctx)
{
  if (ctx == NULL)
    return;

  memset(ctx, 0, sizeof(*ctx));
  ctx->fd = -1;
}

int ime_dict_blob_open(struct ime_dict_blob_context *ctx, const char *path)
{
  u_int32_t bucket_bytes;
  size_t path_len;

  if (ctx == NULL || path == NULL)
    return -1;

  path_len = strlen(path);
  if (path_len == 0 || path_len >= sizeof(ctx->path))
    return -1;

  ime_dict_blob_close(ctx);
  ctx->fd = open(path, O_RDONLY, 0);
  if (ctx->fd < 0) {
    ime_dict_blob_init(ctx);
    return -1;
  }

  if (ime_dict_blob_read_raw_all(ctx->fd, 0, &ctx->header,
                                 sizeof(ctx->header)) < 0)
    goto fail;
  if (ime_dict_blob_memcmp(ctx->header.magic, IME_DICT_BLOB_MAGIC, 4) != 0)
    goto fail;
  if (ctx->header.version != IME_DICT_BLOB_VERSION)
    goto fail;
  if (ctx->header.bucket_count == 0 ||
      ctx->header.bucket_count > IME_DICT_BLOB_BUCKET_COUNT_MAX)
    goto fail;
  if (ctx->header.bucket_offset < sizeof(ctx->header) ||
      ctx->header.entry_offset < ctx->header.bucket_offset ||
      ctx->header.data_offset < ctx->header.entry_offset ||
      ctx->header.file_size < ctx->header.data_offset)
    goto fail;

  bucket_bytes = (ctx->header.bucket_count + 1U) * sizeof(u_int32_t);
  if (ctx->header.bucket_offset + bucket_bytes > ctx->header.file_size)
    goto fail;
  if (ime_dict_blob_read_raw_all(ctx->fd, ctx->header.bucket_offset,
                                 ctx->bucket_offsets,
                                 (int)bucket_bytes) < 0)
    goto fail;

  memcpy(ctx->path, path, path_len + 1);
  ctx->ready = 1;
  return 0;

fail:
  ime_dict_blob_close(ctx);
  return -1;
}

void ime_dict_blob_close(struct ime_dict_blob_context *ctx)
{
  if (ctx == NULL)
    return;
  if (ctx->fd >= 0)
    close(ctx->fd);
  ime_dict_blob_init(ctx);
}

int ime_dict_blob_lookup(struct ime_dict_blob_context *ctx,
                         const char *reading,
                         char *storage, int storage_cap,
                         const char **out_candidates, int candidate_cap,
                         int *out_count)
{
  struct ime_dict_blob_entry entry;
  u_int32_t bucket;
  u_int32_t reading_hash;
  u_int32_t index;
  u_int32_t start;
  u_int32_t end;
  int reading_len;
  int i;

  if (ctx == NULL || reading == NULL || storage == NULL || storage_cap <= 0 ||
      out_candidates == NULL || candidate_cap <= 0 || out_count == NULL ||
      ctx->ready == 0)
    return -1;

  storage[0] = '\0';
  *out_count = 0;
  for (i = 0; i < candidate_cap; i++)
    out_candidates[i] = NULL;
  if (reading[0] == '\0')
    return 0;

  ctx->metrics.lookups++;
  reading_len = (int)strlen(reading);
  reading_hash = ime_dict_blob_hash(reading);
  bucket = reading_hash % ctx->header.bucket_count;
  start = ctx->bucket_offsets[bucket];
  end = ctx->bucket_offsets[bucket + 1U];

  for (index = start; index < end; index++) {
    int matched;

    if (ime_dict_blob_read_entry(ctx, index, &entry) < 0)
      return -1;
    matched = ime_dict_blob_match_reading(ctx, &entry, reading,
                                          reading_hash, reading_len);
    if (matched < 0)
      return -1;
    if (matched == 0)
      continue;

    if (entry.candidate_count == 0 || entry.candidate_count > candidate_cap ||
        entry.candidate_bytes == 0 || entry.candidate_bytes > (u_int32_t)storage_cap)
      return -1;
    if (ctx->header.data_offset + entry.candidate_offset + entry.candidate_bytes >
        ctx->header.file_size)
      return -1;
    if (ime_dict_blob_read_at(ctx,
                              ctx->header.data_offset + entry.candidate_offset,
                              storage, (int)entry.candidate_bytes) < 0)
      return -1;
    if (ime_dict_blob_parse_candidates(storage, (int)entry.candidate_bytes,
                                       out_candidates, candidate_cap,
                                       (int)entry.candidate_count) < 0)
      return -1;
    *out_count = (int)entry.candidate_count;
    return 1;
  }

  return 0;
}

const struct ime_dict_blob_metrics *
ime_dict_blob_get_metrics(const struct ime_dict_blob_context *ctx)
{
  if (ctx == NULL)
    return NULL;
  return &ctx->metrics;
}

u_int32_t ime_dict_blob_memory_budget(void)
{
  return (u_int32_t)sizeof(struct ime_dict_blob_context);
}

static int ime_dict_blob_read_raw_all(int fd, u_int32_t offset,
                                      void *buf, int len)
{
  int total = 0;
  char *dst = (char *)buf;

  if (fd < 0 || buf == NULL || len < 0)
    return -1;
  if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
    return -1;

  while (total < len) {
    ssize_t chunk = read(fd, dst + total, (size_t)(len - total));

    if (chunk <= 0)
      return -1;
    total += (int)chunk;
  }
  return 0;
}

static int ime_dict_blob_read_raw_partial(int fd, u_int32_t offset,
                                          void *buf, int len, int *out_read)
{
  int total = 0;
  char *dst = (char *)buf;

  if (out_read != NULL)
    *out_read = 0;
  if (fd < 0 || buf == NULL || len < 0)
    return -1;
  if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
    return -1;

  while (total < len) {
    ssize_t chunk = read(fd, dst + total, (size_t)(len - total));

    if (chunk < 0)
      return -1;
    if (chunk == 0)
      break;
    total += (int)chunk;
  }
  if (out_read != NULL)
    *out_read = total;
  return 0;
}

static struct ime_dict_blob_cache_entry *
ime_dict_blob_load_block(struct ime_dict_blob_context *ctx, u_int32_t block_index)
{
  struct ime_dict_blob_cache_entry *slot = NULL;
  u_int32_t i;
  int read_bytes = 0;

  if (ctx == NULL || ctx->ready == 0)
    return NULL;

  for (i = 0; i < IME_DICT_BLOB_CACHE_SLOTS; i++) {
    if (ctx->cache[i].valid != 0 && ctx->cache[i].block_index == block_index) {
      ctx->metrics.cache_hits++;
      ctx->cache[i].age = ++ctx->tick;
      return &ctx->cache[i];
    }
  }

  ctx->metrics.cache_misses++;
  for (i = 0; i < IME_DICT_BLOB_CACHE_SLOTS; i++) {
    if (ctx->cache[i].valid == 0) {
      slot = &ctx->cache[i];
      break;
    }
    if (slot == NULL || ctx->cache[i].age < slot->age)
      slot = &ctx->cache[i];
  }
  if (slot == NULL)
    return NULL;

  memset(slot->data, 0, sizeof(slot->data));
  if (ime_dict_blob_read_raw_partial(ctx->fd,
                                     block_index * IME_DICT_BLOB_CACHE_BLOCK_SIZE,
                                     slot->data,
                                     (int)sizeof(slot->data),
                                     &read_bytes) < 0)
    return NULL;

  slot->valid = 1;
  slot->block_index = block_index;
  slot->age = ++ctx->tick;
  ctx->metrics.bytes_read += (u_int32_t)read_bytes;
  return slot;
}

static int ime_dict_blob_read_at(struct ime_dict_blob_context *ctx,
                                 u_int32_t offset, void *buf, int len)
{
  char *dst = (char *)buf;
  int total = 0;

  if (ctx == NULL || buf == NULL || len < 0)
    return -1;
  if (offset + (u_int32_t)len > ctx->header.file_size)
    return -1;

  while (total < len) {
    struct ime_dict_blob_cache_entry *slot;
    u_int32_t current = offset + (u_int32_t)total;
    u_int32_t block_index = current / IME_DICT_BLOB_CACHE_BLOCK_SIZE;
    u_int32_t block_offset = current % IME_DICT_BLOB_CACHE_BLOCK_SIZE;
    int chunk = len - total;

    if (chunk > (int)(IME_DICT_BLOB_CACHE_BLOCK_SIZE - block_offset))
      chunk = (int)(IME_DICT_BLOB_CACHE_BLOCK_SIZE - block_offset);

    slot = ime_dict_blob_load_block(ctx, block_index);
    if (slot == NULL)
      return -1;
    memcpy(dst + total, slot->data + block_offset, (size_t)chunk);
    total += chunk;
  }

  return 0;
}

static int ime_dict_blob_read_entry(struct ime_dict_blob_context *ctx,
                                    u_int32_t index,
                                    struct ime_dict_blob_entry *entry)
{
  u_int8_t raw[IME_DICT_BLOB_ENTRY_SIZE];

  if (ctx == NULL || entry == NULL || index >= ctx->header.entry_count)
    return -1;
  if (ime_dict_blob_read_at(ctx,
                            ctx->header.entry_offset +
                            index * IME_DICT_BLOB_ENTRY_SIZE,
                            raw, sizeof(raw)) < 0)
    return -1;
  memcpy(entry, raw, sizeof(*entry));
  return 0;
}

static int ime_dict_blob_match_reading(struct ime_dict_blob_context *ctx,
                                       const struct ime_dict_blob_entry *entry,
                                       const char *reading,
                                       u_int32_t reading_hash,
                                       int reading_len)
{
  char chunk[32];
  int total = 0;

  if (ctx == NULL || entry == NULL || reading == NULL)
    return -1;

  if (entry->reading_hash != reading_hash)
    return 0;
  if ((u_int16_t)reading_len != entry->reading_len)
    return 0;
  if (ctx->header.data_offset + entry->reading_offset + entry->reading_len >
      ctx->header.file_size)
    return -1;

  while (total < reading_len) {
    int part = reading_len - total;

    if (part > (int)sizeof(chunk))
      part = (int)sizeof(chunk);
    if (ime_dict_blob_read_at(ctx,
                              ctx->header.data_offset + entry->reading_offset +
                              (u_int32_t)total,
                              chunk, part) < 0)
      return -1;
    if (ime_dict_blob_memcmp(chunk, reading + total, (size_t)part) != 0)
      return 0;
    total += part;
  }

  return 1;
}

static int ime_dict_blob_parse_candidates(char *storage, int candidate_bytes,
                                          const char **out_candidates,
                                          int candidate_cap,
                                          int candidate_count)
{
  int index = 0;
  int offset = 0;

  if (storage == NULL || out_candidates == NULL || candidate_bytes <= 0 ||
      candidate_cap <= 0 || candidate_count <= 0 || candidate_count > candidate_cap)
    return -1;

  while (index < candidate_count && offset < candidate_bytes) {
    out_candidates[index++] = storage + offset;
    while (offset < candidate_bytes && storage[offset] != '\0')
      offset++;
    if (offset >= candidate_bytes)
      return -1;
    offset++;
  }
  if (index != candidate_count || offset != candidate_bytes)
    return -1;
  while (index < candidate_cap) {
    out_candidates[index] = NULL;
    index++;
  }
  return 0;
}

static u_int32_t ime_dict_blob_hash(const char *reading)
{
  const unsigned char *p = (const unsigned char *)reading;
  u_int32_t value = 2166136261U;

  if (reading == NULL)
    return 0;
  while (*p != '\0') {
    value ^= (u_int32_t)*p;
    value *= 16777619U;
    p++;
  }
  return value;
}

static int ime_dict_blob_memcmp(const void *a, const void *b, size_t len)
{
  const unsigned char *left = (const unsigned char *)a;
  const unsigned char *right = (const unsigned char *)b;
  size_t i;

  for (i = 0; i < len; i++) {
    if (left[i] != right[i])
      return (int)left[i] - (int)right[i];
  }
  return 0;
}
