/*
 * Unit tests for socket ring buffer, inet functions, and IP checksum
 * Phase 9 of POSIX socket implementation
 */
#include "test_framework.h"
#include <string.h>

/* --- Types needed for tests --- */
typedef unsigned char  u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int   u_int32_t;

/* --- Ring buffer implementation (extracted from socket.c) --- */
#define SOCK_RXBUF_SIZE 4096
#define SOCK_RAW    3
#define SOCK_DGRAM  2
#define SOCK_STREAM 1

struct sockaddr_in_t {
    u_int16_t sin_family;
    u_int16_t sin_port;
    u_int32_t sin_addr;
};

struct test_socket {
    int      type;
    u_int8_t rx_buf[SOCK_RXBUF_SIZE];
    u_int16_t rx_head;
    u_int16_t rx_tail;
    u_int16_t rx_len;
    struct sockaddr_in_t rx_from[16];
    u_int16_t rx_pkt_boundary[16];
    u_int8_t  rx_pkt_count;
};

static void rxbuf_init(struct test_socket *sk)
{
    sk->rx_head = 0;
    sk->rx_tail = 0;
    sk->rx_len = 0;
    sk->rx_pkt_count = 0;
}

static int rxbuf_write(struct test_socket *sk, u_int8_t *data, u_int16_t len,
                       struct sockaddr_in_t *from)
{
    if (len == 0) return 0;
    if (sk->rx_len + len > SOCK_RXBUF_SIZE) return -1;

    int i;
    for (i = 0; i < len; i++) {
        sk->rx_buf[sk->rx_head] = data[i];
        sk->rx_head = (sk->rx_head + 1) % SOCK_RXBUF_SIZE;
    }
    sk->rx_len += len;

    if (sk->rx_pkt_count < 16) {
        if (from) {
            sk->rx_from[sk->rx_pkt_count] = *from;
        } else {
            memset(&sk->rx_from[sk->rx_pkt_count], 0, sizeof(struct sockaddr_in_t));
        }
        sk->rx_pkt_boundary[sk->rx_pkt_count] = len;
        sk->rx_pkt_count++;
    }
    return len;
}

static int rxbuf_read(struct test_socket *sk, u_int8_t *buf, u_int16_t maxlen,
                      struct sockaddr_in_t *from)
{
    if (sk->rx_len == 0) return 0;

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

    if ((sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) && sk->rx_pkt_count > 0) {
        u_int16_t pkt_len = sk->rx_pkt_boundary[0];
        u_int16_t remaining = pkt_len - to_read;
        for (i = 0; i < remaining; i++) {
            sk->rx_tail = (sk->rx_tail + 1) % SOCK_RXBUF_SIZE;
        }
        sk->rx_len -= remaining;

        for (i = 1; i < sk->rx_pkt_count; i++) {
            sk->rx_from[i-1] = sk->rx_from[i];
            sk->rx_pkt_boundary[i-1] = sk->rx_pkt_boundary[i];
        }
        sk->rx_pkt_count--;
    }

    return to_read;
}

/* --- inet functions (from usr/lib/libc/inet.c) --- */
u_int16_t htons(u_int16_t h)
{
    return ((h & 0xff) << 8) | ((h >> 8) & 0xff);
}

u_int16_t ntohs(u_int16_t n)
{
    return ((n & 0xff) << 8) | ((n >> 8) & 0xff);
}

u_int32_t htonl(u_int32_t h)
{
    return ((h & 0xff) << 24) | ((h & 0xff00) << 8) |
           ((h >> 8) & 0xff00) | ((h >> 24) & 0xff);
}

u_int32_t ntohl(u_int32_t n)
{
    return htonl(n);
}

struct in_addr {
    u_int32_t s_addr;
};

static int _atoi_simple(const char *s, const char **endp)
{
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    *endp = s;
    return val;
}

