/*
 *  @File socket.c
 *  @Brief POSIX-like socket implementation for Sodex kernel
 */

#include <sodex/const.h>
#include <sys/types.h>
#include <string.h>
#include <io.h>
#include <vga.h>
#include <process.h>
#include <socket.h>
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>

EXTERN volatile u_int32_t kernel_tick;
EXTERN void *uip_sappdata;
EXTERN u16_t uip_slen;

PUBLIC struct kern_socket socket_table[MAX_SOCKETS];

PRIVATE u_int8_t icmp_sendbuf[1500];

PRIVATE void rxbuf_init(struct kern_socket *sk)
{
  sk->rx_head = 0;
  sk->rx_tail = 0;
  sk->rx_len = 0;
  sk->rx_pkt_count = 0;
}

PRIVATE int rxbuf_write(struct kern_socket *sk, u_int8_t *data, u_int16_t len,
                        struct sockaddr_in *from)
{
  if (len == 0) return 0;
  if (sk->rx_len + len > SOCK_RXBUF_SIZE) return -1;

  int i;
  for (i = 0; i < len; i++) {
    sk->rx_buf[sk->rx_head] = data[i];
    sk->rx_head = (sk->rx_head + 1) % SOCK_RXBUF_SIZE;
  }
  sk->rx_len += len;

  /* Record packet boundary and source for recvfrom */
  if (sk->rx_pkt_count < 16) {
    if (from) {
      sk->rx_from[sk->rx_pkt_count] = *from;
    } else {
      memset(&sk->rx_from[sk->rx_pkt_count], 0, sizeof(struct sockaddr_in));
    }
    sk->rx_pkt_boundary[sk->rx_pkt_count] = len;
    sk->rx_pkt_count++;
  }
  return len;
}

PRIVATE int rxbuf_read(struct kern_socket *sk, u_int8_t *buf, u_int16_t maxlen,
                       struct sockaddr_in *from)
{
  if (sk->rx_len == 0) return 0;

  /* For raw/dgram sockets, read one packet at a time */
  u_int16_t to_read;
  if ((sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) && sk->rx_pkt_count > 0) {
    to_read = sk->rx_pkt_boundary[0];
    if (to_read > maxlen) to_read = maxlen;
    if (from) {
      *from = sk->rx_from[0];
    }
  } else {
    to_read = sk->rx_len;
    if (to_read > maxlen) to_read = maxlen;
  }

  int i;
  for (i = 0; i < to_read; i++) {
    buf[i] = sk->rx_buf[sk->rx_tail];
    sk->rx_tail = (sk->rx_tail + 1) % SOCK_RXBUF_SIZE;
  }
  sk->rx_len -= to_read;

  /* For raw/dgram, consume the full packet even if we read partial */
  if ((sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) && sk->rx_pkt_count > 0) {
    u_int16_t pkt_len = sk->rx_pkt_boundary[0];
    /* Skip remaining bytes if we read less than packet */
    u_int16_t remaining = pkt_len - to_read;
    for (i = 0; i < remaining; i++) {
      sk->rx_tail = (sk->rx_tail + 1) % SOCK_RXBUF_SIZE;
    }
    sk->rx_len -= remaining;

    /* Shift packet metadata */
    for (i = 1; i < sk->rx_pkt_count; i++) {
      sk->rx_from[i-1] = sk->rx_from[i];
      sk->rx_pkt_boundary[i-1] = sk->rx_pkt_boundary[i];
    }
    sk->rx_pkt_count--;
  }

  return to_read;
}

PUBLIC int kern_socket(int domain, int type, int protocol)
{
  if (domain != AF_INET) return -1;

  int i;
  for (i = 0; i < MAX_SOCKETS; i++) {
    if (socket_table[i].state == SOCK_STATE_UNUSED) {
      struct kern_socket *sk = &socket_table[i];
      memset(sk, 0, sizeof(struct kern_socket));
      sk->state = SOCK_STATE_CREATED;
      sk->type = type;
      sk->protocol = protocol;
      if (type == SOCK_RAW && protocol == 0)
        sk->protocol = IPPROTO_ICMP;
      sk->timeout_ticks = 500; /* 5 seconds default timeout */
      rxbuf_init(sk);
      return i;
    }
  }
  return -1;
}

PUBLIC int kern_bind(int sockfd, struct sockaddr_in *addr)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return -1;

  sk->local_addr = *addr;
  sk->state = SOCK_STATE_BOUND;
  return 0;
}

