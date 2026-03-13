#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

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

  /* Store in network byte order (big-endian) */
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
