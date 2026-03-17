/*
 * dns.c - Minimal DNS resolver for freestanding environment
 *
 * UDP-based A record resolver with caching.
 * Uses QEMU SLiRP DNS at 10.0.2.3:53.
 */

#include <dns.h>
#include <string.h>
#include <stdio.h>

#ifndef TEST_BUILD
#include <debug.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

/* ---- DNS header structure ---- */
#define DNS_HDR_SIZE   12
#define DNS_TYPE_A     1
#define DNS_CLASS_IN   1
#define DNS_FLAG_RD    0x0100   /* Recursion Desired */
#define DNS_FLAG_QR    0x8000   /* Response flag */
#define DNS_RCODE_MASK 0x000F

/* ---- Cache ---- */
struct dns_cache_entry {
    char hostname[DNS_MAX_NAME];
    u_int8_t addr[4];
    u_int32_t expire_tick;   /* kernel_tick when entry expires */
    int valid;
};

static struct dns_cache_entry dns_cache[DNS_CACHE_SIZE];
static u_int16_t dns_txid_counter = 0x1234;

/* ---- Helpers ---- */

static u_int16_t read_u16be(const u_int8_t *p)
{
    return ((u_int16_t)p[0] << 8) | p[1];
}

static void write_u16be(u_int8_t *p, u_int16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

static u_int32_t read_u32be(const u_int8_t *p)
{
    return ((u_int32_t)p[0] << 24) | ((u_int32_t)p[1] << 16) |
           ((u_int32_t)p[2] << 8) | p[3];
}

/* ---- Label encoding ---- */

int dns_encode_labels(const char *hostname, u_int8_t *buf, int buf_cap)
{
    int pos = 0;
    const char *p = hostname;

    if (!hostname || !buf || buf_cap < 2)
        return DNS_ERR_TOOLONG;

    while (*p) {
        const char *dot = p;
        int label_len;

        /* Find next dot or end */
        while (*dot && *dot != '.')
            dot++;
        label_len = dot - p;

        if (label_len == 0 || label_len > 63)
            return DNS_ERR_TOOLONG;
        if (pos + 1 + label_len + 1 > buf_cap)
            return DNS_ERR_TOOLONG;

        buf[pos++] = (u_int8_t)label_len;
        memcpy(buf + pos, p, label_len);
        pos += label_len;

        p = dot;
        if (*p == '.')
            p++;
    }

    buf[pos++] = 0;  /* Root label terminator */
    return pos;
}

/* ---- Query builder ---- */

int dns_build_query(const char *hostname, u_int16_t txid,
                    u_int8_t *buf, int buf_cap)
{
    int pos = 0;
    int label_len;

    if (buf_cap < DNS_HDR_SIZE + 4)
        return DNS_ERR_FORMAT;

    /* Header */
    write_u16be(buf + 0, txid);         /* ID */
    write_u16be(buf + 2, DNS_FLAG_RD);  /* Flags: RD=1 */
    write_u16be(buf + 4, 1);            /* QDCOUNT = 1 */
    write_u16be(buf + 6, 0);            /* ANCOUNT = 0 */
    write_u16be(buf + 8, 0);            /* NSCOUNT = 0 */
    write_u16be(buf + 10, 0);           /* ARCOUNT = 0 */
    pos = DNS_HDR_SIZE;

    /* Question: QNAME */
    label_len = dns_encode_labels(hostname, buf + pos, buf_cap - pos - 4);
    if (label_len < 0)
        return label_len;
    pos += label_len;

    /* QTYPE = A (1) */
    write_u16be(buf + pos, DNS_TYPE_A);
    pos += 2;

    /* QCLASS = IN (1) */
    write_u16be(buf + pos, DNS_CLASS_IN);
    pos += 2;

    return pos;
}

/* ---- Response parser ---- */

/* Skip a DNS name (labels or pointers). Returns new offset, or -1 on error. */
static int skip_dns_name(const u_int8_t *buf, int len, int offset)
{
    int jumps = 0;
    int pos = offset;
    int followed_pointer = 0;
    int result_pos = -1;

    while (pos < len) {
        u_int8_t label_len = buf[pos];

        if (label_len == 0) {
            /* Root label - end of name */
            if (!followed_pointer)
                return pos + 1;
            else
                return result_pos;
        }

        if ((label_len & 0xC0) == 0xC0) {
            /* Pointer */
            if (pos + 1 >= len)
                return -1;
            if (!followed_pointer)
                result_pos = pos + 2;  /* Position after the pointer */
            followed_pointer = 1;
            pos = ((label_len & 0x3F) << 8) | buf[pos + 1];
            if (++jumps > 128)
                return -1;  /* Circular reference protection */
            continue;
        }

        if ((label_len & 0xC0) != 0)
            return -1;  /* Invalid label type */

        pos += 1 + label_len;
    }
    return -1;
}

int dns_parse_response(const u_int8_t *buf, int len,
                       u_int16_t expected_txid,
                       struct dns_result *out)
{
    int pos;
    u_int16_t txid, flags, qdcount, ancount;
    u_int16_t rtype, rclass, rdlength;
    u_int32_t ttl;
    int i;

    if (!buf || !out || len < DNS_HDR_SIZE)
        return DNS_ERR_FORMAT;

    /* Parse header */
    txid = read_u16be(buf + 0);
    flags = read_u16be(buf + 2);
    qdcount = read_u16be(buf + 4);
    ancount = read_u16be(buf + 6);

    /* Verify transaction ID */
    if (txid != expected_txid)
        return DNS_ERR_FORMAT;

    /* Check QR bit (must be response) */
    if (!(flags & DNS_FLAG_QR))
        return DNS_ERR_FORMAT;

    /* Check RCODE */
    if ((flags & DNS_RCODE_MASK) == 3)
        return DNS_ERR_NXDOMAIN;
    if ((flags & DNS_RCODE_MASK) != 0)
        return DNS_ERR_FORMAT;

    /* Skip question section */
    pos = DNS_HDR_SIZE;
    for (i = 0; i < qdcount; i++) {
        pos = skip_dns_name(buf, len, pos);
        if (pos < 0 || pos + 4 > len)
            return DNS_ERR_FORMAT;
        pos += 4;  /* Skip QTYPE + QCLASS */
    }

    /* Parse answer section - find first A record */
    for (i = 0; i < ancount; i++) {
        /* Skip NAME */
        pos = skip_dns_name(buf, len, pos);
        if (pos < 0 || pos + 10 > len)
            return DNS_ERR_FORMAT;

        rtype = read_u16be(buf + pos);
        rclass = read_u16be(buf + pos + 2);
        ttl = read_u32be(buf + pos + 4);
        rdlength = read_u16be(buf + pos + 8);
        pos += 10;

        if (pos + rdlength > len)
            return DNS_ERR_FORMAT;

        if (rtype == DNS_TYPE_A && rclass == DNS_CLASS_IN && rdlength == 4) {
            /* Found A record */
            out->addr[0] = buf[pos];
            out->addr[1] = buf[pos + 1];
            out->addr[2] = buf[pos + 2];
            out->addr[3] = buf[pos + 3];
            out->ttl = ttl;
            /* Clamp TTL: min 60s, max 300s */
            if (out->ttl < 60)
                out->ttl = 60;
            if (out->ttl > 300)
                out->ttl = 300;
            out->error = 0;
            return DNS_OK;
        }

        /* Skip RDATA (e.g., CNAME records) */
        pos += rdlength;
    }

    /* No A record found */
    return DNS_ERR_FORMAT;
}

/* ---- Cache ---- */

void dns_cache_clear(void)
{
    memset(dns_cache, 0, sizeof(dns_cache));
}

#ifndef TEST_BUILD

static u_int32_t get_tick(void)
{
    return get_kernel_tick();
}

static int cache_lookup(const char *hostname, struct dns_result *out)
{
    int i;
    u_int32_t now = get_tick();

    for (i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid &&
            strcmp(dns_cache[i].hostname, hostname) == 0) {
            if ((int)(now - dns_cache[i].expire_tick) < 0) {
                /* Cache hit, not expired */
                memcpy(out->addr, dns_cache[i].addr, 4);
                out->ttl = (dns_cache[i].expire_tick - now) / 100;
                out->error = 0;
                debug_printf("[DNS] cache hit: %s -> %d.%d.%d.%d\n",
                            hostname,
                            out->addr[0], out->addr[1],
                            out->addr[2], out->addr[3]);
                return 1;
            } else {
                /* Expired */
                dns_cache[i].valid = 0;
            }
        }
    }
    return 0;
}

