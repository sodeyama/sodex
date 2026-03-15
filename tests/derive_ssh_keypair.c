#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BUILD 1
#define PUBLIC
#define PRIVATE static
#include "../src/include/ssh_crypto.h"

static void to_hex(const uint8_t *input, size_t input_len, char *output)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < input_len; i++) {
        output[i * 2] = digits[(input[i] >> 4) & 0x0f];
        output[i * 2 + 1] = digits[input[i] & 0x0f];
    }
    output[input_len * 2] = '\0';
}

int main(int argc, char **argv)
{
    uint8_t seed[SSH_CRYPTO_SEED_BYTES];
    uint8_t public_key[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES];
    uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES];
    char public_hex[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES * 2 + 1];
    char secret_hex[SSH_CRYPTO_ED25519_SECRETKEY_BYTES * 2 + 1];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <seed_hex>\n", argv[0]);
        return 2;
    }
    if (ssh_crypto_hex_to_bytes(argv[1], seed, sizeof(seed)) < 0) {
        fprintf(stderr, "invalid seed hex\n");
        return 2;
    }
    if (ssh_crypto_ed25519_seed_keypair(public_key, secret_key, seed) < 0) {
        fprintf(stderr, "failed to derive keypair\n");
        return 1;
    }

    to_hex(public_key, sizeof(public_key), public_hex);
    to_hex(secret_key, sizeof(secret_key), secret_hex);
    printf("public=%s\n", public_hex);
    printf("secret=%s\n", secret_hex);
    return 0;
}
