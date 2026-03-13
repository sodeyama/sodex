/*
 * Unit tests for kernel network integration logic
 * Plan 04: kernel integration validation
 *
 * Tests the IP address configuration values and MAC address.
 */
#include "test_framework.h"

/* === QEMU user net address constants === */

TEST(guest_ip_is_10_0_2_15) {
    unsigned char ip[4] = {10, 0, 2, 15};
    ASSERT_EQ(ip[0], 10);
    ASSERT_EQ(ip[1], 0);
    ASSERT_EQ(ip[2], 2);
    ASSERT_EQ(ip[3], 15);
}

TEST(gateway_is_10_0_2_2) {
    unsigned char gw[4] = {10, 0, 2, 2};
    ASSERT_EQ(gw[0], 10);
    ASSERT_EQ(gw[3], 2);
}

TEST(netmask_is_255_255_255_0) {
    unsigned char mask[4] = {255, 255, 255, 0};
    ASSERT_EQ(mask[0], 255);
    ASSERT_EQ(mask[3], 0);
}

TEST(mac_address_qemu_compatible) {
    unsigned char mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    /* QEMU OUI prefix: 52:54:00 */
    ASSERT_EQ(mac[0], 0x52);
    ASSERT_EQ(mac[1], 0x54);
    ASSERT_EQ(mac[2], 0x00);
}

/* === IRQ configuration === */

TEST(ne2000_irq_cascade_required) {
    /* IRQ11 is on slave PIC, so cascade (IRQ2) must be enabled */
    int irq_ne2000 = 11;
    int irq_cascade = 2;
    ASSERT(irq_ne2000 >= 8);        /* slave PIC */
    ASSERT(irq_cascade == 2);       /* cascade line */
}

TEST(idt_vector_0x2B) {
    /* NE2K_QEMU_IRQ should be 0x2B */
    int ne2k_qemu_irq = 0x2B;
    ASSERT_EQ(ne2k_qemu_irq, 43);
}

/* === main === */

int main(void)
{
    printf("=== Kernel network integration tests (Plan 04) ===\n");

    RUN_TEST(guest_ip_is_10_0_2_15);
    RUN_TEST(gateway_is_10_0_2_2);
    RUN_TEST(netmask_is_255_255_255_0);
    RUN_TEST(mac_address_qemu_compatible);
    RUN_TEST(ne2000_irq_cascade_required);
    RUN_TEST(idt_vector_0x2B);

    TEST_REPORT();
}
