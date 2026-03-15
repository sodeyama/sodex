#include <io.h>
#include <ne2000.h>
#include <pit8254.h>
#include <descriptor.h>
#include <vga.h>
#include <nic.h>
#include <uip.h>

#define WAIT_MAX_COUNT 100000

PRIVATE void ne_serial_putc(char c)
{
  while (!(in8(0x3F8 + 5) & 0x20));
  out8(0x3F8, c);
}

PRIVATE void ne_serial_puts(const char *s)
{
  while (*s) {
    if (*s == '\n') ne_serial_putc('\r');
    ne_serial_putc(*s++);
  }
}

PRIVATE void ne_serial_hex8(u_int8_t v)
{
  const char *hex = "0123456789ABCDEF";
  ne_serial_putc(hex[(v >> 4) & 0xF]);
  ne_serial_putc(hex[v & 0xF]);
}

PRIVATE void ne_serial_hex16(u_int16_t v)
{
  ne_serial_hex8((v >> 8) & 0xFF);
  ne_serial_hex8(v & 0xFF);
}

PRIVATE u_int16_t io_base = NE2K_QEMU_BASEADDR;

PRIVATE u_int16_t get_baseaddr();
PRIVATE void __attribute__((unused)) set_macaddr(u_int8_t* buff);
//PRIVATE void nic_enable_ctl(int flag);
PRIVATE void __attribute__((unused)) nic_sendenable_ctl(int flag);
PRIVATE void __attribute__((unused)) nic_recvenable_ctl(int flag);
PRIVATE void __attribute__((unused)) nic_enable_interrupt(int flag);
PRIVATE void read_remote_dma(u_int16_t addr, void* buf, u_int16_t len);
PRIVATE void write_remote_dma(u_int16_t addr, void* buf, u_int16_t len);
PRIVATE void __attribute__((unused)) init_ringbuf();
PRIVATE void __attribute__((unused)) init_sendreceive_config();

PUBLIC void init_ne2000()
{
  io_base = get_baseaddr();

  /* 1. Reset NIC */
  out8(io_base+RESET_PORT_OFFSET, in8(io_base+RESET_PORT_OFFSET));
  kwait(10);

  /* 2. Stop NIC, select Page 0, abort DMA */
  out8(io_base+O_CR, CR_STP|CR_RD_STOP);

  /* 3. Wait for reset to complete (ISR.RST should be set) */
  {
    int wait = 0;
    while (!(in8(io_base+I_ISR) & ISR_RST) && wait < WAIT_MAX_COUNT)
      wait++;
  }

  /* 4. Configure data path (Page 0, NIC stopped) */
  out8(io_base+O_DCR, DCR_FT_8B|DCR_LS|DCR_WTS);
  out8(io_base+O_RBCR0, 0);
  out8(io_base+O_RBCR1, 0);
  out8(io_base+O_RCR, RCR_AB);
  out8(io_base+O_TCR, TCR_LB_NIC);  /* internal loopback during setup */
  out8(io_base+O_IMR, 0);           /* disable all interrupts for now */
  out8(io_base+O_ISR, 0xFF);        /* clear all pending interrupts */

  /* 5. Init ring buffer pointers (Page 0) */
  out8(io_base+O_PSTART, PSTART_ADDR);
  out8(io_base+O_PSTOP, PSTOP_ADDR);
  out8(io_base+O_BNRY, BNRY_ADDR);
  out8(io_base+O_TPSR, SEND_ADDR);

  /* 6. Switch to Page 1 to set MAC and CURR (keep STP|RD_STOP) */
  out8(io_base+O_CR, CR_PAGE1|CR_STP|CR_RD_STOP);
  out8(io_base+IO_PAR0, 0x52);
  out8(io_base+IO_PAR1, 0x54);
  out8(io_base+IO_PAR2, 0x00);
  out8(io_base+IO_PAR3, 0x12);
  out8(io_base+IO_PAR4, 0x34);
  out8(io_base+IO_PAR5, 0x56);
  out8(io_base+IO_CURR, CURR_ADDR);
  /* Clear multicast filter (accept none) */
  out8(io_base+IO_MAR0, 0x00);
  out8(io_base+IO_MAR1, 0x00);
  out8(io_base+IO_MAR2, 0x00);
  out8(io_base+IO_MAR3, 0x00);
  out8(io_base+IO_MAR4, 0x00);
  out8(io_base+IO_MAR5, 0x00);
  out8(io_base+IO_MAR6, 0x00);
  out8(io_base+IO_MAR7, 0x00);

  /* 7. Back to Page 0, clear ISR, enable interrupts, start NIC */
  out8(io_base+O_CR, CR_PAGE0|CR_STP|CR_RD_STOP);
  out8(io_base+O_ISR, 0xFF);        /* clear all pending interrupts */
  out8(io_base+O_IMR, IMR_ALL);     /* enable all interrupt sources */
  out8(io_base+O_TCR, TCR_LB_NORMAL); /* exit loopback, normal operation */

  /* 8. Start NIC */
  out8(io_base+O_CR, CR_STA|CR_RD_STOP);

  ne_serial_puts("NE2K: init done CR=");
  ne_serial_hex8(in8(io_base+I_CR));
  ne_serial_puts(" IMR(p2)=QEMU-N/A");
  ne_serial_putc('\n');
}

