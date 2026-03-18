/*
 *  @File socket.c
 *  @Brief POSIX-like socket implementation for Sodex kernel
 */

#include <sodex/const.h>
#include <sys/types.h>
#include <string.h>
#include <io.h>
#include <vga.h>
#include <poll.h>
#include <process.h>
#include <socket.h>
#include <uip.h>
#include <uip_arp.h>
#include <ne2000.h>

EXTERN volatile u_int32_t kernel_tick;
EXTERN void *uip_sappdata;
EXTERN u16_t uip_slen;
EXTERN void network_poll(void);

PUBLIC struct kern_socket socket_table[MAX_SOCKETS];

PRIVATE u_int8_t icmp_sendbuf[1500];

PRIVATE int socket_tcp_send_limit(struct kern_socket *sk)
{
  int limit = SOCK_TXBUF_SIZE;

  if (sk != 0 && sk->tcp_conn != 0 &&
      sk->tcp_conn->mss > 0 &&
      sk->tcp_conn->mss < limit)
    limit = sk->tcp_conn->mss;
  return limit;
}

PRIVATE u_int16_t socket_debug_host_port(u_int16_t port)
{
  return (u_int16_t)(((port & 0x00ffU) << 8) |
                     ((port & 0xff00U) >> 8));
}

PRIVATE void socket_dbg_putc(char c)
{
  while (!(in8(0x3F8 + 5) & 0x20))
    ;
  out8(0x3F8, c);
}

PRIVATE void socket_dbg_puts(const char *s)
{
  while (*s) {
    if (*s == '\n')
      socket_dbg_putc('\r');
    socket_dbg_putc(*s++);
  }
}

PRIVATE void socket_dbg_dec(int value)
{
  char buf[12];
  int len = 0;

  if (value < 0) {
    socket_dbg_putc('-');
    value = -value;
  }
  if (value == 0) {
    socket_dbg_putc('0');
    return;
  }
  while (value > 0 && len < (int)sizeof(buf)) {
    buf[len++] = (char)('0' + (value % 10));
    value /= 10;
  }
  while (len > 0)
    socket_dbg_putc(buf[--len]);
}

PRIVATE void socket_dbg_hex16(u_int16_t value)
{
  int shift;

  for (shift = 12; shift >= 0; shift -= 4) {
    int digit = (value >> shift) & 0x0f;

    if (digit < 10)
      socket_dbg_putc((char)('0' + digit));
    else
      socket_dbg_putc((char)('A' + digit - 10));
  }
}

PRIVATE void socket_dbg_listen_event(const char *label, int sockfd,
                                     u_int16_t port)
{
  socket_dbg_puts("TCP: ");
  socket_dbg_puts(label);
  socket_dbg_puts(" sockfd=");
  socket_dbg_dec(sockfd);
  socket_dbg_puts(" port=");
  socket_dbg_dec((int)socket_debug_host_port(port));
  socket_dbg_puts(" raw=0x");
  socket_dbg_hex16(port);
  socket_dbg_puts("\n");
}

PRIVATE void socket_dbg_listener_miss(u_int16_t port)
{
  socket_dbg_puts("TCP: LISTENER MISS port=");
  socket_dbg_dec((int)socket_debug_host_port(port));
  socket_dbg_puts(" raw=0x");
  socket_dbg_hex16(port);
  socket_dbg_puts("\n");
}

PRIVATE void socket_dbg_accept_event(const char *label, int listener_fd,
                                     int child_fd, int backlog_count)
{
  socket_dbg_puts("TCP: ");
  socket_dbg_puts(label);
  socket_dbg_puts(" listener=");
  socket_dbg_dec(listener_fd);
  socket_dbg_puts(" child=");
  socket_dbg_dec(child_fd);
  socket_dbg_puts(" backlog=");
  socket_dbg_dec(backlog_count);
  socket_dbg_puts("\n");
}

PRIVATE int socket_dbg_is_signer_port(u_int16_t port)
{
  return socket_debug_host_port(port) == 10026;
}

PRIVATE void socket_dbg_udp_event(const char *label, int sockfd,
                                  u_int16_t lport, u_int16_t rport,
                                  u_int16_t len)
{
  socket_dbg_puts("UDP: ");
  socket_dbg_puts(label);
  socket_dbg_puts(" sockfd=");
  socket_dbg_dec(sockfd);
  socket_dbg_puts(" lport=");
  socket_dbg_dec((int)socket_debug_host_port(lport));
  socket_dbg_puts(" rport=");
  socket_dbg_dec((int)socket_debug_host_port(rport));
  socket_dbg_puts(" len=");
  socket_dbg_dec((int)len);
  socket_dbg_puts("\n");
}

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

