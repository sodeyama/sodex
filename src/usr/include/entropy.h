#ifndef _ENTROPY_H
#define _ENTROPY_H

#include <sys/types.h>

#define ENTROPY_POOL_SIZE  64   /* bytes */
#define PRNG_SEED_SIZE     32   /* bytes (256 bits) */

/* ---- Entropy pool ---- */

/* Initialize entropy pool */
void entropy_init(void);

/* Add a byte of entropy with estimated bit count */
void entropy_add(u_int8_t byte, int estimated_bits);

/* Add multiple bytes of entropy */
void entropy_add_bytes(const u_int8_t *data, int len, int estimated_bits);

/* Check if pool has enough entropy (>= 256 bits) */
int entropy_ready(void);

/* Get collected bit count */
int entropy_bits(void);

/* Extract seed from pool via SHA-256. Returns 32 bytes. */
void entropy_get_seed(u_int8_t *seed, int len);

/* ---- PRNG (AES-CTR based) ---- */

/* Initialize PRNG from entropy pool. Returns 0 on success, -1 if not ready. */
int prng_init(void);

/* Generate random bytes */
void prng_bytes(u_int8_t *out, int len);

/* Collect entropy from PIT jitter (call during startup) */
void entropy_collect_jitter(int samples);

#endif /* _ENTROPY_H */