#define NE2000_IRQ 11

PUBLIC volatile u_int8_t ne2000_rx_pending = 0;

PUBLIC volatile u_int32_t ne2k_irq_count = 0;
PUBLIC volatile u_int8_t ne2k_last_isr = 0;

PUBLIC void i2Bh_ne2000_interrupt()
{
  u_int8_t status = in8(io_base + I_ISR);
  ne2k_irq_count++;
  ne2k_last_isr = status;

  if (status & ISR_PRX) {
    ne2000_rx_pending = 1;
  }

  if (status & ISR_OVW) {
    out8(io_base + O_CR, CR_STP | CR_RD_STOP);
    out8(io_base + O_RBCR0, 0);
    out8(io_base + O_RBCR1, 0);
    out8(io_base + O_TCR, TCR_LB_NIC);
    out8(io_base + O_CR, CR_STA | CR_RD_STOP);
    out8(io_base + O_TCR, TCR_LB_NORMAL);
    ne2000_rx_pending = 1;
  }

  /* 処理済みフラグをクリア */
  out8(io_base + O_ISR, status);

  pic_eoi(NE2000_IRQ);
}

PUBLIC int ne2000_send(void* buf, u_int16_t len)
{
  ne_serial_puts("NE2K-TX: len=0x");
  ne_serial_hex16(len);
  ne_serial_putc('\n');

  u_int8_t status = in8(io_base+I_CR);
  if (status & CR_TXP)
    return -1;

  disableInterrupt();
  write_remote_dma(SEND_ADDR << 8, buf, len);
  out8(io_base+O_ISR, ISR_RDC);
  out8(io_base+O_TPSR, SEND_ADDR);
  out8(io_base+O_TBCR0, len&0xff);
  out8(io_base+O_TBCR1, (len>>8));
  out8(io_base+O_CR, CR_STA|CR_TXP|CR_RD_STOP);
  enableInterrupt();

  return 0;
}

PUBLIC int ne2000_receive()
{
  u_int8_t curr, bnry, packet_page, next_page, new_bnry;
  u_int16_t pkt_addr, pkt_len, data_len;

  /* Page1に切り替えてCURRを読む */
  out8(io_base + O_CR, CR_PAGE1 | CR_STA | CR_RD_STOP);
  curr = in8(io_base + IO_CURR);
  /* Page0に戻す */
  out8(io_base + O_CR, CR_PAGE0 | CR_STA | CR_RD_STOP);

  /* BNRYを読む */
  bnry = in8(io_base + I_BNRY);
  packet_page = bnry + 1;
  if (packet_page >= PSTOP_ADDR) {
    packet_page = PSTART_ADDR;
  }

  /* BNRYの次ページがCURRに追いついていたら空 */
  if (packet_page == curr) {
    return 0;
  }

  ne_serial_puts("NE2K-RX: bnry=0x");
  ne_serial_hex8(bnry);
  ne_serial_puts(" curr=0x");
  ne_serial_hex8(curr);
  ne_serial_puts(" pkt=0x");
  ne_serial_hex8(packet_page);
  ne_serial_putc('\n');

  /* パケットヘッダ読み出し（4バイト） */
  u_int8_t pkt_hdr[4];
  pkt_addr = packet_page << 8;
  read_remote_dma(pkt_addr, pkt_hdr, 4);

  next_page = pkt_hdr[1];
  pkt_len = pkt_hdr[2] | (pkt_hdr[3] << 8);

  ne_serial_puts("NE2K-RX: status=0x");
  ne_serial_hex8(pkt_hdr[0]);
  ne_serial_puts(" next=0x");
  ne_serial_hex8(next_page);
  ne_serial_puts(" len=0x");
  ne_serial_hex16(pkt_len);
  ne_serial_putc('\n');

  data_len = (pkt_len > 4) ? (pkt_len - 4) : 0;

  /* 異常値チェック */
  if (!(pkt_hdr[0] & RSR_PRX) ||
      next_page < PSTART_ADDR || next_page >= PSTOP_ADDR ||
      data_len > UIP_BUFSIZE || data_len == 0) {
    new_bnry = packet_page;
    if (next_page >= PSTART_ADDR && next_page < PSTOP_ADDR) {
      new_bnry = next_page - 1;
      if (new_bnry < PSTART_ADDR) {
        new_bnry = PSTOP_ADDR - 1;
      }
    }
    out8(io_base + O_CR, CR_PAGE0 | CR_STA | CR_RD_STOP);
    out8(io_base + O_BNRY, new_bnry);
    ne_serial_puts("NE2K-RX: drop new_bnry=0x");
    ne_serial_hex8(new_bnry);
    ne_serial_putc('\n');
    return -1;
  }

  /* パケットデータ読み出し（ヘッダ4バイトの直後から） */
  u_int16_t data_addr = pkt_addr + 4;

  /* リングバッファのラップアラウンドチェック */
  u_int16_t ring_end = PSTOP_ADDR << 8;
  if (data_addr + data_len > ring_end) {
    u_int16_t first_len = ring_end - data_addr;
    read_remote_dma(data_addr, uip_buf, first_len);
    u_int16_t second_addr = PSTART_ADDR << 8;
    read_remote_dma(second_addr, (u_int8_t *)uip_buf + first_len, data_len - first_len);
  } else {
    read_remote_dma(data_addr, uip_buf, data_len);
  }

  /* BNRYは読み終えた最後のページを指す */
  new_bnry = next_page - 1;
  if (new_bnry < PSTART_ADDR) {
    new_bnry = PSTOP_ADDR - 1;
  }
  out8(io_base + O_CR, CR_PAGE0 | CR_STA | CR_RD_STOP);
  ne_serial_puts("BNRY-W: ");
  ne_serial_hex8(new_bnry);
  out8(io_base + O_BNRY, new_bnry);
  ne_serial_puts(" read-back=");
  ne_serial_hex8(in8(io_base + I_BNRY));
  ne_serial_putc('\n');

  return data_len;
}