PRIVATE void socket_init_entry(struct kern_socket *sk)
{
  int i;

  memset(sk, 0, sizeof(struct kern_socket));
  sk->state = SOCK_STATE_CREATED;
  sk->timeout_ticks = 500;
  sk->parent_fd = -1;
  for (i = 0; i < SOCK_ACCEPT_BACKLOG_SIZE; i++) {
    sk->backlog_fds[i] = -1;
  }
  rxbuf_init(sk);
}

PRIVATE int socket_alloc_entry(void)
{
  int i;

  for (i = 0; i < MAX_SOCKETS; i++) {
    if (socket_table[i].state == SOCK_STATE_UNUSED) {
      socket_init_entry(&socket_table[i]);
      return i;
    }
  }
  return -1;
}

PRIVATE int socket_backlog_enqueue(struct kern_socket *listener, int child_fd)
{
  if (listener == 0) return -1;
  if (listener->backlog_limit <= 0) return -1;
  if (listener->backlog_count >= listener->backlog_limit) return -1;
  if (listener->backlog_count >= SOCK_ACCEPT_BACKLOG_SIZE) return -1;

  listener->backlog_fds[listener->backlog_count++] = child_fd;
  poll_notify_all();
  return 0;
}

PRIVATE int socket_backlog_dequeue(struct kern_socket *listener)
{
  int child_fd;
  int i;

  if (listener == 0) return -1;
  if (listener->backlog_count <= 0) return -1;

  child_fd = listener->backlog_fds[0];
  for (i = 1; i < listener->backlog_count; i++) {
    listener->backlog_fds[i - 1] = listener->backlog_fds[i];
  }
  listener->backlog_count--;
  listener->backlog_fds[listener->backlog_count] = -1;
  return child_fd;
}

PRIVATE void socket_backlog_remove_fd(struct kern_socket *listener, int child_fd)
{
  int i;

  if (listener == 0) return;
  for (i = 0; i < listener->backlog_count; i++) {
    if (listener->backlog_fds[i] == child_fd) {
      int j;
      for (j = i + 1; j < listener->backlog_count; j++) {
        listener->backlog_fds[j - 1] = listener->backlog_fds[j];
      }
      listener->backlog_count--;
      listener->backlog_fds[listener->backlog_count] = -1;
      return;
    }
  }
}

PRIVATE int socket_find_listener_by_port(u_int16_t port)
{
  int i;

  for (i = 0; i < MAX_SOCKETS; i++) {
    struct kern_socket *sk = &socket_table[i];
    if (sk->state == SOCK_STATE_LISTENING &&
        sk->type == SOCK_STREAM &&
        sk->local_addr.sin_port == port) {
      return i;
    }
  }
  return -1;
}

PRIVATE void socket_fill_addr_from_uip(struct sockaddr_in *addr,
                                       u_int16_t port,
                                       uip_ipaddr_t *ipaddr)
{
  u_int8_t *raw;

  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = port;
  raw = (u_int8_t *)&addr->sin_addr;
  raw[0] = uip_ipaddr1(ipaddr);
  raw[1] = uip_ipaddr2(ipaddr);
  raw[2] = uip_ipaddr3(ipaddr);
  raw[3] = uip_ipaddr4(ipaddr);
}

PRIVATE void socket_release_entry(int sockfd)
{
  struct kern_socket *sk = &socket_table[sockfd];

  if (sk->tcp_conn) {
    sk->tcp_conn->appstate = -1;
  }

  memset(sk, 0, sizeof(struct kern_socket));
  sk->state = SOCK_STATE_UNUSED;
  sk->parent_fd = -1;
}

