#ifdef TEST_BUILD
#include <memory_layout.h>
#else
#include <string.h>
#include <memory_layout.h>
#endif

#ifndef SODEX_RAM_CAP_MB
#define SODEX_RAM_CAP_MB 0
#endif

#define MEMORY_BOOT_INFO_VADDR (__PAGE_OFFSET + 0x00098000)

PRIVATE memory_info_t g_memory_info;
PRIVATE memory_layout_policy_t g_memory_layout;
PRIVATE u_int8_t g_memory_layout_initialized = 0;

PRIVATE u_int32_t align_up(u_int32_t value, u_int32_t align)
{
  if (align == 0)
    return value;
  if ((value & (align - 1)) == 0)
    return value;
  return (value & ~(align - 1)) + align;
}

PRIVATE u_int32_t align_down(u_int32_t value, u_int32_t align)
{
  if (align == 0)
    return value;
  return (value & ~(align - 1));
}

PRIVATE u_int32_t min_u32(u_int32_t a, u_int32_t b)
{
  if (a < b)
    return a;
  return b;
}

PRIVATE u_int32_t max_u32(u_int32_t a, u_int32_t b)
{
  if (a > b)
    return a;
  return b;
}

PRIVATE void append_range(memory_range_t *ranges, u_int32_t *count,
                          u_int32_t base, u_int32_t size)
{
  if (size == 0)
    return;
  if (*count >= BOOT_E820_MAX_ENTRIES)
    return;
  ranges[*count].base = base;
  ranges[*count].size = size;
  (*count)++;
}

PRIVATE u_int32_t boot_detected_bytes(const boot_memory_info_t *boot_info)
{
  u_int32_t detected_bytes;

  if (boot_info == NULL || boot_info->detected_ram_mb == 0)
    return MEMORY_LAYOUT_DEFAULT_RAM_BYTES;

  detected_bytes = (u_int32_t)boot_info->detected_ram_mb * 1024 * 1024;
  if (detected_bytes == 0)
    return MEMORY_LAYOUT_DEFAULT_RAM_BYTES;
  return detected_bytes;
}

PRIVATE u_int32_t sanitize_detected_bytes(u_int32_t detected_bytes)
{
  detected_bytes = min_u32(detected_bytes, MEMORY_LAYOUT_MAX_RAM_BYTES);
  detected_bytes = align_down(detected_bytes, MEMORY_LAYOUT_PDE_BYTES);
  if (detected_bytes < MEMORY_LAYOUT_DEFAULT_RAM_BYTES)
    detected_bytes = MEMORY_LAYOUT_DEFAULT_RAM_BYTES;
  return detected_bytes;
}

PRIVATE u_int32_t configured_cap_bytes(void)
{
  if (SODEX_RAM_CAP_MB <= 0)
    return 0;
  return sanitize_detected_bytes((u_int32_t)SODEX_RAM_CAP_MB * 1024 * 1024);
}

PRIVATE int boot_has_e820(const boot_memory_info_t *boot_info)
{
  if (boot_info == NULL)
    return 0;
  if (boot_info->e820_entry_count == 0)
    return 0;
  return 1;
}

PRIVATE memory_range_t select_process_pool(const memory_info_t *info,
                                           u_int32_t start,
                                           u_int32_t end)
{
  memory_range_t best;
  u_int32_t i;

  best.base = start;
  best.size = 0;

  if (end <= start)
    return best;

  for (i = 0; i < info->usable_range_count; i++) {
    u_int32_t range_base = info->usable_ranges[i].base;
    u_int32_t range_end = range_base + info->usable_ranges[i].size;
    u_int32_t clipped_base = max_u32(range_base, start);
    u_int32_t clipped_end = min_u32(range_end, end);
    u_int32_t clipped_size;

    if (clipped_end <= clipped_base)
      continue;

    clipped_size = clipped_end - clipped_base;
    if (clipped_size > best.size) {
      best.base = clipped_base;
      best.size = clipped_size;
    }
  }

  if (best.size == 0) {
    best.base = start;
    best.size = end - start;
  }

  return best;
}

PUBLIC void memory_info_init(memory_info_t *info)
{
  if (info == NULL)
    return;
  memset(info, 0, sizeof(memory_info_t));
}

