/*
 * tls_internal.h - Internal TLS connection structure
 *
 * Only include this from files that need the full definition.
 * Other files should use the opaque pointer from tls_client.h.
 */
#ifndef _TLS_INTERNAL_H
#define _TLS_INTERNAL_H

#include "bearssl.h"

#define TLS_IOBUF_SIZE  BR_SSL_BUFSIZE_MONO

struct tls_connection {
    br_ssl_client_context sc;
    br_x509_knownkey_context xkc;
    br_x509_minimal_context xmc;
    unsigned char iobuf[TLS_IOBUF_SIZE];
    int sockfd;
    int connected;
    int use_knownkey;
};

#endif /* _TLS_INTERNAL_H */