int inet_aton(const char *cp, struct in_addr *inp)
{
    u_int8_t parts[4];
    int i;
    const char *p = cp;

    for (i = 0; i < 4; i++) {
        const char *end;
        int val = _atoi_simple(p, &end);
        if (val < 0 || val > 255) return 0;
        parts[i] = (u_int8_t)val;
        if (i < 3) {
            if (*end != '.') return 0;
            p = end + 1;
        } else {
            p = end;
        }
    }

    inp->s_addr = (u_int32_t)parts[0] | ((u_int32_t)parts[1] << 8) |
                  ((u_int32_t)parts[2] << 16) | ((u_int32_t)parts[3] << 24);
    return 1;
}

static char inet_ntoa_buf[16];

char *inet_ntoa(struct in_addr in)
{
    u_int8_t *a = (u_int8_t *)&in.s_addr;
    char *p = inet_ntoa_buf;
    int i;

    for (i = 0; i < 4; i++) {
        int val = a[i];
        if (val >= 100) {
            *p++ = '0' + val / 100;
            *p++ = '0' + (val / 10) % 10;
            *p++ = '0' + val % 10;
        } else if (val >= 10) {
            *p++ = '0' + val / 10;
            *p++ = '0' + val % 10;
        } else {
            *p++ = '0' + val;
        }
        if (i < 3) *p++ = '.';
    }
    *p = '\0';
    return inet_ntoa_buf;
}

/* --- IP/ICMP checksum (from socket.c) --- */
static u_int16_t ip_checksum(u_int8_t *data, int len)
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

/* ========== Ring Buffer Tests ========== */

TEST(rxbuf_write_read_basic) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_STREAM;
    rxbuf_init(&sk);

    u_int8_t data[] = {0x41, 0x42, 0x43, 0x44};
    int ret = rxbuf_write(&sk, data, 4, NULL);
    ASSERT_EQ(ret, 4);
    ASSERT_EQ(sk.rx_len, 4);

    u_int8_t buf[8] = {0};
    ret = rxbuf_read(&sk, buf, 8, NULL);
    ASSERT_EQ(ret, 4);
    ASSERT_EQ(buf[0], 0x41);
    ASSERT_EQ(buf[1], 0x42);
    ASSERT_EQ(buf[2], 0x43);
    ASSERT_EQ(buf[3], 0x44);
    ASSERT_EQ(sk.rx_len, 0);
}

TEST(rxbuf_empty_read) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_STREAM;
    rxbuf_init(&sk);

    u_int8_t buf[8];
    int ret = rxbuf_read(&sk, buf, 8, NULL);
    ASSERT_EQ(ret, 0);
}

TEST(rxbuf_write_zero_len) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_STREAM;
    rxbuf_init(&sk);

    int ret = rxbuf_write(&sk, NULL, 0, NULL);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(sk.rx_len, 0);
}

TEST(rxbuf_overflow) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_STREAM;
    rxbuf_init(&sk);

    u_int8_t data[SOCK_RXBUF_SIZE];
    memset(data, 0xAA, SOCK_RXBUF_SIZE);

    /* Fill the buffer */
    int ret = rxbuf_write(&sk, data, SOCK_RXBUF_SIZE, NULL);
    ASSERT_EQ(ret, SOCK_RXBUF_SIZE);
    ASSERT_EQ(sk.rx_len, SOCK_RXBUF_SIZE);

    /* Attempt to write one more byte should fail */
    u_int8_t extra = 0xBB;
    ret = rxbuf_write(&sk, &extra, 1, NULL);
    ASSERT_EQ(ret, -1);
}

