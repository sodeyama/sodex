#include <vga.h>
#include "uip-conf.h"
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>
#include <socket.h>
#include <string.h>

void tcpip_output(void)
{
  if (uip_len > 0) {
    uip_arp_out();
    ne2000_send(uip_buf, uip_len);
  }
}

void uip_appcall(void) {
  int sockfd = uip_conn->appstate;
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return;
  struct kern_socket *sk = &socket_table[sockfd];

  if (uip_connected()) {
    sk->state = SOCK_STATE_CONNECTED;
    wakeup(&sk->connect_wq);
  }

  if (uip_newdata()) {
    socket_tcp_input(sockfd, (u_int8_t *)uip_appdata, uip_datalen());
  }

  if (uip_closed() || uip_aborted() || uip_timedout()) {
    sk->state = SOCK_STATE_CLOSED;
    wakeup(&sk->recv_wq);
    wakeup(&sk->connect_wq);
  }

  if (uip_rexmit()) {
    /* Retransmit: for now, just let uIP handle it */
  }
}

void uip_udp_appcall(void) {
  if (uip_newdata()) {
    socket_udp_input(uip_udp_conn, (u_int8_t *)uip_appdata, uip_datalen());
  }
}

void uip_log(char *msg) {
  _kprintf(msg);
}
