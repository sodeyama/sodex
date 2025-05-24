#include <vga.h>
#include <io.h>
#include <ihandlers.h>
#include <memory.h>
#include <pci.h>
#include <lib.h>
#include <uhci-hcd.h>
#include <delay.h>
#include <dma.h>

//#define DEBUG_UHCI 1

#define UHCI_DELAY_TIMES  1000000

PRIVATE void init_uhci_port(USB_HC_INFO* uhci);
PRIVATE void uhc_enable_interrupt();
PRIVATE void set_framelist_base(UHCI_FRAME_LIST* frame_list);
PRIVATE void make_init_frame_list(UHCI_FRAME_LIST* frame_list);
PRIVATE void global_reset();
PRIVATE void force_global_resume();
PRIVATE void reset_uhc();
PRIVATE void run_uhci();
PRIVATE void stop_uhci();
PRIVATE void debug_uhci();
PRIVATE UHCI_QH* make_setaddress_qh();
PRIVATE UHCI_QH* make_setconfiguration_qh();
PRIVATE void insert_qh_queue(UHCI_QH *qh);
PRIVATE void clear_qh_queue();
PRIVATE UHCI_QH* make_bulkin_transaction(char *data, int size, int toggle, int endflag);
PRIVATE UHCI_QH* make_bulkout_transaction(char *data, int size, int toggle, int endflag);
PRIVATE int uhci_wait_interrupt();
PRIVATE u_int32_t uhci_check_interrupt();
PRIVATE void uhci_clear_interrupt();
PRIVATE void free_qh_list();
PRIVATE void free_td_list();

PRIVATE int io_base, uhci_irq;
PRIVATE UHCI_QH *head_qh, *current_qh;
PRIVATE QH_FREE_LIST* qh_free_list_head;
PRIVATE QH_FREE_LIST* qh_free_list_tail;
PRIVATE TD_FREE_LIST* td_free_list_head;
PRIVATE TD_FREE_LIST* td_free_list_tail;
PRIVATE u_int32_t uhci_interrupt = 0;

PUBLIC void init_uhci()
{
  USB_HC_INFO* main_usb_info = usb_info[0];
  qh_free_list_head = NULL;
  qh_free_list_tail = NULL;
  td_free_list_head = NULL;
  td_free_list_tail = NULL;
  io_base = main_usb_info->pci_info->base_addr;
  uhci_irq = main_usb_info->pci_info->irq;

  global_reset();
  reset_uhc();

  init_uhci_port(main_usb_info);

  set_trap_gate(PIC_BASE + uhci_irq, &asm_uhcihandler);
  set_intr_bit(uhci_irq);

  out8(io_base + SOFMOD_R, USBSOF_DEFAULT);
  delay(UHCI_DELAY_TIMES);


  // 4Kbyte align
  frame_list = aalloc(sizeof(UHCI_FRAME_LIST) * UHCI_FRAME_SIZE, 12);
  memset(frame_list, 0, sizeof(UHCI_FRAME_LIST) * UHCI_FRAME_SIZE);
  dma_trans.addr = frame_list;
  dma_trans.count = sizeof(UHCI_FRAME_LIST) * UHCI_FRAME_SIZE;
  make_init_frame_list(frame_list);

  set_framelist_base(frame_list);
  out32(io_base + USBFRNUM_R, 0);
  delay(UHCI_DELAY_TIMES);


  u_int16_t status;
  u_int16_t port_addr = PORTSC1_R;
  int i;
  for (i = 0; i < main_usb_info->numports; i++) {
    u_int16_t addr = io_base + port_addr + i * 2;

    //ClearPortFeature
    CLR_RH_PORTSTAT(USBPORTSC_CSC, addr);
    delay(UHCI_DELAY_TIMES);
    //SetPortFeature PORT_RESET
    SET_RH_PORTSTAT(USBPORTSC_PR, addr);
    delay(UHCI_DELAY_TIMES);
    //ClearPortFeture C_PORT_RESET
    CLR_RH_PORTSTAT(USBPORTSC_CSC, addr);
    delay(UHCI_DELAY_TIMES);

    CLR_RH_PORTSTAT(USBPORTSC_PR, addr);
    delay(UHCI_DELAY_TIMES);
    CLR_RH_PORTSTAT(USBPORTSC_CSC | USBPORTSC_PEC, addr);
    delay(UHCI_DELAY_TIMES);
    SET_RH_PORTSTAT(USBPORTSC_PE, addr);
    delay(UHCI_DELAY_TIMES);

    status = in16(addr);
    _kprintf(" UHCI Root Hub init. port_addr:%x status:%x\n", addr, status);
  }


  UHCI_QH *setaddr_qh = make_setaddress_qh();
  UHCI_QH *setconf_qh = make_setconfiguration_qh();
  uhci_clear_interrupt();

  insert_qh_queue(setaddr_qh);
  run_uhci();
  uhc_enable_interrupt();
  if (!uhci_wait_interrupt()) {
    _kputs("[UHCI] setaddr wait interrupt error\n");
  }
  uhci_clear_interrupt();
  insert_qh_queue(setconf_qh);
  run_uhci();
  if (!uhci_wait_interrupt()) {
    _kputs("[UHCI] setconfiguration wait interrupt error\n");
  }
}

