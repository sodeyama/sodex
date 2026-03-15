#ifndef _SSH_CRYPTO_H
#define _SSH_CRYPTO_H

#ifdef TEST_BUILD
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u_int8_t;
typedef uint32_t u_int32_t;
#ifndef PUBLIC
#define PUBLIC
#endif
#else
#include <sodex/const.h>
#include <types.h>
typedef u_int8_t uint8_t;
typedef u_int32_t uint32_t;
#endif

#include <aes.h>

#define SSH_CRYPTO_SEED_BYTES 32
#define SSH_CRYPTO_SHA256_BYTES 32
#define SSH_CRYPTO_ED25519_PUBLICKEY_BYTES 32
#define SSH_CRYPTO_ED25519_SECRETKEY_BYTES 64
#define SSH_CRYPTO_ED25519_SIGNATURE_BYTES 64
#define SSH_CRYPTO_CURVE25519_BYTES 32

struct ssh_aes_ctr_ctx {
  struct AES_ctx aes;
};

PUBLIC int ssh_crypto_hex_to_bytes(const char *hex, uint8_t *out, int out_len);
PUBLIC void ssh_crypto_sha256(uint8_t out[SSH_CRYPTO_SHA256_BYTES],
                              const uint8_t *data, size_t len);
PUBLIC void ssh_crypto_hmac_sha256(uint8_t out[SSH_CRYPTO_SHA256_BYTES],
                                   const uint8_t *key, size_t key_len,
                                   const uint8_t *data, size_t data_len);
PUBLIC void ssh_crypto_random_seed(const uint8_t seed[SSH_CRYPTO_SEED_BYTES]);
PUBLIC void ssh_crypto_random_fill(uint8_t *out, size_t len);
PUBLIC int ssh_crypto_ed25519_seed_keypair(
    uint8_t public_key[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES],
    uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES],
    const uint8_t seed[SSH_CRYPTO_SEED_BYTES]);
PUBLIC int ssh_crypto_ed25519_sign(
    uint8_t signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES],
    const uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES],
    const uint8_t *message, size_t message_len);
PUBLIC int ssh_crypto_curve25519_public_key(
    uint8_t public_key[SSH_CRYPTO_CURVE25519_BYTES],
    const uint8_t secret_key[SSH_CRYPTO_CURVE25519_BYTES]);
PUBLIC int ssh_crypto_curve25519_shared(
    uint8_t shared_secret[SSH_CRYPTO_CURVE25519_BYTES],
    const uint8_t secret_key[SSH_CRYPTO_CURVE25519_BYTES],
    const uint8_t public_key[SSH_CRYPTO_CURVE25519_BYTES]);
PUBLIC void ssh_crypto_aes128_ctr_init(struct ssh_aes_ctr_ctx *ctx,
                                       const uint8_t key[16],
                                       const uint8_t iv[16]);
PUBLIC void ssh_crypto_aes128_ctr_xcrypt(struct ssh_aes_ctr_ctx *ctx,
                                         uint8_t *buf, size_t len);

#endif
