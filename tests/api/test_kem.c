/**
 * @file test_kem.c
 * @brief MUnit tests for KEM API correctness and robustness against corruption.
 *
 * This suite covers:
 * - Positive path KEM API (`keypair`, `enc`, `dec`) over configurable iterations.
 * - Ciphertext corruption tests:
 *   - Single-field corruption in **u**, **v**, and **salt**.
 *   - Double-field corruption in **(u & v)**, **(v & salt)**, and **(u & salt)**.
 * - Secret key corruption tests: `dk_kem` is the KEM master seed `seed_kem`, from
 *   which `ek_kem`, `dk_pke`, and `sigma` are all re-derived. Corrupting any bit of it
 *   is expected to make decapsulation fail (shared secrets differ).
 * - Keygen determinism: reseeding the PRNG identically before two `hqc_kem_keygen`
 *   calls must yield byte-identical `(ek_kem, dk_kem)` pairs.
 * - Implicit-rejection determinism: repeated decapsulations of the same corrupted
 *   ciphertext (or same corrupted key) must agree with each other, from both sides.
 *
 * @note These tests assert success/failure via equality/inequality of shared secrets.
 *       If the implementation returns a fixed key on failure, the inequality checks
 *       still hold w.h.p. across random encapsulations.
 */

#define _DEFAULT_SOURCE

#include <munit.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hqc-kem.h"
#include "munit_utils.h"
#include "parameters.h"

/** @defgroup helpers Internal helpers
 *  @brief Utilities used by tests.
 *  @{
 */

/**
 * @brief Flip a random bit within a contiguous region of a buffer.
 *
 * Picks a uniformly random byte in \p buf[start, start+len) and flips a uniformly
 * random bit in that byte.
 *
 * @param[in,out] buf   Target buffer.
 * @param[in]     start Region start offset (in bytes) within @p buf.
 * @param[in]     len   Region length (in bytes); must be > 0.
 */
static inline void flip_random_bit(uint8_t* buf, size_t start, size_t len) {
    uint32_t idx = munit_rand_uint32() % (uint32_t)len;
    uint8_t bit = (uint8_t)(1u << (munit_rand_uint32() & 7u));
    buf[start + idx] ^= bit;
}

/**
 * @brief Fill a buffer with the bitwise complement of another buffer.
 *
 * This is used to prefill decapsulation outputs with a value guaranteed to differ
 * from a known shared secret. That way, a decapsulation routine that fails to
 * write output cannot accidentally pass an equality assertion.
 *
 * @param[out] dst Destination buffer.
 * @param[in]  src Source buffer.
 * @param[in]  len Number of bytes to copy.
 */
static inline void fill_bitwise_not(uint8_t* dst, const uint8_t* src, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        dst[i] = (uint8_t)~src[i];
    }
}

/** @} */ /* end of helpers */

extern int randombytes_init(void);
extern void randombytes_seed(const uint8_t* seed, size_t seed_len);

/** @defgroup tests MUnit tests
 *  @brief Test cases for KEM API and corruption behavior.
 *  @{
 */

/**
 * @brief Positive-path test of the KEM API (keypair/enc/dec) over multiple iterations.
 *
 * @param params     MUnit parameter array; supports `iterations` (stringified int > 0).
 * @param user_data  Unused.
 * @return MUNIT_OK on success; failures are asserted.
 *
 * @test Ensures that for a valid `(pk, sk)` and ciphertext `ct`, the shared secret
 *       produced by `hqc_kem_encaps` equals that from `hqc_kem_decaps`.
 */
