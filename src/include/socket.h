#ifndef _KERNEL_SOCKET_H
#define _KERNEL_SOCKET_H

#include <sodex/const.h>
#include <sys/types.h>
#include <process.h>

#define MAX_SOCKETS      16

#define AF_INET          2

#define SOCK_STREAM      1
#define SOCK_DGRAM       2
#define SOCK_RAW         3

#define IPPROTO_TCP      6
#define IPPROTO_UDP      17
#define IPPROTO_ICMP     1

#define SOCK_STATE_UNUSED     0
#define SOCK_STATE_CREATED    1
#define SOCK_STATE_BOUND      2
#define SOCK_STATE_LISTENING  3
#define SOCK_STATE_CONNECTED  4
#define SOCK_STATE_CLOSED     5

#define SOCK_RXBUF_SIZE  4096

struct sockaddr_in {
    u_int16_t sin_family;
    u_int16_t sin_port;
    u_int32_t sin_addr;
};

struct kern_socket {
    int      state;
    int      type;
    int      protocol;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    /* uIP connection reference */
    struct uip_conn     *tcp_conn;
    struct uip_udp_conn *udp_conn;

    /* Receive ring buffer */
    u_int8_t rx_buf[SOCK_RXBUF_SIZE];
    u_int16_t rx_head;
    u_int16_t rx_tail;
    u_int16_t rx_len;

    /* Source address storage for recvfrom */
    struct sockaddr_in rx_from[16];
    u_int16_t rx_pkt_boundary[16];
    u_int8_t  rx_pkt_count;

    /* Blocking control */
    struct wait_queue *recv_wq;
    struct wait_queue *accept_wq;
    struct wait_queue *connect_wq;

    /* Accept backlog */
    int backlog_fds[4];
    int backlog_count;

    int      error;
    u_int32_t timeout_ticks;
};

PUBLIC int  kern_socket(int domain, int type, int protocol);
PUBLIC int  kern_bind(int sockfd, struct sockaddr_in *addr);
PUBLIC int  kern_listen(int sockfd, int backlog);
PUBLIC int  kern_accept(int sockfd, struct sockaddr_in *addr);
PUBLIC int  kern_connect(int sockfd, struct sockaddr_in *addr);
PUBLIC int  kern_send(int sockfd, void *buf, int len, int flags);
PUBLIC int  kern_recv(int sockfd, void *buf, int len, int flags);
PUBLIC int  kern_sendto(int sockfd, void *buf, int len, int flags, struct sockaddr_in *addr);
PUBLIC int  kern_recvfrom(int sockfd, void *buf, int len, int flags, struct sockaddr_in *addr);
PUBLIC int  kern_close_socket(int sockfd);
PUBLIC void socket_icmp_input(u_int8_t *pkt, u_int16_t len);

PUBLIC struct kern_socket socket_table[MAX_SOCKETS];
PUBLIC int rxbuf_read_direct(int sockfd, u_int8_t *buf, u_int16_t maxlen,
                             struct sockaddr_in *from);

#endif /* _KERNEL_SOCKET_H */
