#ifndef _PIT8254_H
#define _PIT8254_H

#include <sys/types.h>

#define CLOCK_TICK_RATE 1193180
#define HZ  100
#define LATCH (CLOCK_TICK_RATE/HZ)


struct timer_struct {
  u_int32_t counter;
};

PUBLIC void init_pit();
PUBLIC void sys_timer(u_int8_t cpl);
PUBLIC void kwait(u_int32_t time);

#endif