static MunitResult test_kem_api(const MunitParameter params[], void* user_data) {
    (void)user_data;

    int iterations = 1;
    const char* iter_param = munit_parameters_get(params, "iterations");
    if (iter_param != NULL) {
        int parsed = atoi(iter_param);
        if (parsed > 0)
            iterations = parsed;
    }

    munit_assert_int(randombytes_init(), ==, 0);

    for (int run = 0; run < iterations; run++) {
        uint8_t pk[PUBLIC_KEY_BYTES_KEM] = {0};
        uint8_t sk[SECRET_KEY_BYTES_KEM] = {0};
        uint8_t ct[CIPHERTEXT_BYTES_KEM] = {0};
        uint8_t ss_enc[SHARED_SECRET_BYTES_KEM] = {0};
        uint8_t ss_dec[SHARED_SECRET_BYTES_KEM] = {0};

        munit_assert_int(hqc_kem_keygen(pk, sk), ==, 0);
        munit_assert_int(hqc_kem_encaps(ss_enc, ct, pk), ==, 0);
        fill_bitwise_not(ss_dec, ss_enc, sizeof ss_dec);
        munit_assert_int(hqc_kem_decaps(ss_dec, sk, ct), ==, 0);

        munit_assert_memory_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);
    }

    return MUNIT_OK;
}

/**
 * @brief Corrupt ciphertext components and assert decapsulation failure.
 *
 * The ciphertext is laid out as:
 * @code
 * ct = [ u | v | salt ]
 *     [ 0..VEC_N_SIZE_BYTES-1 |
 *       VEC_N_SIZE_BYTES..VEC_N_SIZE_BYTES+VEC_N1N2_SIZE_BYTES-1 |
 *       ... + SALT_BYTES - 1 ]
 * @endcode
 *
 * This test flips a random bit in:
 *  - Single components: **u**, **v**, **salt**
 *  - Double combinations: **(u & v)**, **(v & salt)**, **(u & salt)**
 *
 * For all these corruptions, decapsulation must fail (shared secrets differ).
 *
 * @param params     Unused.
 * @param user_data  Unused.
 * @return MUNIT_OK on success; failures are asserted.
 */
static MunitResult test_kem_ct_corruption(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    uint8_t pk[PUBLIC_KEY_BYTES_KEM] = {0};
    uint8_t sk[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t ct[CIPHERTEXT_BYTES_KEM] = {0};
    uint8_t mutated[CIPHERTEXT_BYTES_KEM] = {0};
    uint8_t ss_enc[SHARED_SECRET_BYTES_KEM] = {0};
    uint8_t ss_dec[SHARED_SECRET_BYTES_KEM] = {0};
    /* Component offsets within ct */
    const size_t off_u = 0U;
    const size_t len_u = VEC_N_SIZE_BYTES;
    const size_t off_v = off_u + len_u;
    const size_t len_v = VEC_N1N2_SIZE_BYTES;
    const size_t off_salt = off_v + len_v;
    const size_t len_salt = SALT_BYTES;

    munit_assert_int(randombytes_init(), ==, 0);

    munit_assert_int(hqc_kem_keygen(pk, sk), ==, 0);
    munit_assert_int(hqc_kem_encaps(ss_enc, ct, pk), ==, 0);

    /* Corrupt u */
    memcpy(mutated, ct, CIPHERTEXT_BYTES_KEM);
    flip_random_bit(mutated, off_u, len_u);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk, mutated), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* Corrupt v */
    memcpy(mutated, ct, CIPHERTEXT_BYTES_KEM);
    flip_random_bit(mutated, off_v, len_v);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk, mutated), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* Corrupt salt */
    memcpy(mutated, ct, CIPHERTEXT_BYTES_KEM);
    flip_random_bit(mutated, off_salt, len_salt);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk, mutated), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* Corrupt u & v */
    memcpy(mutated, ct, CIPHERTEXT_BYTES_KEM);
    flip_random_bit(mutated, off_u, len_u);
    flip_random_bit(mutated, off_v, len_v);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk, mutated), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* Corrupt v & salt */
    memcpy(mutated, ct, CIPHERTEXT_BYTES_KEM);
    flip_random_bit(mutated, off_v, len_v);
    flip_random_bit(mutated, off_salt, len_salt);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk, mutated), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* Corrupt u & salt */
    memcpy(mutated, ct, CIPHERTEXT_BYTES_KEM);
    flip_random_bit(mutated, off_u, len_u);
    flip_random_bit(mutated, off_salt, len_salt);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk, mutated), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    return MUNIT_OK;
}