PUBLIC int kern_socket(int domain, int type, int protocol)
{
  if (domain != AF_INET) return -1;

  int sockfd = socket_alloc_entry();
  if (sockfd < 0) return -1;

  struct kern_socket *sk = &socket_table[sockfd];
  sk->type = type;
  sk->protocol = protocol;
  if (type == SOCK_RAW && protocol == 0)
    sk->protocol = IPPROTO_ICMP;
  return sockfd;
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
  if (sk->state != SOCK_STATE_BOUND && sk->state != SOCK_STATE_CREATED)
    return -1;
  if (sk->local_addr.sin_port == 0) return -1;

  if (backlog <= 0) backlog = 1;
  if (backlog > SOCK_ACCEPT_BACKLOG_SIZE) backlog = SOCK_ACCEPT_BACKLOG_SIZE;

  sk->state = SOCK_STATE_LISTENING;
  sk->backlog_count = 0;
  sk->backlog_limit = backlog;
  {
    int i;
    for (i = 0; i < SOCK_ACCEPT_BACKLOG_SIZE; i++) {
      sk->backlog_fds[i] = -1;
    }
  }
  uip_listen(sk->local_addr.sin_port);
  socket_dbg_listen_event("LISTEN", sockfd, sk->local_addr.sin_port);
  return 0;
}

PUBLIC int socket_try_accept(int sockfd, struct sockaddr_in *addr)
{
  int child_fd;
  struct kern_socket *listener;
  struct kern_socket *child;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  listener = &socket_table[sockfd];
  if (listener->state != SOCK_STATE_LISTENING) return -1;
  if (listener->backlog_count > 0) {
    socket_dbg_accept_event("ACCEPT TRY", sockfd,
                            listener->backlog_fds[0],
                            listener->backlog_count);
  }

  for (;;) {
    child_fd = socket_backlog_dequeue(listener);
    if (child_fd < 0) return -1;

    child = &socket_table[child_fd];
    if (child->state == SOCK_STATE_CLOSED) {
      socket_dbg_accept_event("ACCEPT DROP", sockfd, child_fd,
                              listener->backlog_count);
      child->pending_accept = 0;
      child->parent_fd = -1;
      kern_close_socket(child_fd);
      continue;
    }
    child->pending_accept = 0;
    child->parent_fd = -1;
    if (addr) {
      *addr = child->remote_addr;
    }
    socket_dbg_accept_event("ACCEPT DEQUEUE", sockfd, child_fd,
                            listener->backlog_count);
    wakeup(&listener->accept_wq);
    return child_fd;
  }
}

PUBLIC int kern_accept(int sockfd, struct sockaddr_in *addr)
{
  struct kern_socket *listener;
  u_int32_t deadline;
  int child_fd;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  listener = &socket_table[sockfd];
  if (listener->state != SOCK_STATE_LISTENING) return -1;

  deadline = kernel_tick + listener->timeout_ticks;
  for (;;) {
    disableInterrupt();
    child_fd = socket_try_accept(sockfd, addr);
    enableInterrupt();
    if (child_fd >= 0)
      return child_fd;

    socket_dbg_puts("TCP: ACCEPT POLL\n");
    disableInterrupt();
    network_poll();
    enableInterrupt();
    socket_dbg_puts("TCP: ACCEPT POLL DONE\n");

    if ((int)(kernel_tick - deadline) >= 0)
      return -1;
  }
}

PUBLIC int socket_bind_inbound_tcp(struct uip_conn *conn)
{
  int listener_fd;
  int child_fd;
  struct kern_socket *listener;
  struct kern_socket *child;

  if (conn == 0) return -1;

  listener_fd = socket_find_listener_by_port(conn->lport);
  if (listener_fd < 0) {
    socket_dbg_listener_miss(conn->lport);
    return -1;
  }

  listener = &socket_table[listener_fd];
  if (listener->backlog_count >= listener->backlog_limit)
    return -1;

  child_fd = socket_alloc_entry();
  if (child_fd < 0)
    return -1;

  child = &socket_table[child_fd];
  child->type = SOCK_STREAM;
  child->protocol = IPPROTO_TCP;
  child->state = SOCK_STATE_CONNECTED;
  child->local_addr = listener->local_addr;
  socket_fill_addr_from_uip(&child->remote_addr, conn->rport, &conn->ripaddr);
  child->tcp_conn = conn;
  child->parent_fd = listener_fd;
  child->pending_accept = 1;
  child->timeout_ticks = listener->timeout_ticks;

  if (socket_backlog_enqueue(listener, child_fd) < 0) {
    socket_release_entry(child_fd);
    return -1;
  }

  socket_dbg_accept_event("ACCEPT ENQUEUE", listener_fd, child_fd,
                          listener->backlog_count);
  conn->appstate = child_fd;
  wakeup(&listener->accept_wq);
  poll_notify_all();
  return child_fd;
}

