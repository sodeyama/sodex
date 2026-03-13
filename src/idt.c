/*
 *  @File        idt.c
 *  @Brief       set Interrupt Descriptor Table
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/23  update: 2007/05/15  
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <kernel.h>
#include <vga.h>
#include <key.h>
#include <descriptor.h>
#include <ihandlers.h>
#include <io.h>
#include <floppy.h>
#include <ne2000.h>

/* IDT gate types */
#define TYPE_INTR_GATE 0xEE  /* DPL=3, 32-bit interrupt gate */
#define TYPE_TRAP_GATE 0xEF  /* DPL=3, 32-bit trap gate */
#define TYPE_TASK_GATE 0x85  /* DPL=0, task gate */

#define IDTNUM  256

/* 8259A PIC port addresses */
#define PIC1_CMD   0x20  /* Master PIC command port */
#define PIC1_DATA  0x21  /* Master PIC data/mask port */
#define PIC2_CMD   0xA0  /* Slave PIC command port */
#define PIC2_DATA  0xA1  /* Slave PIC data/mask port */

/* PIC commands */
#define PIC_EOI_BASE   0x60  /* Specific EOI command base */
#define PIC_EOI        0x20  /* Non-specific EOI command */
#define PIC_ICW1_INIT  0x11  /* ICW1: edge triggered, cascade, ICW4 needed */

/* PIC ICW2: interrupt vector base */
#define PIC_ICW2_MASTER  0x20  /* Master PIC vector offset (IRQ0 = 0x20) */
#define PIC_ICW2_SLAVE   0x28  /* Slave PIC vector offset (IRQ8 = 0x28) */

/* PIC ICW3 */
#define PIC_ICW3_MASTER  0x04  /* Slave on IRQ2 */
#define PIC_ICW3_SLAVE   0x02  /* Cascade identity */

/* PIC ICW4 */
#define PIC_ICW4_8086    0x01  /* 8086/88 mode */

/* Keyboard controller */
#define KB_DATA_PORT     0x60  /* PS/2 keyboard data port */

/* IRQ for cascade (slave PIC on master IRQ2) */
#define IRQ_CASCADE      2

PRIVATE InterruptDescTable interruptDescTable[IDTNUM];
PRIVATE IDTR idtr;
PRIVATE u_int16_t count = 0;
PRIVATE char timercount[8] = {0x7C,0x2F,0x2D,0x5C,0x7C,0x2F,0x2D,0x5C};

PRIVATE void makeGate(InterruptDescTable* idt, u_int16_t selector,
                     u_int32_t offset, u_int8_t copy, u_int8_t type);


PRIVATE void makeGate(InterruptDescTable* idt, u_int16_t selector,
                     u_int32_t offset, u_int8_t copy, u_int8_t type)
{
  idt->offsetL   = offset & 0xffff;
  idt->selector  = selector;
  idt->copy      = copy;
  idt->type      = type;
  idt->offsetH   = (offset >> 16) & 0xffff;
}

PUBLIC void set_intr_gate(idtno_t idtno, void* offset)
{
  makeGate(&interruptDescTable[idtno], __KERNEL_CS,
		   (u_int32_t)offset, 0, TYPE_INTR_GATE);
}

PUBLIC void set_trap_gate(idtno_t idtno, void* offset)
{
  makeGate(&interruptDescTable[idtno], __KERNEL_CS, 
		   (u_int32_t)offset, 0, TYPE_TRAP_GATE);
}

PUBLIC void set_task_gate(idtno_t idtno, void* offset)
{
  makeGate(&interruptDescTable[idtno], __KERNEL_CS, 
		   (u_int32_t)offset, 0, TYPE_TASK_GATE);
}

PUBLIC void init_setupidthandlers()
{
  disableInterrupt();

  int i;
  for (i=0; i<256; i++)
    set_trap_gate(i, &asm_defaulthandler);

  /* hardware interrupt */
  set_intr_gate(0x00,&asm_i00h);
  set_intr_gate(0x01,&asm_i01h);
  set_intr_gate(0x02,&asm_i02h);
  set_intr_gate(0x03,&asm_i03h);
  set_trap_gate(0x04,&asm_i04h);
  set_trap_gate(0x05,&asm_i05h);
  set_trap_gate(0x06,&asm_i06h);
  set_trap_gate(0x07,&asm_i07h);
  set_trap_gate(0x08,&asm_i08h);
  set_trap_gate(0x09,&asm_i09h);
  set_trap_gate(0x0A,&asm_i0Ah);
  set_trap_gate(0x0B,&asm_i0Bh);
  set_trap_gate(0x0C,&asm_i0Ch);
  set_trap_gate(0x0D,&asm_i0Dh);
  set_intr_gate(0x0E,&asm_i0Eh);
  set_trap_gate(0x0F,&asm_defaulthandler);
  set_trap_gate(0x10,&asm_i10h);
  set_trap_gate(0x11,&asm_i11h);
  set_trap_gate(0x12,&asm_i12h);
  set_trap_gate(0x13,&asm_i13h);


  /* pic interrupt */
  set_trap_gate(0x20,&asm_pictimer); // timer handler
  set_trap_gate(0x21,&asm_i21h); // key handler
  set_trap_gate(NE2K_QEMU_IRQ, &asm_i2Bh);
}

