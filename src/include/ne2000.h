#ifndef _NE2000_H
#define _NE2000_H

#include <sys/types.h>
#include <sodex/const.h>

#define NE2K_QEMU_IRQ       0x2B
#define NE2K_QEMU_BASEADDR  0xC100

#define DATA_PORT_OFFSET    0x10
#define RESET_PORT_OFFSET   0x18

// page0 registers
#define I_CR        0x0
#define I_CLDA0     0x1
#define I_CLDA1     0x2
#define I_BNRY      0x3
#define I_TSR       0x4
#define I_NCR       0x5
#define I_FIFO      0x6
#define I_ISR       0x7
#define I_CRDA0     0x8
#define I_CRDA1     0x9
#define I_RSR       0xC
#define I_NCTR0     0xD
#define I_NCTR1     0xE
#define I_NCTR2     0xF

#define O_CR        0x0
#define O_PSTART    0x1
#define O_PSTOP     0x2
#define O_BNRY      0x3
#define O_TPSR      0x4
#define O_TBCR0     0x5
#define O_TBCR1     0x6
#define O_ISR       0x7
#define O_RSAR0     0x8
#define O_RSAR1     0x9
#define O_RBCR0     0xA
#define O_RBCR1     0xB
#define O_RCR       0xC
#define O_TCR       0xD
#define O_DCR       0xE
#define O_IMR       0xF // write mode

// page1 registers
#define IO_PAR0      0x1
#define IO_PAR1      0x2
#define IO_PAR2      0x3
#define IO_PAR3      0x4
#define IO_PAR4      0x5
#define IO_PAR5      0x6
#define IO_CURR      0x7
#define IO_MAR0      0x8
#define IO_MAR1      0x9
#define IO_MAR2      0xA
#define IO_MAR3      0xB
#define IO_MAR4      0xC
#define IO_MAR5      0xD
#define IO_MAR6      0xE
#define IO_MAR7      0xF

// page2 registers
#define I_PSTART    0x1
#define I_PSTOP     0x2
#define I_RNPP      0x3  // remote next packet pointer
#define I_TPSR      0x4
#define I_ADDRCNT_H 0x5 // address counter high
#define I_ADDRCNT_L 0x6 // address counter low
#define I_RCR       0xC
#define I_TCR       0xD
#define I_DCR       0xE
#define I_IMR       0xF // read mode

#define O_CLDA0     0x1
#define O_CLDA1     0x2
#define O_RNPP      0x3
#define O_LNPP      0x5 // local next packet pointer
#define O_ADDRCNT_H 0x6 // address counter high
#define O_ADDRCNT_L 0x7 // address counter low

// Command Register
#define CR_STP      (1<<0)
#define CR_STA      (1<<1)
#define CR_TXP      (1<<2)
#define CR_RD_BAN   0
#define CR_RD_READ  (1<<3)
#define CR_RD_WRITE (1<<4)
#define CR_RD_SEND  ((1<<3)|(1<<4))
#define CR_RD_STOP  (1<<5)
#define CR_PAGE0    0
#define CR_PAGE1    (1<<6)
#define CR_PAGE2    (1<<7)

// Data Configuration Register
#define DCR_WTS     (1<<0)
#define DCR_BOS     (1<<1)
#define DCR_LAS     (1<<2)
#define DCR_LS      (1<<3)
#define DCR_AR      (1<<4)
#define DCR_FT_2B   0
#define DCR_FT_4B   (1<<5)
#define DCR_FT_8B   (1<<6)
#define DCR_FT_12B  ((1<<5)|(1<<6))

// Transfer Configuration Register
#define TCR_CRC         (1<<0)
#define TCR_LB_NORMAL   0
#define TCR_LB_NIC      (1<<1)
#define TCR_LB_ENDEC    (1<<2)
#define TCR_LB_EXTERNAL ((1<<1)|(1<<2))
#define TCR_ATD         (1<<3)
#define TCR_OFSP        (1<<4)

// Transfer Status Register
#define TSR_PTX     (1<<0)
#define TSR_COL     (1<<2)
#define TSR_ABT     (1<<3)
#define TSR_CRS     (1<<4)
#define TSR_FU      (1<<5)
#define TSR_CHD     (1<<6)
#define TSR_OWC     (1<<7)

// Receive Congiguration Register
#define RCR_SEP     (1<<0)
#define RCR_AR      (1<<1)
#define RCR_AB      (1<<2)
#define RCR_AM      (1<<3)
#define RCR_PR0     (1<<4)
#define RCR_MON     (1<<5)

// Interrupt Status Register
#define ISR_PRX     (1<<0)
#define ISR_PTX     (1<<1)
#define ISR_RXE     (1<<2)
#define ISR_TXE     (1<<3)
#define ISR_OVW     (1<<4)
#define ISR_CNT     (1<<5)
#define ISR_RDC     (1<<6)
#define ISR_RST     (1<<7)

// Receive Status Register
#define RSR_PRX     (1<<0)
#define RSR_CRC     (1<<1)
#define RSR_PAE     (1<<2)
#define RSR_FO      (1<<3)
#define RSR_MPA     (1<<4)
#define RSR_PHY     (1<<5)
#define RSR_DIS     (1<<6)
#define RSR_DFR     (1<<7)

// Interrupt Mask Reigster
#define IMR_PRXE    (1<<0)
#define IMR_PTXE    (1<<1)
#define IMR_RXEE    (1<<2)
#define IMR_TXEE    (1<<3)
#define IMR_OVWE    (1<<4)
#define IMR_CNTE    (1<<5)
#define IMR_RDCE    (1<<6)
#define IMR_ALL     127

// for initilize ring buffer
#define SEND_ADDR   0x40
#define PSTART_ADDR 0x46
#define PSTOP_ADDR  0x80
#define BNRY_ADDR   0x46
#define CURR_ADDR   0x46


PUBLIC void init_ne2000();
PUBLIC int ne2000_send(void* buf, u_int16_t len);
PUBLIC int ne2000_receive();
//PUBLIC void i2Bh_ne2000_interrupt();

#endif
