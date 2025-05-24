#ifndef _DMA_H
#define _DMA_H

#include <sodex/const.h>
#include <sys/types.h>

#define DMA_DATABUF 1024
#define FDC_RESULT_MAXCOUNT 1000000//0x20

#define DMA_ADD_SEC 0x04    //channel2 low address
#define DMA_CNT_SEC 0x05    //channel2 count address
#define DMA_TOP 0x81        //channel2 high address

#define DMA_CMD_PRI 0xD0
#define DMA_CMD_SEC 0x08
#define DMA_REQ_PRI 0xD2
#define DMA_REQ_SEC 0x09
#define DMA_SGL_MSK_PRI 0xD4
#define DMA_SGL_MSK_SEC 0x0A
#define DMA_MOD_PRI 0xD6
#define DMA_MOD_SEC 0x0B
#define DMA_CLR_FLP_PRI 0x0C
#define DMA_CLR_FLP_SEC 0xD8
#define DMA_MSR_CLR_PRI 0xDA
#define DMA_MSR_CLR_SEC 0x0D
#define DMA_CLR_MSK_PRI 0xDC
#define DMA_CLR_MSK_SEC 0x0E
#define DMA_ALL_MSK_PRI 0xDE
#define DMA_ALL_MSK_SEC 0x0F

#define DMA_DELAY_TIMES 10000

PUBLIC void dma_start();
PUBLIC void dma_stop();
PUBLIC void init_dma_r();
PUBLIC void init_dma_w();

PUBLIC struct _dma_trans {
  u_int32_t count;
  u_int32_t addr;
} dma_trans;

#endif /* _DMA_H */
