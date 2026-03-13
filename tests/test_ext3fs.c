/*
 * Unit tests for ext3fs bitmap and block index operations.
 *
 * Since ext3fs.c has too many kernel dependencies to compile on host,
 * we re-implement the pure logic functions here and test them.
 * This validates the algorithms used in ext3fs.c.
 */
#include "test_framework.h"
#include <stdint.h>

typedef uint8_t  u_int8_t;
typedef uint32_t u_int32_t;

#define BLOCK_SIZE 4096
#define EXT3_DIRECT_BLOCKS 12
#define IBLOCK_SIZE 512

/*
 * Bitmap functions - reimplemented from ext3fs.c for testing.
 * These match the logic in __get_free_bitmap and __set_bitmap.
 */

/* Find first free (0) bit in bitmap. Returns bit index or -1 if full. */
static int bitmap_find_free(u_int8_t *bitmap, int bitmap_size)
{
    int free_bit = 0;
    int i, j;
    for (i = 0; i < bitmap_size; i++) {
        if ((0xff & ~(bitmap[i])) == 0) {
            free_bit += 8;
            continue;
        }
        for (j = 0; j < 8; j++) {
            if (~(bitmap[i]) & (u_int8_t)(1 << j)) {
                free_bit += j;
                break;
            }
        }
        break;
    }
    if (i == bitmap_size)
        return -1;
    return free_bit;
}

/* Set a bit in the bitmap. */
static void bitmap_set(u_int8_t *bitmap, u_int32_t bit)
{
    bitmap[bit / 8] |= (1 << (bit % 8));
}

