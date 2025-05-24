#include <io.h>
#include <ne2000.h>
#include <pit8254.h>
#include <descriptor.h>
#include <vga.h>
#include <nic.h>
#include <uip.h>

#define WAIT_MAX_COUNT 100000

int io_base;

PRIVATE u_int16_t get_baseaddr();
PRIVATE void set_macaddr();
//PRIVATE void nic_enable_ctl(int flag);
PRIVATE void nic_sendenable_ctl(int flag);
PRIVATE void nic_recvenable_ctl(int flag);
PRIVATE void nic_enable_interrupt(int flag);
PRIVATE void read_remote_dma(u_int16_t addr, void* buf, u_int16_t len);
PRIVATE void write_remote_dma(u_int16_t addr, void* buf, u_int16_t len);
PRIVATE void init_ringbuf();
PRIVATE void init_sendreceive_config();
PRIVATE void init_interrupt();

PUBLIC void init_ne2000()
{
  io_base = get_baseaddr();
  out8(io_base+RESET_PORT_OFFSET, 0);
  kwait(10);
  out8(io_base+O_CR, CR_STP|CR_RD_STOP);
  out8(io_base+O_IMR, 0);
  out8(io_base+O_ISR, 0xff);
  out8(io_base+O_RBCR0, 0);
  out8(io_base+O_RBCR1, 0);
  out8(io_base+O_DCR, DCR_FT_8B|DCR_LS|DCR_WTS);
  nic_sendenable_ctl(TRUE);
  nic_recvenable_ctl(TRUE);
  init_ringbuf();
  init_sendreceive_config();

  // At once, we don't get the mac address from eeprom
  // we set the pseudo mac address which we decide.
  //  pseudo mac address is "1.1.1.1.1.1"
  u_int8_t buff[6];
  buff[0] = 1;
  buff[1] = 1;
  buff[2] = 1;
  buff[3] = 1;
  buff[4] = 1;
  buff[5] = 1;
  set_macaddr(buff);


  out8(io_base+O_TPSR, SEND_ADDR);
  //nic_enable_ctl(TRUE);
  out8(io_base+O_CR, CR_RD_STOP); 

  nic_enable_interrupt(TRUE);
  //u_int8_t cr_status = in8(io_base+I_CR);
  //out8(io_base + O_CR, cr_status|CR_PAGE0);
  //out8(io_base + O_IMR, IMR_ALL)

  //set_trap_gate(NE2K_QEMU_IRQ, &i2Bh_ne2000_interrupt);
}

PUBLIC void i2Bh_ne2000_interrupt()
{
  _kprintf("ne2000 int\n");
  u_int8_t status = in8(io_base+I_ISR);
  if (status & ISR_PRX) {
    _kprintf("ISR_PRX\n");
  } else if (status & ISR_PTX) {
    _kprintf("ISR_PTX\n");
  } else if (status & ISR_OVW) {
    _kprintf("ISR_OVW\n");
  } else {
    _kprintf("other\n");
    // error
  }
  pic_eoi();
}

PUBLIC int ne2000_send(void* buf, u_int16_t len)
{
  u_int8_t status = in8(io_base+I_CR);
  if (status & CR_TXP)
    return -1;
  
  disableInterrupt();
  write_remote_dma(SEND_ADDR, buf, len);
  out8(io_base+O_CR, CR_RD_SEND);
  out8(io_base+O_TPSR, SEND_ADDR);
  out8(io_base+O_TBCR0, len&0xff);
  out8(io_base+O_TBCR1, (len>>8));
  out8(io_base+O_CR, CR_STA|CR_TXP|CR_RD_SEND);
  enableInterrupt();
  out8(io_base+O_CR, 0x26);
  
  //out8(io_base+O_CR, status|CR_PAGE2);
  //u_int8_t st = in8(io_base+I_TPSR);
  //_kprintf("st:%x\n", st);
  //out8(io_base+O_CR, status|CR_PAGE0);

  /*
  int count = 0;
  while (TRUE) {
    status = in8(io_base+I_CR);
    if (!(status & CR_TXP) || count > WAIT_MAX_COUNT)
      break;
    count++;
  }

  status = in8(io_base+I_TSR);
  if (status & TSR_PTX) { // success 
    return 0;
  } else { // error

    return -1;
  }
  */
}

PUBLIC int ne2000_receive()
{
}

PRIVATE u_int16_t get_baseaddr()
{
  return nic_info->pci->base_addr;
}

PRIVATE void set_macaddr(u_int8_t* buff)
{
  disableInterrupt();
  out8(io_base+O_CR, CR_PAGE1);
  out8(io_base+IO_PAR0, buff[0]);
  out8(io_base+IO_PAR1, buff[1]);
  out8(io_base+IO_PAR2, buff[2]);
  out8(io_base+IO_PAR3, buff[3]);
  out8(io_base+IO_PAR4, buff[4]);
  out8(io_base+IO_PAR5, buff[5]);
  out8(io_base+O_CR, CR_PAGE0);
  enableInterrupt();
}