PUBLIC int kern_listen(int sockfd, int backlog)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];
  if (sk->type != SOCK_STREAM) return -1;

  sk->state = SOCK_STATE_LISTENING;
  uip_listen(sk->local_addr.sin_port);
  return 0;
}

PUBLIC int kern_accept(int sockfd, struct sockaddr_in *addr)
{
  /* TODO: implement for TCP */
  return -1;
}

PUBLIC int kern_connect(int sockfd, struct sockaddr_in *addr)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];

  sk->remote_addr = *addr;

  if (sk->type == SOCK_STREAM) {
    /* TCP connect via uIP */
    uip_ipaddr_t ipaddr;
    u_int8_t *a = (u_int8_t *)&addr->sin_addr;
    uip_ipaddr(&ipaddr, a[0], a[1], a[2], a[3]);

    disableInterrupt();
    struct uip_conn *conn = uip_connect(&ipaddr, addr->sin_port);
    if (!conn) {
      enableInterrupt();
      return -1;
    }
    conn->appstate = sockfd;
    sk->tcp_conn = conn;

    /* Trigger SYN send (may generate ARP request first if MAC unknown) */
    uip_periodic_conn(conn);
    if (uip_len > 0) {
      uip_arp_out();
      ne2000_send(uip_buf, uip_len);
    }
    enableInterrupt();

    /* Poll network until connected or timeout.
     * The sequence is: ARP request → ARP reply → SYN → SYN-ACK → CONNECTED.
     * We need periodic processing to retransmit SYN after ARP resolves. */
    {
      EXTERN void network_poll(void);
      int poll_count;
      int syn_retry = 0;
      for (poll_count = 0; poll_count < 20000000; poll_count++) {
        disableInterrupt();
        network_poll();
        enableInterrupt();

        if (sk->state == SOCK_STATE_CONNECTED)
          return 0;
        if (sk->state == SOCK_STATE_CLOSED)
          return -1;

        /* Periodically retrigger SYN in case first was replaced by ARP */
        if ((poll_count % 500000) == 499999 && syn_retry < 10) {
          disableInterrupt();
          uip_periodic_conn(conn);
          if (uip_len > 0) {
            uip_arp_out();
            ne2000_send(uip_buf, uip_len);
          }
          enableInterrupt();
          syn_retry++;
        }
      }
    }
    return -1; /* timeout */
  } else if (sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) {
    sk->state = SOCK_STATE_CONNECTED;
    return 0;
  }
  return -1;
}

/* Compute ICMP/IP checksum */
PRIVATE u_int16_t ip_checksum(u_int8_t *data, int len)
{
  u_int32_t sum = 0;
  int i;
  for (i = 0; i < len - 1; i += 2) {
    sum += (u_int16_t)((data[i] << 8) | data[i+1]);
  }
  if (len & 1) {
    sum += (u_int16_t)(data[len-1] << 8);
  }
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (u_int16_t)(~sum);
}

PUBLIC int kern_send(int sockfd, void *buf, int len, int flags)
{
  return kern_sendto(sockfd, buf, len, flags, NULL);
}

PUBLIC int kern_recv(int sockfd, void *buf, int len, int flags)
{
  return kern_recvfrom(sockfd, buf, len, flags, NULL);
}

