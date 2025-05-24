#include <dma.h>
#include <delay.h>


PUBLIC void init_dma()
{
  // DMAC reset
  out8(DMA_MSR_CLR_PRI, 0x00);
  out8(DMA_MSR_CLR_SEC, 0x00);

  out8(DMA_CMD_PRI, 0x00);
  out8(DMA_CMD_SEC, 0x00);
 
  // DMAC mode register setting
  out8(DMA_MOD_PRI, 0xc0);
  out8(DMA_MOD_SEC, 0x46);

  out8(DMA_SGL_MSK_PRI, 0x00);
}

PUBLIC void dma_start()
{
  out8(DMA_SGL_MSK_SEC, 0x02);
}

PUBLIC void dma_stop()
{
  out8(DMA_SGL_MSK_SEC, 0x06);
}

PUBLIC void init_dma_r()
{
  delay(DMA_DELAY_TIMES);
  dma_stop();

  out8(DMA_MSR_CLR_SEC, 0x00);
  out8(DMA_CLR_FLP_SEC, 0);
  out8(DMA_MOD_SEC, 0x46);  // I/O >> memory


  out8(DMA_ADD_SEC, dma_trans.addr >> 0);
  out8(DMA_ADD_SEC, dma_trans.addr >> 8);
  out8(DMA_TOP, dma_trans.addr >> 16);
  out8(DMA_CNT_SEC, dma_trans.count >> 0);
  out8(DMA_CNT_SEC, dma_trans.count >> 8);
  dma_start();
}

PUBLIC void init_dma_w()
{
  dma_stop();
  out8(DMA_MSR_CLR_SEC, 0x00);
  out8(DMA_CLR_FLP_SEC, 0);
  out8(DMA_MOD_SEC, 0x4a);  // memory >> I/O

  out8(DMA_ADD_SEC, dma_trans.addr >> 0);
  out8(DMA_ADD_SEC, dma_trans.addr >> 8);
  out8(DMA_TOP, dma_trans.addr >> 16);
  out8(DMA_CNT_SEC, dma_trans.count >> 0);
  out8(DMA_CNT_SEC, dma_trans.count >> 8);
  dma_start();
}