PRIVATE void init_uhci_port(USB_HC_INFO* uhci)
{
  int base = uhci->pci_info->base_addr;
  int max_port_size = (0x20 - PORTSC1_R) / 2;
  int port;
  for (port = 0; port < max_port_size; port++) {
    u_int16_t status;
    status = in16(base + PORTSC1_R + port * 2);
    if (!(status & 0x0080) || status == 0xffff)
      break;
  }
  uhci->numports = port;
}

PRIVATE void insert_qh_free_list(UHCI_QH *qh)
{
  if (qh_free_list_head == NULL) {
    qh_free_list_head = kalloc(sizeof(QH_FREE_LIST));
    qh_free_list_tail = qh_free_list_head;
    qh_free_list_head->qh_next = NULL;
    qh_free_list_head->qh = qh;
  } else {
    qh_free_list_tail->qh_next = kalloc(sizeof(QH_FREE_LIST));
    qh_free_list_tail->qh = qh; 
    qh_free_list_tail = qh_free_list_tail->qh_next;
  }
}

PRIVATE void insert_td_free_list(UHCI_TD *td)
{
  if (td_free_list_head == NULL) {
    td_free_list_head = kalloc(sizeof(TD_FREE_LIST));
    td_free_list_tail = td_free_list_head;
    td_free_list_head->td_next = NULL;
    td_free_list_head->td = td;
  } else {
    td_free_list_tail->td_next = kalloc(sizeof(TD_FREE_LIST));
    td_free_list_tail->td = td;    
    td_free_list_tail = td_free_list_tail->td_next;
  }
}

PRIVATE void free_qh_list()
{
  QH_FREE_LIST* pqh = qh_free_list_head;
  while (pqh) {
    kfree(pqh->qh);
    QH_FREE_LIST* old_pqh = pqh;
    pqh = pqh->qh_next;
    kfree(old_pqh);
  }
  qh_free_list_head = NULL;
  qh_free_list_tail = NULL;
}

PRIVATE void free_td_list()
{
  TD_FREE_LIST* ptd = td_free_list_head;
  while (ptd) {
    kfree(ptd->td);
    TD_FREE_LIST* old_ptd = ptd;
    ptd = ptd->td_next;
    kfree(old_ptd);
  }
  td_free_list_head = NULL;
  td_free_list_tail = NULL;
}

static int count = 0;
PUBLIC void intr_uhcihandler()
{
  //disable_pic_interrupt(uhci_irq);
  u_int16_t status = in16(io_base + USBSTS_R);
  if (status & USBSTS_INT) {
    //_kprintf(" USB Interrupt:%x\n", count++);
  }
  if (status & USBSTS_ERR_INT) {
    _kputs(" [UHCI INT] USB Error Interrupt\n");
  }
  if (status & USBSTS_RESUME) {
    _kputs(" [UHCI INT] Resume Detect\n");
  }
  if (status & USBSTS_SYS_ERR) {
    _kputs(" [UHCI INT] Host System Error\n");
  }
  if (status & USBSTS_PROC_ERR) {
    _kputs(" [UHCI INT] Host Controller Process Error\n");
  }
  if (status & USBSTS_HALT) {
    _kputs(" [UHCI INT] HCHalted\n");
    for(;;);
  }
  //stop_uhci();
  out16(io_base + USBSTS_R, 0);
  clear_qh_queue();
  uhci_interrupt++;
  pic_eoi(uhci_irq);
  //enable_pic_interrupt(uhci_irq);
}

