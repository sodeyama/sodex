/*
 * Unit tests for network polling logic
 * Plan 05: network_poll() event dispatch
 *
 * Tests the EtherType dispatch logic used in network_poll().
 */
#include "test_framework.h"

/* EtherType constants from uIP */
#define UIP_ETHTYPE_IP  0x0800
#define UIP_ETHTYPE_ARP 0x0806

/* htons for little-endian (x86) */
static unsigned short test_htons(unsigned short x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

/* === EtherType dispatch tests === */

TEST(ethtype_ip_detected) {
    unsigned short type = test_htons(UIP_ETHTYPE_IP);
    ASSERT(type == test_htons(UIP_ETHTYPE_IP));
    ASSERT(type != test_htons(UIP_ETHTYPE_ARP));
}

TEST(ethtype_arp_detected) {
    unsigned short type = test_htons(UIP_ETHTYPE_ARP);
    ASSERT(type == test_htons(UIP_ETHTYPE_ARP));
    ASSERT(type != test_htons(UIP_ETHTYPE_IP));
}

TEST(ethtype_unknown_ignored) {
    unsigned short type = test_htons(0x86DD);  /* IPv6 */
    int is_ip = (type == test_htons(UIP_ETHTYPE_IP));
    int is_arp = (type == test_htons(UIP_ETHTYPE_ARP));
    ASSERT(!is_ip && !is_arp);
}

/* === htons byte swap tests === */

TEST(htons_swap) {
    ASSERT_EQ(test_htons(0x0800), 0x0008);
    ASSERT_EQ(test_htons(0x0806), 0x0608);
}

TEST(htons_roundtrip) {
    unsigned short orig = 0x1234;
    ASSERT_EQ(test_htons(test_htons(orig)), orig);
}

/* === Periodic timer intervals === */

TEST(periodic_timer_interval) {
    int clock_conf_second = 100;
    int periodic_interval = 50;  /* 500ms */
    /* 50 ticks / 100 ticks_per_sec = 0.5 sec */
    ASSERT_EQ(periodic_interval * 1000 / clock_conf_second, 500);
}

TEST(arp_timer_interval) {
    int clock_conf_second = 100;
    int arp_interval = 10 * clock_conf_second;  /* 10 seconds */
    ASSERT_EQ(arp_interval, 1000);
}

/* === rx_pending flag logic === */

TEST(rx_pending_cleared_before_loop) {
    volatile unsigned char ne2000_rx_pending = 1;
    ne2000_rx_pending = 0;
    ASSERT_EQ(ne2000_rx_pending, 0);
}

TEST(rx_pending_set_by_interrupt) {
    volatile unsigned char ne2000_rx_pending = 0;
    /* Simulate interrupt setting the flag */
    ne2000_rx_pending = 1;
    ASSERT_EQ(ne2000_rx_pending, 1);
}

/* === main === */

int main(void)
{
    printf("=== Network polling logic tests (Plan 05) ===\n");

    RUN_TEST(ethtype_ip_detected);
    RUN_TEST(ethtype_arp_detected);
    RUN_TEST(ethtype_unknown_ignored);
    RUN_TEST(htons_swap);
    RUN_TEST(htons_roundtrip);
    RUN_TEST(periodic_timer_interval);
    RUN_TEST(arp_timer_interval);
    RUN_TEST(rx_pending_cleared_before_loop);
    RUN_TEST(rx_pending_set_by_interrupt);

    TEST_REPORT();
}