PUBLIC void i00h()
{
  _kputs("Int0x00 div error!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i01h()
{
  _kputs("Int0x01 intel reserved\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i02h()
{
  _kputs("Int0x02 NMI interrupt\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i03h()
{
  _kputs("Int0x03 break point (int 3)\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i04h()
{
  _kputs("Int0x04 OverFlow !\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i05h()
{
  _kputs("Int0x05 boundary error!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i06h()
{
  _kputs("Int0x06 invalid opecode!\n");

  disableInterrupt();
  for(;;);
}

PUBLIC void i07h()
{
  _kputs("Int0x07 can't use device!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i08h()
{
  _kputs("Int0x08 Double Fault!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i09h()
{
  _kputs("Int0x09 FPU reserved\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i0Ah()
{
  _kputs("Int0x0A invalid tss!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i0Bh()
{
  _kputs("Int0x0B non exist segment!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i0Ch()
{
  _kputs("Int0xc stack segment fault!\n");
  disableInterrupt();
  for(;;);
}

/*
PUBLIC void i0Dh(u_int16_t di, u_int16_t si, u_int16_t bp,
		  u_int32_t esp, u_int32_t ebx, u_int32_t edx,
		  u_int32_t ecx, u_int32_t eax, u_int16_t es,
		  u_int16_t ds, u_int32_t error, u_int32_t eip,
		  u_int16_t cs, u_int16_t dummy, u_int32_t eflags)
{
  _kprintf("int0x0D General Protection Exception! %x\n", error);
  _kprintf("cs:%x ds:%x eip:%x esp:%x eflags:%x\n",
		   cs, ds, eip, esp, eflags);
  disableInterrupt();
  for(;;);
}
*/
PUBLIC void i0Dh(u_int32_t ebp)
{
  /*
  _kprintf("1:%x 2:%x 3:%x 4:%x 5:%x 6:%x\n", *(u_int32_t*)(ebp),
           *(u_int32_t*)(ebp+4), *(u_int32_t*)(ebp+8),
           *(u_int32_t*)(ebp+12), *(u_int32_t*)(ebp+16),
           *(u_int32_t*)(ebp+18));
  */
  int error = *(u_int32_t*)(ebp);
  u_int32_t eip = *(u_int32_t*)(ebp+4);
  u_int32_t cs = *(u_int32_t*)(ebp+8);
  u_int32_t eflags = *(u_int32_t*)(ebp+12);
  _kprintf("\nint0x0D General Protection Exception!\n");
  _kprintf("error:%x eip:%x cs:%x eflags:%x\n",
           error, eip, cs, eflags);
  disableInterrupt();
  for(;;);
}

PUBLIC void i0Eh()
{
  u_int32_t cr2, ebp_val;
  asm volatile("movl %%cr2, %0" : "=r"(cr2));
  asm volatile("movl %%ebp, %0" : "=r"(ebp_val));
  /* stack: ebp -> old_ebp -> ret_addr -> pusha(32) -> error_code -> eip -> cs */
  u_int32_t error = *(u_int32_t*)(ebp_val + 40);
  u_int32_t eip   = *(u_int32_t*)(ebp_val + 44);
  u_int32_t cs    = *(u_int32_t*)(ebp_val + 48);
  _kprintf("\nPageFault CR2=%x err=%x eip=%x cs=%x\n", cr2, error, eip, cs);
  disableInterrupt();
  for(;;);
}

PUBLIC void i10h()
{
  _kputs("Int0x10 x87 FPU error!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i11h()
{
  _kputs("Int0x11 alignment check\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i12h()
{
  _kputs("Int0x12 machine check\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i13h()
{
  _kputs("Int0x13 SMID error!\n");
  disableInterrupt();
  for(;;);
}

PUBLIC void i20h_pictimer()
{
  /*
  _pos_putc(0, 6, timercount[count%8]);
  if (count == 255)
    count = 0;
  else
    count++;
  */
  pic_eoi(IRQ_TIMER);
}

PUBLIC void i21h_keyhandler()
{
  static int shift_flag = FALSE;
  u_int8_t a;
  char c;

  a = in8(KB_DATA_PORT);
  if (a <= 127) {
    c = get_keymap(a);
    if (c == KEY_ENTER) {
      _kputc('\n');
      set_stdin(a);
    } else if (c == KEY_BACK) {
      _kputc(KEY_BACK);
      set_stdin(a);
    } else if (c == KEY_SHIFT) {
      shift_flag = TRUE;
    } else {
      if (shift_flag == TRUE) {
        c = get_shiftkeymap(a);
        _kputc(c);
      } else {
        _kputc(c);
      }
      set_stdin(a);
    } 
  }
  pic_eoi(IRQ_KEY);
}

PUBLIC void interrupt_selector(int selector)
{
  _kprintf("selector:%x\n", selector);
}

PUBLIC void defaulthandler()
{
  /* Send EOI to both PICs in case this is a hardware interrupt */
  out8(PIC2_CMD, PIC_EOI);
  out8(PIC1_CMD, PIC_EOI);
}

PRIVATE void init_pic()
{
  // just in case
  disableInterrupt();

  //out8(0x21, 0xFB);
  //out8(0xA1, 0xFF);

  out8(PIC1_DATA, 0xFF);
  out8(PIC2_DATA, 0xFF);

  // 8259A master setting
  out8(PIC1_CMD, PIC_ICW1_INIT);
  out8(PIC1_DATA, PIC_ICW2_MASTER);
  out8(PIC1_DATA, PIC_ICW3_MASTER);
  out8(PIC1_DATA, PIC_ICW4_8086);

  // 8259A slave setting
  out8(PIC2_CMD, PIC_ICW1_INIT);
  out8(PIC2_DATA, PIC_ICW2_SLAVE);
  out8(PIC2_DATA, PIC_ICW3_SLAVE);
  out8(PIC2_DATA, PIC_ICW4_8086);

  // mask outport at not timer(0) & key(1) & fdc(6) & serial(4)
  set_intr_bit(IRQ_TIMER);
  set_intr_bit(IRQ_KEY);
  //set_intr_bit(IRQ_FDC);
  //set_intr_bit(IRQ_NE2000);
}

PUBLIC void set_intr_bit(u_int8_t intr_num)
{
  disableInterrupt();
  u_int8_t low = in8(PIC1_DATA);
  u_int8_t high = in8(PIC2_DATA);
  if (intr_num < 8) {
    low = (low & (~(1<<intr_num)));
  } else if (intr_num < 16) {
    high = (high & (~(1<<(intr_num-8))));
  } else {
    _kputs("set_intr_bit error. The intr_num must be under 16.\n");
    return;
  }
  out8(PIC1_DATA, low);
  out8(PIC2_DATA, high);
  enableInterrupt();
}

PUBLIC void enable_pic_interrupt(u_int8_t intr_num)
{
  if (is_enableInterrupt()) {
    disableInterrupt();
    u_int8_t bit_low = get_interrupt_bit_low();
    u_int8_t bit_high = get_interrupt_bit_high();
    bit_low &= 0xff & ~(1 << intr_num);
    out8(PIC1_DATA, bit_low);
    out8(PIC2_DATA, bit_high);
    enableInterrupt();
  } else {
    u_int8_t bit_low = get_interrupt_bit_low();
    u_int8_t bit_high = get_interrupt_bit_high();
    bit_low &= 0xff & ~(1 << intr_num);
    out8(PIC1_DATA, bit_low);
    out8(PIC2_DATA, bit_high);
  }
}

PUBLIC void disable_pic_interrupt(u_int8_t intr_num)
{
  if (is_enableInterrupt()) {
    disableInterrupt();
    u_int8_t bit_low = get_interrupt_bit_low();
    u_int8_t bit_high = get_interrupt_bit_high();
    bit_low |= 0xff & (1 << intr_num);
    out8(PIC1_DATA, bit_low);
    out8(PIC2_DATA, bit_high);
    enableInterrupt();
  } else {
    u_int8_t bit_low = get_interrupt_bit_low();
    u_int8_t bit_high = get_interrupt_bit_high();
    bit_low |= 0xff & (1 << intr_num);
    out8(PIC1_DATA, bit_low);
    out8(PIC2_DATA, bit_high);
  }
}

PUBLIC u_int8_t get_interrupt_bit_low()
{
  u_int8_t bit = in8(PIC1_DATA);
  return bit;
}

PUBLIC u_int8_t get_interrupt_bit_high()
{
  u_int8_t bit = in8(PIC2_DATA);
  return bit;
}

PUBLIC void init_setupidt()
{
  idtr.limit = sizeof(InterruptDescTable)*IDTNUM - 1;
  idtr.baseH = (u_int16_t)(((u_int32_t)&interruptDescTable >> 16) & 0xffff);
  idtr.baseL = (u_int16_t)(((u_int32_t)&interruptDescTable >>  0) & 0xffff);

  init_pic();

  lidt(&idtr);

  enableInterrupt();
}

PUBLIC int wait_interrupt(int intno)
{
}

PUBLIC void pic_eoi(int irq)
{
  if (irq < 8) {
    out8(PIC1_CMD, PIC_EOI_BASE + irq);
  } else if (irq < 16) {
    disableInterrupt();
    out8(PIC2_CMD, PIC_EOI_BASE + irq - 8);
    out8(PIC1_CMD, PIC_EOI_BASE + IRQ_CASCADE);
    enableInterrupt();
  }
}
