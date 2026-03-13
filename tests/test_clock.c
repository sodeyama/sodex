/*
 * Unit tests for clock_time() and uIP timer integration
 * Plan 01: clock_time() implementation
 *
 * TDD: RED -> GREEN -> REFACTOR
 */
#include "test_framework.h"

/* clock-arch.h defines - override CLOCK_CONF_SECOND to 100 for tick-based */
typedef int clock_time_t;
#define CLOCK_CONF_SECOND 100

/* The kernel_tick variable that clock_time() reads */
volatile unsigned int kernel_tick = 0;

/* clock_time() under test - linked from clock-arch.c */
clock_time_t clock_time(void);

/* Timer struct (from uIP timer.h) */
struct timer {
    clock_time_t start;
    clock_time_t interval;
};

/* Timer functions - linked from timer.c */
void timer_set(struct timer *t, clock_time_t interval);
void timer_reset(struct timer *t);
int timer_expired(struct timer *t);

/* === clock_time() tests === */

TEST(clock_time_returns_kernel_tick) {
    kernel_tick = 0;
    ASSERT_EQ(clock_time(), 0);
}

TEST(clock_time_reflects_tick_increment) {
    kernel_tick = 42;
    ASSERT_EQ(clock_time(), 42);
}

TEST(clock_time_large_value) {
    kernel_tick = 100000;
    ASSERT_EQ(clock_time(), 100000);
}

/* === timer integration tests === */

TEST(timer_not_expired_immediately) {
    kernel_tick = 0;
    struct timer t;
    timer_set(&t, 100);  /* 100 ticks = 1 second */
    ASSERT_EQ(timer_expired(&t), 0);
}

TEST(timer_expired_after_interval) {
    kernel_tick = 0;
    struct timer t;
    timer_set(&t, 100);
    kernel_tick = 100;
    ASSERT(timer_expired(&t));
}

TEST(timer_expired_well_after_interval) {
    kernel_tick = 0;
    struct timer t;
    timer_set(&t, 50);
    kernel_tick = 200;
    ASSERT(timer_expired(&t));
}

TEST(timer_reset_extends) {
    kernel_tick = 0;
    struct timer t;
    timer_set(&t, 100);
    kernel_tick = 100;
    ASSERT(timer_expired(&t));
    timer_reset(&t);
    /* After reset, start = 0 + 100 = 100, so expires at 200 */
    ASSERT_EQ(timer_expired(&t), 0);
    kernel_tick = 200;
    ASSERT(timer_expired(&t));
}

TEST(timer_set_at_nonzero_tick) {
    kernel_tick = 500;
    struct timer t;
    timer_set(&t, 100);
    ASSERT_EQ(timer_expired(&t), 0);
    kernel_tick = 599;
    ASSERT_EQ(timer_expired(&t), 0);
    kernel_tick = 600;
    ASSERT(timer_expired(&t));
}

/* === main === */

int main(void)
{
    printf("=== clock_time / timer tests (Plan 01) ===\n");

    RUN_TEST(clock_time_returns_kernel_tick);
    RUN_TEST(clock_time_reflects_tick_increment);
    RUN_TEST(clock_time_large_value);

    RUN_TEST(timer_not_expired_immediately);
    RUN_TEST(timer_expired_after_interval);
    RUN_TEST(timer_expired_well_after_interval);
    RUN_TEST(timer_reset_extends);
    RUN_TEST(timer_set_at_nonzero_tick);

    TEST_REPORT();
}
