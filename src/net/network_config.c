#include <sys/types.h>
#include <string.h>
#include <uip.h>
#include <uip_arp.h>
#include <socket.h>
#include <network_config.h>

PRIVATE void network_fill_addr(struct sockaddr_in *addr, u_int16_t port,
                               u_int8_t ip0, u_int8_t ip1,
                               u_int8_t ip2, u_int8_t ip3)
{
  u_int8_t *raw;

  memset(addr, 0, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = port;

  raw = (u_int8_t *)&addr->sin_addr;
  raw[0] = ip0;
  raw[1] = ip1;
  raw[2] = ip2;
  raw[3] = ip3;
}

PUBLIC void network_apply_default_config(void)
{
  uip_ipaddr_t ipaddr;
  struct uip_eth_addr mac = {{
    SODEX_NET_MAC0,
    SODEX_NET_MAC1,
    SODEX_NET_MAC2,
    SODEX_NET_MAC3,
    SODEX_NET_MAC4,
    SODEX_NET_MAC5
  }};

  uip_ipaddr(&ipaddr,
             SODEX_NET_HOST_IP0, SODEX_NET_HOST_IP1,
             SODEX_NET_HOST_IP2, SODEX_NET_HOST_IP3);
  uip_sethostaddr(&ipaddr);

  uip_ipaddr(&ipaddr,
             SODEX_NET_NETMASK_IP0, SODEX_NET_NETMASK_IP1,
             SODEX_NET_NETMASK_IP2, SODEX_NET_NETMASK_IP3);
  uip_setnetmask(&ipaddr);

  uip_ipaddr(&ipaddr,
             SODEX_NET_GATEWAY_IP0, SODEX_NET_GATEWAY_IP1,
             SODEX_NET_GATEWAY_IP2, SODEX_NET_GATEWAY_IP3);
  uip_setdraddr(&ipaddr);

  uip_setethaddr(mac);
}

PUBLIC void network_fill_gateway_addr(struct sockaddr_in *addr, u_int16_t port)
{
  network_fill_addr(addr, port,
                    SODEX_NET_GATEWAY_IP0, SODEX_NET_GATEWAY_IP1,
                    SODEX_NET_GATEWAY_IP2, SODEX_NET_GATEWAY_IP3);
}

PUBLIC void network_fill_test_peer_addr(struct sockaddr_in *addr, u_int16_t port)
{
  network_fill_addr(addr, port,
                    SODEX_NET_TEST_PEER_IP0, SODEX_NET_TEST_PEER_IP1,
                    SODEX_NET_TEST_PEER_IP2, SODEX_NET_TEST_PEER_IP3);
}
