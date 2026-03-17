/*
 * test_dns.c - Host-side unit test for DNS query/response
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dns.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_START(name) printf("TEST: %s\n", name)
#define TEST_PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while(0)

/* ---- Label encoding tests ---- */

static void test_encode_simple(void)
{
    u_int8_t buf[64];
    int len;

    TEST_START("encode_simple");
    len = dns_encode_labels("api.anthropic.com", buf, sizeof(buf));
    ASSERT(len == 19, "expected 19 bytes");
    /* \x03api\x09anthropic\x03com\x00 */
    ASSERT(buf[0] == 3, "first label len");
    ASSERT(memcmp(buf + 1, "api", 3) == 0, "first label");
    ASSERT(buf[4] == 9, "second label len");
    ASSERT(memcmp(buf + 5, "anthropic", 9) == 0, "second label");
    ASSERT(buf[14] == 3, "third label len");
    ASSERT(memcmp(buf + 15, "com", 3) == 0, "third label");
    ASSERT(buf[18] == 0, "root terminator");
    TEST_PASS("encode_simple");
}

static void test_encode_single(void)
{
    u_int8_t buf[64];
    int len;

    TEST_START("encode_single");
    len = dns_encode_labels("localhost", buf, sizeof(buf));
    ASSERT(len == 11, "expected 11 bytes");
    ASSERT(buf[0] == 9, "label len");
    ASSERT(memcmp(buf + 1, "localhost", 9) == 0, "label content");
    ASSERT(buf[10] == 0, "root terminator");
    TEST_PASS("encode_single");
}

static void test_encode_too_long(void)
{
    char long_name[200];
    u_int8_t buf[16];
    int len;

    TEST_START("encode_too_long");
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    len = dns_encode_labels(long_name, buf, sizeof(buf));
    ASSERT(len < 0, "should fail for too-long name");
    TEST_PASS("encode_too_long");
}

/* ---- Query builder tests ---- */

static void test_build_query(void)
{
    u_int8_t buf[128];
    int len;

    TEST_START("build_query");
    len = dns_build_query("api.anthropic.com", 0xAABB, buf, sizeof(buf));
    ASSERT(len > 0, "build should succeed");
    /* Header checks */
    ASSERT(buf[0] == 0xAA && buf[1] == 0xBB, "transaction ID");
    ASSERT(buf[2] == 0x01 && buf[3] == 0x00, "flags: RD=1");
    ASSERT(buf[4] == 0x00 && buf[5] == 0x01, "QDCOUNT=1");
    ASSERT(buf[6] == 0 && buf[7] == 0, "ANCOUNT=0");
    /* Question section starts at offset 12 */
    ASSERT(buf[12] == 3, "QNAME first label");
    TEST_PASS("build_query");
}

/* ---- Response parser tests ---- */

static void test_parse_a_record(void)
{
    /*
     * Minimal DNS response for api.anthropic.com -> 104.18.32.7
     * Header: ID=0xAABB, QR=1, RD=1, RA=1, RCODE=0, QDCOUNT=1, ANCOUNT=1
     * Question: api.anthropic.com A IN
     * Answer: (pointer to question) A IN TTL=120 RDATA=104.18.32.7
     */
    u_int8_t resp[] = {
        /* Header */
        0xAA, 0xBB,  /* ID */
        0x81, 0x80,  /* Flags: QR=1, RD=1, RA=1 */
        0x00, 0x01,  /* QDCOUNT=1 */
        0x00, 0x01,  /* ANCOUNT=1 */
        0x00, 0x00,  /* NSCOUNT=0 */
        0x00, 0x00,  /* ARCOUNT=0 */
        /* Question: api.anthropic.com */
        0x03, 'a', 'p', 'i',
        0x09, 'a', 'n', 't', 'h', 'r', 'o', 'p', 'i', 'c',
        0x03, 'c', 'o', 'm',
        0x00,         /* root */
        0x00, 0x01,   /* QTYPE=A */
        0x00, 0x01,   /* QCLASS=IN */
        /* Answer: pointer to offset 12 (question name) */
        0xC0, 0x0C,   /* Name pointer to offset 12 */
        0x00, 0x01,   /* TYPE=A */
        0x00, 0x01,   /* CLASS=IN */
        0x00, 0x00, 0x00, 0x78, /* TTL=120 */
        0x00, 0x04,   /* RDLENGTH=4 */
        104, 18, 32, 7 /* RDATA=104.18.32.7 */
    };
    struct dns_result result;
    int ret;

    TEST_START("parse_a_record");
    ret = dns_parse_response(resp, sizeof(resp), 0xAABB, &result);
    ASSERT(ret == DNS_OK, "parse should succeed");
    ASSERT(result.addr[0] == 104, "addr[0]");
    ASSERT(result.addr[1] == 18, "addr[1]");
    ASSERT(result.addr[2] == 32, "addr[2]");
    ASSERT(result.addr[3] == 7, "addr[3]");
    ASSERT(result.ttl >= 60 && result.ttl <= 300, "TTL clamped");
    TEST_PASS("parse_a_record");
}

