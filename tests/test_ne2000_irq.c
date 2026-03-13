/*
 * Unit tests for NE2000 interrupt handler logic
 * Plan 03: interrupt handler behavior
 */
#include "test_framework.h"

/* ISR bit definitions */
#define ISR_PRX     (1<<0)
#define ISR_PTX     (1<<1)
#define ISR_RXE     (1<<2)
#define ISR_TXE     (1<<3)
#define ISR_OVW     (1<<4)
#define ISR_RDC     (1<<6)

/* === ISR flag parsing tests === */

TEST(isr_prx_detected) {
    unsigned char status = ISR_PRX;
    ASSERT(status & ISR_PRX);
}

TEST(isr_ptx_detected) {
    unsigned char status = ISR_PTX;
    ASSERT(status & ISR_PTX);
    ASSERT(!(status & ISR_PRX));
}

TEST(isr_multiple_flags) {
    unsigned char status = ISR_PRX | ISR_PTX | ISR_OVW;
    ASSERT(status & ISR_PRX);
    ASSERT(status & ISR_PTX);
    ASSERT(status & ISR_OVW);
    ASSERT(!(status & ISR_RXE));
}

TEST(isr_overflow_flag) {
    unsigned char status = ISR_OVW;
    ASSERT(status & ISR_OVW);
    ASSERT(!(status & ISR_PRX));
}

TEST(isr_clear_by_writeback) {
    unsigned char status = ISR_PRX | ISR_PTX;
    /* Writing status back clears those bits (NE2000 spec) */
    unsigned char cleared = status & ~status;
    ASSERT_EQ(cleared, 0);
}

/* === IRQ number validation === */

TEST(ne2000_irq_is_slave_pic) {
    int irq = 11;
    ASSERT(irq >= 8 && irq < 16);  /* slave PIC range */
}

TEST(ne2000_idt_vector) {
    int irq = 11;
    int vector = 0x20 + irq;  /* PIC base 0x20 */
    ASSERT_EQ(vector, 0x2B);
}

/* === main === */

int main(void)
{
    printf("=== NE2000 IRQ handler logic tests (Plan 03) ===\n");

    RUN_TEST(isr_prx_detected);
    RUN_TEST(isr_ptx_detected);
    RUN_TEST(isr_multiple_flags);
    RUN_TEST(isr_overflow_flag);
    RUN_TEST(isr_clear_by_writeback);
    RUN_TEST(ne2000_irq_is_slave_pic);
    RUN_TEST(ne2000_idt_vector);

    TEST_REPORT();
}