TEST(rxbuf_wraparound) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_STREAM;
    rxbuf_init(&sk);

    /* Write 3000 bytes, read them, then write 2000 more (wraps around) */
    u_int8_t data1[3000];
    memset(data1, 0x11, 3000);
    rxbuf_write(&sk, data1, 3000, NULL);

    u_int8_t buf[3000];
    rxbuf_read(&sk, buf, 3000, NULL);
    ASSERT_EQ(sk.rx_len, 0);
    ASSERT_EQ(sk.rx_tail, 3000);
    ASSERT_EQ(sk.rx_head, 3000);

    /* Now write 2000 bytes - should wrap around */
    u_int8_t data2[2000];
    memset(data2, 0x22, 2000);
    rxbuf_write(&sk, data2, 2000, NULL);
    ASSERT_EQ(sk.rx_len, 2000);

    /* Read back and verify */
    u_int8_t buf2[2000];
    rxbuf_read(&sk, buf2, 2000, NULL);
    ASSERT_EQ(sk.rx_len, 0);
    ASSERT_EQ(buf2[0], 0x22);
    ASSERT_EQ(buf2[1999], 0x22);
}

TEST(rxbuf_partial_read) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_STREAM;
    rxbuf_init(&sk);

    u_int8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    rxbuf_write(&sk, data, 8, NULL);

    u_int8_t buf[4];
    int ret = rxbuf_read(&sk, buf, 4, NULL);
    ASSERT_EQ(ret, 4);
    ASSERT_EQ(buf[0], 1);
    ASSERT_EQ(buf[3], 4);
    ASSERT_EQ(sk.rx_len, 4);

    ret = rxbuf_read(&sk, buf, 4, NULL);
    ASSERT_EQ(ret, 4);
    ASSERT_EQ(buf[0], 5);
    ASSERT_EQ(buf[3], 8);
    ASSERT_EQ(sk.rx_len, 0);
}

TEST(rxbuf_dgram_packet_boundary) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_DGRAM;
    rxbuf_init(&sk);

    struct sockaddr_in_t from1 = {2, 0x3500, 0x0100A8C0}; /* 192.168.0.1:53 */
    struct sockaddr_in_t from2 = {2, 0x5000, 0x0200A8C0}; /* 192.168.0.2:80 */

    u_int8_t pkt1[] = {0xAA, 0xBB, 0xCC};
    u_int8_t pkt2[] = {0xDD, 0xEE};
    rxbuf_write(&sk, pkt1, 3, &from1);
    rxbuf_write(&sk, pkt2, 2, &from2);
    ASSERT_EQ(sk.rx_pkt_count, 2);
    ASSERT_EQ(sk.rx_len, 5);

    /* Read first packet */
    u_int8_t buf[16];
    struct sockaddr_in_t rfrom;
    int ret = rxbuf_read(&sk, buf, 16, &rfrom);
    ASSERT_EQ(ret, 3);
    ASSERT_EQ(buf[0], 0xAA);
    ASSERT_EQ(buf[2], 0xCC);
    ASSERT_EQ(rfrom.sin_port, 0x3500);
    ASSERT_EQ(sk.rx_pkt_count, 1);

    /* Read second packet */
    ret = rxbuf_read(&sk, buf, 16, &rfrom);
    ASSERT_EQ(ret, 2);
    ASSERT_EQ(buf[0], 0xDD);
    ASSERT_EQ(buf[1], 0xEE);
    ASSERT_EQ(rfrom.sin_port, 0x5000);
    ASSERT_EQ(sk.rx_pkt_count, 0);
    ASSERT_EQ(sk.rx_len, 0);
}

TEST(rxbuf_dgram_partial_read_discards) {
    struct test_socket sk;
    memset(&sk, 0, sizeof(sk));
    sk.type = SOCK_DGRAM;
    rxbuf_init(&sk);

    u_int8_t pkt[] = {1, 2, 3, 4, 5, 6, 7, 8};
    rxbuf_write(&sk, pkt, 8, NULL);

    /* Read only 3 bytes of an 8-byte packet - remaining 5 should be discarded */
    u_int8_t buf[3];
    int ret = rxbuf_read(&sk, buf, 3, NULL);
    ASSERT_EQ(ret, 3);
    ASSERT_EQ(buf[0], 1);
    ASSERT_EQ(buf[2], 3);
    ASSERT_EQ(sk.rx_len, 0);  /* Entire packet consumed */
    ASSERT_EQ(sk.rx_pkt_count, 0);
}

