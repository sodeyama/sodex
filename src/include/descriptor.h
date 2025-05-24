/*
 * This file is for following descriptor table.
 *   GDT - Global Descriptor Table
 *   LDT - Local Descriptor Table
 *   IDT - Interrupt Descriptor Table
 *     for 3 gates ... task gate, interrupt gate, trap gate
 */

#ifndef _DESCRIPTOR_H
#define _DESCRIPTOR_H

#include <sodex/const.h>
#include <sys/types.h>

#define DPL0    0
#define DPL1    1
#define DPL2    2
#define DPL3    3

#define GDTNUM  128


/* for GDT */
typedef struct _GlobalDescTable {
  u_int16_t limitL;
  u_int16_t segBaseL;
  u_int8_t  segBaseM;
  u_int8_t  typeL;
  u_int8_t  typeH_limitH;
  u_int8_t  segBaseH;
} GlobalDescTable;

typedef struct _GDTR {
  u_int16_t limit;
  u_int16_t baseL;
  u_int16_t baseH;
} GDTR;

PUBLIC void lgdt(GDTR* pgdtr);
PUBLIC void init_setupgdt();
PUBLIC u_int16_t allocSel();
PUBLIC GlobalDescTable* getGdt(u_int16_t selector);


/* for IDT */
typedef struct _InterruptDescTable {
  u_int16_t offsetL;
  u_int16_t selector;
  u_int8_t  copy;
  u_int8_t  type;
  u_int16_t offsetH;
} InterruptDescTable;

typedef struct _IDTR {
  u_int16_t limit;
  u_int16_t baseL;
  u_int16_t baseH;
} IDTR;


PUBLIC void lidt(IDTR* pidtr);
PUBLIC void init_setupidt();
PUBLIC void init_setupidthandlers();
PUBLIC void makeGdt(u_int32_t segBase, u_int32_t limit,
			 u_int16_t type, u_int16_t selector);

PUBLIC void enable_pic_interrupt(u_int8_t intr_num);
PUBLIC void disable_pic_interrupt(u_int8_t intr_num);

/* to set the interrupt gate */
PUBLIC void set_intr_gate(idtno_t idtno, void* offset);
PUBLIC void set_trap_gate(idtno_t idtno, void* offset);
PUBLIC void set_task_gate(idtno_t idtno, void* offset);

PUBLIC int wait_interrupt(int intno);

#endif /* descriptor.h */
