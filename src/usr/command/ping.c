#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>

#define PING_DATA_SIZE 56
#define PING_PKT_SIZE (sizeof(struct icmp_hdr) + PING_DATA_SIZE)
#define PING_COUNT     4

static u_int16_t checksum(u_int8_t *data, int len)
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

static void print_decimal(int n)
{
    char buf[12];
    int i = 0;
    if (n == 0) { putc('0'); return; }
    if (n < 0) { putc('-'); n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) putc(buf[--i]);
}

static void print_ip(u_int8_t *a)
{
    int i;
    for (i = 0; i < 4; i++) {
        print_decimal(a[i]);
        if (i < 3) putc('.');
    }
}

static struct sockaddr_in s_dest;
static struct sockaddr_in s_from;
static u_int8_t s_pkt[PING_PKT_SIZE];
static u_int8_t s_reply[128];
static int s_sent, s_received, s_seq;
static int s_sock;

int main(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: ping <ip>\n");
        return 1;
    }

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family = AF_INET;
    inet_aton(argv[1], &s_dest.sin_addr);

    puts("PING ");
    print_ip((u_int8_t *)&s_dest.sin_addr);
    puts("\n");

    s_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s_sock < 0) {
        puts("socket fail\n");
        return 1;
    }

    s_sent = 0;
    s_received = 0;

    for (s_seq = 1; s_seq <= PING_COUNT; s_seq++) {
        memset(s_pkt, 0, PING_PKT_SIZE);

        struct icmp_hdr *icmp = (struct icmp_hdr *)s_pkt;
        icmp->type = ICMP_ECHO;
        icmp->code = 0;
        icmp->checksum = 0;
        icmp->id = htons(0x1234);
        icmp->sequence = htons(s_seq);

        int i;
        for (i = 0; i < PING_DATA_SIZE; i++)
            s_pkt[sizeof(struct icmp_hdr) + i] = (u_int8_t)(i & 0xff);

        u_int16_t ck = checksum(s_pkt, PING_PKT_SIZE);
        icmp->checksum = htons(ck);

        int ret = sendto(s_sock, s_pkt, PING_PKT_SIZE, 0,
                        (struct sockaddr *)&s_dest, sizeof(s_dest));
        if (ret < 0) {
            puts("sendto fail\n");
            continue;
        }
        s_sent++;

        int n = recvfrom(s_sock, s_reply, sizeof(s_reply), 0,
                        (struct sockaddr *)&s_from, 0);

        if (n > 0) {
            s_received++;
            print_decimal(n);
            puts(" bytes from ");
            print_ip((u_int8_t *)&s_from.sin_addr);
            puts(": icmp_seq=");
            print_decimal(s_seq);
            putc('\n');
        } else {
            puts("Request timeout for icmp_seq ");
            print_decimal(s_seq);
            putc('\n');
        }
    }

    puts("--- ping statistics ---\n");
    print_decimal(s_sent);
    puts(" packets transmitted, ");
    print_decimal(s_received);
    puts(" received\n");

    closesocket(s_sock);
    return 0;
}
