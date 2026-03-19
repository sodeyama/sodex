/*
 * test_stubs.c - Stub functions for host-side unit tests
 *
 * These stubs satisfy symbols that the libagent code references
 * from the Sodex userland libc, but which already exist in the
 * host's libc. We just provide the missing ones.
 */

#include <stdio.h>
#include <stdarg.h>

/* debug_printf and debug_write are Sodex-specific */
void debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int debug_write(const char *buf, unsigned int len)
{
    return (int)fwrite(buf, 1, len, stderr);
}

/* These may be referenced by http_client.c but not called in unit tests */
int socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    return -1;
}

int connect(int sockfd, const void *addr, unsigned int addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1;
}

int send_msg(int sockfd, const void *buf, int len, int flags)
{
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    return -1;
}

int recv_msg(int sockfd, void *buf, int len, int flags)
{
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    return -1;
}

int closesocket(int sockfd)
{
    (void)sockfd;
    return -1;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, unsigned int optlen)
{
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return -1;
}

/* inet_aton stub */
struct mock_in_addr { unsigned int s_addr; };
int inet_aton(const char *cp, struct mock_in_addr *inp) {
    /* Simple parse for test */
    unsigned int a, b, c, d;
    if (sscanf(cp, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        inp->s_addr = (d << 24) | (c << 16) | (b << 8) | a;
        return 1;
    }
    return 0;
}

unsigned short htons(unsigned short v) {
    return ((v & 0xff) << 8) | ((v >> 8) & 0xff);
}