/* ========== Inet Function Tests ========== */

TEST(htons_basic) {
    ASSERT_EQ(htons(0x1234), 0x3412);
}

TEST(htons_zero) {
    ASSERT_EQ(htons(0), 0);
}

TEST(htons_ff) {
    ASSERT_EQ(htons(0x00ff), 0xff00);
}

TEST(htons_double) {
    /* htons(htons(x)) == x */
    ASSERT_EQ(htons(htons(0x1234)), 0x1234);
}

TEST(ntohs_basic) {
    ASSERT_EQ(ntohs(0x3412), 0x1234);
}

TEST(htonl_basic) {
    ASSERT_EQ(htonl(0x12345678), 0x78563412);
}

TEST(htonl_zero) {
    ASSERT_EQ(htonl(0), 0);
}

TEST(htonl_double) {
    ASSERT_EQ(htonl(htonl(0x12345678)), 0x12345678);
}

TEST(ntohl_basic) {
    ASSERT_EQ(ntohl(0x78563412), 0x12345678);
}

TEST(inet_aton_basic) {
    struct in_addr addr;
    int ret = inet_aton("10.0.2.15", &addr);
    ASSERT_EQ(ret, 1);
    u_int8_t *a = (u_int8_t *)&addr.s_addr;
    ASSERT_EQ(a[0], 10);
    ASSERT_EQ(a[1], 0);
    ASSERT_EQ(a[2], 2);
    ASSERT_EQ(a[3], 15);
}

TEST(inet_aton_loopback) {
    struct in_addr addr;
    int ret = inet_aton("127.0.0.1", &addr);
    ASSERT_EQ(ret, 1);
    u_int8_t *a = (u_int8_t *)&addr.s_addr;
    ASSERT_EQ(a[0], 127);
    ASSERT_EQ(a[1], 0);
    ASSERT_EQ(a[2], 0);
    ASSERT_EQ(a[3], 1);
}

TEST(inet_aton_broadcast) {
    struct in_addr addr;
    int ret = inet_aton("255.255.255.255", &addr);
    ASSERT_EQ(ret, 1);
    ASSERT_EQ(addr.s_addr, 0xFFFFFFFF);
}

TEST(inet_aton_zero) {
    struct in_addr addr;
    int ret = inet_aton("0.0.0.0", &addr);
    ASSERT_EQ(ret, 1);
    ASSERT_EQ(addr.s_addr, 0);
}

TEST(inet_aton_invalid_no_dots) {
    struct in_addr addr;
    int ret = inet_aton("12345", &addr);
    ASSERT_EQ(ret, 0);
}

TEST(inet_ntoa_basic) {
    struct in_addr addr;
    u_int8_t *a = (u_int8_t *)&addr.s_addr;
    a[0] = 10; a[1] = 0; a[2] = 2; a[3] = 15;
    char *s = inet_ntoa(addr);
    ASSERT_STR_EQ(s, "10.0.2.15");
}

TEST(inet_ntoa_loopback) {
    struct in_addr addr;
    u_int8_t *a = (u_int8_t *)&addr.s_addr;
    a[0] = 127; a[1] = 0; a[2] = 0; a[3] = 1;
    char *s = inet_ntoa(addr);
    ASSERT_STR_EQ(s, "127.0.0.1");
}

TEST(inet_ntoa_broadcast) {
    struct in_addr addr;
    addr.s_addr = 0xFFFFFFFF;
    char *s = inet_ntoa(addr);
    ASSERT_STR_EQ(s, "255.255.255.255");
}

TEST(inet_ntoa_zero) {
    struct in_addr addr;
    addr.s_addr = 0;
    char *s = inet_ntoa(addr);
    ASSERT_STR_EQ(s, "0.0.0.0");
}

TEST(inet_roundtrip) {
    struct in_addr addr1, addr2;
    inet_aton("192.168.1.100", &addr1);
    char *s = inet_ntoa(addr1);
    inet_aton(s, &addr2);
    ASSERT_EQ(addr1.s_addr, addr2.s_addr);
}