static void cache_store(const char *hostname, const struct dns_result *result)
{
    int i;
    int oldest = 0;
    u_int32_t oldest_tick = 0xFFFFFFFF;
    u_int32_t now = get_tick();

    /* Find free slot or oldest entry */
    for (i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            oldest = i;
            break;
        }
        if ((int)(dns_cache[i].expire_tick - oldest_tick) < 0) {
            oldest_tick = dns_cache[i].expire_tick;
            oldest = i;
        }
    }

    strncpy(dns_cache[oldest].hostname, hostname, DNS_MAX_NAME - 1);
    dns_cache[oldest].hostname[DNS_MAX_NAME - 1] = '\0';
    memcpy(dns_cache[oldest].addr, result->addr, 4);
    dns_cache[oldest].expire_tick = now + result->ttl * 100;  /* seconds to ticks */
    dns_cache[oldest].valid = 1;
}

/* ---- Main resolve function ---- */

int dns_resolve(const char *hostname, struct dns_result *out)
{
    u_int8_t query[DNS_MAX_PACKET];
    u_int8_t response[DNS_MAX_PACKET];
    int qlen, rlen;
    int sockfd;
    struct sockaddr_in dns_addr;
    u_int16_t txid;
    int retry;
    u_int32_t timeout;
    int ret;

    if (!hostname || !out)
        return DNS_ERR_FORMAT;

    /* Check cache first */
    if (cache_lookup(hostname, out))
        return DNS_OK;

    /* Build query */
    txid = dns_txid_counter++;
    qlen = dns_build_query(hostname, txid, query, sizeof(query));
    if (qlen < 0) {
        debug_printf("[DNS] query build failed: %d\n", qlen);
        return qlen;
    }

    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        debug_printf("[DNS] socket() failed: %d\n", sockfd);
        return DNS_ERR_SOCKET;
    }

    /* Set recv timeout */
    timeout = DNS_TIMEOUT_MS;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    /* DNS server address */
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(DNS_PORT);
    inet_aton(DNS_SERVER_IP, &dns_addr.sin_addr);

    debug_printf("[DNS] resolving %s via %s ...\n", hostname, DNS_SERVER_IP);

    for (retry = 0; retry <= DNS_RETRY_COUNT; retry++) {
        /* Send query */
        ret = sendto(sockfd, query, qlen, 0,
                    (struct sockaddr *)&dns_addr, sizeof(dns_addr));
        if (ret < 0) {
            debug_printf("[DNS] sendto failed: %d\n", ret);
            continue;
        }

        /* Receive response */
        rlen = recvfrom(sockfd, response, sizeof(response), 0,
                       (struct sockaddr *)0, (u_int32_t *)0);
        if (rlen <= 0) {
            debug_printf("[DNS] recvfrom timeout (retry %d/%d)\n",
                        retry, DNS_RETRY_COUNT);
            continue;
        }

        /* Parse response */
        ret = dns_parse_response(response, rlen, txid, out);
        if (ret == DNS_OK) {
            debug_printf("[DNS] resolved %s -> %d.%d.%d.%d (TTL=%u)\n",
                        hostname,
                        out->addr[0], out->addr[1],
                        out->addr[2], out->addr[3],
                        (unsigned)out->ttl);
            cache_store(hostname, out);
            closesocket(sockfd);
            return DNS_OK;
        }

        if (ret == DNS_ERR_NXDOMAIN) {
            debug_printf("[DNS] NXDOMAIN: %s\n", hostname);
            closesocket(sockfd);
            return DNS_ERR_NXDOMAIN;
        }

        debug_printf("[DNS] parse error: %d (retry %d/%d)\n",
                    ret, retry, DNS_RETRY_COUNT);
    }

    closesocket(sockfd);
    debug_printf("[DNS] failed to resolve %s\n", hostname);
    return DNS_ERR_TIMEOUT;
}

#endif /* !TEST_BUILD */
