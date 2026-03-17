/*
 * entropy.c - Entropy collection and PRNG for userland
 *
 * Collects entropy from PIT tick jitter via get_kernel_tick() syscall.
 * Uses SHA-256 to compress pool into PRNG seed.
 * PRNG is AES-CTR style using repeated SHA-256 hashing.
 */

#include <entropy.h>
#include <string.h>

#ifndef TEST_BUILD
#include <debug.h>
#include <stdlib.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
static u_int32_t mock_tick = 1000;
static u_int32_t get_kernel_tick(void) { return mock_tick++; }
#endif

/* ---- SHA-256 (minimal inline for userland) ---- */
/* We implement a minimal SHA-256 here since the kernel's sha256.c
 * is not available in userland. */

static const u_int32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x) (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x) (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static void sha256_hash(const u_int8_t *data, int len, u_int8_t out[32])
{
    u_int32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    u_int8_t block[64];
    u_int32_t w[64];
    int total_len = len;
    int pos = 0;
    int i;

    while (1) {
        int chunk = (len - pos > 64) ? 64 : (len - pos);
        int last = 0;

        if (chunk < 64) {
            memset(block, 0, 64);
            memcpy(block, data + pos, chunk);
            block[chunk] = 0x80;
            if (chunk < 56) {
                /* Length fits in this block */
                u_int32_t bit_len = total_len * 8;
                block[60] = (bit_len >> 24) & 0xFF;
                block[61] = (bit_len >> 16) & 0xFF;
                block[62] = (bit_len >> 8) & 0xFF;
                block[63] = bit_len & 0xFF;
                last = 1;
            } else {
                /* Need one more block for length */
                /* Process this block, then another with just length */
            }
            pos = len;
        } else {
            memcpy(block, data + pos, 64);
            pos += 64;
        }

        /* Parse block into 16 words */
        for (i = 0; i < 16; i++)
            w[i] = ((u_int32_t)block[i*4] << 24) |
                   ((u_int32_t)block[i*4+1] << 16) |
                   ((u_int32_t)block[i*4+2] << 8) |
                   block[i*4+3];
        for (i = 16; i < 64; i++)
            w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

        {
            u_int32_t a = h[0], b = h[1], c = h[2], d = h[3];
            u_int32_t e = h[4], f = h[5], g = h[6], hh = h[7];
            for (i = 0; i < 64; i++) {
                u_int32_t t1 = hh + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
                u_int32_t t2 = EP0(a) + MAJ(a, b, c);
                hh = g; g = f; f = e; e = d + t1;
                d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        if (last || (pos >= len && chunk == 64)) {
            if (chunk == 64 && pos >= len && !last) {
                /* Need padding block */
                memset(block, 0, 64);
                if (chunk == 64 && len % 64 == 0)
                    block[0] = 0x80;
                {
                    u_int32_t bit_len = total_len * 8;
                    block[60] = (bit_len >> 24) & 0xFF;
                    block[61] = (bit_len >> 16) & 0xFF;
                    block[62] = (bit_len >> 8) & 0xFF;
                    block[63] = bit_len & 0xFF;
                }
                for (i = 0; i < 16; i++)
                    w[i] = ((u_int32_t)block[i*4] << 24) |
                           ((u_int32_t)block[i*4+1] << 16) |
                           ((u_int32_t)block[i*4+2] << 8) |
                           block[i*4+3];
                for (i = 16; i < 64; i++)
                    w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];
                {
                    u_int32_t a = h[0], b = h[1], c = h[2], d = h[3];
                    u_int32_t e = h[4], f = h[5], g = h[6], hh = h[7];
                    for (i = 0; i < 64; i++) {
                        u_int32_t t1 = hh + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
                        u_int32_t t2 = EP0(a) + MAJ(a, b, c);
                        hh = g; g = f; f = e; e = d + t1;
                        d = c; c = b; b = a; a = t1 + t2;
                    }
                    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
                    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
                }
            }
            break;
        }
    }

    for (i = 0; i < 8; i++) {
        out[i*4] = (h[i] >> 24) & 0xFF;
        out[i*4+1] = (h[i] >> 16) & 0xFF;
        out[i*4+2] = (h[i] >> 8) & 0xFF;
        out[i*4+3] = h[i] & 0xFF;
    }
}

/* ---- Entropy Pool ---- */

static u_int8_t entropy_pool[ENTROPY_POOL_SIZE];
static int entropy_collected_bits = 0;
static int entropy_write_pos = 0;

void entropy_init(void)
{
    memset(entropy_pool, 0, sizeof(entropy_pool));
    entropy_collected_bits = 0;
    entropy_write_pos = 0;
}

void entropy_add(u_int8_t byte, int estimated_bits)
{
    entropy_pool[entropy_write_pos] ^= byte;
    entropy_write_pos = (entropy_write_pos + 1) % ENTROPY_POOL_SIZE;
    entropy_collected_bits += estimated_bits;
}

void entropy_add_bytes(const u_int8_t *data, int len, int estimated_bits)
{
    int i;
    for (i = 0; i < len; i++)
        entropy_add(data[i], 0);
    entropy_collected_bits += estimated_bits;
}

int entropy_ready(void)
{
    return entropy_collected_bits >= 256;
}

int entropy_bits(void)
{
    return entropy_collected_bits;
}

void entropy_get_seed(u_int8_t *seed, int len)
{
    u_int8_t hash[32];
    sha256_hash(entropy_pool, ENTROPY_POOL_SIZE, hash);

    if (len > 32)
        len = 32;
    memcpy(seed, hash, len);

    /* Feedback: XOR hash back into pool to update state */
    {
        int i;
        for (i = 0; i < 32 && i < ENTROPY_POOL_SIZE; i++)
            entropy_pool[i] ^= hash[i];
    }
}

/* ---- PIT Jitter Collection ---- */

void entropy_collect_jitter(int samples)
{
    int i;
    u_int32_t prev_tick, curr_tick;
    u_int8_t diff;

    prev_tick = get_kernel_tick();
    for (i = 0; i < samples; i++) {
        /* Small busy loop to create timing variation */
        {
            volatile int j;
            for (j = 0; j < 10 + (i & 0xF); j++)
                ;
        }
        curr_tick = get_kernel_tick();
        diff = (u_int8_t)((curr_tick ^ prev_tick) & 0xFF);
        entropy_add(diff, 1);  /* ~1 bit per sample */
        prev_tick = curr_tick;
    }

    debug_printf("[ENTROPY] collected %d bits from %d jitter samples\n",
                entropy_bits(), samples);
}

/* ---- PRNG ---- */

static u_int8_t prng_state[32];
static u_int32_t prng_counter = 0;
static int prng_seeded = 0;

int prng_init(void)
{
    if (!entropy_ready()) {
        debug_printf("[PRNG] not enough entropy (%d bits)\n", entropy_bits());
        return -1;
    }

    entropy_get_seed(prng_state, 32);
    prng_counter = 0;
    prng_seeded = 1;

    debug_printf("[PRNG] initialized with %d bits of entropy\n", entropy_bits());
    return 0;
}

void prng_bytes(u_int8_t *out, int len)
{
    u_int8_t block[32];
    u_int8_t input[36];  /* state(32) + counter(4) */
    int copied = 0;

    while (copied < len) {
        memcpy(input, prng_state, 32);
        input[32] = (prng_counter >> 24) & 0xFF;
        input[33] = (prng_counter >> 16) & 0xFF;
        input[34] = (prng_counter >> 8) & 0xFF;
        input[35] = prng_counter & 0xFF;
        prng_counter++;

        sha256_hash(input, 36, block);

        {
            int chunk = len - copied;
            if (chunk > 32)
                chunk = 32;
            memcpy(out + copied, block, chunk);
            copied += chunk;
        }
    }
}