/**
 * @brief Corrupt the secret key and assert decapsulation fails.
 *
 * The KEM decapsulation key (`dk_kem`) is the KEM master seed `seed_kem`, from which
 * `ek_kem`, `dk_pke`, and `sigma` are all re-derived. Flipping any single bit of it is
 * expected to change all of those derived values, so decapsulation must fail
 * (shared secrets differ).
 *
 * @param params     Unused.
 * @param user_data  Unused.
 * @return MUNIT_OK on success; failures are asserted.
 */
static MunitResult test_kem_sk_corruption(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    uint8_t pk[PUBLIC_KEY_BYTES_KEM] = {0};
    uint8_t sk[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t sk_bad[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t ct[CIPHERTEXT_BYTES_KEM] = {0};
    uint8_t ss_enc[SHARED_SECRET_BYTES_KEM] = {0};
    uint8_t ss_dec[SHARED_SECRET_BYTES_KEM] = {0};

    munit_assert_int(randombytes_init(), ==, 0);

    munit_assert_int(hqc_kem_keygen(pk, sk), ==, 0);
    munit_assert_int(hqc_kem_encaps(ss_enc, ct, pk), ==, 0);

    /* dk_kem (seed_kem) → expect failure */
    memcpy(sk_bad, sk, sizeof sk_bad);
    flip_random_bit(sk_bad, 0U, SECRET_KEY_BYTES_KEM);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk_bad, ct), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    return MUNIT_OK;
}

/**
 * @brief Same `seed_kem` must yield identical `(ek_kem, dk_kem)` across keygen calls.
 *
 * `hqc_kem_keygen` samples `seed_kem` via `randombytes()` and derives every other
 * piece of key material from it (`ek_kem`, `dk_pke`, `sigma`). This reseeds the test
 * PRNG identically before two consecutive keygen calls and asserts both produce
 * byte-identical keypairs, guarding the seed-expansion path against any accidental
 * source of nondeterminism (e.g. uninitialized memory).
 *
 * @param params     Unused.
 * @param user_data  Unused.
 * @return MUNIT_OK on success; failures are asserted.
 */
static MunitResult test_kem_keygen_determinism(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    static const uint8_t fixed_seed[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };
    uint8_t pk1[PUBLIC_KEY_BYTES_KEM] = {0};
    uint8_t sk1[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t pk2[PUBLIC_KEY_BYTES_KEM] = {0};
    uint8_t sk2[SECRET_KEY_BYTES_KEM] = {0};

    randombytes_seed(fixed_seed, sizeof fixed_seed);
    munit_assert_int(hqc_kem_keygen(pk1, sk1), ==, 0);

    randombytes_seed(fixed_seed, sizeof fixed_seed);
    munit_assert_int(hqc_kem_keygen(pk2, sk2), ==, 0);

    munit_assert_memory_equal(PUBLIC_KEY_BYTES_KEM, pk1, pk2);
    munit_assert_memory_equal(SECRET_KEY_BYTES_KEM, sk1, sk2);

    return MUNIT_OK;
}

/**
 * @brief Implicit rejection must be deterministic under repeated queries.
 *
 * HQC's IND-CCA2 argument relies on implicit rejection producing a *stable*
 * pseudorandom output for a given `(dk_kem, ciphertext)` pair: if the "wrong"
 * shared secret returned on rejection varied between calls, that would itself be a
 * distinguishing oracle. This exercises the rejection path from three angles:
 *  - Corrupting the **PKE ciphertext** (`u`/`v`) perturbs `hqc_pke_decrypt` itself,
 *    so `m_prime` comes out wrong (or decoding fails outright).
 *  - Corrupting the **salt** does *not* affect decryption (salt isn't a decrypt
 *    input), so `m_prime` is still correct; rejection instead comes purely from the
 *    FO ciphertext-consistency check failing because `theta'` was re-derived from
 *    the wrong salt.
 *  - Corrupting the **key** (`dk_kem`) perturbs the re-derived `ek_kem`/`dk_pke`/`sigma`.
 *
 * Each case asserts repeated decapsulations agree with each other while still
 * disagreeing with the original shared secret.
 *
 * @param params     Unused.
 * @param user_data  Unused.
 * @return MUNIT_OK on success; failures are asserted.
 */
static MunitResult test_kem_rejection_determinism(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    uint8_t pk[PUBLIC_KEY_BYTES_KEM] = {0};
    uint8_t sk[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t sk_bad[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t ct[CIPHERTEXT_BYTES_KEM] = {0};
    uint8_t ct_bad[CIPHERTEXT_BYTES_KEM] = {0};
    uint8_t ss_enc[SHARED_SECRET_BYTES_KEM] = {0};
    uint8_t ss_1[SHARED_SECRET_BYTES_KEM] = {0};
    uint8_t ss_2[SHARED_SECRET_BYTES_KEM] = {0};
    /* Component offsets within ct: [ pke_ct (u | v) | salt ] */
    const size_t off_pke_ct = 0U;
    const size_t len_pke_ct = VEC_N_SIZE_BYTES + VEC_N1N2_SIZE_BYTES;
    const size_t off_salt = off_pke_ct + len_pke_ct;
    const size_t len_salt = SALT_BYTES;

    munit_assert_int(randombytes_init(), ==, 0);

    munit_assert_int(hqc_kem_keygen(pk, sk), ==, 0);
    munit_assert_int(hqc_kem_encaps(ss_enc, ct, pk), ==, 0);

    /* Corrupted PKE ciphertext (u | v), valid key: rejection output must repeat. */
    memcpy(ct_bad, ct, sizeof ct_bad);
    flip_random_bit(ct_bad, off_pke_ct, len_pke_ct);
    munit_assert_int(hqc_kem_decaps(ss_1, sk, ct_bad), ==, 0);
    munit_assert_int(hqc_kem_decaps(ss_2, sk, ct_bad), ==, 0);
    munit_assert_memory_equal(SHARED_SECRET_BYTES_KEM, ss_1, ss_2);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_1);

    /* Corrupted salt only, valid key: rejection output must repeat. */
    memcpy(ct_bad, ct, sizeof ct_bad);
    flip_random_bit(ct_bad, off_salt, len_salt);
    munit_assert_int(hqc_kem_decaps(ss_1, sk, ct_bad), ==, 0);
    munit_assert_int(hqc_kem_decaps(ss_2, sk, ct_bad), ==, 0);
    munit_assert_memory_equal(SHARED_SECRET_BYTES_KEM, ss_1, ss_2);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_1);

    /* Valid ciphertext, corrupted key: rejection output must repeat. */
    memcpy(sk_bad, sk, sizeof sk_bad);
    flip_random_bit(sk_bad, 0U, SECRET_KEY_BYTES_KEM);
    munit_assert_int(hqc_kem_decaps(ss_1, sk_bad, ct), ==, 0);
    munit_assert_int(hqc_kem_decaps(ss_2, sk_bad, ct), ==, 0);
    munit_assert_memory_equal(SHARED_SECRET_BYTES_KEM, ss_1, ss_2);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_1);

    return MUNIT_OK;
}

/**
 * @brief Acceptable values for the `iterations` parameter (stringified integers).
 *
 * Extend as needed:
 * @code
 * static char* iteration_values[] = { (char*)"1", (char*)"10", (char*)"100", NULL };
 * @endcode
 */
static char* iteration_values[] = {(char*)"10", NULL};

/**
 * @brief Parameter schema for @ref test_kem_api.
 */
static MunitParameterEnum kem_api_params[] = {
    {(char*)"iterations", iteration_values},
    {NULL, NULL},
};

/**
 * @brief Test registry for the KEM API suite.
 */
MunitTest kem_tests[] = {MUNIT_TEST_ENTRY_ITER("kem_api", test_kem_api, kem_api_params),
                         MUNIT_TEST_ENTRY("kem_ct_corruption", test_kem_ct_corruption),
                         MUNIT_TEST_ENTRY("kem_sk_corruption", test_kem_sk_corruption),
                         MUNIT_TEST_ENTRY("kem_keygen_determinism", test_kem_keygen_determinism),
                         MUNIT_TEST_ENTRY("kem_rejection_determinism", test_kem_rejection_determinism),
                         MUNIT_TEST_END};