PUBLIC int kern_connect(int sockfd, struct sockaddr_in *addr)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return SOCK_ERR_BAD_STATE;
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
      return SOCK_ERR_NO_SOCKET;
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

    /* Poll network until connected or timeout (PIT tick based).
     * The sequence is: ARP request → ARP reply → SYN → SYN-ACK → CONNECTED.
     * We need periodic processing to retransmit SYN after ARP resolves. */
    {
      EXTERN void network_poll(void);
      u_int32_t deadline = kernel_tick + TCP_CONNECT_TIMEOUT_TICKS;
      u_int32_t next_retry = kernel_tick + 50; /* retry SYN every 500ms */
      int syn_retry = 0;

      while ((int)(kernel_tick - deadline) < 0) {
        disableInterrupt();
        network_poll();
        enableInterrupt();

        if (sk->state == SOCK_STATE_CONNECTED)
          return 0;
        if (sk->state == SOCK_STATE_CLOSED) {
          /* Distinguish RST (refused) from other close reasons */
          if (sk->error == SOCK_ERR_REFUSED)
            return SOCK_ERR_REFUSED;
          return SOCK_ERR_REFUSED;
        }

        /* Periodically retrigger SYN in case first was replaced by ARP */
        if ((int)(kernel_tick - next_retry) >= 0 && syn_retry < 10) {
          disableInterrupt();
          uip_periodic_conn(conn);
          if (uip_len > 0) {
            uip_arp_out();
            ne2000_send(uip_buf, uip_len);
          }
          enableInterrupt();
          syn_retry++;
          next_retry = kernel_tick + 50; /* next retry in 500ms */
        }
      }
    }
    return SOCK_ERR_TIMEOUT;
  } else if (sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) {
    sk->state = SOCK_STATE_CONNECTED;
    return 0;
  }
  return SOCK_ERR_BAD_STATE;
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

    /* Source IP: 現在のホスト設定をそのまま使う */
    ip[12] = uip_ipaddr1(&uip_hostaddr);
    ip[13] = uip_ipaddr2(&uip_hostaddr);
    ip[14] = uip_ipaddr3(&uip_hostaddr);
    ip[15] = uip_ipaddr4(&uip_hostaddr);

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
    if (socket_dbg_is_signer_port(sk->udp_conn->rport))
      socket_dbg_udp_event("SEND", sockfd, sk->udp_conn->lport,
                           sk->udp_conn->rport, (u_int16_t)len);
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
    /* TCP send: split into MSS-sized chunks.
     * uIP は 1 セグメントずつ ACK を待つ前提なので、
     * 前回送信分の ACK が返るまでは tx_buf を再利用しない。 */
    u_int8_t *src = (u_int8_t *)buf;
    int total_sent = 0;

    while (total_sent < len) {
      int chunk = len - total_sent;
      int send_limit;
      u_int32_t deadline;

      send_limit = socket_tcp_send_limit(sk);
      if (chunk > send_limit)
        chunk = send_limit;

      /* 前回分が ACK 済みになるまで待つ。 */
      deadline = kernel_tick + sk->timeout_ticks;
      while (sk->tx_pending ||
             (sk->tcp_conn != 0 && uip_outstanding(sk->tcp_conn))) {
        disableInterrupt();
        network_poll();
        enableInterrupt();
        if ((int)(kernel_tick - deadline) >= 0)
          return total_sent > 0 ? total_sent : SOCK_ERR_TIMEOUT;
        if (sk->state == SOCK_STATE_CLOSED || sk->tcp_conn == 0)
          return total_sent > 0 ? total_sent : SOCK_ERR_REFUSED;
      }

      disableInterrupt();
      memcpy(sk->tx_buf, src + total_sent, chunk);
      sk->tx_len = chunk;
      sk->tx_pending = 1;
      enableInterrupt();

      total_sent += chunk;
    }

    /* 最後のチャンクも ACK 済みになるまで待ってから返す。 */
    {
      u_int32_t deadline = kernel_tick + sk->timeout_ticks;

      while (sk->tx_pending ||
             (sk->tcp_conn != 0 && uip_outstanding(sk->tcp_conn))) {
        disableInterrupt();
        network_poll();
        enableInterrupt();
        if ((int)(kernel_tick - deadline) >= 0)
          return total_sent > 0 ? total_sent : SOCK_ERR_TIMEOUT;
        if (sk->state == SOCK_STATE_CLOSED || sk->tcp_conn == 0)
          return total_sent > 0 ? total_sent : SOCK_ERR_REFUSED;
      }
    }
    return total_sent;
  }

  return -1;
}

