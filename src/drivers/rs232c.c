/*
 *  @File rs232c.c @Brief serial driver for 16450 (UART)
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/05/18  update: 2007/05/18
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <rs232c.h>
#include <io.h>
#include <stdarg.h>

/*
 * This defines are i/o ports about NS16450 controller 
 *  The last comment,like R/W,indicate whether the register is the thing
 *  to read or to write.
 */

/* Receiver Buffer Register */
#define RBR_COM1    0x3F8  // R
#define RBR_COM2    0x2F8  // R
#define RBR_COM3    0x3E8  // R
#define RBR_COM4    0x2E8  // R
/* Transmit Holding Register */
#define THR_COM1    0x3F8  // W
#define THR_COM2    0x2F8  // W
#define THR_COM3    0x3E8  // W
#define THR_COM4    0x2E8  // W

/* Interrupt Enable Register */
#define IER_COM1    0x3F9  // R/W
#define IER_COM2    0x2F9  // R/W
#define IER_COM3    0x3E9  // R/W
#define IER_COM4    0x2E9  // R/W

#define DLABL_COM1  0x3F8
#define DLABL_COM2  0x2F8
#define DLABL_COM3  0x3E8
#define DLABL_COM4  0x2E8
#define DLABH_COM1  0x3F9
#define DLABH_COM2  0x2F9
#define DLABH_COM3  0x3E9
#define DLABH_COM4  0x2E9

/* Interrupt Identifing Register */
#define IIR_COM1    0x3FA  // R
#define IIR_COM2    0x2FA  // R
#define IIR_COM3    0x3EA  // R
#define IIR_COM4    0x2EA  // R
/* FIFO Control Reigster */
#define FCR_COM1    0x3FA  // W
#define FCR_COM2    0x2FA  // W
#define FCR_COM3    0x3EA  // W
#define FCR_COM4    0x2EA  // W

/* Line Control Register */
#define LCR_COM1    0x3FB  // R/W
#define LCR_COM2    0x2FB  // R/W
#define LCR_COM3    0x3EB  // R/W
#define LCR_COM4    0x2EB  // R/W

/* Modem Control Register */
#define MCR_COM1    0x3FC  // R/W
#define MCR_COM2    0x2FC  // R/W
#define MCR_COM3    0x3EC  // R/W
#define MCR_COM4    0x2EC  // R/W

/* Line Status Register */
#define LSR_COM1    0x3FD  // R
#define LSR_COM2    0x2FD  // R
#define LSR_COM3    0x3ED  // R
#define LSR_COM4    0x2ED  // R

/* Modem Status Reigster */
#define MSR_COM1    0x3FE  // R
#define MSR_COM2    0x2FE  // R
#define MSR_COM3    0x3EE  // R
#define MSR_COM4    0x2EE  // R

/* IER bit */
#define IER_ALL_DISABLE 0
#define IER_RX_ENABLE   1 // recieve interrupt
#define IER_TX_ENABLE   2 // transmit interrupt
#define IER_RLS_ENABLE  4 // reciever line status interrupt
#define IER_MS_ENABLE   8 // modem status interrupt

/* LCR bit */
#define LCR_DLAB_ENABLE     0x80 
#define LCR_STOP_BIT2       4       // stop bit is 2bit
#define LCR_WORD_LENGTH8    3       // 1word is 8bit

/* MCR bit */
#define MCR_OUT2_ENABLE     0xB

/* DLAB default value */
#define DLAB_9600   0xC
#define DLAB_14400  0x8
#define DLAB_19200  0x6
#define DLAB_38400  0x3
#define DLAB_57600  0x2
#define DLAB_115200 0x1

PUBLIC void init_serial()
{
  out8(IER_COM1, IER_ALL_DISABLE);
  out8(LCR_COM1, LCR_DLAB_ENABLE|LCR_STOP_BIT2|LCR_WORD_LENGTH8);
  out8(DLABL_COM1, DLAB_9600);
  out8(DLABH_COM1, 0); // dlab disable
  out8(LCR_COM1, LCR_STOP_BIT2|LCR_WORD_LENGTH8);
  out8(IER_COM1, IER_RX_ENABLE|IER_TX_ENABLE);
  out8(MCR_COM1, MCR_OUT2_ENABLE);
  out8(FCR_COM1, 0); // fifo disable
}

PUBLIC void com1_printf(char *fmt, ...)
{
  char *p;
  va_list ap;

  va_start(ap, fmt);
  for (p = fmt; *p != '\0'; p++) {
    if (*p == '%') {
      switch(*(++p)) {
      case 'x':
        {
          u_int32_t x = va_arg(ap, u_int32_t);
          if ((x & 0xffffff00) == 0) {
            com1_putb8(x);
          } else if ((x & 0xffff0000) == 0) {
            com1_putb16(x);
          } else {
            com1_putb32(x);
          }
        }
        break;
      case 'c':
        {
          char c = va_arg(ap, char);
          com1_putc(c);
        }
        break;
      case 's':
        {
          char* s = va_arg(ap, char*);
          com1_puts(s);
        }
        break;
      case '%':
        com1_putc('%');
        break;
      }
    } else {
      com1_putc(*p);
    }
  }
  va_end(ap);
}

PUBLIC void com1_puts(char* s)
{
  while (*s != '\0') {
	com1_tx(*s);
	s++;
  }
}

PUBLIC void com1_putc(char a)
{
  com1_tx(a);
}

PUBLIC void com1_putb8(u_int8_t c)
{
  char high, low;
  high = (c >> 4) & 0xf;
  low  = c & 0xf;
  if (high >= 0 && high <= 9)
    high += 0x30;
  else
    high += 0x37;
  com1_putc(high);
  
  if (low >= 0 && low <= 9)
    low += 0x30;
  else
    low += 0x37;
  com1_putc(low);
}

PUBLIC void com1_putb16(u_int16_t c)
{
  u_int8_t high, low;
  high = (c >> 8) & 0xff;
  low  = c & 0xff;

  com1_putb8(high);
  com1_putb8(low);
}

PUBLIC void com1_putb32(u_int32_t c)
{
  u_int16_t high, low;
  high = (c >> 16) & 0xffff;
  low  = c & 0xffff;

  com1_putb16(high);
  com1_putb16(low);
}

PUBLIC void com1_tx(char a)
{
  out8(THR_COM1, a);
}
