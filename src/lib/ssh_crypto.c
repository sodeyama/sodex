#include <string.h>

#include <ssh_crypto.h>
#include <sha256.h>
#include <tweetnacl.h>

#define SSH_RANDOM_BLOCK_BYTES 32
#define SSH_SHA256_BLOCK_BYTES 64
#define SSH_ED25519_SIGN_INPUT_MAX 512

#ifndef PRIVATE
#define PRIVATE static
#endif

struct ssh_random_state {
  uint8_t seed[SSH_CRYPTO_SEED_BYTES];
  uint32_t counter;
  int seeded;
  int literal_pending;
  uint8_t literal_seed[SSH_CRYPTO_SEED_BYTES];
};

PRIVATE struct ssh_random_state ssh_random_state;
PRIVATE uint8_t ssh_ed25519_signed_message[SSH_ED25519_SIGN_INPUT_MAX +
                                           SSH_CRYPTO_ED25519_SIGNATURE_BYTES];

void randombytes(unsigned char *buf, unsigned long long len);

PRIVATE int ssh_crypto_hex_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

PRIVATE void ssh_crypto_random_block(uint8_t out[SSH_RANDOM_BLOCK_BYTES])
{
  uint8_t input[SSH_CRYPTO_SEED_BYTES + 4];

  memcpy(input, ssh_random_state.seed, SSH_CRYPTO_SEED_BYTES);
  input[32] = (uint8_t)(ssh_random_state.counter >> 24);
  input[33] = (uint8_t)(ssh_random_state.counter >> 16);
  input[34] = (uint8_t)(ssh_random_state.counter >> 8);
  input[35] = (uint8_t)ssh_random_state.counter;
  ssh_random_state.counter++;
  ssh_crypto_sha256(out, input, sizeof(input));
}

PRIVATE void ssh_crypto_random_seed_literal(
    const uint8_t seed[SSH_CRYPTO_SEED_BYTES])
{
  memcpy(ssh_random_state.seed, seed, SSH_CRYPTO_SEED_BYTES);
  memcpy(ssh_random_state.literal_seed, seed, SSH_CRYPTO_SEED_BYTES);
  ssh_random_state.counter = 0;
  ssh_random_state.seeded = 1;
  ssh_random_state.literal_pending = 1;
}

PUBLIC int ssh_crypto_hex_to_bytes(const char *hex, uint8_t *out, int out_len)
{
  int i;

  if (hex == 0 || out == 0 || out_len <= 0)
    return -1;
  if ((int)strlen(hex) != out_len * 2)
    return -1;

  for (i = 0; i < out_len; i++) {
    int hi = ssh_crypto_hex_value(hex[i * 2]);
    int lo = ssh_crypto_hex_value(hex[i * 2 + 1]);

    if (hi < 0 || lo < 0)
      return -1;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

PUBLIC void ssh_crypto_sha256(uint8_t out[SSH_CRYPTO_SHA256_BYTES],
                              const uint8_t *data, size_t len)
{
  SHA256_CTX ctx;

  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, out);
}

PUBLIC void ssh_crypto_hmac_sha256(uint8_t out[SSH_CRYPTO_SHA256_BYTES],
                                   const uint8_t *key, size_t key_len,
                                   const uint8_t *data, size_t data_len)
{
  uint8_t key_block[SSH_SHA256_BLOCK_BYTES];
  uint8_t inner_pad[SSH_SHA256_BLOCK_BYTES];
  uint8_t outer_pad[SSH_SHA256_BLOCK_BYTES];
  uint8_t inner_hash[SSH_CRYPTO_SHA256_BYTES];
  uint8_t inner_input[SSH_SHA256_BLOCK_BYTES + data_len];
  uint8_t outer_input[SSH_SHA256_BLOCK_BYTES + SSH_CRYPTO_SHA256_BYTES];
  size_t i;

  memset(key_block, 0, sizeof(key_block));
  if (key_len > SSH_SHA256_BLOCK_BYTES) {
    ssh_crypto_sha256(key_block, key, key_len);
  } else if (key_len > 0) {
    memcpy(key_block, key, key_len);
  }

  for (i = 0; i < SSH_SHA256_BLOCK_BYTES; i++) {
    inner_pad[i] = (uint8_t)(key_block[i] ^ 0x36);
    outer_pad[i] = (uint8_t)(key_block[i] ^ 0x5c);
  }

  memcpy(inner_input, inner_pad, SSH_SHA256_BLOCK_BYTES);
  if (data_len > 0)
    memcpy(inner_input + SSH_SHA256_BLOCK_BYTES, data, data_len);
  ssh_crypto_sha256(inner_hash, inner_input, sizeof(inner_input));

  memcpy(outer_input, outer_pad, SSH_SHA256_BLOCK_BYTES);
  memcpy(outer_input + SSH_SHA256_BLOCK_BYTES,
         inner_hash, SSH_CRYPTO_SHA256_BYTES);
  ssh_crypto_sha256(out, outer_input, sizeof(outer_input));
}

PUBLIC void ssh_crypto_random_seed(const uint8_t seed[SSH_CRYPTO_SEED_BYTES])
{
  memcpy(ssh_random_state.seed, seed, SSH_CRYPTO_SEED_BYTES);
  ssh_random_state.counter = 0;
  ssh_random_state.seeded = 1;
  ssh_random_state.literal_pending = 0;
}

PUBLIC void ssh_crypto_random_fill(uint8_t *out, size_t len)
{
  randombytes(out, (unsigned long long)len);
}

PUBLIC int ssh_crypto_ed25519_seed_keypair(
    uint8_t public_key[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES],
    uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES],
    const uint8_t seed[SSH_CRYPTO_SEED_BYTES])
{
  struct ssh_random_state saved_state = ssh_random_state;
  int result;

  ssh_crypto_random_seed_literal(seed);
  result = crypto_sign_keypair(public_key, secret_key);
  ssh_random_state = saved_state;
  return result;
}