PRIVATE void init_ringbuf()
{
  out8(io_base+O_CR, CR_PAGE0);
  out8(io_base+O_PSTART, PSTART_ADDR);
  out8(io_base+O_PSTOP, PSTOP_ADDR);
  out8(io_base+O_BNRY, BNRY_ADDR);
  out8(io_base+O_CR, CR_PAGE1);
  out8(io_base+IO_CURR, CURR_ADDR);
  out8(io_base+O_CR, CR_PAGE0);
}

PRIVATE void init_sendreceive_config()
{
  u_int8_t cr_status = in8(io_base+I_CR);
  out8(io_base+O_CR, cr_status|CR_PAGE0);
  out8(io_base+O_TCR, TCR_LB_NORMAL);
  out8(io_base+O_RCR, 0);
}

PRIVATE void nic_sendenable_ctl(int flag)
{
  if (flag) {
    out8(io_base+O_TCR, TCR_LB_NORMAL);
    out8(io_base+O_CR, CR_STA);
  } else {
    out8(io_base+O_TCR, TCR_LB_NIC);
  }
}

PRIVATE void nic_recvenable_ctl(int flag)
{
  if (flag) {
    out8(io_base+O_RCR, RCR_AB);
    out8(io_base+O_CR, CR_STA);
  } else {
    out8(io_base+O_RCR, RCR_MON);
  }
}

PRIVATE void nic_enable_interrupt(int flag)
{
  if (flag) {
    out8(io_base + O_ISR, 0xFF);
    //u_int8_t cr_status = in8(io_base+I_CR);
    //out8(io_base+O_CR, cr_status|CR_PAGE0);
    out8(io_base + O_IMR, IMR_ALL);
  } else {
    out8(io_base + O_IMR, 0);
  }
}

PRIVATE void read_remote_dma(u_int16_t addr, void* buf, u_int16_t len)
{
  u_int16_t data, end, *p;
  end = (len%2) == 1 ? ((len+1)>>1) : (len>>1);

  disableInterrupt();

  if (len == 0) return;
  out8(io_base+O_RBCR0, len&0xff);
  out8(io_base+O_RBCR1, (len>>8));
  out8(io_base+O_RSAR0, addr&0xff);
  out8(io_base+O_RSAR1, (addr>>8));
  out8(io_base+O_CR, CR_STA|CR_RD_READ);
  p = (u_int16_t*)buf;
  int i;
  for (i = 0; i < end; i++) {
    data = in16(io_base+DATA_PORT_OFFSET);
    *p = data;
    p++;
  }
  
  enableInterrupt();
}

PRIVATE void write_remote_dma(u_int16_t addr, void* buf, u_int16_t len)
{
  u_int16_t data, end, *p;
  end = (len%2) == 1 ? ((len+1)>>1) : (len>>1);

  p = (u_int16_t*)buf;
  u_int8_t status = in8(io_base+I_ISR);
  status = (status & 0xbf);//(~ISR_RDC));
  //out8(io_base+O_ISR, status);
  out8(io_base+O_RSAR1, addr&0xff);
  out8(io_base+O_RSAR0, (addr>>8));
  out8(io_base+O_RBCR0, len&0xff);
  out8(io_base+O_RBCR1, (len>>8));
  out8(io_base+O_CR, CR_STA|CR_RD_WRITE);


  int i;
  for (i = 0; i < end; i++) {
    data = *p;
    out16(io_base+DATA_PORT_OFFSET, data);
    p++;
  }

  /*
  out8(io_base+O_CR, CR_STA|CR_RD_READ);
  for (i=0; i<16; i++) {
    u_int16_t data = in16(io_base+DATA_PORT_OFFSET);
    _kprintf("first data:%x\n", data);
  }
  */

  int count = 0;
  while (TRUE) {
    status = in8(io_base+I_ISR);
    if (status & ISR_RDC || count > WAIT_MAX_COUNT)
      break;
    count++;
  }
}

PUBLIC void send_test()
{
  u_int8_t buf[512];
  memset(buf, 0, 512);

  // set source mac address
  int i;
  for (i=0; i<6; i++) {
    buf[i] = 0xff;
  }	

  // set destination mac address
  for (i=6; i<12; i++)
    buf[i] = 0xff;//i - 5;

  buf[12] = 0x00;
  buf[13] = 0x08;

  int count = 0;
  while (TRUE) {
    int ret = ne2000_send(buf, 512);
    _kprintf("%x\n", ret);
  }
}

