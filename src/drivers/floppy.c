/*
 *  @File floppy.c @Brief row floppy access
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/05/02  update: 2007/05/07
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <kernel.h>
#include <descriptor.h>
#include <vga.h>
#include <io.h>
#include <memory.h>
#include <floppy.h>
#include <ihandlers.h>
#include <string.h>
#include <ext3fs.h>
#include <delay.h>
#include <dma.h>

//#define DEBUG

PRIVATE void fdc_reset();
PRIVATE int  fdc_cmd(const u_int8_t *cmd, const u_int8_t length);
PRIVATE int  fdc_wait_msrStatus(u_int8_t mask, u_int8_t expected);
PRIVATE void fdc_motor_on();
PRIVATE void fdc_motor_off();
PRIVATE int  fdc_read_results();
PRIVATE int  fdc_specify();
PRIVATE int  fdc_recalibrate();
PRIVATE int  fdc_seek(u_int8_t track);
PRIVATE int  fdc_sense_interrupt();
PRIVATE u_int32_t fdc_wait_interrupt();
PRIVATE void fdc_clear_interrupt();

PRIVATE void fdc_trans_sector(u_int32_t logical, u_int8_t* head,
                              u_int8_t* track, u_int8_t* sector);


PRIVATE u_int8_t dma_databuf[DMA_DATABUF];
PRIVATE u_int32_t fdc_interrupt = 0; // for FDC interrupt

PRIVATE struct FDC_RESULTS {
  u_int8_t  gets;
  u_int8_t  req_sense;
  u_int32_t status_count;
  u_int8_t  status[10];
} fdc_results;


PUBLIC void init_fdc()
{
  set_trap_gate(0x26,&asm_fdchandler); // floppy handler
  dma_trans.addr = (u_int32_t)&dma_databuf[0];
  dma_trans.count = 512;
  enableInterrupt();
  fdc_motor_on();
  fdc_specify();
  rawdev.raw_read = fdc_read;
  rawdev.raw_write = fdc_write;
}


PUBLIC void i26h_fdchandler()
{
  fdc_interrupt++;

#ifdef DEBUG
  _kputs("[FDC] Interrupt occur!\n");
#endif


  enable_pic_interrupt(IRQ_FDC);
  out8(0x20, 0x66);
  pic_eoi(IRQ_FDC);
}

PRIVATE void fdc_reset()
{
  out8(FDC_DSR, 0x0);
  out8(FDC_CCR, 0x0);
  out8(FDC_DOR, 0x8);
  out8(FDC_DOR, 0xc);
}

PRIVATE void fdc_motor_on()
{
  out8(FDC_DOR, 0x1c);
}

PRIVATE void fdc_motor_off()
{
  out8(FDC_DOR, 0x0c);
}

PRIVATE int fdc_chk_interrupt()
{
  return fdc_interrupt;
}

PRIVATE u_int32_t fdc_wait_interrupt()
{
  int count = 0;
  while (count++ < 30000000) {
    if (fdc_chk_interrupt())
      return TRUE;
  }
  return FALSE;
}

PRIVATE void fdc_clear_interrupt()
{
  fdc_interrupt = 0;
}

PRIVATE int fdc_cmd(const u_int8_t *cmd, const u_int8_t length)
{
#ifdef DEBUG
  _kputs("[FDC] cmd busy check.\n");
#endif
  if (!fdc_wait_msrStatus(MSR_BUSY, MSR_READY)) {
    _kputs("[FDC] cmd busy check error.\n");
    return FALSE;
  }
#ifdef DEBUG
  _kputs("[FDC] cmd busy check [OK]\n");
#endif

#ifdef DEBUG
  _kputs("[FDC] cmd out and msr check.\n");
#endif
  int i;
  for (i=0; i < length; i++) {
    if (!fdc_wait_msrStatus(MSR_RQM|MSR_DIO, MSR_RQM)) {
      _kputs("[FDC] msr RQM|DIO error\n");
      return FALSE;
    }
    out8(FDC_DAT, cmd[i]);
  }
#ifdef DEBUG
  _kputs("[FDC] cmd out and msr check [OK]\n");
#endif

  return TRUE;
}

PRIVATE int fdc_wait_msrStatus(u_int8_t mask, u_int8_t expected)
{
  u_int8_t status;
  volatile u_int32_t count = 0;

  do {
    status = in8(FDC_MSR);
    count++;
  } while (((status & mask) != expected) && (count < FDC_RESULT_MAXCOUNT));
  
  if (count == FDC_RESULT_MAXCOUNT) {
    _kprintf("[FDC] msr wait error. status:%x\n",status);
    return FALSE;
  }

  return TRUE;
}   

PRIVATE int fdc_sense_interrupt()
{
  u_int8_t cmd[] = {CMD_SENSE_INT_STS};

  if (!fdc_cmd(cmd, sizeof(cmd))) {
    _kputs("[FDC] sense interrupt status cmd error\n");
    return FALSE;
  }

  if (!fdc_read_results()) {
    _kputs("[FDC] SIS result error\n");
    return FALSE;
  }

  return TRUE;
}

// FDC Read Result Phase
PRIVATE int fdc_read_results()
{
  u_int8_t* msr = &(fdc_results.status[0]);
  u_int8_t status;
  int count;

  fdc_results.status_count = 0;

#ifdef DEBUG
  _kputs("[FDC] read result RQM|DIO check.\n");
#endif
  if (!fdc_wait_msrStatus(MSR_RQM|MSR_DIO, MSR_RQM|MSR_DIO)) {
    _kputs("[FDC] read result MSR_RQM|MSR_DIO error.\n");
    return FALSE;
  }
#ifdef DEBUG
  _kputs("[FDC] read result RQM|DIO check [OK]\n");
#endif
  
#ifdef DEBUG
  _kputs("[FDC] read result check.\n");
#endif
  do {
    *msr = in8(FDC_DAT);

    msr++;
    fdc_results.status_count++;

    for (count = 0; count < FDC_RESULT_MAXCOUNT; count++) {
      status = in8(FDC_MSR);
      if (status & MSR_RQM)
        break;
    }
    if (count == FDC_RESULT_MAXCOUNT) {
      _kprintf("[FDC] result phase FDC_MSR:%x\n", status);

      _kputs("[FDC] result phase RQM wait error\n");
      break;
    }
  } while (status & MSR_DIO);
#ifdef DEBUG
  _kputs("[FDC] read result check [OK]\n");
#endif

#ifdef DEBUG
  _kputs("[FDC] Results:");
  int i;
  for (i = 0; i < fdc_results.status_count; i++) {
    _kprintb(fdc_results.status[i]);
    _kputc(' ');
  }
  _kputc('\n');
#endif

  return TRUE;
}

PRIVATE int fdc_specify()
{
  u_int8_t specify_cmd[] = {CMD_SPECIFY, 0xc1, 0x10};

  fdc_clear_interrupt();

#ifdef DEBUG
  _kputs("[FDC] Specify Cmd.\n");
#endif
  if (!fdc_cmd(specify_cmd, sizeof(specify_cmd))) {
    _kputs("[FDC] Sepcify Cmd error\n");
    return FALSE;
  }
#ifdef DEBUG
  _kputs("[FDC] Specify Cmd [OK]\n");
#endif

  return TRUE;
}

PRIVATE int fdc_recalibrate()
{
  u_int8_t cmd[] = {CMD_RECALIBRATE, CMD_SUB};

  fdc_clear_interrupt();

#ifdef DEBUG  
  _kputs("[FDC] Recalibrate Cmd.\n");
#endif
  if (!fdc_cmd(cmd, sizeof(cmd))) {
    _kputs("[FDC] Recalibrate Cmd error\n");
    return FALSE;
  }
#ifdef DEBUG
  _kputs("[FDC] Recalibrate Cmd [OK]\n");
#endif


  if (!fdc_wait_interrupt()) {
    _kputs("[FDC] wait interrupt error\n");
    return FALSE;
  }

  /* get result */
  if (!fdc_sense_interrupt()) {
    _kputs("[FDC] SIS error\n");
    return FALSE;
  }

  if (!fdc_wait_msrStatus(MSR_BUSY, MSR_READY)) {
    _kputs("[FDC] Recalibrate  wait fail\n");
    return FALSE;
  }

  return TRUE;
}

