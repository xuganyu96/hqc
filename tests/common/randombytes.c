#define _DEFAULT_SOURCE

#include "randombytes.h"

#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "crypto_memset.h"
#include "fips202.h"

#define RANDOMBYTES_SEED_BYTES 32U
#define RANDOMBYTES_DOMAIN     0U

/**
 * @note Not thread-safe: uses global state for PRNG context and init flag.
 *       If you need per-thread state on Linux, use `__thread`:
 *       @code
 *       static __thread shake256incctx prng_ctx;
 *       static __thread int prng_initialized = 0;
 *       @endcode
 *       With C11, use `_Thread_local`:
 *       @code
 *       static _Thread_local shake256incctx prng_ctx;
 *       static _Thread_local int prng_initialized = 0;
 *       @endcode
 */
static shake256incctx prng_ctx;
static int prng_initialized = 0;

int randombytes_init(void);
void randombytes_seed(const uint8_t *seed, size_t seed_len);

static void prng_init(const uint8_t *seed, uint32_t seed_len) {
    uint8_t domain = RANDOMBYTES_DOMAIN;

    shake256_inc_init(&prng_ctx);
    if (seed != NULL && seed_len > 0) {
        shake256_inc_absorb(&prng_ctx, seed, seed_len);
    }
    shake256_inc_absorb(&prng_ctx, &domain, 1);
    shake256_inc_finalize(&prng_ctx);
}

static void prng_get_bytes(uint8_t *output, uint32_t outlen) {
    shake256_inc_squeeze(output, outlen, &prng_ctx);
}

/**
 * @brief Deterministically (re)seed the test PRNG (test-only helper).
 *
 * Unlike ::randombytes_init(), which seeds from OS entropy, this lets tests reseed
 * the stream with caller-provided material so that two subsequent sequences of
 * ::randombytes() calls are reproducible.
 *
 * @param[in] seed     Pointer to the seed material.
 * @param[in] seed_len Length of @p seed in bytes.
 */
void randombytes_seed(const uint8_t *seed, size_t seed_len) {
    prng_init(seed, (uint32_t)seed_len);
    prng_initialized = 1;
}

int randombytes_init(void) {
    uint8_t seed[RANDOMBYTES_SEED_BYTES] = {0};
    size_t filled = 0;

    while (filled < sizeof seed) {
        ssize_t got = syscall(SYS_getrandom, seed + filled, sizeof seed - filled, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            memset_zero(seed, sizeof seed);
            return -1;
        }
        filled += (size_t)got;
    }

    prng_init(seed, sizeof seed);
    memset_zero(seed, sizeof seed);
    prng_initialized = 1;

    return 0;
}

int randombytes(uint8_t *out, size_t outlen) {
    if (outlen == 0) {
        return 0;
    }
    if (out == NULL) {
        return -1;
    }
    if (!prng_initialized) {
        if (randombytes_init() != 0) {
            return -1;
        }
    }

    prng_get_bytes(out, (uint32_t)outlen);

    return 0;
}
