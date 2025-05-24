#ifndef _IHANDLERS_H
#define _IHANDLERS_H

#include <sys/types.h>

#define IRQ_8259A_MASTER 0x20
#define IRQ_8259A_SLAVE  0xA1
#define IRQ_TIMER   0
#define IRQ_KEY     1
#define IRQ_COM2    3
#define IRQ_COM1    4
#define IRQ_FDC     6
#define IRQ_RTC     8
#define IRQ_UHCI    11
#define IRQ_NE2000  11
//#define IRQ_UHCI    9
//#define IRQ_NE2000  11
#define IRQ_PS2     12
#define IRQ_IDE1    14
#define IRQ_IDE2    15

#define PIC_BASE    0x20

void asm_defaulthandler();
void asm_i00h();
void asm_i01h();
void asm_i02h();
void asm_i03h();
void asm_i04h();
void asm_i05h();
void asm_i06h();
void asm_i07h();
void asm_i08h();
void asm_i09h();
void asm_i0Ah();
void asm_i0Bh();
void asm_i0Ch();
void asm_i0Dh();
void asm_i0Eh();
void asm_i10h();
void asm_i11h();
void asm_i12h();
void asm_i13h();
void asm_process_switch(); // int 0x20
void asm_pictimer();  // int 0x20
void asm_i21h();
void asm_fdchandler(); // int 0x26
void asm_uhcihandler();
//void asm_i29h();
void asm_i2Bh();
void asm_syscall(); // int 0x80

void asm_interrupt_selector(int);
void interrupt_selector(int);

void defaulthandler();
void i00h();
void i01h();
void i02h();
void i03h();
void i04h();
void i05h();
void i06h();
void i07h();
void i08h();
void i09h();
void i00h();
void i0Ah();
void i0Bh();
void i0Ch();
void i0Eh();
void i10h();
void i11h();
void i12h();
void i13h();
void i20h_pictimer();
PUBLIC void i20h_do_timer(int is_usermode, u_int32_t iret1,
                          u_int32_t iret2, u_int32_t iret3,
                          u_int32_t iret4, u_int32_t iret5,
                          u_int32_t ebp);
void i21h_keyhandler();
void i26h_fdchandler();
void intr_uhcihandler();
void i2Bh_ne2000_interrupt();

void i0Dh_GPEfault(u_int16_t di, u_int16_t si, u_int16_t bp,
				   u_int32_t esp, u_int32_t ebx, u_int32_t edx,
				   u_int32_t ecx, u_int32_t eax, u_int16_t es,
				   u_int16_t ds, u_int32_t error, u_int32_t eip,
				   u_int16_t cs, u_int16_t dummy, u_int32_t eflags);

PUBLIC void i80h_syscall(int is_usermode, u_int32_t iret_eip,
                         u_int32_t iret_cs, u_int32_t iret_eflags,
                         u_int32_t iret_esp, u_int32_t iret_ss,
                         u_int32_t ebp);

PUBLIC void set_intr_bit(u_int8_t intr_num);
PUBLIC void pic_eoi(int irq);

#endif /* _IHANDLERS_H */