PUBLIC u_int16_t ne2000_get_iobase(void)
{
  return io_base;
}

PUBLIC void ne2000_enable_interrupts(void)
{
  out8(io_base + O_CR, CR_PAGE0 | CR_STA | CR_RD_STOP);
  out8(io_base + O_IMR, IMR_ALL);
  out8(io_base + O_ISR, 0xFF);
}

PUBLIC u_int8_t ne2000_read_isr(void)
{
  return in8(io_base + I_ISR);
}

PUBLIC void ne2000_ack_isr(u_int8_t status)
{
  out8(io_base + O_ISR, status);
}

PUBLIC u_int8_t ne2000_read_bnry(void)
{
  return in8(io_base + I_BNRY);
}

PRIVATE u_int16_t get_baseaddr()
{
  /* ISA NE2000: fixed I/O base address (not on PCI bus) */
  return NE2K_QEMU_BASEADDR;
}

PRIVATE void __attribute__((unused)) set_macaddr(u_int8_t* buff)
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

PRIVATE void __attribute__((unused)) init_ringbuf()
{
  out8(io_base+O_CR, CR_PAGE0);
  out8(io_base+O_PSTART, PSTART_ADDR);
  out8(io_base+O_PSTOP, PSTOP_ADDR);
  out8(io_base+O_BNRY, BNRY_ADDR);
  out8(io_base+O_CR, CR_PAGE1);
  out8(io_base+IO_CURR, CURR_ADDR);
  out8(io_base+O_CR, CR_PAGE0);
}

PRIVATE void __attribute__((unused)) init_sendreceive_config()
{
  u_int8_t cr_status = in8(io_base+I_CR);
  out8(io_base+O_CR, cr_status|CR_PAGE0);
  out8(io_base+O_TCR, TCR_LB_NORMAL);
  out8(io_base+O_RCR, RCR_AB);
}

PRIVATE void __attribute__((unused)) nic_sendenable_ctl(int flag)
{
  if (flag) {
    out8(io_base+O_TCR, TCR_LB_NORMAL);
    out8(io_base+O_CR, CR_STA);
  } else {
    out8(io_base+O_TCR, TCR_LB_NIC);
  }
}

PRIVATE void __attribute__((unused)) nic_recvenable_ctl(int flag)
{
  if (flag) {
    out8(io_base+O_RCR, RCR_AB);
    out8(io_base+O_CR, CR_STA);
  } else {
    out8(io_base+O_RCR, RCR_MON);
  }
}

PRIVATE void __attribute__((unused)) nic_enable_interrupt(int flag)
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

  if (len == 0) {
    enableInterrupt();
    return;
  }
  out8(io_base+O_ISR, ISR_RDC);
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

  {
    int count = 0;
    while (TRUE) {
      u_int8_t s = in8(io_base+I_ISR);
      if (s & ISR_RDC || count > WAIT_MAX_COUNT)
        break;
      count++;
    }
  }

  enableInterrupt();
}

PRIVATE void write_remote_dma(u_int16_t addr, void* buf, u_int16_t len)
{
  u_int16_t data, end, *p;
  end = (len%2) == 1 ? ((len+1)>>1) : (len>>1);

  p = (u_int16_t*)buf;
  out8(io_base+O_ISR, ISR_RDC);  /* Clear RDC before DMA */
  out8(io_base+O_RSAR0, addr&0xff);
  out8(io_base+O_RSAR1, (addr>>8));
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
    u_int8_t s = in8(io_base+I_ISR);
    if (s & ISR_RDC || count > WAIT_MAX_COUNT)
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

  while (TRUE) {
    int ret = ne2000_send(buf, 512);
    _kprintf("%x\n", ret);
  }
}