/* ========== IP Checksum Tests ========== */

TEST(checksum_zeros) {
    u_int8_t data[20];
    memset(data, 0, 20);
    u_int16_t ck = ip_checksum(data, 20);
    ASSERT_EQ(ck, 0xFFFF);
}

TEST(checksum_icmp_echo) {
    /* ICMP echo request: type=8, code=0, checksum=0, id=0x1234, seq=1 */
    u_int8_t icmp[8] = {0x08, 0x00, 0x00, 0x00, 0x12, 0x34, 0x00, 0x01};
    u_int16_t ck = ip_checksum(icmp, 8);
    /* Verify checksum: when placed in packet, overall sum should be 0xFFFF */
    icmp[2] = (ck >> 8) & 0xFF;
    icmp[3] = ck & 0xFF;
    u_int16_t verify = ip_checksum(icmp, 8);
    ASSERT_EQ(verify, 0);
}

TEST(checksum_odd_length) {
    /* Test with odd-length data */
    u_int8_t data[5] = {0x00, 0x01, 0x00, 0x02, 0x03};
    u_int16_t ck = ip_checksum(data, 5);
    /* Verify by computing manually:
     * 0x0001 + 0x0002 + 0x0300 = 0x0303
     * ~0x0303 = 0xFCFC */
    ASSERT_EQ(ck, 0xFCFC);
}

TEST(checksum_known_ip_header) {
    /* Known IP header from RFC 1071 example */
    u_int8_t ip[20] = {
        0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00, /* checksum field = 0 */
        0xac, 0x10, 0x0a, 0x63, /* src: 172.16.10.99 */
        0xac, 0x10, 0x0a, 0x0c  /* dst: 172.16.10.12 */
    };
    u_int16_t ck = ip_checksum(ip, 20);
    /* Place checksum and verify */
    ip[10] = (ck >> 8) & 0xFF;
    ip[11] = ck & 0xFF;
    u_int16_t verify = ip_checksum(ip, 20);
    ASSERT_EQ(verify, 0);
}

/* ========== main ========== */

int main(void)
{
    printf("=== socket ring buffer / inet / checksum tests ===\n");

    /* Ring buffer tests */
    RUN_TEST(rxbuf_write_read_basic);
    RUN_TEST(rxbuf_empty_read);
    RUN_TEST(rxbuf_write_zero_len);
    RUN_TEST(rxbuf_overflow);
    RUN_TEST(rxbuf_wraparound);
    RUN_TEST(rxbuf_partial_read);
    RUN_TEST(rxbuf_dgram_packet_boundary);
    RUN_TEST(rxbuf_dgram_partial_read_discards);

    /* htons/ntohs/htonl/ntohl */
    RUN_TEST(htons_basic);
    RUN_TEST(htons_zero);
    RUN_TEST(htons_ff);
    RUN_TEST(htons_double);
    RUN_TEST(ntohs_basic);
    RUN_TEST(htonl_basic);
    RUN_TEST(htonl_zero);
    RUN_TEST(htonl_double);
    RUN_TEST(ntohl_basic);

    /* inet_aton / inet_ntoa */
    RUN_TEST(inet_aton_basic);
    RUN_TEST(inet_aton_loopback);
    RUN_TEST(inet_aton_broadcast);
    RUN_TEST(inet_aton_zero);
    RUN_TEST(inet_aton_invalid_no_dots);
    RUN_TEST(inet_ntoa_basic);
    RUN_TEST(inet_ntoa_loopback);
    RUN_TEST(inet_ntoa_broadcast);
    RUN_TEST(inet_ntoa_zero);
    RUN_TEST(inet_roundtrip);

    /* IP/ICMP checksum */
    RUN_TEST(checksum_zeros);
    RUN_TEST(checksum_icmp_echo);
    RUN_TEST(checksum_odd_length);
    RUN_TEST(checksum_known_ip_header);

    TEST_REPORT();
}