PUBLIC void memory_info_from_boot_raw(memory_info_t *info,
                                      const boot_memory_info_t *boot_info)
{
  u_int32_t i;
  u_int32_t detected_bytes;
  u_int32_t highest_usable_end = 0;

  memory_info_init(info);
  if (info == NULL)
    return;

  detected_bytes = boot_detected_bytes(boot_info);
  detected_bytes = sanitize_detected_bytes(detected_bytes);

  if (boot_has_e820(boot_info) != 0) {
    for (i = 0; i < boot_info->e820_entry_count && i < BOOT_E820_MAX_ENTRIES; i++) {
      u_int32_t base;
      u_int32_t size;
      u_int32_t end;
      const boot_e820_entry_t *entry = &boot_info->e820_entries[i];

      if (entry->length_low == 0)
        continue;
      if (entry->base_high != 0 || entry->length_high != 0)
        continue;

      base = entry->base_low;
      size = entry->length_low;
      end = base + size;
      if (end < base)
        continue;

      if (entry->type == MEMORY_E820_TYPE_USABLE) {
        append_range(info->usable_ranges, &info->usable_range_count, base, size);
        if (end > highest_usable_end)
          highest_usable_end = end;
      } else {
        append_range(info->reserved_ranges, &info->reserved_range_count, base, size);
      }
    }
  }

  if (info->usable_range_count > 0) {
    info->source = MEMORY_INFO_SOURCE_E820;
    info->detected_ram_bytes = sanitize_detected_bytes(highest_usable_end);
    info->e820_truncated = boot_info->e820_truncated;
    return;
  }

  if (boot_info != NULL)
    info->source = boot_info->source;
  if (info->source == MEMORY_INFO_SOURCE_UNKNOWN)
    info->source = MEMORY_INFO_SOURCE_DEFAULT;

  info->detected_ram_bytes = detected_bytes;
  append_range(info->reserved_ranges, &info->reserved_range_count,
               0, MEMORY_LAYOUT_LOW_RESERVED_BYTES);
  if (detected_bytes > MEMORY_LAYOUT_LOW_RESERVED_BYTES) {
    append_range(info->usable_ranges, &info->usable_range_count,
                 MEMORY_LAYOUT_LOW_RESERVED_BYTES,
                 detected_bytes - MEMORY_LAYOUT_LOW_RESERVED_BYTES);
  }
}

PUBLIC void memory_layout_init(memory_layout_policy_t *policy)
{
  if (policy == NULL)
    return;
  memset(policy, 0, sizeof(memory_layout_policy_t));
}

PUBLIC void memory_layout_build(memory_layout_policy_t *policy,
                                const memory_info_t *info,
                                u_int32_t kernel_image_end_phys,
                                u_int32_t configured_cap_bytes)
{
  u_int32_t detected_bytes;
  u_int32_t effective_bytes;
  u_int32_t heap_base_phys;
  u_int32_t process_pool_base;
  u_int32_t reserved_kernel_bytes;

  memory_layout_init(policy);
  if (policy == NULL || info == NULL)
    return;

  detected_bytes = sanitize_detected_bytes(info->detected_ram_bytes);
  if (configured_cap_bytes != 0)
    configured_cap_bytes = sanitize_detected_bytes(configured_cap_bytes);

  effective_bytes = detected_bytes;
  if (configured_cap_bytes != 0)
    effective_bytes = min_u32(effective_bytes, configured_cap_bytes);

  heap_base_phys = align_up(kernel_image_end_phys, BLOCK_SIZE);
  reserved_kernel_bytes = max_u32(MEMORY_LAYOUT_MIN_POOL_BASE, effective_bytes / 8);
  reserved_kernel_bytes = align_up(reserved_kernel_bytes, MEMORY_LAYOUT_PDE_BYTES);
  if (reserved_kernel_bytes > effective_bytes)
    reserved_kernel_bytes = effective_bytes;

  process_pool_base = reserved_kernel_bytes;
  if (process_pool_base < heap_base_phys)
    process_pool_base = align_up(heap_base_phys, MEMORY_LAYOUT_PDE_BYTES);
  if (process_pool_base > effective_bytes)
    process_pool_base = effective_bytes;

  policy->configured_cap_bytes = configured_cap_bytes;
  policy->effective_ram_bytes = effective_bytes;
  policy->direct_map_bytes = effective_bytes;
  policy->kernel_pde_end = MEMORY_LAYOUT_KERNEL_PDE_BASE +
    (effective_bytes / MEMORY_LAYOUT_PDE_BYTES);
  policy->kernel_heap.base = __PAGE_OFFSET + heap_base_phys;
  policy->kernel_heap.size = (process_pool_base > heap_base_phys) ?
    (process_pool_base - heap_base_phys) : 0;
  policy->process_pool = select_process_pool(info, process_pool_base, effective_bytes);
}

PUBLIC void memory_layout_init_runtime(u_int32_t kernel_image_end_phys)
{
  const boot_memory_info_t *boot_info;

  boot_info = (const boot_memory_info_t *)MEMORY_BOOT_INFO_VADDR;
  memory_info_from_boot_raw(&g_memory_info, boot_info);
  memory_layout_build(&g_memory_layout, &g_memory_info,
                      kernel_image_end_phys, configured_cap_bytes());
  g_memory_layout_initialized = 1;
}

PUBLIC int memory_layout_is_initialized(void)
{
  return g_memory_layout_initialized;
}

PUBLIC const memory_info_t *memory_get_info(void)
{
  return &g_memory_info;
}

PUBLIC const memory_layout_policy_t *memory_get_layout_policy(void)
{
  return &g_memory_layout;
}

PUBLIC u_int32_t memory_get_configured_cap_bytes(void)
{
  return configured_cap_bytes();
}

PUBLIC const char *memory_source_name(u_int32_t source)
{
  switch (source) {
  case MEMORY_INFO_SOURCE_E820:
    return "e820";
  case MEMORY_INFO_SOURCE_E801:
    return "e801";
  case MEMORY_INFO_SOURCE_88:
    return "88h";
  case MEMORY_INFO_SOURCE_DEFAULT:
    return "default";
  default:
    return "unknown";
  }
}
