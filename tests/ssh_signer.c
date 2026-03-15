#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST_BUILD 1
#define PUBLIC
#define PRIVATE static
#include "../src/include/ssh_crypto.h"

#define SSH_SIGNER_MAGIC "SIG1"
#define SSH_SIGNER_MAGIC_BYTES 4
#define SSH_SIGNER_REQUEST_BYTES \
    (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_SHA256_BYTES)
#define SSH_SIGNER_RESPONSE_BYTES \
    (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_ED25519_SIGNATURE_BYTES)
#define SSH_CURVE25519_MAGIC "KEX1"
#define SSH_CURVE25519_REQUEST_BYTES \
    (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES + \
     SSH_CRYPTO_CURVE25519_BYTES)
#define SSH_CURVE25519_RESPONSE_BYTES \
    (SSH_SIGNER_MAGIC_BYTES + SSH_CRYPTO_CURVE25519_BYTES + \
     SSH_CRYPTO_CURVE25519_BYTES)

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signo)
{
    (void)signo;
    keep_running = 0;
}

static int read_exact(int fd, uint8_t *buf, size_t len)
{
    size_t total = 0;

    while (total < len) {
        ssize_t rc = read(fd, buf + total, len - total);

        if (rc == 0) {
            return -1;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)rc;
    }
    return 0;
}

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t total = 0;

    while (total < len) {
        ssize_t rc = write(fd, buf + total, len - total);

        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)rc;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct sockaddr_in addr;
    uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES];
    int sock_fd;
    int opt = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <secret_hex>\n", argv[0]);
        return 2;
    }
    if (ssh_crypto_hex_to_bytes(argv[2], secret_key, sizeof(secret_key)) < 0) {
        fprintf(stderr, "invalid secret hex\n");
        return 2;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sock_fd);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock_fd);
        return 1;
    }

    printf("ready port=%s\n", argv[1]);
    fflush(stdout);

    while (keep_running) {
        uint8_t prefix[SSH_SIGNER_MAGIC_BYTES];
        uint8_t request[SSH_CURVE25519_REQUEST_BYTES];
        uint8_t response[SSH_CURVE25519_RESPONSE_BYTES];
        uint8_t signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES];
        uint8_t server_public[SSH_CRYPTO_CURVE25519_BYTES];
        uint8_t shared_secret[SSH_CRYPTO_CURVE25519_BYTES];
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        ssize_t recv_len;

        recv_len = recvfrom(sock_fd, request, sizeof(request), 0,
                            (struct sockaddr *)&peer_addr, &peer_len);
        if (recv_len < 0) {
            if (errno == EINTR && !keep_running) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            close(sock_fd);
            return 1;
        }

        fprintf(stderr, "accept signer request\n");
        fflush(stderr);

        if (recv_len < (ssize_t)sizeof(prefix)) {
            fprintf(stderr, "signer request invalid\n");
            fflush(stderr);
            continue;
        }
        memcpy(prefix, request, sizeof(prefix));

        if (memcmp(prefix, SSH_SIGNER_MAGIC, sizeof(prefix)) == 0) {
            if (recv_len == SSH_SIGNER_REQUEST_BYTES &&
                ssh_crypto_ed25519_sign(
                    signature,
                    secret_key,
                    request + SSH_SIGNER_MAGIC_BYTES,
                    SSH_CRYPTO_SHA256_BYTES) == 0) {
                memcpy(response, SSH_SIGNER_MAGIC, SSH_SIGNER_MAGIC_BYTES);
                memcpy(response + SSH_SIGNER_MAGIC_BYTES,
                       signature,
                       SSH_CRYPTO_ED25519_SIGNATURE_BYTES);
                fprintf(stderr, "signer reply ok\n");
                fflush(stderr);
                if (sendto(sock_fd, response, SSH_SIGNER_RESPONSE_BYTES, 0,
                           (struct sockaddr *)&peer_addr, peer_len) < 0) {
                    perror("sendto");
                    close(sock_fd);
                    return 1;
                }
            } else {
                fprintf(stderr, "signer request invalid\n");
                fflush(stderr);
            }
        } else if (memcmp(prefix, SSH_CURVE25519_MAGIC, sizeof(prefix)) == 0) {
            if (recv_len == SSH_CURVE25519_REQUEST_BYTES &&
                ssh_crypto_curve25519_public_key(
                    server_public,
                    request + SSH_SIGNER_MAGIC_BYTES) == 0 &&
                ssh_crypto_curve25519_shared(
                    shared_secret,
                    request + SSH_SIGNER_MAGIC_BYTES,
                    request + SSH_SIGNER_MAGIC_BYTES +
                        SSH_CRYPTO_CURVE25519_BYTES) == 0) {
                memcpy(response, SSH_CURVE25519_MAGIC, SSH_SIGNER_MAGIC_BYTES);
                memcpy(response + SSH_SIGNER_MAGIC_BYTES,
                       server_public,
                       SSH_CRYPTO_CURVE25519_BYTES);
                memcpy(response + SSH_SIGNER_MAGIC_BYTES +
                           SSH_CRYPTO_CURVE25519_BYTES,
                       shared_secret,
                       SSH_CRYPTO_CURVE25519_BYTES);
                fprintf(stderr, "curve25519 reply ok\n");
                fflush(stderr);
                if (sendto(sock_fd, response, SSH_CURVE25519_RESPONSE_BYTES, 0,
                           (struct sockaddr *)&peer_addr, peer_len) < 0) {
                    perror("sendto");
                    close(sock_fd);
                    return 1;
                }
            } else {
                fprintf(stderr, "curve25519 request invalid\n");
                fflush(stderr);
            }
        } else {
            fprintf(stderr, "signer magic invalid\n");
            fflush(stderr);
        }
    }

    close(sock_fd);
    return 0;
}
