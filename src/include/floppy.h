#ifndef _FLOPPY_H
#define _FLOPPY_H

#include <sodex/const.h>
#include <sys/types.h>
#include <memory.h>

#define FDC_RESULT_MAXCOUNT 1000000//0x20

#define FDC_SRA     0x3f0   // FDC status registerA (R)
#define FDC_SRB     0x3f1   // FDC status registerB (R) 
#define FDC_DOR     0x3f2   // FDC Control register (R/W)
#define FDC_MSR     0x3f4   // FDC Status register (R)
#define FDC_DSR     0x3f4   // FDC data rate select register (W)
#define FDC_DAT     0x3f5   // FDC Data (R/W)
#define FDC_DIR     0x3f7   // FDC digital input register (R)
#define FDC_CCR     0x3f7   // FDC configuration control register (W)

/* FDC MSR */
#define MSR_RQM         0x80
#define MSR_DIO         0x40
#define MSR_BUSY        0x10
#define MSR_READY       0

/* FDC CMD */
#define CMD_SPECIFY         0x03
#define CMD_RECALIBRATE     0x07
#define CMD_SENSE_INT_STS   0x08
#define CMD_SEEK            0x0f
#define CMD_READ            0x46 //MT=0,MF=1,SK=0
#define CMD_WRITE           0x45 //MT=0,MF=1,SK=0

/*
  == FDC_CMD_SUB format ==
  x x x x x HD US1 US0
  x is anyone.
  HD is head number.
  US1 and US0 are drive number of FD.

  This cmd is used as the second byte of almost all command.
*/
#define CMD_SUB 0x00 //HD=0, US1 & US0 = 0

#define IO_DELAY 0x80

#define FDC_SECTOR_SIZE 512
#define FDC_SECTORS     18
//#define FDC_SECTORS     8

PUBLIC void init_dma();
PUBLIC void init_fdc();
PUBLIC char* fdc_rowread(u_int8_t head, u_int8_t track, u_int8_t sector);
PUBLIC int fdc_read(u_int32_t logical_sector, u_int32_t num_sects, void* buf);
PUBLIC int fdc_rowwrite(char* buf, u_int8_t head, u_int8_t track, 
                        u_int8_t sector);
PUBLIC int fdc_write(u_int32_t logical_sector, u_int32_t num_sects,
                     void* buf);

#endif /* _FLOPPY_H */
