/*
 * dig.c - Simple DNS lookup command
 *
 * Usage: dig <hostname>
 * Example: dig api.anthropic.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <dns.h>

int main(int argc, char *argv[])
{
    struct dns_result result;
    int ret;

    if (argc < 2) {
        printf("usage: dig <hostname>\n");
        exit(1);
        return 1;
    }

    printf("Resolving %s ...\n", argv[1]);

    ret = dns_resolve(argv[1], &result);

    if (ret == DNS_OK) {
        printf("%s -> %d.%d.%d.%d (TTL=%u)\n",
               argv[1],
               result.addr[0], result.addr[1],
               result.addr[2], result.addr[3],
               (unsigned)result.ttl);
    } else if (ret == DNS_ERR_NXDOMAIN) {
        printf("NXDOMAIN: %s not found\n", argv[1]);
    } else if (ret == DNS_ERR_TIMEOUT) {
        printf("TIMEOUT: no response from DNS server\n");
    } else {
        printf("ERROR: dns_resolve returned %d\n", ret);
    }

    exit(ret == DNS_OK ? 0 : 1);
    return 0;
}