static void test_parse_nxdomain(void)
{
    u_int8_t resp[] = {
        /* Header */
        0xAA, 0xBB,  /* ID */
        0x81, 0x83,  /* Flags: QR=1, RD=1, RA=1, RCODE=3 (NXDOMAIN) */
        0x00, 0x01,  /* QDCOUNT=1 */
        0x00, 0x00,  /* ANCOUNT=0 */
        0x00, 0x00,  /* NSCOUNT=0 */
        0x00, 0x00,  /* ARCOUNT=0 */
        /* Question */
        0x07, 'n', 'o', 'e', 'x', 'i', 's', 't',
        0x03, 'c', 'o', 'm',
        0x00,
        0x00, 0x01,
        0x00, 0x01,
    };
    struct dns_result result;
    int ret;

    TEST_START("parse_nxdomain");
    ret = dns_parse_response(resp, sizeof(resp), 0xAABB, &result);
    ASSERT(ret == DNS_ERR_NXDOMAIN, "should return NXDOMAIN");
    TEST_PASS("parse_nxdomain");
}

static void test_parse_wrong_txid(void)
{
    u_int8_t resp[] = {
        0xCC, 0xDD,  /* Wrong ID */
        0x81, 0x80,
        0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'f', 'o', 'o', 0x00,
        0x00, 0x01, 0x00, 0x01,
        0xC0, 0x0C,
        0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x3C,
        0x00, 0x04,
        1, 2, 3, 4,
    };
    struct dns_result result;
    int ret;

    TEST_START("parse_wrong_txid");
    ret = dns_parse_response(resp, sizeof(resp), 0xAABB, &result);
    ASSERT(ret == DNS_ERR_FORMAT, "should reject wrong txid");
    TEST_PASS("parse_wrong_txid");
}

static void test_parse_cname_then_a(void)
{
    /*
     * Response with CNAME followed by A record.
     * Manually constructed with correct offsets.
     */
    u_int8_t resp[] = {
        /* Header (12 bytes) */
        0x11, 0x22,               /* ID */
        0x81, 0x80,               /* Flags: QR=1, RD=1, RA=1 */
        0x00, 0x01,               /* QDCOUNT=1 */
        0x00, 0x02,               /* ANCOUNT=2 */
        0x00, 0x00,               /* NSCOUNT=0 */
        0x00, 0x00,               /* ARCOUNT=0 */
        /* Question: example.com (offset 12) */
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',   /* offset 12..19 */
        0x03, 'c', 'o', 'm',                         /* offset 20..23 */
        0x00,                                         /* offset 24 */
        0x00, 0x01,               /* QTYPE=A */       /* offset 25 */
        0x00, 0x01,               /* QCLASS=IN */     /* offset 27 */
        /* Answer 1: CNAME (offset 29) */
        0xC0, 0x0C,               /* NAME pointer to offset 12 */
        0x00, 0x05,               /* TYPE=CNAME */
        0x00, 0x01,               /* CLASS=IN */
        0x00, 0x00, 0x01, 0x2C,  /* TTL=300 */
        0x00, 0x07,               /* RDLENGTH=7 */
        0x04, 'r', 'e', 'a', 'l', 0xC0, 0x0C, /* real + pointer to example.com */
        /* Answer 2: A record (offset 48) */
        0xC0, 0x29,               /* NAME pointer to offset 41 (the CNAME RDATA) */
        0x00, 0x01,               /* TYPE=A */
        0x00, 0x01,               /* CLASS=IN */
        0x00, 0x00, 0x00, 0x3C,  /* TTL=60 */
        0x00, 0x04,               /* RDLENGTH=4 */
        93, 184, 216, 34,         /* RDATA */
    };
    struct dns_result result;
    int ret;

    TEST_START("parse_cname_then_a");
    ret = dns_parse_response(resp, sizeof(resp), 0x1122, &result);
    ASSERT(ret == DNS_OK, "should find A record after CNAME");
    ASSERT(result.addr[0] == 93, "addr[0]");
    ASSERT(result.addr[1] == 184, "addr[1]");
    ASSERT(result.addr[2] == 216, "addr[2]");
    ASSERT(result.addr[3] == 34, "addr[3]");
    TEST_PASS("parse_cname_then_a");
}

static void test_parse_truncated(void)
{
    u_int8_t resp[] = {
        0xAA, 0xBB, 0x81, 0x80,
        0x00, 0x01, 0x00, 0x01,
    };
    struct dns_result result;
    int ret;

    TEST_START("parse_truncated");
    ret = dns_parse_response(resp, sizeof(resp), 0xAABB, &result);
    ASSERT(ret == DNS_ERR_FORMAT, "should fail on truncated response");
    TEST_PASS("parse_truncated");
}

int main(void)
{
    printf("=== DNS Parser Unit Tests ===\n\n");

    test_encode_simple();
    test_encode_single();
    test_encode_too_long();
    test_build_query();
    test_parse_a_record();
    test_parse_nxdomain();
    test_parse_wrong_txid();
    test_parse_cname_then_a();
    test_parse_truncated();

    printf("\n=== RESULT: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
