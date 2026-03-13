/*
 *  @File        gdt.c
 *  @Brief       set Global Descriptor Table
 *
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/23  update: 2007/05/15
 *
 *  Copyright (C) 2007 Sodex
 */

#include <descriptor.h>
#include <vga.h>

/* Use the GDT defined in startup.S (.data section) */
EXTERN GlobalDescTable gdt[];
EXTERN GDTR gdtr;

PRIVATE u_int16_t selNo = 0;

PUBLIC void makeGdt(u_int32_t segBase, u_int32_t limit, u_int16_t type,
                    u_int16_t selector)
{
  gdtno_t gdtno = selector >> 3; // selector is multiples of 8

  gdt[gdtno].limitL = (0xffff & limit);
  gdt[gdtno].segBaseL = (0xffff & segBase);
  gdt[gdtno].segBaseM = ((segBase >> 16) & 0xff);
  gdt[gdtno].typeL = (0xff & type);
  gdt[gdtno].typeH_limitH = ((limit >> 16) & 0xf);
  gdt[gdtno].typeH_limitH |= (((type >> 8)&0xf) <<4);
  gdt[gdtno].segBaseH = ((segBase >> 24) & 0xff);
}

PUBLIC GlobalDescTable* getGdt(u_int16_t selector)
{
  gdtno_t gdtno = selector >> 3;
  return &gdt[gdtno];
}

PUBLIC u_int16_t allocSel()
{
  u_int16_t sel = selNo;
  selNo += 0x08;

  return sel;
}

PUBLIC void init_setupgdt()
{
  /* GDT entries are already set up in startup.S (.data section).
   * The startup.S GDT includes:
   *   0x00: NULL descriptor
   *   0x08: Kernel code (DPL=0)
   *   0x10: Kernel data (DPL=0)
   *   0x18: Reserved
   *   0x20: User code (DPL=3)
   *   0x28: User data (DPL=3)
   *
   * Just reload GDTR to ensure it points to the correct address.
   * The gdtr is also defined in startup.S .data section.
   */
  gdtr.limit = sizeof(GlobalDescTable) * GDTNUM - 1;
  gdtr.baseH = (u_int16_t)(((u_int32_t)&gdt >> 16) & 0xffff);
  gdtr.baseL = (u_int16_t)(((u_int32_t)&gdt >>  0) & 0xffff);

  lgdt(&gdtr);

  selNo = 0x30;
}
