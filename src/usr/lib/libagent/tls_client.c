/*
 * tls_client.c - TLS 1.2 client using BearSSL (userland)
 *
 * Uses BearSSL's low-level state machine API.
 * I/O through Sodex socket syscalls.
 * Single static connection (only one TLS session at a time).
 */

#include <tls_client.h>
#include <dns.h>
#include <entropy.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>

/* Internal header with BearSSL types */
#include "tls_internal.h"

/* ---- Trust-all X.509 engine using x509_decoder to extract public key ---- */

struct x509_novalidate_ctx {
    const br_x509_class *vtable;
    br_x509_decoder_context decoder;
    br_x509_pkey pkey;
    int got_pkey;
};

static void xnv_start_chain(const br_x509_class **ctx, const char *server_name)
{
    struct x509_novalidate_ctx *xc = (struct x509_novalidate_ctx *)ctx;
    (void)server_name;
    xc->got_pkey = 0;
    br_x509_decoder_init(&xc->decoder, NULL, NULL);
}

static void xnv_start_cert(const br_x509_class **ctx, uint32_t length)
{
    (void)ctx; (void)length;
}

static void xnv_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
    struct x509_novalidate_ctx *xc = (struct x509_novalidate_ctx *)ctx;
    /* Only decode the first certificate (EE cert) */
    if (!xc->got_pkey)
        br_x509_decoder_push(&xc->decoder, buf, len);
}

static void xnv_end_cert(const br_x509_class **ctx)
{
    struct x509_novalidate_ctx *xc = (struct x509_novalidate_ctx *)ctx;
    if (!xc->got_pkey) {
        const br_x509_pkey *pk = br_x509_decoder_get_pkey(&xc->decoder);
        if (pk) {
            xc->pkey = *pk;
            xc->got_pkey = 1;
        }
    }
}

static unsigned xnv_end_chain(const br_x509_class **ctx)
{
    (void)ctx;
    return 0;  /* Always succeed - no CA verification */
}

static const br_x509_pkey *xnv_get_pkey(const br_x509_class *const *ctx,
                                          unsigned *usages)
{
    struct x509_novalidate_ctx *xc = (struct x509_novalidate_ctx *)(void *)ctx;
    if (usages)
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return xc->got_pkey ? &xc->pkey : NULL;
}

static const br_x509_class x509_novalidate_class = {
    sizeof(struct x509_novalidate_ctx),
    xnv_start_chain,
    xnv_start_cert,
    xnv_append,
    xnv_end_cert,
    xnv_end_chain,
    xnv_get_pkey,
};

static struct x509_novalidate_ctx tls_x509_novalidate;

/* ---- Static connection and PRNG ---- */
static struct tls_connection tls_conn;
static br_hmac_drbg_context tls_rng;
static int tls_rng_seeded = 0;

/* ---- Cipher suites (ChaCha20 first for i486) ---- */
static const uint16_t tls_suites[] = {
    BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
};
#define TLS_NUM_SUITES  (sizeof(tls_suites) / sizeof(tls_suites[0]))

/* ---- TLS init ---- */

int tls_init(void)
{
    unsigned char seed[48];

    if (tls_rng_seeded)
        return TLS_OK;

    if (!entropy_ready())
        entropy_collect_jitter(512);
    if (!entropy_ready()) {
        debug_printf("[TLS] not enough entropy\n");
        return TLS_ERR_INIT;
    }

    entropy_get_seed(seed, sizeof(seed));
    br_hmac_drbg_init(&tls_rng, &br_sha256_vtable, seed, sizeof(seed));
    tls_rng_seeded = 1;
    debug_printf("[TLS] PRNG seeded\n");
    return TLS_OK;
}

/* ---- Engine pump: run handshake until SENDAPP/RECVAPP or error ---- */

static int tls_pump_handshake(void)
{
    br_ssl_engine_context *eng = &tls_conn.sc.eng;

    for (;;) {
        unsigned state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(eng);
            debug_printf("[TLS] engine closed, err=%d\n", err);
            return (err == BR_ERR_OK) ? TLS_OK : TLS_ERR_HANDSHAKE;
        }

        /* Priority: send first, then receive */
        if (state & BR_SSL_SENDREC) {
            unsigned char *buf;
            size_t len;
            int wlen;
            buf = br_ssl_engine_sendrec_buf(eng, &len);
            wlen = send_msg(tls_conn.sockfd, buf, (int)len, 0);
            if (wlen <= 0) {
                debug_printf("[TLS] pump send err: %d\n", wlen);
                return TLS_ERR_SEND;
            }
            br_ssl_engine_sendrec_ack(eng, (size_t)wlen);
            continue;
        }

        if (state & (BR_SSL_SENDAPP | BR_SSL_RECVAPP))
            return TLS_OK;

        if (state & BR_SSL_RECVREC) {
            unsigned char *buf;
            size_t len;
            int rlen;
            buf = br_ssl_engine_recvrec_buf(eng, &len);
            rlen = recv_msg(tls_conn.sockfd, buf, (int)len, 0);
            if (rlen <= 0) {
                debug_printf("[TLS] pump recv err: %d (want %d)\n", rlen, (int)len);
                return TLS_ERR_RECV;
            }
            br_ssl_engine_recvrec_ack(eng, (size_t)rlen);
            continue;
        }

        debug_printf("[TLS] state: 0x%x\n", state);
        return TLS_ERR_HANDSHAKE;
    }
}