PUBLIC int kern_recvfrom(int sockfd, void *buf, int len, int flags,
                        struct sockaddr_in *addr)
{
  int signer = FALSE;
  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  struct kern_socket *sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return -1;
  if (sk->type == SOCK_DGRAM && sk->udp_conn != 0)
    signer = socket_dbg_is_signer_port(sk->udp_conn->rport);

  /* If no data, poll NE2000 directly (PIT tick based timeout) */
  if (sk->rx_len == 0) {
    EXTERN void network_poll(void);
    u_int32_t deadline = kernel_tick + sk->timeout_ticks;
    int close_polls = 0;

    while ((int)(kernel_tick - deadline) < 0) {
      disableInterrupt();
      network_poll();
      enableInterrupt();
      if (sk->rx_len > 0)
        break;
      if (sk->state == SOCK_STATE_CLOSED) {
        /* FIN arrived but data may still be queued in uIP.
         * Poll a few more times to drain any remaining segments. */
        if (++close_polls > 20)
          break;
      }
    }
    if (signer && sk->rx_len > 0)
      socket_dbg_udp_event("KRECV-HAVE", sockfd, sk->udp_conn->lport,
                           sk->udp_conn->rport, sk->rx_len);
    if (sk->rx_len == 0)
      return 0; /* timeout or closed with no data */
  }

  disableInterrupt();
  int ret = rxbuf_read(sk, (u_int8_t *)buf, len, addr);
  enableInterrupt();
  if (signer)
    socket_dbg_udp_event("KRECV-RET", sockfd, sk->udp_conn->lport,
                         sk->udp_conn->rport, (u_int16_t)ret);

  return ret;
}

PUBLIC int socket_begin_close(int sockfd)
{
  struct kern_socket *sk;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return -1;

  if (sk->tcp_conn) {
    /* FIN も pending だけ立てて periodic 側で流す。 */
    disableInterrupt();
    sk->close_pending = 1;
    enableInterrupt();
    return 0;
  }

  if (sk->udp_conn) {
    uip_udp_remove(sk->udp_conn);
    sk->udp_conn = 0;
  }

  sk->state = SOCK_STATE_CLOSED;
  wakeup(&sk->recv_wq);
  wakeup(&sk->accept_wq);
  wakeup(&sk->connect_wq);
  poll_notify_all();
  return 0;
}

PUBLIC int kern_setsockopt(int sockfd, int level, int optname,
                          const void *optval, int optlen)
{
  struct kern_socket *sk;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  sk = &socket_table[sockfd];
  if (sk->state == SOCK_STATE_UNUSED) return -1;

  if (level == SOL_SOCKET && optname == SO_RCVTIMEO) {
    if (optlen < (int)sizeof(u_int32_t) || optval == 0)
      return -1;
    /* optval is timeout in milliseconds; convert to PIT ticks (1 tick = 10ms) */
    u_int32_t ms = *(const u_int32_t *)optval;
    sk->timeout_ticks = ms / 10;
    if (sk->timeout_ticks == 0 && ms > 0)
      sk->timeout_ticks = 1;
    return 0;
  }

  return -1;  /* unsupported option */
}

