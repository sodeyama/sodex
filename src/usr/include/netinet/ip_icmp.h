#ifndef _NETINET_IP_ICMP_H
#define _NETINET_IP_ICMP_H

#include <sys/types.h>

#define ICMP_ECHO       8
#define ICMP_ECHOREPLY  0

struct icmp_hdr {
    u_int8_t  type;
    u_int8_t  code;
    u_int16_t checksum;
    u_int16_t id;
    u_int16_t sequence;
};

#endif