PRIVATE int fdc_seek(u_int8_t track)
{
  u_int8_t cmd[] = {
    CMD_SEEK,       // 0x0f
    0,              
    track
  };

  fdc_clear_interrupt();

#ifdef DEBUG
  _kputs("[FDC] seek cmd check.\n");
#endif
  if (!fdc_cmd(cmd, sizeof(cmd))) {
    _kputs("[FDC] seek cmd error\n");
    return FALSE;
  }
#ifdef DEBUG
  _kputs("[FDC] seek cmd check [OK]\n");
#endif


  if (!fdc_wait_interrupt()) {
    _kputs("[FDC][SEEK] wait interrupt error\n");
    return FALSE;
  }
  
  /* get result */
  if (!fdc_sense_interrupt()) {
    _kputs("[FDC][SEEK] SIS error\n");
    return FALSE;
  }

  return TRUE;
}

PUBLIC char* fdc_rowread(u_int8_t head, u_int8_t track, u_int8_t sector)
{
  init_dma_r();

#ifdef DEBUG
  _kprintf("start recalibrate\n");
#endif
  if (!fdc_recalibrate()) {
    _kputs("[FDC][READ] recalibrate error 1\n");
    //return NULL;
    if (!fdc_recalibrate()) {
      _kputs("[FDC][READ] recalibrate error 2\n");
      return NULL;
    }
  }
#ifdef DEBUG
  _kprintf("end of recalibrate\n");
#endif

  if (!fdc_seek(track)) {
    _kputs("[FDC][READ] seek error\n");
    return NULL;
  }
#ifdef DEBUG
  _kprintf("end of seek\n");
#endif

  u_int8_t cmd[] = {
    CMD_READ,
    head << 2,      // head
    track,          // track
    head,           // head
    sector,         // sector
    0x2,            // sector length (0x2 = 512byte)
    0x12,           // end of track (EOT)
    0x1b,           // dummy GSR
    0               // dummy STP
  };

  fdc_clear_interrupt();

  if (!fdc_cmd(cmd, sizeof(cmd))) {
    _kputs("[FDC][READ] cmd error\n");
    return NULL;
  }
#ifdef DEBUG
  _kprintf("end of cmd\n");  
#endif
  
  if (!fdc_wait_interrupt()) {
    _kputs("[FDC][READ] wait interrupt error\n");
    return NULL;
  }
  //_kprintf("end of wait\n");  

  if (!fdc_read_results()) {
    _kputs("[FDC][READ] read result error\n");
    return NULL;
  }
#ifdef DEBUG
  _kprintf("end of result\n");
#endif

  // write the binary which we get from DMA
  /*
  int i;
  _kputs("[FDC] READ DATA:");
  for (i = 0; i < 16; i++) {
    _kprintb(dma_databuf[i]);
    _kputc(' ');
  }
  _kputc('\n');
  */

  return dma_databuf;
}

