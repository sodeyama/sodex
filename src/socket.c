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
    struct uip_conn *conn = uip_connect(&ipaddr, addr->sin_port);
    if (!conn) return -1;
    conn->appstate = sockfd;
    sk->tcp_conn = conn;
    sk->state = SOCK_STATE_CONNECTED;
    /* Block until connected */
    sleep_on(&sk->connect_wq);
    return 0;
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

/* htons/htonl for kernel use */
PRIVATE u_int16_t k_htons(u_int16_t h)
{
  return ((h & 0xff) << 8) | ((h >> 8) & 0xff);
}

PRIVATE u_int32_t k_htonl(u_int32_t h)
{
  return ((h & 0xff) << 24) | ((h & 0xff00) << 8) |
         ((h >> 8) & 0xff00) | ((h >> 24) & 0xff);
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

    /* uip_arp_out() either:
     * 1. Resolved MAC and updated uip_buf ethernet header -> send uip_buf
     * 2. Replaced uip_buf with ARP request -> send ARP first, then retry
     */
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
    /* TODO: send via uip_udp_send in next periodic */
    return len;
  }

  if (sk->type == SOCK_STREAM && sk->tcp_conn) {
    /* TCP send via uIP */
    uip_send(buf, len);
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

  /* If no data, block */
  if (sk->rx_len == 0) {
    sleep_on_timeout(&sk->recv_wq, sk->timeout_ticks);
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

  if (sk->tcp_conn) {
    uip_close();
  }
  if (sk->udp_conn) {
    uip_udp_remove(sk->udp_conn);
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
