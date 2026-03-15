#ifndef _MEMORY_LAYOUT_H
#define _MEMORY_LAYOUT_H

#ifndef TEST_BUILD
#include <sodex/const.h>
#include <sys/types.h>
#include <ld/page_linker.h>
#endif

#define BOOT_E820_MAX_ENTRIES            32
#define MEMORY_LAYOUT_LOW_RESERVED_BYTES 0x00100000
#define MEMORY_LAYOUT_DEFAULT_RAM_BYTES  (64 * 1024 * 1024)
#define MEMORY_LAYOUT_MAX_RAM_BYTES      0x40000000
#define MEMORY_LAYOUT_MIN_POOL_BASE      0x02000000
#define MEMORY_LAYOUT_PDE_BYTES          0x00400000
#define MEMORY_LAYOUT_KERNEL_PDE_BASE    768

#define MEMORY_E820_TYPE_USABLE          1
#define MEMORY_E820_TYPE_RESERVED        2

#define MEMORY_INFO_SOURCE_UNKNOWN       0
#define MEMORY_INFO_SOURCE_E820          1
#define MEMORY_INFO_SOURCE_E801          2
#define MEMORY_INFO_SOURCE_88            3
#define MEMORY_INFO_SOURCE_DEFAULT       4

typedef struct _boot_e820_entry {
  u_int32_t base_low;
  u_int32_t base_high;
  u_int32_t length_low;
  u_int32_t length_high;
  u_int32_t type;
} boot_e820_entry_t;

typedef struct _boot_memory_info {
  u_int16_t detected_ram_mb;
  u_int16_t source;
  u_int16_t e820_entry_count;
  u_int16_t e820_truncated;
  u_int16_t reserved_words[12];
  boot_e820_entry_t e820_entries[BOOT_E820_MAX_ENTRIES];
} boot_memory_info_t;

typedef struct _memory_range {
  u_int32_t base;
  u_int32_t size;
} memory_range_t;

typedef struct _memory_info {
  u_int32_t source;
  u_int32_t detected_ram_bytes;
  u_int32_t usable_range_count;
  u_int32_t reserved_range_count;
  u_int32_t e820_truncated;
  memory_range_t usable_ranges[BOOT_E820_MAX_ENTRIES];
  memory_range_t reserved_ranges[BOOT_E820_MAX_ENTRIES];
} memory_info_t;

typedef struct _memory_layout_policy {
  u_int32_t configured_cap_bytes;
  u_int32_t effective_ram_bytes;
  u_int32_t direct_map_bytes;
  u_int32_t kernel_pde_end;
  memory_range_t kernel_heap;
  memory_range_t process_pool;
} memory_layout_policy_t;

PUBLIC void memory_info_init(memory_info_t *info);
PUBLIC void memory_info_from_boot_raw(memory_info_t *info,
                                      const boot_memory_info_t *boot_info);
PUBLIC void memory_layout_init(memory_layout_policy_t *policy);
PUBLIC void memory_layout_build(memory_layout_policy_t *policy,
                                const memory_info_t *info,
                                u_int32_t kernel_image_end_phys,
                                u_int32_t configured_cap_bytes);
PUBLIC void memory_layout_init_runtime(u_int32_t kernel_image_end_phys);
PUBLIC int memory_layout_is_initialized(void);
PUBLIC const memory_info_t *memory_get_info(void);
PUBLIC const memory_layout_policy_t *memory_get_layout_policy(void);
PUBLIC u_int32_t memory_get_configured_cap_bytes(void);
PUBLIC const char *memory_source_name(u_int32_t source);

#endif /* _MEMORY_LAYOUT_H */