PRIVATE int uhci_wait_interrupt()
{
  int count = 0;
  // XXX need timer
  while (TRUE) {
    if (count != 0 && (count % UHCI_WAIT_COUNTMAX) == 0) {
      _kprintf(" [UHCI] wait interrupt. over countmax\n");
    }
    if (uhci_check_interrupt()) {
      return TRUE;
    }
    count++;
  }
  return FALSE;
}

PRIVATE u_int32_t uhci_check_interrupt()
{
  return uhci_interrupt;
}

PRIVATE void uhci_clear_interrupt()
{
  uhci_interrupt = 0;
}

PRIVATE void make_init_frame_list(UHCI_FRAME_LIST* frame_list)
{
  head_qh = aalloc(sizeof(UHCI_QH), 4);
  head_qh->head_link = QH_Q | QH_T;
  head_qh->elem_link = TD_T;
  current_qh = head_qh;
  int i;
  for (i = 0; i < UHCI_FRAME_SIZE; i++) {
    memset(&frame_list[i], 0, sizeof(UHCI_FRAME_LIST));
    frame_list[i].frame_list_pointer = get_realaddr(head_qh);
    frame_list[i].s.q = 1;
    frame_list[i].s.t = 0;
  }
}

PRIVATE void uhc_enable_interrupt()
{
  USBINTR enable_intr;
  memset(&enable_intr, 0, sizeof(USBINTR));
  enable_intr.s.crc_enable = 1;
  enable_intr.s.sp_enable = 1;
  enable_intr.s.ioc_enable = 1;
  enable_intr.s.resume_enable = 1;
  out16(io_base + USBINTR_R, enable_intr.cmd);
}

PRIVATE void set_framelist_base(UHCI_FRAME_LIST* frame_list)
{
  // This address must be real address !!
  out32(io_base + FRBASEADD_R, get_realaddr((u_int32_t)frame_list));
  delay(UHCI_DELAY_TIMES);
}

PRIVATE void global_reset()
{
  USBCMD greset_cmd;
  memset(&greset_cmd, 0, sizeof(USBCMD));
  greset_cmd.s.greset = 1;
  out16(io_base + USBCMD_R, greset_cmd.cmd);
  delay(UHCI_DELAY_TIMES);
}

PRIVATE void force_global_resume()
{
  USBCMD cmd;
  memset(&cmd, 0, sizeof(USBCMD));
  cmd.s.fgr = 1;
  out16(io_base + USBCMD_R, cmd.cmd);
  delay(UHCI_DELAY_TIMES);
}

PRIVATE void reset_uhc()
{
  USBCMD reset_cmd;
  memset(&reset_cmd, 0, sizeof(USBCMD));
  reset_cmd.s.hcreset = 1;
  out16(io_base + USBCMD_R, reset_cmd.cmd);
  delay(UHCI_DELAY_TIMES);
  if (in16(io_base + USBCMD_R) & UHCI_USBCMD_UCRESET) {
    _kprintf("HCRESET not completed yet\n");
  }
  out16(io_base + USBINTR_R, 0);
  out16(io_base + USBCMD_R, 0);
}

PRIVATE void run_uhci()
{
  USBCMD cmd;
  memset(&cmd, 0, sizeof(USBCMD));
  cmd.s.rs = 1;
  cmd.s.cf = 1;
  cmd.s.maxp = 1;
  //reset_cmd.s.swdbg = 1;
  out16(io_base + USBCMD_R, cmd.cmd);
  delay(UHCI_DELAY_TIMES);
}

PRIVATE void stop_uhci()
{
  USBCMD reset_cmd;
  memset(&reset_cmd, 0, sizeof(USBCMD));
  reset_cmd.s.rs = 0;
  out16(io_base + USBCMD_R, reset_cmd.cmd);
  delay(UHCI_DELAY_TIMES);
}

PRIVATE void debug_uhci()
{
  USBCMD cmd;
  memset(&cmd, 0, sizeof(USBCMD));
  cmd.s.swdbg = 1;
  out16(io_base + USBCMD_R, cmd.cmd);
  delay(UHCI_DELAY_TIMES);
}

