#ifndef _TLS_CLIENT_H
#define _TLS_CLIENT_H

#include <sys/types.h>

/* ---- Error codes ---- */
#define TLS_OK                    0
#define TLS_ERR_DNS              (-1)
#define TLS_ERR_TCP              (-2)
#define TLS_ERR_HANDSHAKE        (-3)
#define TLS_ERR_CERT_VERIFY      (-4)
#define TLS_ERR_CERT_PIN         (-5)
#define TLS_ERR_SEND             (-6)
#define TLS_ERR_RECV             (-7)
#define TLS_ERR_CLOSED           (-8)
#define TLS_ERR_INIT             (-9)

/*
 * Initialize TLS subsystem (PRNG seeding).
 * Call once at startup.
 */
int tls_init(void);

/*
 * Establish TLS connection using internal static connection.
 * Only one TLS connection can be active at a time.
 * Returns 0 on success, negative error code on failure.
 */
int tls_connect(const char *hostname, u_int16_t port);

/*
 * Send data over the active TLS connection.
 * Returns bytes sent, or negative error code.
 */
int tls_send(const void *buf, int len);

/*
 * Receive data over the active TLS connection.
 * Returns bytes received, or negative error code. 0 = peer closed.
 */
int tls_recv(void *buf, int len);

/*
 * Close the active TLS connection.
 */
int tls_close(void);

#endif /* _TLS_CLIENT_H */
