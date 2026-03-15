#include "test_framework.h"
#include <stdint.h>

#define TEST_BUILD
#include "mocks/mock_memory_deps.h"
#include <memory_layout.h>

static void set_entry(boot_e820_entry_t *entry, uint32_t base, uint32_t size, uint32_t type)
{
    entry->base_low = base;
    entry->base_high = 0;
    entry->length_low = size;
    entry->length_high = 0;
    entry->type = type;
}

TEST(memory_info_prefers_e820_map) {
    boot_memory_info_t boot_info;
    memory_info_t info;

    memory_info_init(&info);
    boot_info.detected_ram_mb = 64;
    boot_info.source = MEMORY_INFO_SOURCE_E801;
    boot_info.e820_entry_count = 3;
    boot_info.e820_truncated = 0;

    set_entry(&boot_info.e820_entries[0], 0x00000000u, 0x0009fc00u, MEMORY_E820_TYPE_USABLE);
    set_entry(&boot_info.e820_entries[1], 0x00100000u, 0x1ff00000u, MEMORY_E820_TYPE_USABLE);
    set_entry(&boot_info.e820_entries[2], 0x0009fc00u, 0x00060400u, MEMORY_E820_TYPE_RESERVED);

    memory_info_from_boot_raw(&info, &boot_info);

    ASSERT_EQ(info.source, MEMORY_INFO_SOURCE_E820);
    ASSERT_EQ(info.detected_ram_bytes, 512u * 1024u * 1024u);
    ASSERT_EQ(info.usable_range_count, 2);
    ASSERT_EQ(info.usable_ranges[1].base, 0x00100000u);
    ASSERT_EQ(info.usable_ranges[1].size, 0x1ff00000u);
    ASSERT_EQ(info.reserved_range_count, 1);
    ASSERT_EQ(info.reserved_ranges[0].base, 0x0009fc00u);
}

TEST(memory_info_falls_back_without_e820) {
    boot_memory_info_t boot_info;
    memory_info_t info;

    memory_info_init(&info);
    boot_info.detected_ram_mb = 128;
    boot_info.source = MEMORY_INFO_SOURCE_E801;
    boot_info.e820_entry_count = 0;
    boot_info.e820_truncated = 0;

    memory_info_from_boot_raw(&info, &boot_info);

    ASSERT_EQ(info.source, MEMORY_INFO_SOURCE_E801);
    ASSERT_EQ(info.detected_ram_bytes, 128u * 1024u * 1024u);
    ASSERT_EQ(info.usable_range_count, 1);
    ASSERT_EQ(info.usable_ranges[0].base, MEMORY_LAYOUT_LOW_RESERVED_BYTES);
    ASSERT_EQ(info.usable_ranges[0].size, (128u * 1024u * 1024u) - MEMORY_LAYOUT_LOW_RESERVED_BYTES);
}

TEST(memory_layout_applies_cap) {
    memory_info_t info;
    memory_layout_policy_t policy;

    memory_info_init(&info);
    info.source = MEMORY_INFO_SOURCE_E820;
    info.detected_ram_bytes = 512u * 1024u * 1024u;
    info.usable_range_count = 2;
    info.usable_ranges[0].base = 0x00000000u;
    info.usable_ranges[0].size = 0x0009fc00u;
    info.usable_ranges[1].base = 0x00100000u;
    info.usable_ranges[1].size = 0x1ff00000u;

    memory_layout_init(&policy);
    memory_layout_build(&policy, &info, 0x00400000u, 256u * 1024u * 1024u);

    ASSERT_EQ(policy.effective_ram_bytes, 256u * 1024u * 1024u);
    ASSERT_EQ(policy.direct_map_bytes, 256u * 1024u * 1024u);
    ASSERT_EQ(policy.kernel_heap.base, __PAGE_OFFSET + 0x00400000u);
    ASSERT_EQ(policy.kernel_heap.size, 0x01c00000u);
    ASSERT_EQ(policy.process_pool.base, 0x02000000u);
    ASSERT_EQ(policy.process_pool.size, 0x0e000000u);
    ASSERT_EQ(policy.kernel_pde_end, MEMORY_LAYOUT_KERNEL_PDE_BASE + 64);
}

TEST(memory_layout_scales_to_1g) {
    memory_info_t info;
    memory_layout_policy_t policy;

    memory_info_init(&info);
    info.source = MEMORY_INFO_SOURCE_E820;
    info.detected_ram_bytes = 1024u * 1024u * 1024u;
    info.usable_range_count = 2;
    info.usable_ranges[0].base = 0x00000000u;
    info.usable_ranges[0].size = 0x0009fc00u;
    info.usable_ranges[1].base = 0x00100000u;
    info.usable_ranges[1].size = 0x3ff00000u;

    memory_layout_init(&policy);
    memory_layout_build(&policy, &info, 0x00800000u, 0);

    ASSERT_EQ(policy.effective_ram_bytes, 1024u * 1024u * 1024u);
    ASSERT_EQ(policy.direct_map_bytes, 1024u * 1024u * 1024u);
    ASSERT_EQ(policy.kernel_heap.base, __PAGE_OFFSET + 0x00800000u);
    ASSERT_EQ(policy.process_pool.base, 0x08000000u);
    ASSERT_EQ(policy.process_pool.size, 0x38000000u);
    ASSERT_EQ(policy.kernel_pde_end, MEMORY_LAYOUT_KERNEL_PDE_BASE + 256);
}

int main(void)
{
    printf("=== memory_layout tests ===\n");

    RUN_TEST(memory_info_prefers_e820_map);
    RUN_TEST(memory_info_falls_back_without_e820);
    RUN_TEST(memory_layout_applies_cap);
    RUN_TEST(memory_layout_scales_to_1g);

    TEST_REPORT();
}