PUBLIC int fdc_read(u_int32_t logical_sector, u_int32_t num_sects,
                    void* buf)
{
  disable_pic_interrupt(IRQ_TIMER);
  char* tempbuf;
  u_int8_t head, track, sector;

  dma_start();
  //delay(DELAY_TIMES);
  //init_dma_r();
  //fdc_motor_on();
  //delay(DELAY_TIMES);

  int i;
  for (i = 0; i < num_sects; i++) {
    int logical = logical_sector+i;
    //track = logical/(FDC_SECTORS*2);
    //head = (logical%(FDC_SECTORS*2))/FDC_SECTORS;
    //sector = (logical%(FDC_SECTORS*2))%FDC_SECTORS+1;
    fdc_trans_sector(logical_sector+i, &head, &track, &sector);
    //_kprintf("logic:%x head:%x track:%x sector:%x\n", logical_sector+i, head, track, sector);

    tempbuf = fdc_rowread(head, track, sector);

	  //return TRUE;
    if (tempbuf == NULL) {
      _kprintf("fdc_logical_read error: logical sect is %x\n",
                logical_sector+i);
      for(;;);
	  return FALSE;
    }
	//_kprintf("buf:%x tempbuf:%x i:%x\n", buf+i*FDC_SECTOR_SIZE, tempbuf, i);
    /*
    if (i == 9) {
      char *p = buf + i*FDC_SECTOR_SIZE;
      int j;
      for (j = 0; j<16; j++) {
        _kprintf("%x ", *(p+j));
      }
      _kputc('\n');
    }
    */
    memcpy((char*)buf+i*FDC_SECTOR_SIZE, tempbuf, FDC_SECTOR_SIZE);
    //_kprintf("end of memcpy\n");
  }

  //fdc_motor_off();
  //delay(DELAY_TIMES);
  dma_stop();
  //delay(DELAY_TIMES);
  enable_pic_interrupt(IRQ_TIMER);
}

