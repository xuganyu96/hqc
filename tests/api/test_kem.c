/**
 * @file test_kem.c
 * @brief MUnit tests for KEM API correctness and robustness against corruption.
 *
 * This suite covers:
 * - Positive path KEM API (`keypair`, `enc`, `dec`) over configurable iterations.
 * - Ciphertext corruption tests:
 *   - Single-field corruption in **u**, **v**, and **salt**.
 *   - Double-field corruption in **(u & v)**, **(v & salt)**, and **(u & salt)**.
 * - Secret key component corruption tests for a dk_kem laid out as:
 *   @code
 *   dk_kem = [ ek_kem | dk_pke | sigma | seed_kem ]
 *   @endcode
 *   Expected outcomes:
 *   - Corrupting **ek_kem**   → decapsulation fails (shared secrets differ).
 *   - Corrupting **dk_pke**   → decapsulation fails (shared secrets differ).
 *   - Corrupting **sigma**    → decapsulation succeeds (shared secrets equal).
 *   - Corrupting **seed_kem** → decapsulation succeeds (shared secrets equal).
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
        munit_assert_int(hqc_kem_encaps(ct, ss_enc, pk), ==, 0);
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
    munit_assert_int(hqc_kem_encaps(ct, ss_enc, pk), ==, 0);

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
 * @brief Corrupt each component of the secret key and assert expected outcomes.
 *
 * The KEM decapsulation key (dk_kem) is serialized as:
 * @code
 * dk_kem = [ ek_kem | dk_pke | sigma | seed_kem ]
 * @endcode
 *
 * Expectations:
 * - **ek_kem** corruption  → decapsulation fails (ss differs).
 * - **dk_pke** corruption  → decapsulation fails (ss differs).
 * - **sigma** corruption   → decapsulation succeeds (ss equal).
 * - **seed_kem** corruption → decapsulation succeeds (ss equal).
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
    /* Layout offsets inside dk_kem (sk) */
    const size_t off_ek_kem = 0U;
    const size_t len_ek_kem = PUBLIC_KEY_BYTES_KEM;

    const size_t off_dk_pke = off_ek_kem + len_ek_kem;
    const size_t len_dk_pke = SECRET_KEY_BYTES_PKE;

    const size_t off_sigma = off_dk_pke + len_dk_pke;
    const size_t len_sigma = PARAM_K;

    const size_t off_seed_kem = off_sigma + len_sigma;
    const size_t len_seed_kem = SEED_BYTES;

    munit_assert_int(randombytes_init(), ==, 0);

    munit_assert_int(hqc_kem_keygen(pk, sk), ==, 0);
    munit_assert_int(hqc_kem_encaps(ct, ss_enc, pk), ==, 0);

    /* ek_kem → expect failure */
    memcpy(sk_bad, sk, sizeof sk_bad);
    flip_random_bit(sk_bad, off_ek_kem, len_ek_kem);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk_bad, ct), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* dk_pke → expect failure */
    memcpy(sk_bad, sk, sizeof sk_bad);
    flip_random_bit(sk_bad, off_dk_pke, len_dk_pke);
    memcpy(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk_bad, ct), ==, 0);
    munit_assert_memory_not_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* sigma → expect success */
    memcpy(sk_bad, sk, sizeof sk_bad);
    flip_random_bit(sk_bad, off_sigma, len_sigma);
    fill_bitwise_not(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk_bad, ct), ==, 0);
    munit_assert_memory_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

    /* seed_kem → expect success */
    memcpy(sk_bad, sk, sizeof sk_bad);
    flip_random_bit(sk_bad, off_seed_kem, len_seed_kem);
    fill_bitwise_not(ss_dec, ss_enc, sizeof ss_dec);
    munit_assert_int(hqc_kem_decaps(ss_dec, sk_bad, ct), ==, 0);
    munit_assert_memory_equal(SHARED_SECRET_BYTES_KEM, ss_enc, ss_dec);

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
                         MUNIT_TEST_ENTRY("kem_sk_corruption", test_kem_sk_corruption), MUNIT_TEST_END};
