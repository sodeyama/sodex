#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <netinet/in.h>

int inet_aton(const char *cp, struct in_addr *inp);
char *inet_ntoa(struct in_addr in);

#endif
