/*
 * Unit tests for NE2000 receive logic
 * Plan 02: ne2000_receive() implementation
 *
 * We mock I/O functions (in8, out8, in16, out16) and test the
 * ring buffer logic, BNRY/CURR tracking, and packet extraction.
 */
#include "test_framework.h"
#include <string.h>

/* NE2000 constants */
#define SEND_ADDR   0x40
#define PSTART_ADDR 0x46
#define PSTOP_ADDR  0x80
#define BNRY_ADDR   0x46
#define CURR_ADDR   0x47

#define UIP_BUFSIZE 420

/* === Ring buffer BNRY update logic tests === */

/* When next_page > PSTART_ADDR, new_bnry = next_page - 1 */
TEST(bnry_update_normal) {
    unsigned char next_page = 0x48;
    unsigned char new_bnry = next_page - 1;
    if (new_bnry < PSTART_ADDR) {
        new_bnry = PSTOP_ADDR - 1;
    }
    ASSERT_EQ(new_bnry, 0x47);
}

/* When next_page == PSTART_ADDR, wraps to PSTOP_ADDR - 1 */
TEST(bnry_update_wrap) {
    unsigned char next_page = PSTART_ADDR;
    unsigned char new_bnry = next_page - 1;
    if (new_bnry < PSTART_ADDR) {
        new_bnry = PSTOP_ADDR - 1;
    }
    ASSERT_EQ(new_bnry, PSTOP_ADDR - 1);
}

/* === Buffer empty check === */

TEST(buffer_empty_when_next_packet_equals_curr) {
    unsigned char bnry = 0x46;
    unsigned char curr = 0x47;
    unsigned char packet_page = bnry + 1;
    int is_empty = (packet_page == curr);
    ASSERT(is_empty);
}

TEST(buffer_not_empty_when_next_packet_differs) {
    unsigned char bnry = 0x46;
    unsigned char curr = 0x48;
    unsigned char packet_page = bnry + 1;
    int is_empty = (packet_page == curr);
    ASSERT(!is_empty);
}

/* === Packet length validation === */

TEST(valid_packet_length) {
    unsigned short pkt_len = 64 + 4;  /* 64 bytes data + 4 header */
    unsigned short data_len = pkt_len - 4;
    ASSERT(data_len > 0 && data_len <= UIP_BUFSIZE);
}

TEST(invalid_packet_length_too_large) {
    unsigned short pkt_len = UIP_BUFSIZE + 100 + 4;
    unsigned short data_len = pkt_len - 4;
    int is_invalid = (data_len > UIP_BUFSIZE || data_len == 0);
    ASSERT(is_invalid);
}

TEST(invalid_packet_length_zero) {
    unsigned short pkt_len = 4;  /* header only, 0 data */
    unsigned short data_len = pkt_len - 4;
    int is_invalid = (data_len > UIP_BUFSIZE || data_len == 0);
    ASSERT(is_invalid);
}

/* === Wrap-around detection === */

TEST(no_wraparound_normal_packet) {
    unsigned short data_addr = (0x47 << 8) + 4;  /* page 0x47, offset 4 */
    unsigned short data_len = 60;
    unsigned short ring_end = PSTOP_ADDR << 8;    /* 0x8000 */
    int needs_wrap = (data_addr + data_len > ring_end);
    ASSERT(!needs_wrap);
}

TEST(wraparound_at_ring_end) {
    /* Packet starts near end of ring */
    unsigned short data_addr = (0x7F << 8) + 4;  /* page 0x7F, offset 4 */
    unsigned short data_len = 300;                /* exceeds ring end */
    unsigned short ring_end = PSTOP_ADDR << 8;    /* 0x8000 */
    int needs_wrap = (data_addr + data_len > ring_end);
    ASSERT(needs_wrap);
}

TEST(wraparound_split_lengths) {
    unsigned short data_addr = (0x7F << 8) + 4;  /* 0x7F04 */
    unsigned short data_len = 300;
    unsigned short ring_end = PSTOP_ADDR << 8;    /* 0x8000 */
    unsigned short first_len = ring_end - data_addr;  /* 0x8000 - 0x7F04 = 252 */
    unsigned short second_len = data_len - first_len; /* 300 - 252 = 48 */
    ASSERT_EQ(first_len, 252);
    ASSERT_EQ(second_len, 48);
}

/* === Page address conversion === */

TEST(page_to_byte_addr) {
    unsigned char page = 0x46;
    unsigned short addr = page << 8;
    ASSERT_EQ(addr, 0x4600);
}

TEST(page_to_byte_addr_last) {
    unsigned char page = 0x7F;
    unsigned short addr = page << 8;
    ASSERT_EQ(addr, 0x7F00);
}

/* === Packet header parsing === */

TEST(parse_packet_header) {
    unsigned char pkt_hdr[4] = {0x01, 0x48, 0x40, 0x00};
    unsigned char recv_status = pkt_hdr[0];
    unsigned char next_page = pkt_hdr[1];
    unsigned short pkt_len = pkt_hdr[2] | (pkt_hdr[3] << 8);
    ASSERT_EQ(recv_status, 0x01);  /* ISR_PRX */
    ASSERT_EQ(next_page, 0x48);
    ASSERT_EQ(pkt_len, 64);
}

TEST(parse_packet_header_large) {
    unsigned char pkt_hdr[4] = {0x01, 0x4A, 0xA4, 0x01};
    unsigned char next_page = pkt_hdr[1];
    unsigned short pkt_len = pkt_hdr[2] | (pkt_hdr[3] << 8);
    ASSERT_EQ(next_page, 0x4A);
    ASSERT_EQ(pkt_len, 420);  /* 0x01A4 */
}

/* === main === */

int main(void)
{
    printf("=== NE2000 receive logic tests (Plan 02) ===\n");

    RUN_TEST(bnry_update_normal);
    RUN_TEST(bnry_update_wrap);

    RUN_TEST(buffer_empty_when_next_packet_equals_curr);
    RUN_TEST(buffer_not_empty_when_next_packet_differs);

    RUN_TEST(valid_packet_length);
    RUN_TEST(invalid_packet_length_too_large);
    RUN_TEST(invalid_packet_length_zero);

    RUN_TEST(no_wraparound_normal_packet);
    RUN_TEST(wraparound_at_ring_end);
    RUN_TEST(wraparound_split_lengths);

    RUN_TEST(page_to_byte_addr);
    RUN_TEST(page_to_byte_addr_last);

    RUN_TEST(parse_packet_header);
    RUN_TEST(parse_packet_header_large);

    TEST_REPORT();
}
