#ifndef _NETWORK_CONFIG_H
#define _NETWORK_CONFIG_H

#include <sodex/const.h>
#include <sys/types.h>

struct sockaddr_in;

#define SODEX_NET_HOST_IP0      10
#define SODEX_NET_HOST_IP1      0
#define SODEX_NET_HOST_IP2      2
#define SODEX_NET_HOST_IP3      15

#define SODEX_NET_NETMASK_IP0   255
#define SODEX_NET_NETMASK_IP1   255
#define SODEX_NET_NETMASK_IP2   255
#define SODEX_NET_NETMASK_IP3   0

#define SODEX_NET_GATEWAY_IP0   10
#define SODEX_NET_GATEWAY_IP1   0
#define SODEX_NET_GATEWAY_IP2   2
#define SODEX_NET_GATEWAY_IP3   2

#define SODEX_NET_TEST_PEER_IP0 10
#define SODEX_NET_TEST_PEER_IP1 0
#define SODEX_NET_TEST_PEER_IP2 2
#define SODEX_NET_TEST_PEER_IP3 100

#define SODEX_NET_MAC0          0x52
#define SODEX_NET_MAC1          0x54
#define SODEX_NET_MAC2          0x00
#define SODEX_NET_MAC3          0x12
#define SODEX_NET_MAC4          0x34
#define SODEX_NET_MAC5          0x56

#define SODEX_NET_TEST_TCP_PORT 7777
#define SODEX_NET_TEST_UDP_PORT 7778

PUBLIC void network_apply_default_config(void);
PUBLIC void network_fill_gateway_addr(struct sockaddr_in *addr, u_int16_t port);
PUBLIC void network_fill_test_peer_addr(struct sockaddr_in *addr, u_int16_t port);

#endif
