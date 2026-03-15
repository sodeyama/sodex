#include "test_framework.h"

#include <stdint.h>
#include <string.h>

#define TEST_BUILD 1
#define PUBLIC
#define PRIVATE static
#include "../src/include/ssh_crypto.h"

static int from_hex(const char *hex, uint8_t *out, int out_len)
{
    return ssh_crypto_hex_to_bytes(hex, out, out_len);
}

TEST(sha256_matches_known_vector) {
    uint8_t digest[SSH_CRYPTO_SHA256_BYTES];
    uint8_t expected[SSH_CRYPTO_SHA256_BYTES];

    ASSERT_EQ(from_hex(
                  "ba7816bf8f01cfea414140de5dae2223"
                  "b00361a396177a9cb410ff61f20015ad",
                  expected, sizeof(expected)),
              0);
    ssh_crypto_sha256(digest, (const uint8_t *)"abc", 3);
    ASSERT_EQ(memcmp(digest, expected, sizeof(digest)), 0);
}

TEST(hmac_sha256_matches_known_vector) {
    uint8_t digest[SSH_CRYPTO_SHA256_BYTES];
    uint8_t expected[SSH_CRYPTO_SHA256_BYTES];
    const char *message = "The quick brown fox jumps over the lazy dog";

    ASSERT_EQ(from_hex(
                  "f7bc83f430538424b13298e6aa6fb143"
                  "ef4d59a14946175997479dbc2d1a3cd8",
                  expected, sizeof(expected)),
              0);
    ssh_crypto_hmac_sha256(digest,
                           (const uint8_t *)"key", 3,
                           (const uint8_t *)message, strlen(message));
    ASSERT_EQ(memcmp(digest, expected, sizeof(digest)), 0);
}

TEST(aes128_ctr_matches_nist_vector) {
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t input[64];
    uint8_t expected[64];
    struct ssh_aes_ctr_ctx ctx;

    ASSERT_EQ(from_hex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key)), 0);
    ASSERT_EQ(from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv, sizeof(iv)), 0);
    ASSERT_EQ(from_hex(
                  "6bc1bee22e409f96e93d7e117393172a"
                  "ae2d8a571e03ac9c9eb76fac45af8e51"
                  "30c81c46a35ce411e5fbc1191a0a52ef"
                  "f69f2445df4f9b17ad2b417be66c3710",
                  input, sizeof(input)),
              0);
    ASSERT_EQ(from_hex(
                  "874d6191b620e3261bef6864990db6ce"
                  "9806f66b7970fdff8617187bb9fffdff"
                  "5ae4df3edbd5d35e5b4f09020db03eab"
                  "1e031dda2fbe03d1792170a0f3009cee",
                  expected, sizeof(expected)),
              0);

    ssh_crypto_aes128_ctr_init(&ctx, key, iv);
    ssh_crypto_aes128_ctr_xcrypt(&ctx, input, sizeof(input));
    ASSERT_EQ(memcmp(input, expected, sizeof(input)), 0);
}

TEST(ed25519_seed_keypair_matches_rfc8032_vector) {
    uint8_t seed[SSH_CRYPTO_SEED_BYTES];
    uint8_t public_key[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES];
    uint8_t secret_key[SSH_CRYPTO_ED25519_SECRETKEY_BYTES];
    uint8_t expected_public[SSH_CRYPTO_ED25519_PUBLICKEY_BYTES];
    uint8_t signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES];
    uint8_t expected_signature[SSH_CRYPTO_ED25519_SIGNATURE_BYTES];

    ASSERT_EQ(from_hex(
                  "9d61b19deffd5a60ba844af492ec2cc4"
                  "4449c5697b326919703bac031cae7f60",
                  seed, sizeof(seed)),
              0);
    ASSERT_EQ(from_hex(
                  "d75a980182b10ab7d54bfed3c964073a"
                  "0ee172f3daa62325af021a68f707511a",
                  expected_public, sizeof(expected_public)),
              0);
    ASSERT_EQ(from_hex(
                  "e5564300c360ac729086e2cc806e828a"
                  "84877f1eb8e5d974d873e06522490155"
                  "5fb8821590a33bacc61e39701cf9b46b"
                  "d25bf5f0595bbe24655141438e7a100b",
                  expected_signature, sizeof(expected_signature)),
              0);

    ASSERT_EQ(ssh_crypto_ed25519_seed_keypair(public_key, secret_key, seed), 0);
    ASSERT_EQ(memcmp(public_key, expected_public, sizeof(public_key)), 0);
    ASSERT_EQ(memcmp(secret_key, seed, sizeof(seed)), 0);
    ASSERT_EQ(memcmp(secret_key + 32, expected_public, sizeof(public_key)), 0);

    ASSERT_EQ(ssh_crypto_ed25519_sign(signature, secret_key, (const uint8_t *)"", 0), 0);
    ASSERT_EQ(memcmp(signature, expected_signature, sizeof(signature)), 0);
}

TEST(curve25519_matches_rfc7748_vector) {
    uint8_t alice_secret[SSH_CRYPTO_CURVE25519_BYTES];
    uint8_t alice_public[SSH_CRYPTO_CURVE25519_BYTES];
    uint8_t expected_public[SSH_CRYPTO_CURVE25519_BYTES];
    uint8_t bob_secret[SSH_CRYPTO_CURVE25519_BYTES];
    uint8_t bob_public[SSH_CRYPTO_CURVE25519_BYTES];
    uint8_t shared[SSH_CRYPTO_CURVE25519_BYTES];
    uint8_t expected_shared[SSH_CRYPTO_CURVE25519_BYTES];

    ASSERT_EQ(from_hex(
                  "77076d0a7318a57d3c16c17251b26645"
                  "df4c2f87ebc0992ab177fba51db92c2a",
                  alice_secret, sizeof(alice_secret)),
              0);
    ASSERT_EQ(from_hex(
                  "8520f0098930a754748b7ddcb43ef75a"
                  "0dbf3a0d26381af4eba4a98eaa9b4e6a",
                  expected_public, sizeof(expected_public)),
              0);
    ASSERT_EQ(from_hex(
                  "5dab087e624a8a4b79e17f8b83800ee6"
                  "6f3bb1292618b6fd1c2f8b27ff88e0eb",
                  bob_secret, sizeof(bob_secret)),
              0);
    ASSERT_EQ(from_hex(
                  "de9edb7d7b7dc1b4d35b61c2ece43537"
                  "3f8343c85b78674dadfc7e146f882b4f",
                  bob_public, sizeof(bob_public)),
              0);
    ASSERT_EQ(from_hex(
                  "4a5d9d5ba4ce2de1728e3bf480350f25"
                  "e07e21c947d19e3376f09b3c1e161742",
                  expected_shared, sizeof(expected_shared)),
              0);

    ASSERT_EQ(ssh_crypto_curve25519_public_key(alice_public, alice_secret), 0);
    ASSERT_EQ(memcmp(alice_public, expected_public, sizeof(alice_public)), 0);
    ASSERT_EQ(ssh_crypto_curve25519_shared(shared, alice_secret, bob_public), 0);
    ASSERT_EQ(memcmp(shared, expected_shared, sizeof(shared)), 0);
}

int main(void) {
    RUN_TEST(sha256_matches_known_vector);
    RUN_TEST(hmac_sha256_matches_known_vector);
    RUN_TEST(aes128_ctr_matches_nist_vector);
    RUN_TEST(ed25519_seed_keypair_matches_rfc8032_vector);
    RUN_TEST(curve25519_matches_rfc7748_vector);
    TEST_REPORT();
}