PUBLIC int kern_sendto(int sockfd, void *buf, int len, int flags,
                       struct sockaddr_in *addr)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return -1;

  struct sockaddr_in *dest = addr ? addr : &sk->remote_addr;

  /* Copy user-space sockaddr_in to kernel stack before interrupts can change CR3 */
  struct sockaddr_in dest_copy;
  if (addr) {
    memcpy(&dest_copy, addr, sizeof(struct sockaddr_in));
    dest = &dest_copy;
  }
  if (sk->type == SOCK_RAW && sk->protocol == IPPROTO_ICMP) {
    /* Build raw IP packet with ICMP payload */
    /* User provides: ICMP header + data in buf */
    EXTERN struct uip_eth_addr uip_ethaddr;
    int ip_len = 20 + len;  /* IP header + ICMP payload */
    int total_len = 14 + ip_len;  /* Ethernet + IP + ICMP */

    if (total_len > 1500) return -1;

    disableInterrupt();

    memset(icmp_sendbuf, 0, total_len < 60 ? 60 : total_len);

    /* Ethernet header placeholder */
    memset(icmp_sendbuf, 0xff, 6);  /* dst: broadcast placeholder */
    memcpy(icmp_sendbuf + 6, &uip_ethaddr, 6);  /* src: our MAC */
    icmp_sendbuf[12] = 0x08;
    icmp_sendbuf[13] = 0x00;  /* EtherType: IP */

    /* IP header at offset 14 */
    u_int8_t *ip = icmp_sendbuf + 14;
    ip[0] = 0x45;  /* version=4, ihl=5 */
    ip[1] = 0;     /* TOS */
    ip[2] = (ip_len >> 8) & 0xff;
    ip[3] = ip_len & 0xff;
    ip[4] = 0; ip[5] = 0;  /* identification */
    ip[6] = 0; ip[7] = 0;  /* flags, fragment offset */
    ip[8] = 64;   /* TTL */
    ip[9] = IPPROTO_ICMP;
    ip[10] = 0; ip[11] = 0; /* checksum (calculated below) */

    /* Source IP: our IP */
    ip[12] = 10; ip[13] = 0; ip[14] = 2; ip[15] = 15;

    /* Destination IP */
    u_int8_t *da = (u_int8_t *)&dest->sin_addr;
    ip[16] = da[0]; ip[17] = da[1]; ip[18] = da[2]; ip[19] = da[3];

    /* IP header checksum */
    u_int16_t cksum = ip_checksum(ip, 20);
    ip[10] = (cksum >> 8) & 0xff;
    ip[11] = cksum & 0xff;

    /* ICMP payload at offset 34 */
    memcpy(icmp_sendbuf + 34, buf, len);

    /* Copy to uip_buf for ARP resolution */
    memcpy(uip_buf, icmp_sendbuf, total_len < 60 ? 60 : total_len);
    uip_len = total_len;

    uip_arp_out();

    int send_len = uip_len < 60 ? 60 : uip_len;
    ne2000_send(uip_buf, send_len);

    enableInterrupt();
    return len;
  }

  if (sk->type == SOCK_DGRAM) {
    /* UDP send via uIP */
    if (!sk->udp_conn) {
      uip_ipaddr_t ipaddr;
      u_int8_t *a = (u_int8_t *)&dest->sin_addr;
      uip_ipaddr(&ipaddr, a[0], a[1], a[2], a[3]);
      sk->udp_conn = uip_udp_new(&ipaddr, dest->sin_port);
      if (!sk->udp_conn) return -1;
      if (sk->local_addr.sin_port)
        uip_udp_bind(sk->udp_conn, sk->local_addr.sin_port);
    }
    /* Copy data to uip_appdata and trigger send */
    disableInterrupt();
    if (len > UIP_APPDATA_SIZE) len = UIP_APPDATA_SIZE;
    uip_udp_conn = sk->udp_conn;
    uip_sappdata = uip_appdata = &uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN];
    memcpy(uip_appdata, buf, len);
    uip_slen = len;
    uip_process(UIP_UDP_SEND_CONN);
    if (uip_len > 0) {
      uip_arp_out();
      ne2000_send(uip_buf, uip_len);
    }
    enableInterrupt();
    return len;
  }

  if (sk->type == SOCK_STREAM && sk->tcp_conn) {
    /* TCP send via uIP: buffer data and trigger poll */
    disableInterrupt();
    if (len > SOCK_TXBUF_SIZE) len = SOCK_TXBUF_SIZE;
    memcpy(sk->tx_buf, buf, len);
    sk->tx_len = len;
    sk->tx_pending = 1;
    /* Trigger uip_appcall via poll so uip_send() runs in correct context */
    uip_poll_conn(sk->tcp_conn);
    if (uip_len > 0) {
      uip_arp_out();
      ne2000_send(uip_buf, uip_len);
    }
    enableInterrupt();
    return len;
  }

  return -1;
}

PUBLIC int kern_recvfrom(int sockfd, void *buf, int len, int flags,
                        struct sockaddr_in *addr)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return -1;

  /* If no data, poll NE2000 directly (interrupts disabled to avoid context switch) */
  if (sk->rx_len == 0) {
    EXTERN void network_poll(void);
    int poll_count;
    for (poll_count = 0; poll_count < 5000000; poll_count++) {
      disableInterrupt();
      network_poll();
      enableInterrupt();
      if (sk->rx_len > 0)
        break;
    }
    if (sk->rx_len == 0)
      return 0; /* timeout */
  }

  disableInterrupt();
  int ret = rxbuf_read(sk, (u_int8_t *)buf, len, addr);
  enableInterrupt();

  return ret;
}