/* ---- Public API ---- */

int tls_connect(const char *hostname, u_int16_t port)
{
    struct dns_result dns;
    struct sockaddr_in addr;
    int ret;
    u_int32_t timeout;
    char ip_str[16];

    ret = tls_init();
    if (ret != TLS_OK)
        return ret;

    memset(&tls_conn, 0, sizeof(tls_conn));
    tls_conn.sockfd = -1;

    /* Check if hostname is already an IP address */
    {
        struct in_addr test_addr;
        if (inet_aton(hostname, &test_addr)) {
            /* Already an IP, skip DNS */
            strncpy(ip_str, hostname, sizeof(ip_str) - 1);
            ip_str[sizeof(ip_str) - 1] = '\0';
            debug_printf("[TLS] using IP directly: %s\n", ip_str);
        } else {
            /* DNS resolve */
            debug_printf("[TLS] resolving %s ...\n", hostname);
            ret = dns_resolve(hostname, &dns);
            if (ret != DNS_OK) {
                debug_printf("[TLS] DNS failed: %d\n", ret);
                return TLS_ERR_DNS;
            }
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                     dns.addr[0], dns.addr[1], dns.addr[2], dns.addr[3]);
            debug_printf("[TLS] resolved -> %s\n", ip_str);
        }
    }

    /* TCP */
    tls_conn.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tls_conn.sockfd < 0)
        return TLS_ERR_TCP;

    timeout = 15000;
    setsockopt(tls_conn.sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_aton(ip_str, &addr.sin_addr);

    debug_printf("[TLS] TCP connecting %s:%d ...\n", ip_str, port);
    ret = connect(tls_conn.sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        debug_printf("[TLS] TCP failed: %d\n", ret);
        closesocket(tls_conn.sockfd);
        tls_conn.sockfd = -1;
        return TLS_ERR_TCP;
    }
    debug_printf("[TLS] TCP connected\n");

    /* BearSSL init - full profile sets up all crypto algorithms */
    br_ssl_client_init_full(&tls_conn.sc, &tls_conn.xmc, NULL, 0);

    /* Override X.509: accept any certificate for now.
     * We use the "noanchor" approach - set x509 to our custom no-op.
     * TODO: add proper CA bundle or cert pinning for production. */
    tls_x509_novalidate.vtable = &x509_novalidate_class;
    br_ssl_engine_set_x509(&tls_conn.sc.eng, &tls_x509_novalidate.vtable);

    /* Set our userland PRNG (critical - no system RNG available) */
    br_ssl_engine_set_prf10(&tls_conn.sc.eng, &br_tls10_prf);
    br_ssl_engine_set_prf_sha256(&tls_conn.sc.eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&tls_conn.sc.eng, &br_tls12_sha384_prf);
    br_ssl_engine_inject_entropy(&tls_conn.sc.eng, tls_rng.vtable, sizeof(tls_rng));
    {
        unsigned char rng_seed[32];
        br_hmac_drbg_generate(&tls_rng, rng_seed, sizeof(rng_seed));
        br_ssl_engine_inject_entropy(&tls_conn.sc.eng, rng_seed, sizeof(rng_seed));
    }

    br_ssl_engine_set_suites(&tls_conn.sc.eng, tls_suites, TLS_NUM_SUITES);
    br_ssl_engine_set_buffer(&tls_conn.sc.eng, tls_conn.iobuf,
                             sizeof(tls_conn.iobuf), 0);

    /* Handshake */
    br_ssl_client_reset(&tls_conn.sc, hostname, 0);
    debug_printf("[TLS] handshake ...\n");
    ret = tls_pump_handshake();
    if (ret != TLS_OK) {
        int err = br_ssl_engine_last_error(&tls_conn.sc.eng);
        debug_printf("[TLS] handshake failed: ret=%d bearssl=%d\n", ret, err);
        closesocket(tls_conn.sockfd);
        tls_conn.sockfd = -1;
        return TLS_ERR_HANDSHAKE;
    }

    tls_conn.connected = 1;
    debug_printf("[TLS] handshake OK\n");
    return TLS_OK;
}

