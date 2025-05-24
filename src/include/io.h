#ifndef _IO_H
#define _IO_H

#include <sodex/const.h>
#include <sys/types.h>

PUBLIC void out8(u_int16_t port, u_int8_t num);
PUBLIC void out16(u_int16_t port, u_int16_t num);
PUBLIC void out32(u_int16_t port, u_int32_t num);
PUBLIC u_int8_t in8(u_int16_t port);
PUBLIC u_int16_t in16(u_int16_t port);
PUBLIC u_int32_t in32(u_int16_t port);

PUBLIC void enableInterrupt();
PUBLIC void disableInterrupt();
PUBLIC u_int8_t get_interrupt_bit_low();
PUBLIC u_int8_t get_interrupt_bit_high();
PUBLIC int is_enableInterrupt();

#endif /* io.h */
