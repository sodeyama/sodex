#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>

#define AF_INET     2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_ICMP 1

typedef u_int32_t socklen_t;

struct sockaddr {
    u_int16_t sa_family;
    char      sa_data[14];
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int send_msg(int sockfd, const void *buf, int len, int flags);
int recv_msg(int sockfd, void *buf, int len, int flags);
int sendto(int sockfd, const void *buf, int len, int flags,
           const struct sockaddr *addr, socklen_t addrlen);
int recvfrom(int sockfd, void *buf, int len, int flags,
             struct sockaddr *addr, socklen_t *addrlen);
int closesocket(int sockfd);

#endif