PRIVATE void fdc_trans_sector(u_int32_t logical, u_int8_t* head,
                              u_int8_t* track, u_int8_t* sector)
{
  *track = logical/(FDC_SECTORS*2);
  *head = (logical%(FDC_SECTORS*2))/FDC_SECTORS;
  //*head = (logical/FDC_SECTORS)%2;
  *sector = (logical%(FDC_SECTORS*2))%FDC_SECTORS+1;
  //*sector = (logical%FDC_SECTORS) + 1;
}

PUBLIC int fdc_rowwrite(char* buf, u_int8_t head,
                        u_int8_t track, u_int8_t sector)
{
  init_dma_w();

  if (!fdc_recalibrate()) {
    _kputs("[FDC][WRITE] recalibrate error\n");
    return FALSE;
  }

  if (!fdc_seek(track)) {
    _kputs("[FDC][WRITE] seek error\n");
    return FALSE;
  }

  memcpy(dma_databuf, buf, FDC_SECTOR_SIZE);

  u_int8_t cmd[] = {
    CMD_WRITE,
    head << 2,      // head
    track,          // track
    head,           // head
    sector,         // sector
    0x2,            // sector length (0x2 = 512byte)
    0x12,           // end of track (EOT)
    0x1b,           // dummy GSR
    0               // dummy STP
  };

  fdc_clear_interrupt();

  if (!fdc_cmd(cmd, sizeof(cmd))) {
    _kputs("[FDC][WRITE] cmd error\n");
    return FALSE;
  }
  
  if (!fdc_wait_interrupt()) {
    _kputs("[FDC][WRITE] wait interrupt error\n");
    return FALSE;
  }

  if (!fdc_read_results()) {
    _kputs("[FDC][WRITE] read result error\n");
    return FALSE;
  }

  return TRUE;
}

PUBLIC int fdc_write(u_int32_t logical_sector, u_int32_t num_sects,
                      void* buf)
{
  u_int8_t head, track, sector;

  dma_start();
  //delay(DELAY_TIMES);
  //init_dma_w();
  //delay(DELAY_TIMES);
  //fdc_motor_on();
  //delay(DELAY_TIMES);


  int i, err;
  for (i = 0; i < num_sects; i++) {
    fdc_trans_sector(logical_sector+i, &head, &track, &sector);
    err = fdc_rowwrite((char*)buf + i*FDC_SECTOR_SIZE, head, track, sector);
    if (err == FALSE) {
      _kprintf("%s:error. logical sect is %x\n", __func__,
                logical_sector+i);
      return FALSE;
    }
  }

  dma_stop();
  //fdc_motor_off();
}
