#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <sys/types.h>

#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_ICMP 1

struct in_addr {
    u_int32_t s_addr;
};

struct sockaddr_in {
    u_int16_t      sin_family;
    u_int16_t      sin_port;
    struct in_addr sin_addr;
};

u_int16_t htons(u_int16_t hostshort);
u_int16_t ntohs(u_int16_t netshort);
u_int32_t htonl(u_int32_t hostlong);
u_int32_t ntohl(u_int32_t netlong);

#endif