PRIVATE UHCI_QH* make_setaddress_qh()
{
  STD_DEVICE_REQUEST *req = kalloc(sizeof(STD_DEVICE_REQUEST));
  memset(req, 0, sizeof(STD_DEVICE_REQUEST));
  req->bmRequestType = 0;
  req->bRequest = SET_ADDRESS;
  req->wValue = 1; // address 1
  req->wIndex = 0;
  req->wLength = 0;

  UHCI_TD *in_td = aalloc(sizeof(UHCI_TD), 4);
  memset(in_td, 0, sizeof(UHCI_TD));
  in_td->link_pointer = NULL | TD_DEPTH_FIRST | TD_T;
  in_td->control.ioc = TD_ISSUE_IOC;
  in_td->control.status = TD_STATUS_ACTIVE;
  in_td->token.maxlen = 0x40;
  in_td->token.data_toggle = 0;
  in_td->token.endpoint = 0;
  in_td->token.dev_addr = 0;
  in_td->token.pid = TD_PID_IN;
  in_td->buffer_pointer = NULL;

  UHCI_TD *setup_td = aalloc(sizeof(UHCI_TD), 4);
  memset(setup_td, 0, sizeof(UHCI_TD));
  setup_td->link_pointer = get_realaddr(in_td) | TD_DEPTH_FIRST;
  setup_td->control.status = TD_STATUS_ACTIVE;
  setup_td->token.maxlen = 0x07; // 8byte for setup 
  setup_td->token.data_toggle = 0;
  setup_td->token.endpoint = 0;
  setup_td->token.dev_addr = 0;
  setup_td->token.pid = TD_PID_SETUP;
  setup_td->buffer_pointer = get_realaddr(req);

  UHCI_QH *qh = aalloc(sizeof(UHCI_QH), 4);
  qh->head_link = QH_T;
  qh->elem_link = get_realaddr(setup_td);

  return qh;
}

PRIVATE UHCI_QH* make_setconfiguration_qh()
{
  STD_DEVICE_REQUEST *req = kalloc(sizeof(STD_DEVICE_REQUEST));
  memset(req, 0, sizeof(STD_DEVICE_REQUEST));
  req->bmRequestType = 0;
  req->bRequest = SET_CONFIGURATION;
  req->wValue = 0;
  req->wIndex = 0;
  req->wLength = 0;

  UHCI_TD *in_td = aalloc(sizeof(UHCI_TD), 4);
  memset(in_td, 0, sizeof(UHCI_TD));
  in_td->link_pointer = NULL | TD_DEPTH_FIRST | TD_T;
  in_td->control.ioc = TD_ISSUE_IOC;
  in_td->control.status = TD_STATUS_ACTIVE;
  in_td->token.maxlen = 0x40;
  in_td->token.data_toggle = 0;
  in_td->token.endpoint = 0;
  in_td->token.dev_addr = USB_MASS_STORAGE_ADDR;
  in_td->token.pid = TD_PID_IN;
  in_td->buffer_pointer = NULL;

  UHCI_TD *setup_td = aalloc(sizeof(UHCI_TD), 4);
  memset(setup_td, 0, sizeof(UHCI_TD));
  setup_td->link_pointer = get_realaddr(in_td) | TD_DEPTH_FIRST;
  setup_td->control.status = TD_STATUS_ACTIVE;
  setup_td->token.maxlen = 0x07; // 8byte for setup 
  setup_td->token.data_toggle = 0;
  setup_td->token.endpoint = 0;
  setup_td->token.dev_addr = USB_MASS_STORAGE_ADDR;
  setup_td->token.pid = TD_PID_SETUP;
  setup_td->buffer_pointer = get_realaddr(req);

  UHCI_QH *qh = aalloc(sizeof(UHCI_QH), 4);
  qh->head_link = QH_T;
  qh->elem_link = get_realaddr(setup_td);

  return qh;
}

PRIVATE void insert_qh_queue(UHCI_QH *qh)
{
  current_qh->head_link = get_realaddr(qh) | QH_Q;
  current_qh = qh;
  qh->head_link = QH_T;
}

PRIVATE void clear_qh_queue()
{
  free_qh_list();
  free_td_list();

  current_qh = head_qh;
  current_qh->head_link = QH_T;
}