PUBLIC int ssh_crypto_ed25519_sign(
    uint8_t signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES],
    const uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES],
    const uint8_t *message, size_t message_len)
{
  unsigned long long signed_len = 0;

  if (message_len > SSH_ED25519_SIGN_INPUT_MAX)
    return -1;

  if (crypto_sign(ssh_ed25519_signed_message, &signed_len,
                  message, (unsigned long long)message_len,
                  secret_key) != 0) {
    return -1;
  }
  if (signed_len < SSH_CRYPTO_ED25519_SIGNATURE_BYTES)
    return -1;

  memcpy(signature, ssh_ed25519_signed_message,
         SSH_CRYPTO_ED25519_SIGNATURE_BYTES);
  return 0;
}

PUBLIC int ssh_crypto_curve25519_public_key(
    uint8_t public_key[SSH_CRYPTO_CURVE25519_BYTES],
    const uint8_t secret_key[SSH_CRYPTO_CURVE25519_BYTES])
{
  return crypto_scalarmult_base(public_key, secret_key);
}

PUBLIC int ssh_crypto_curve25519_shared(
    uint8_t shared_secret[SSH_CRYPTO_CURVE25519_BYTES],
    const uint8_t secret_key[SSH_CRYPTO_CURVE25519_BYTES],
    const uint8_t public_key[SSH_CRYPTO_CURVE25519_BYTES])
{
  return crypto_scalarmult(shared_secret, secret_key, public_key);
}

PUBLIC void ssh_crypto_aes128_ctr_init(struct ssh_aes_ctr_ctx *ctx,
                                       const uint8_t key[16],
                                       const uint8_t iv[16])
{
  AES_init_ctx_iv(&ctx->aes, key, iv);
}

PUBLIC void ssh_crypto_aes128_ctr_xcrypt(struct ssh_aes_ctr_ctx *ctx,
                                         uint8_t *buf, size_t len)
{
  AES_CTR_xcrypt_buffer(&ctx->aes, buf, len);
}

void randombytes(unsigned char *buf, unsigned long long len)
{
  uint8_t block[SSH_RANDOM_BLOCK_BYTES];
  size_t written = 0;

  if (buf == 0)
    return;
  if (!ssh_random_state.seeded) {
    memset(buf, 0, (size_t)len);
    return;
  }

  if (ssh_random_state.literal_pending) {
    size_t copy_len = len;

    if (copy_len > SSH_CRYPTO_SEED_BYTES)
      copy_len = SSH_CRYPTO_SEED_BYTES;
    memcpy(buf, ssh_random_state.literal_seed, copy_len);
    written += copy_len;
    ssh_random_state.literal_pending = 0;
  }

  while (written < (size_t)len) {
    size_t copy_len = (size_t)len - written;

    ssh_crypto_random_block(block);
    if (copy_len > sizeof(block))
      copy_len = sizeof(block);
    memcpy(buf + written, block, copy_len);
    written += copy_len;
  }
}