PUBLIC int kern_close_socket(int sockfd)
{
  struct kern_socket *sk;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  sk = &socket_table[sockfd];
  EXTERN void network_poll(void);

  if (sk->state == SOCK_STATE_UNUSED)
    return 0;

  if (sk->state == SOCK_STATE_LISTENING && sk->local_addr.sin_port != 0) {
    int pending[SOCK_ACCEPT_BACKLOG_SIZE];
    int pending_count = sk->backlog_count;
    int i;

    for (i = 0; i < pending_count; i++) {
      pending[i] = sk->backlog_fds[i];
    }
    sk->backlog_count = 0;
    sk->backlog_limit = 0;
    for (i = 0; i < SOCK_ACCEPT_BACKLOG_SIZE; i++) {
      sk->backlog_fds[i] = -1;
    }
    socket_dbg_listen_event("UNLISTEN", sockfd, sk->local_addr.sin_port);
    uip_unlisten(sk->local_addr.sin_port);
    for (i = 0; i < pending_count; i++) {
      int child_fd = pending[i];
      if (child_fd >= 0 && child_fd < MAX_SOCKETS) {
        socket_table[child_fd].parent_fd = -1;
        socket_table[child_fd].pending_accept = 0;
        kern_close_socket(child_fd);
      }
    }
  }

  if (sk->parent_fd >= 0 && sk->parent_fd < MAX_SOCKETS) {
    socket_backlog_remove_fd(&socket_table[sk->parent_fd], sockfd);
    sk->parent_fd = -1;
    sk->pending_accept = 0;
  }

  if (sk->state == SOCK_STATE_CLOSED) {
    wakeup(&sk->recv_wq);
    wakeup(&sk->accept_wq);
    wakeup(&sk->connect_wq);
    socket_release_entry(sockfd);
    return 0;
  }

  if (sk->tcp_conn) {
    u_int32_t deadline = kernel_tick + TCP_CLOSE_TIMEOUT_TICKS;

    socket_begin_close(sockfd);

    while ((int)(kernel_tick - deadline) < 0) {
      disableInterrupt();
      network_poll();
      enableInterrupt();

      /* close wait 中は stack 上の sk を持ち回らず毎回引き直す。 */
      if (socket_table[sockfd].state == SOCK_STATE_CLOSED ||
          socket_table[sockfd].state == SOCK_STATE_UNUSED)
        break;
    }
    sk = &socket_table[sockfd];
  }
  if (sk->state == SOCK_STATE_UNUSED)
    return 0;
  if (sk->udp_conn) {
    uip_udp_remove(sk->udp_conn);
  }

  /* Wake up any blocked waiters */
  wakeup(&sk->recv_wq);
  wakeup(&sk->accept_wq);
  wakeup(&sk->connect_wq);
  poll_notify_all();

  socket_release_entry(sockfd);
  return 0;
}

PUBLIC int rxbuf_read_direct(int sockfd, u_int8_t *buf, u_int16_t maxlen,
                             struct sockaddr_in *from)
{
  struct kern_socket *sk;
  int signer = FALSE;
  u_int16_t lport = 0;
  u_int16_t rport = 0;
  int ret;

  if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;
  sk = &socket_table[sockfd];
  if (sk->type == SOCK_DGRAM && sk->udp_conn != 0) {
    lport = sk->udp_conn->lport;
    rport = sk->udp_conn->rport;
    signer = socket_dbg_is_signer_port(rport);
  }
  if (signer && sk->rx_len > 0)
    socket_dbg_udp_event("DIRECT-HAVE", sockfd, lport, rport, sk->rx_len);

  ret = rxbuf_read(sk, buf, maxlen, from);
  if (signer && ret > 0)
    socket_dbg_udp_event("DIRECT-READ", sockfd, lport, rport, (u_int16_t)ret);
  return ret;
}

PUBLIC void socket_service_pending_tcp(void)
{
  int i;

  for (i = 0; i < MAX_SOCKETS; i++) {
    struct kern_socket *sk = &socket_table[i];

    if (sk->state == SOCK_STATE_UNUSED)
      continue;
    if (sk->type != SOCK_STREAM || sk->tcp_conn == 0)
      continue;
    if (sk->tcp_conn->appstate != i)
      continue;
    if (!sk->tx_pending && !sk->close_pending)
      continue;

    /* server handler 完了後にだけ appcall を回して pending を flush する */
    uip_poll_conn(sk->tcp_conn);
    if (uip_len > 0) {
      uip_arp_out();
      ne2000_send(uip_buf, uip_len);
    }
  }
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
      poll_notify_all();
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
      if (socket_dbg_is_signer_port(udp_conn->rport))
        socket_dbg_udp_event("RECV", i, udp_conn->lport, udp_conn->rport, len);
      rxbuf_write(sk, data, len, &from);
      if (socket_dbg_is_signer_port(udp_conn->rport))
        socket_dbg_udp_event("RECVBUF", i, udp_conn->lport, udp_conn->rport,
                             sk->rx_len);
      wakeup(&sk->recv_wq);
      poll_notify_all();
      return;
    }
  }
  if (udp_conn != 0 && socket_dbg_is_signer_port(udp_conn->rport))
    socket_dbg_udp_event("DROP", -1, udp_conn->lport, udp_conn->rport, len);
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
  poll_notify_all();
}
