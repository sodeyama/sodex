#ifndef _DNS_H
#define _DNS_H

#include <sys/types.h>

/* ---- Configuration ---- */
#define DNS_SERVER_IP     "10.0.2.3"   /* QEMU user-mode DNS */
#define DNS_PORT          53
#define DNS_MAX_NAME      128
#define DNS_CACHE_SIZE    8
#define DNS_TIMEOUT_MS    3000         /* 3 second timeout */
#define DNS_RETRY_COUNT   2
#define DNS_MAX_PACKET    512          /* Standard DNS max without EDNS0 */

/* ---- Error codes ---- */
#define DNS_OK            0
#define DNS_ERR_TIMEOUT  (-1)
#define DNS_ERR_NXDOMAIN (-2)
#define DNS_ERR_FORMAT   (-3)
#define DNS_ERR_SEND     (-4)
#define DNS_ERR_TOOLONG  (-5)
#define DNS_ERR_SOCKET   (-6)

/* ---- Result structure ---- */
struct dns_result {
    u_int8_t  addr[4];       /* Resolved IPv4 address */
    u_int32_t ttl;           /* TTL in seconds */
    int       error;         /* 0 = success */
};

/* ---- API ---- */

/* Resolve hostname to IPv4. Blocking. Cache-aware. */
int dns_resolve(const char *hostname, struct dns_result *out);

/* Clear DNS cache */
void dns_cache_clear(void);

/* ---- Internal (for unit testing) ---- */

/* Build DNS query packet. Returns length, or negative error. */
int dns_build_query(const char *hostname, u_int16_t txid,
                    u_int8_t *buf, int buf_cap);

/* Parse DNS response. Returns 0 on success, negative error. */
int dns_parse_response(const u_int8_t *buf, int len,
                       u_int16_t expected_txid,
                       struct dns_result *out);

/* Encode hostname to DNS label format. Returns length including terminator. */
int dns_encode_labels(const char *hostname, u_int8_t *buf, int buf_cap);

#endif /* _DNS_H */
