/*
 *  @File pit8254.c @Brief PIT timer for system 
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/10/29  update: 2007/10/29
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <io.h>
#include <vga.h>
#include <process.h>
#include <pit8254.h>

#define PIT_COUNTER0    0x040
#define PIT_COUNTER1    0x041
#define PIT_COUNTER2    0x042
#define PIT_CONTROL     0x043

#define PITCTL_COUNTER0 0x00
#define PITCTL_COUNTER1 0x40
#define PITCTL_COUNTER2 0x80
#define PITCTL_RBC      0xC0    //Read Back Command

#define PITCTL_RW_16    0x30
#define PITCTL_RW_H8    0x20
#define PITCTL_RW_L8    0x10
#define PITCTL_LATCH    0x00

#define PITCTL_MODE0    0x00
#define PITCTL_MODE1    0x02
#define PITCTL_MODE2    0x04
#define PITCTL_MODE3    0x06
#define PITCTL_MODE4    0x08
#define PITCTL_MODE5    0x0A

#define PITCTL_CNTMODE_BIN 0x00
#define PITCTL_CNTMODE_DEC 0x01

PRIVATE void pit_setcounter(u_int16_t counter);

PUBLIC void init_pit()
{
  //pit_setcounter(LATCH);
}

PRIVATE void pit_setcounter(u_int16_t counter)
{
  out8(PIT_CONTROL, PITCTL_COUNTER0|PITCTL_RW_16|PITCTL_MODE2|PITCTL_CNTMODE_BIN);
  out8(PIT_COUNTER0, (counter&0xff));
  out8(PIT_COUNTER0, ((counter>>8)&0xff));
}

PUBLIC void sys_timer(u_int8_t cpl)
{
  //_kprintf("cpl:%x\n", cpl);
  //schedule();
  //_kputs("end sys_timer\n");
}

PUBLIC void kwait(u_int32_t time)
{
  //pseudo code
  u_int32_t waittime = time*10000;
  int i;
  for(i=0; i<waittime; i++);
}
