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

PRIVATE GlobalDescTable globalDescTable[GDTNUM];
PRIVATE GDTR gdtr;
PRIVATE u_int16_t selNo = 0;

PUBLIC void makeGdt(u_int32_t segBase, u_int32_t limit, u_int16_t type,
                    u_int16_t selector)
{
  gdtno_t gdtno = selector >> 3; // selector is multiples of 8

  globalDescTable[gdtno].limitL = (0xffff & limit);
  globalDescTable[gdtno].segBaseL = (0xffff & segBase);
  globalDescTable[gdtno].segBaseM = ((segBase >> 16) & 0xff);
  globalDescTable[gdtno].typeL = (0xff & type);
  globalDescTable[gdtno].typeH_limitH = ((limit >> 16) & 0xf);
  globalDescTable[gdtno].typeH_limitH |= (((type >> 8)&0xf) <<4);
  globalDescTable[gdtno].segBaseH = ((segBase >> 24) & 0xff);
}

PUBLIC GlobalDescTable* getGdt(u_int16_t selector)
{
  gdtno_t gdtno = selector >> 3;
  return &globalDescTable[gdtno];
}

PUBLIC u_int16_t allocSel()
{
  u_int16_t sel = selNo;
  selNo += 0x08;

  return sel;
}

PUBLIC void init_setupgdt()
{
  u_int16_t type_0;
  u_int16_t type_code_dpl0, type_data_dpl0;//, type_stack_dpl0;
  u_int16_t type_code_dpl3, type_data_dpl3;//, type_stack_dpl3;
  
  type_0 = 0;
  type_code_dpl0 = (0xC << 8) | 0x9a | (DPL0 << 5);
  type_data_dpl0 = (0xC << 8) | 0x92 | (DPL0 << 5);
  //type_stack_dpl0 = (0xC << 8) | 0x96 | (DPL0 << 5);
  type_code_dpl3 = (0xC << 8) | 0x9a | (DPL3 << 5);
  type_data_dpl3 = (0xC << 8) | 0x92 | (DPL3 << 5);
  //type_stack_dpl3 = (0xC << 8) | 0x96 | (DPL3 << 5);

  makeGdt(0x0, 0x0, type_0, 0x0);
  makeGdt(0x0, 0xfffff, type_code_dpl0, 0x08);
  makeGdt(0x0, 0xfffff, type_data_dpl0, 0x10);
  //makeGdt(0x0, 0x00000, type_stack_dpl0, 0x18);
  //makeGdt(0x0, 0x00000, type_data_dpl0, 0x18);
  makeGdt(0x0, 0xfffff, type_code_dpl3, 0x20);
  makeGdt(0x0, 0xfffff, type_data_dpl3, 0x28);
  //makeGdt(0x0, 0x00000, type_stack_dpl3, 0x30);
  
  gdtr.limit = sizeof(GlobalDescTable) * GDTNUM-1;
  gdtr.baseH = (u_int16_t)(((u_int32_t)&globalDescTable >> 16) & 0xffff);
  gdtr.baseL = (u_int16_t)(((u_int32_t)&globalDescTable >>  0) & 0xffff);

  lgdt(&gdtr);

  selNo = 0x30;
}
