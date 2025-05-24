#ifndef _RS232C_H
#define _RS232C_H

#include <sodex/const.h>
#include <sys/types.h>

PUBLIC void init_serial();
PUBLIC void com1_tx(char a);
PUBLIC void com1_putc(char a);
PUBLIC void com1_puts(char* s);
PUBLIC void com1_putb8(u_int8_t c);
PUBLIC void com1_putb16(u_int16_t c);
PUBLIC void com1_putb32(u_int32_t c);
PUBLIC void com1_printf(char *fmt, ...);

#endif /* _RS232C_H */
