#include <vga.h>
#include "uip-conf.h"

void tcpip_output(void) {}
void uip_appcall(void) {}
void uip_log(char *msg) {
  _kprintf(msg);
}
