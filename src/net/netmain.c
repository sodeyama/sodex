#include <sodex/const.h>
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>
#include <timer.h>
#include <socket.h>
#include <rs232c.h>

#define CLOCK_CONF_SECOND 100
#define PERIODIC_TIMER_INTERVAL  50
#define ARP_TIMER_INTERVAL       (10 * CLOCK_CONF_SECOND)

PRIVATE struct timer periodic_timer;
PRIVATE struct timer arp_timer;
PRIVATE u_int8_t initialized = 0;

PUBLIC void network_init(void)
{
  timer_set(&periodic_timer, PERIODIC_TIMER_INTERVAL);
  timer_set(&arp_timer, ARP_TIMER_INTERVAL);
  initialized = 1;
}

PUBLIC void network_poll(void)
{
  if (!initialized) return;

  int i;

  if (ne2000_rx_pending) {
    ne2000_rx_pending = 0;

    while (1) {
      uip_len = ne2000_receive();
      if (uip_len <= 0) break;


      struct uip_eth_hdr *eth_hdr = (struct uip_eth_hdr *)uip_buf;

      if (eth_hdr->type == htons(UIP_ETHTYPE_IP)) {
        uip_arp_ipin();

        /* Intercept ICMP echo reply for raw sockets */
        u_int8_t *ip_hdr = &uip_buf[14]; /* after Ethernet header */
        if (ip_hdr[9] == 1) { /* IPPROTO_ICMP */
          u_int8_t icmp_type = ip_hdr[(ip_hdr[0] & 0x0f) * 4];
          if (icmp_type == 0) { /* ICMP_ECHO_REPLY */
            socket_icmp_input(ip_hdr, uip_len - 14);
            uip_len = 0;
            continue;
          }
        }

        uip_input();
        if (uip_len > 0) {
          uip_arp_out();
          ne2000_send(uip_buf, uip_len);
        }
      } else if (eth_hdr->type == htons(UIP_ETHTYPE_ARP)) {
        uip_arp_arpin();
        if (uip_len > 0) {
          ne2000_send(uip_buf, uip_len);
        }
      }
    }
  }

  if (timer_expired(&periodic_timer)) {
    timer_reset(&periodic_timer);

    for (i = 0; i < UIP_CONNS; i++) {
      uip_periodic(i);
      if (uip_len > 0) {
        uip_arp_out();
        ne2000_send(uip_buf, uip_len);
      }
    }

#if UIP_UDP
    for (i = 0; i < UIP_UDP_CONNS; i++) {
      uip_udp_periodic(i);
      if (uip_len > 0) {
        uip_arp_out();
        ne2000_send(uip_buf, uip_len);
      }
    }
#endif
  }

  if (timer_expired(&arp_timer)) {
    timer_reset(&arp_timer);
    uip_arp_timer();
  }
}
