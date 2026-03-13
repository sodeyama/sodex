#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>

#define PING_DATA_SIZE 56
#define PING_PKT_SIZE (sizeof(struct icmp_hdr) + PING_DATA_SIZE)
#define PING_COUNT     4
#define PING_TIMEOUT   300  /* 3 seconds in ticks (100 Hz) */

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

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: ping <ip address>\n");
        return 1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    if (!inet_aton(argv[1], &dest.sin_addr)) {
        printf("Invalid address: %s\n", argv[1]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf("socket() failed\n");
        return 1;
    }

    printf("PING %s: %d data bytes\n", argv[1], PING_DATA_SIZE);

    int sent = 0, received = 0;
    u_int16_t seq;

    for (seq = 1; seq <= PING_COUNT; seq++) {
        u_int8_t pkt[PING_PKT_SIZE];
        memset(pkt, 0, PING_PKT_SIZE);

        /* Build ICMP echo request */
        struct icmp_hdr *icmp = (struct icmp_hdr *)pkt;
        icmp->type = ICMP_ECHO;
        icmp->code = 0;
        icmp->checksum = 0;
        icmp->id = htons(0x1234);
        icmp->sequence = htons(seq);

        /* Fill data payload */
        int i;
        for (i = 0; i < PING_DATA_SIZE; i++)
            pkt[sizeof(struct icmp_hdr) + i] = (u_int8_t)(i & 0xff);

        /* Calculate checksum */
        u_int16_t ck = checksum(pkt, PING_PKT_SIZE);
        icmp->checksum = htons(ck);

        /* Send */
        int ret = sendto(sock, pkt, PING_PKT_SIZE, 0,
                        (struct sockaddr *)&dest, sizeof(dest));
        if (ret < 0) {
            printf("sendto() failed\n");
            continue;
        }
        sent++;

        /* Receive reply */
        u_int8_t reply[128];
        struct sockaddr_in from;
        int n = recvfrom(sock, reply, sizeof(reply), 0,
                        (struct sockaddr *)&from, 0);

        if (n > 0) {
            struct icmp_hdr *reply_icmp = (struct icmp_hdr *)reply;
            if (reply_icmp->type == ICMP_ECHOREPLY) {
                u_int8_t *fa = (u_int8_t *)&from.sin_addr;
                printf("%d bytes from %d.%d.%d.%d: icmp_seq=%d\n",
                       n, fa[0], fa[1], fa[2], fa[3], seq);
                received++;
            }
        } else {
            printf("Request timeout for icmp_seq %d\n", seq);
        }
    }

    printf("--- %s ping statistics ---\n", argv[1]);
    printf("%d packets transmitted, %d received\n", sent, received);

    closesocket(sock);
    return 0;
}
