#include <vga.h>
#include "uip-conf.h"
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>

void tcpip_output(void)
{
  if (uip_len > 0) {
    uip_arp_out();
    ne2000_send(uip_buf, uip_len);
  }
}

void uip_appcall(void) {}

void uip_log(char *msg) {
  _kprintf(msg);
}