/* Test if a bit is set. */
static int bitmap_test(u_int8_t *bitmap, u_int32_t bit)
{
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

/* Clear a bit in the bitmap. */
static void bitmap_clear(u_int8_t *bitmap, u_int32_t bit)
{
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

/*
 * Block index calculation - from ext3 inode structure.
 * i_block[0..11]  = direct blocks
 * i_block[12]     = indirect block (pointers)
 * i_block[13]     = double indirect
 * i_block[14]     = triple indirect
 *
 * Pointers per block = BLOCK_SIZE / sizeof(u_int32_t) = 1024
 */
#define PTRS_PER_BLOCK (BLOCK_SIZE / sizeof(u_int32_t))

/* Determine which i_block[] index and sub-index for a logical block */
static int block_index_level(u_int32_t logical_block)
{
    if (logical_block < EXT3_DIRECT_BLOCKS)
        return 0;  /* direct */
    logical_block -= EXT3_DIRECT_BLOCKS;
    if (logical_block < PTRS_PER_BLOCK)
        return 1;  /* single indirect */
    logical_block -= PTRS_PER_BLOCK;
    if (logical_block < PTRS_PER_BLOCK * PTRS_PER_BLOCK)
        return 2;  /* double indirect */
    return 3;      /* triple indirect */
}

/* === Bitmap tests === */

TEST(bitmap_find_free_empty) {
    u_int8_t bitmap[16] = {0};
    ASSERT_EQ(bitmap_find_free(bitmap, 16), 0);
}

TEST(bitmap_find_free_first_used) {
    u_int8_t bitmap[16] = {0};
    bitmap_set(bitmap, 0);
    ASSERT_EQ(bitmap_find_free(bitmap, 16), 1);
}

TEST(bitmap_find_free_first_byte_full) {
    u_int8_t bitmap[16] = {0};
    bitmap[0] = 0xFF;  /* First 8 bits all used */
    ASSERT_EQ(bitmap_find_free(bitmap, 16), 8);
}

TEST(bitmap_find_free_all_full) {
    u_int8_t bitmap[4];
    int i;
    for (i = 0; i < 4; i++) bitmap[i] = 0xFF;
    ASSERT_EQ(bitmap_find_free(bitmap, 4), -1);
}

TEST(bitmap_find_free_scattered) {
    u_int8_t bitmap[16] = {0};
    /* Set bits 0,1,2 */
    bitmap_set(bitmap, 0);
    bitmap_set(bitmap, 1);
    bitmap_set(bitmap, 2);
    ASSERT_EQ(bitmap_find_free(bitmap, 16), 3);
}

TEST(bitmap_set_basic) {
    u_int8_t bitmap[16] = {0};
    bitmap_set(bitmap, 5);
    ASSERT_EQ(bitmap_test(bitmap, 5), 1);
    ASSERT_EQ(bitmap_test(bitmap, 4), 0);
    ASSERT_EQ(bitmap_test(bitmap, 6), 0);
}

TEST(bitmap_set_across_bytes) {
    u_int8_t bitmap[16] = {0};
    bitmap_set(bitmap, 0);
    bitmap_set(bitmap, 7);
    bitmap_set(bitmap, 8);
    bitmap_set(bitmap, 15);
    ASSERT_EQ(bitmap[0], 0x81);  /* bits 0 and 7 */
    ASSERT_EQ(bitmap[1], 0x81);  /* bits 8 and 15 */
}

TEST(bitmap_clear_basic) {
    u_int8_t bitmap[16];
    int i;
    for (i = 0; i < 16; i++) bitmap[i] = 0xFF;
    bitmap_clear(bitmap, 10);
    ASSERT_EQ(bitmap_test(bitmap, 10), 0);
    ASSERT_EQ(bitmap_test(bitmap, 9), 1);
    ASSERT_EQ(bitmap_test(bitmap, 11), 1);
}

TEST(bitmap_set_clear_roundtrip) {
    u_int8_t bitmap[16] = {0};
    bitmap_set(bitmap, 42);
    ASSERT_EQ(bitmap_test(bitmap, 42), 1);
    bitmap_clear(bitmap, 42);
    ASSERT_EQ(bitmap_test(bitmap, 42), 0);
}

TEST(bitmap_alloc_sequence) {
    /* Simulate allocating multiple inodes/blocks */
    u_int8_t bitmap[16] = {0};
    int i;
    for (i = 0; i < 20; i++) {
        int free = bitmap_find_free(bitmap, 16);
        ASSERT_EQ(free, i);
        bitmap_set(bitmap, free);
    }
}

/* === Block index level tests === */

TEST(block_level_direct) {
    int i;
    for (i = 0; i < EXT3_DIRECT_BLOCKS; i++) {
        ASSERT_EQ(block_index_level(i), 0);
    }
}

TEST(block_level_indirect) {
    ASSERT_EQ(block_index_level(EXT3_DIRECT_BLOCKS), 1);
    ASSERT_EQ(block_index_level(EXT3_DIRECT_BLOCKS + PTRS_PER_BLOCK - 1), 1);
}

TEST(block_level_double_indirect) {
    u_int32_t start = EXT3_DIRECT_BLOCKS + PTRS_PER_BLOCK;
    ASSERT_EQ(block_index_level(start), 2);
    ASSERT_EQ(block_index_level(start + PTRS_PER_BLOCK * PTRS_PER_BLOCK - 1), 2);
}

TEST(block_level_triple_indirect) {
    u_int32_t start = EXT3_DIRECT_BLOCKS + PTRS_PER_BLOCK + PTRS_PER_BLOCK * PTRS_PER_BLOCK;
    ASSERT_EQ(block_index_level(start), 3);
}

/* === Directory entry parsing tests === */

/*
 * ext3 directory entry format (on disk):
 * offset 0: u32 inode number
 * offset 4: u16 record length (to next entry)
 * offset 6: u8  name length
 * offset 7: u8  file type
 * offset 8: char[] name (padded to 4-byte boundary)
 */
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[256];
} __attribute__((packed)) test_dentry;

static int write_dentry(char *buf, uint32_t inode, uint8_t file_type,
                        const char *name, uint16_t rec_len)
{
    uint8_t name_len = 0;
    const char *p = name;
    while (*p++) name_len++;

    *(uint32_t*)(buf + 0) = inode;
    *(uint16_t*)(buf + 4) = rec_len;
    *(uint8_t*)(buf + 6) = name_len;
    *(uint8_t*)(buf + 7) = file_type;
    int i;
    for (i = 0; i < name_len; i++) buf[8 + i] = name[i];
    return rec_len;
}

TEST(dentry_parse_single) {
    char block[BLOCK_SIZE] = {0};
    write_dentry(block, 2, 2, ".", BLOCK_SIZE);

    uint32_t inode = *(uint32_t*)(block + 0);
    uint8_t name_len = *(uint8_t*)(block + 6);
    ASSERT_EQ(inode, 2);
    ASSERT_EQ(name_len, 1);
    ASSERT_EQ(block[8], '.');
}

TEST(dentry_parse_chain) {
    char block[BLOCK_SIZE] = {0};
    int off = 0;

    off += write_dentry(block + off, 2, 2, ".", 12);
    off += write_dentry(block + off, 2, 2, "..", 12);
    write_dentry(block + off, 11, 2, "lost+found", BLOCK_SIZE - off);

    /* Verify chain traversal */
    uint32_t pos = 0;
    uint16_t rec_len;
    int count = 0;

    while (pos < BLOCK_SIZE) {
        uint32_t ino = *(uint32_t*)(block + pos);
        rec_len = *(uint16_t*)(block + pos + 4);
        if (ino == 0) break;
        count++;
        pos += rec_len;
    }
    ASSERT_EQ(count, 3);
}

TEST(dentry_name_extraction) {
    char block[BLOCK_SIZE] = {0};
    write_dentry(block, 14, 1, "hello.txt", BLOCK_SIZE);

    uint8_t name_len = *(uint8_t*)(block + 6);
    char name[256] = {0};
    int i;
    for (i = 0; i < name_len; i++) name[i] = block[8 + i];

    ASSERT_STR_EQ(name, "hello.txt");
}

int main(void)
{
    printf("=== ext3fs bitmap/parse tests ===\n");

    RUN_TEST(bitmap_find_free_empty);
    RUN_TEST(bitmap_find_free_first_used);
    RUN_TEST(bitmap_find_free_first_byte_full);
    RUN_TEST(bitmap_find_free_all_full);
    RUN_TEST(bitmap_find_free_scattered);

    RUN_TEST(bitmap_set_basic);
    RUN_TEST(bitmap_set_across_bytes);
    RUN_TEST(bitmap_clear_basic);
    RUN_TEST(bitmap_set_clear_roundtrip);
    RUN_TEST(bitmap_alloc_sequence);

    RUN_TEST(block_level_direct);
    RUN_TEST(block_level_indirect);
    RUN_TEST(block_level_double_indirect);
    RUN_TEST(block_level_triple_indirect);

    RUN_TEST(dentry_parse_single);
    RUN_TEST(dentry_parse_chain);
    RUN_TEST(dentry_name_extraction);

    TEST_REPORT();
}