PRIVATE UHCI_QH* make_bulkout_transaction(char *data, int size, int toggle, int endflag)
{
  UHCI_TD *out_td = aalloc(sizeof(UHCI_TD), 4);
  memset(out_td, 0, sizeof(UHCI_TD));
  out_td->link_pointer = TD_DEPTH_FIRST | TD_T;
  if (endflag) {
    out_td->control.ioc = TD_ISSUE_IOC;
  }
  out_td->control.status = TD_STATUS_ACTIVE;
  if (size > MAX_BULKTRANS_SIZE) {
    _kprintf(" make_bulkout_transaction error. size must be less than 0x500\n");
    size = MAX_BULKTRANS_SIZE;
  }
  out_td->token.maxlen = size - 1;
  out_td->token.data_toggle = toggle;
  out_td->token.endpoint = 2;
  out_td->token.dev_addr = USB_MASS_STORAGE_ADDR;
  out_td->token.pid = TD_PID_OUT;
  out_td->buffer_pointer = get_realaddr(data);

  UHCI_QH *qh = aalloc(sizeof(UHCI_QH), 4);
  qh->head_link = QH_T;
  qh->elem_link = get_realaddr(out_td);

  return qh;
}

PRIVATE UHCI_QH* make_bulkin_transaction(char *data, int size, int toggle, int endflag)
{
  UHCI_TD *out_td = aalloc(sizeof(UHCI_TD), 4);
  memset(out_td, 0, sizeof(UHCI_TD));
  out_td->link_pointer = TD_DEPTH_FIRST | TD_T;
  if (endflag) {
    out_td->control.ioc = TD_ISSUE_IOC;
  }
  out_td->control.status = TD_STATUS_ACTIVE;
  if (size > MAX_BULKTRANS_SIZE) {
    _kprintf(" make_bulkin_transaction error. size must be less than 0x500\n");
    size = MAX_BULKTRANS_SIZE;
  }
  out_td->token.maxlen = size - 1;
  out_td->token.data_toggle = toggle;
  out_td->token.endpoint = 1;
  out_td->token.dev_addr = USB_MASS_STORAGE_ADDR;
  out_td->token.pid = TD_PID_IN;
  out_td->buffer_pointer = get_realaddr(data);

  UHCI_QH *qh = aalloc(sizeof(UHCI_QH), 4);
  qh->head_link = QH_T;
  qh->elem_link = get_realaddr(out_td);

  return qh;
}

PUBLIC void bulk_out(char *data, int length)
{
  stop_uhci();
  uhci_clear_interrupt();
  int i;
  int count = length % MAX_BULKTRANS_SIZE == 0 ?
    length / MAX_BULKTRANS_SIZE : length / MAX_BULKTRANS_SIZE + 1;
  int size = MAX_BULKTRANS_SIZE;
  int endflag = FALSE;
  int toggle;
  char *p = data;
  for (i = 0; i < count; i++) {
    toggle = i % 2;
    if (i == count - 1) {
      size = length % MAX_BULKTRANS_SIZE == 0 ?
        MAX_BULKTRANS_SIZE : length % MAX_BULKTRANS_SIZE;
      endflag = TRUE;
    }
#ifdef DEBUG_UHCI
    _kprintf(" bulkout size:%x endflag:%x\n", size, endflag);
#endif
    UHCI_QH* qh = make_bulkout_transaction(p, size, toggle, endflag);
    insert_qh_queue(qh);
    p += MAX_BULKTRANS_SIZE;
  }
  run_uhci();
  if (!uhci_wait_interrupt()) {
    _kputs("[UHCI] wait interrupt error\n");
  }
}

PUBLIC void bulk_in(char *data, int length)
{
  stop_uhci();
  uhci_clear_interrupt();
  disable_pic_interrupt(IRQ_UHCI);
  int i;
  int count = length % MAX_BULKTRANS_SIZE == 0 ?
    length / MAX_BULKTRANS_SIZE : length / MAX_BULKTRANS_SIZE + 1;
  int size = MAX_BULKTRANS_SIZE;
  int endflag = FALSE;
  int toggle;
  char *p = data;
  for (i = 0; i < count; i++) {
    toggle = i % 2;
    if (i == count - 1) {
      size = length % MAX_BULKTRANS_SIZE == 0 ?
        MAX_BULKTRANS_SIZE : length % MAX_BULKTRANS_SIZE;
      endflag = TRUE;
    }
#ifdef DEBUG_UHCI
    _kprintf(" bulkin size:%x endflag:%x\n", size, endflag);
#endif
    UHCI_QH* qh = make_bulkin_transaction(p, size, toggle, endflag);
    insert_qh_queue(qh);
    p += MAX_BULKTRANS_SIZE;
  }
  enable_pic_interrupt(IRQ_UHCI);
  run_uhci();
  if (!uhci_wait_interrupt()) {
    _kputs("[UHCI] wait interrupt error\n");
  }
}
