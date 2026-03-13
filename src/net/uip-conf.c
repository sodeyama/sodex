#include <vga.h>
#include <io.h>
#include "uip-conf.h"
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>
#include <socket.h>
#include <string.h>

static void dbg_putc(char c) {
  while (!(in8(0x3F8 + 5) & 0x20));
  out8(0x3F8, c);
}
static void dbg_puts(const char *s) {
  while (*s) { if (*s == '\n') dbg_putc('\r'); dbg_putc(*s++); }
}
static void dbg_dec(int v) {
  if (v < 0) { dbg_putc('-'); v = -v; }
  if (v == 0) { dbg_putc('0'); return; }
  char b[12]; int l=0;
  while (v > 0) { b[l++] = '0' + v%10; v /= 10; }
  while (l--) dbg_putc(b[l]);
}

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
  int ready_for_output = (uip_poll() || uip_acked() || uip_connected());

  if (uip_connected()) {
    dbg_puts("TCP: CONNECTED sockfd=");
    dbg_dec(sockfd);
    dbg_puts("\n");
    sk->state = SOCK_STATE_CONNECTED;
    wakeup(&sk->connect_wq);
  }

  if (uip_newdata()) {
    dbg_puts("TCP: NEWDATA len=");
    dbg_dec(uip_datalen());
    dbg_puts("\n");
    socket_tcp_input(sockfd, (u_int8_t *)uip_appdata, uip_datalen());
  }

  if (uip_closed() || uip_aborted() || uip_timedout()) {
    sk->state = SOCK_STATE_CLOSED;
    wakeup(&sk->recv_wq);
    wakeup(&sk->connect_wq);
  }

  /* appcall 中だけ uip_send() / uip_close() を呼ぶ */
  if (ready_for_output && sk->tx_pending) {
    uip_send(sk->tx_buf, sk->tx_len);
    sk->tx_pending = 0;
  }

  if (ready_for_output && sk->close_pending && !sk->tx_pending &&
      !uip_outstanding(uip_conn)) {
    sk->close_pending = 0;
    uip_close();
  }

  if (uip_rexmit()) {
    /* Retransmit the last sent data */
    if (sk->tx_len > 0) {
      uip_send(sk->tx_buf, sk->tx_len);
    }
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