PUBLIC int kern_close_socket(int sockfd)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];
  EXTERN void network_poll(void);

  if (sk->tcp_conn) {
    int poll_count;

    /* FIN は appcall コンテキストで送る */
    disableInterrupt();
    sk->close_pending = 1;
    uip_poll_conn(sk->tcp_conn);
    if (uip_len > 0) {
      uip_arp_out();
      ne2000_send(uip_buf, uip_len);
    }
    enableInterrupt();

    for (poll_count = 0; poll_count < 10000000; poll_count++) {
      disableInterrupt();
      network_poll();
      enableInterrupt();

      if (sk->state == SOCK_STATE_CLOSED)
        break;
    }
  }
  if (sk->udp_conn) {
    uip_udp_remove(sk->udp_conn);
  }

  if (sk->tcp_conn) {
    sk->tcp_conn->appstate = -1;
  }

  /* Wake up any blocked waiters */
  wakeup(&sk->recv_wq);
  wakeup(&sk->accept_wq);
  wakeup(&sk->connect_wq);

  memset(sk, 0, sizeof(struct kern_socket));
  sk->state = SOCK_STATE_UNUSED;
  return 0;
}

PUBLIC int rxbuf_read_direct(int sockfd, u_int8_t *buf, u_int16_t maxlen,
                             struct sockaddr_in *from)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  return rxbuf_read(&socket_table[sockfd], buf, maxlen, from);
}

/* Called from netmain.c when an ICMP echo reply is received */
PUBLIC void socket_icmp_input(u_int8_t *pkt, u_int16_t len)
{
  /* pkt points to IP header (after Ethernet header) */
  u_int8_t *ip_hdr = pkt;
  u_int8_t proto = ip_hdr[9];
  if (proto != IPPROTO_ICMP) return;

  int ip_hdr_len = (ip_hdr[0] & 0x0f) * 4;
  u_int8_t *icmp_hdr = ip_hdr + ip_hdr_len;
  u_int8_t icmp_type = icmp_hdr[0];

  /* Only deliver echo reply to raw sockets */
  if (icmp_type != 0) return; /* 0 = ICMP_ECHO_REPLY */

  u_int16_t icmp_len = len - ip_hdr_len;

  struct sockaddr_in from;
  from.sin_family = AF_INET;
  from.sin_port = 0;
  memcpy(&from.sin_addr, ip_hdr + 12, 4); /* source IP */

  int i;
  for (i = 0; i < MAX_SOCKETS; i++) {
    struct kern_socket *sk = &socket_table[i];
    if (sk->state != SOCK_STATE_UNUSED &&
        sk->type == SOCK_RAW &&
        sk->protocol == IPPROTO_ICMP) {
      rxbuf_write(sk, icmp_hdr, icmp_len, &from);
      wakeup(&sk->recv_wq);
    }
  }
}

/* Called from uip_udp_appcall when UDP data is received */
PUBLIC void socket_udp_input(struct uip_udp_conn *udp_conn,
                             u_int8_t *data, u_int16_t len)
{
  int i;
  for (i = 0; i < MAX_SOCKETS; i++) {
    struct kern_socket *sk = &socket_table[i];
    if (sk->state != SOCK_STATE_UNUSED &&
        sk->type == SOCK_DGRAM &&
        sk->udp_conn == udp_conn) {
      /* Build source address from uIP packet */
      struct sockaddr_in from;
      from.sin_family = AF_INET;
      /* Source IP and port are in the current uip_buf */
      EXTERN u_int8_t uip_buf[];
      from.sin_port = ((u_int16_t)uip_buf[UIP_LLH_LEN + 20] << 8) |
                       uip_buf[UIP_LLH_LEN + 21]; /* UDP src port (network order) */
      memcpy(&from.sin_addr, &uip_buf[UIP_LLH_LEN + 12], 4); /* IP src addr */
      rxbuf_write(sk, data, len, &from);
      wakeup(&sk->recv_wq);
      return;
    }
  }
}

/* Called from uip_appcall when TCP data is received */
PUBLIC void socket_tcp_input(int sockfd, u_int8_t *data, u_int16_t len)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return;
  struct kern_socket *sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return;

  struct sockaddr_in from;
  from.sin_family = AF_INET;
  from.sin_port = 0;
  from.sin_addr = 0;

  rxbuf_write(sk, data, len, &from);
  wakeup(&sk->recv_wq);
}