int tls_send(const void *buf, int len)
{
    br_ssl_engine_context *eng = &tls_conn.sc.eng;
    const unsigned char *data = (const unsigned char *)buf;
    int sent = 0;

    while (sent < len) {
        unsigned state = br_ssl_engine_current_state(eng);
        if (state & BR_SSL_CLOSED)
            return sent > 0 ? sent : TLS_ERR_CLOSED;

        if (state & BR_SSL_SENDAPP) {
            unsigned char *app_buf;
            size_t app_len;
            int chunk;
            app_buf = br_ssl_engine_sendapp_buf(eng, &app_len);
            chunk = len - sent;
            if (chunk > (int)app_len)
                chunk = (int)app_len;
            memcpy(app_buf, data + sent, chunk);
            br_ssl_engine_sendapp_ack(eng, (size_t)chunk);
            sent += chunk;
            br_ssl_engine_flush(eng, 0);
        }

        state = br_ssl_engine_current_state(eng);
        if (state & BR_SSL_SENDREC) {
            unsigned char *rec_buf;
            size_t rec_len;
            int wlen;
            rec_buf = br_ssl_engine_sendrec_buf(eng, &rec_len);
            wlen = send_msg(tls_conn.sockfd, rec_buf, (int)rec_len, 0);
            if (wlen <= 0)
                return TLS_ERR_SEND;
            br_ssl_engine_sendrec_ack(eng, (size_t)wlen);
        }
    }
    return sent;
}

int tls_recv(void *buf, int len)
{
    br_ssl_engine_context *eng = &tls_conn.sc.eng;
    unsigned char *out = (unsigned char *)buf;
    int total = 0;
    while (total < len) {
        unsigned state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_CLOSED)
            return total > 0 ? total : 0;

        if (state & BR_SSL_RECVAPP) {
            unsigned char *app_buf;
            size_t app_len;
            int chunk;
            app_buf = br_ssl_engine_recvapp_buf(eng, &app_len);
            chunk = len - total;
            if (chunk > (int)app_len)
                chunk = (int)app_len;
            memcpy(out + total, app_buf, chunk);
            br_ssl_engine_recvapp_ack(eng, (size_t)chunk);
            total += chunk;
            continue;  /* Try to read more data */
        }

        if (state & BR_SSL_RECVREC) {
            unsigned char *rec_buf;
            size_t rec_len;
            int rlen;
            unsigned char recv_tmp[1500];
            int ask;
            rec_buf = br_ssl_engine_recvrec_buf(eng, &rec_len);
            /* Receive into stack buffer first, then copy to BearSSL's
             * iobuf.  This works around an issue where kern_recvfrom
             * writing directly to the BSS-resident iobuf can produce
             * zero bytes due to page table interaction. */
            ask = (int)rec_len;
            if (ask > (int)sizeof(recv_tmp))
                ask = (int)sizeof(recv_tmp);
            rlen = recv_msg(tls_conn.sockfd, recv_tmp, ask, 0);
            if (rlen == 0) {
                /* Timeout — no data yet. Return what we have so far,
                 * or TLS_ERR_RECV so caller can retry. */
                return total > 0 ? total : TLS_ERR_RECV;
            }
            if (rlen < 0) {
                /* EOF or error from kernel. Return accumulated data
                 * if any, otherwise signal EOF cleanly with 0. */
                return total > 0 ? total : 0;
            }
            memcpy(rec_buf, recv_tmp, rlen);
            br_ssl_engine_recvrec_ack(eng, (size_t)rlen);
            continue;
        }

        if (state & BR_SSL_SENDREC) {
            unsigned char *rec_buf;
            size_t rec_len;
            int wlen;
            rec_buf = br_ssl_engine_sendrec_buf(eng, &rec_len);
            wlen = send_msg(tls_conn.sockfd, rec_buf, (int)rec_len, 0);
            if (wlen <= 0)
                return TLS_ERR_SEND;
            br_ssl_engine_sendrec_ack(eng, (size_t)wlen);
            continue;
        }

        break;
    }
    return total;
}

int tls_close(void)
{
    if (tls_conn.connected) {
        br_ssl_engine_close(&tls_conn.sc.eng);
        unsigned state = br_ssl_engine_current_state(&tls_conn.sc.eng);
        if (state & BR_SSL_SENDREC) {
            unsigned char *buf;
            size_t len;
            buf = br_ssl_engine_sendrec_buf(&tls_conn.sc.eng, &len);
            send_msg(tls_conn.sockfd, buf, (int)len, 0);
            br_ssl_engine_sendrec_ack(&tls_conn.sc.eng, len);
        }
        tls_conn.connected = 0;
    }
    if (tls_conn.sockfd >= 0) {
        closesocket(tls_conn.sockfd);
        tls_conn.sockfd = -1;
    }
    debug_printf("[TLS] closed\n");
    return TLS_OK;
}
