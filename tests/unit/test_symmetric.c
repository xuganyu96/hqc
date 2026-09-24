/**
 * @file test_symmetric.c
 * @brief Unit tests for the KEM seed-expansion XOF (`xof_init_dom_sep`).
 *
 * ## Covered functionality
 * - `xof_init_dom_sep` construction: the produced stream must equal a one-shot
 *   `SHAKE256(seed || k)` where `k` is the Reed-Solomon dimension `PARAM_K`
 *   encoded as a single byte (HQC-KEM.KeyGen_internal / Decaps_internal).
 * - Parameter-set separation: for a fixed seed, the stream produced for this
 *   build's `PARAM_K` must differ from the stream that any other approved RS
 *   dimension would produce, so a seed reused across parameter sets yields
 *   unrelated key material.
 *
 * @note One `xof_init_dom_sep` output is compared against `shake256()` from
 *       fips202 used as an independent reference. Output is squeezed across
 *       several SHAKE256 rate blocks (rate = 136 bytes) so the check covers more
 *       than the first permutation.
 */

#include <munit.h>
#include <stdint.h>
#include <string.h>

#include "fips202.h"
#include "munit_utils.h"
#include "parameters.h"
#include "symmetric.h"

/* SHAKE256 rate is 136 bytes; squeeze across several blocks. */
#define TEST_XOF_OUT_BYTES (4u * 136u)

/** @brief The approved Reed-Solomon dimensions across the three parameter sets. */
static const uint8_t rs_dimensions[3] = {16u, 24u, 32u};

_Static_assert(PARAM_K == 16 || PARAM_K == 24 || PARAM_K == 32,
               "PARAM_K must be one of the approved RS dimensions (16/24/32)");

/**
 * @brief `xof_init_dom_sep` must equal SHAKE256(seed || (uint8_t)PARAM_K).
 */
static MunitResult test_xof_init_dom_sep_construction(const MunitParameter params[], void *data) {
    (void)params;
    (void)data;

    uint8_t seed[SEED_BYTES];
    munit_rand_memory(sizeof seed, seed);

    uint8_t got[TEST_XOF_OUT_BYTES];
    shake256_xof_ctx ctx;
    xof_init_dom_sep(&ctx, seed, SEED_BYTES, PARAM_K);
    xof_get_bytes(&ctx, got, sizeof got);

    uint8_t buf[SEED_BYTES + 1];
    memcpy(buf, seed, SEED_BYTES);
    buf[SEED_BYTES] = (uint8_t)PARAM_K;

    uint8_t ref[TEST_XOF_OUT_BYTES];
    shake256(ref, sizeof ref, buf, sizeof buf);

    munit_assert_memory_equal(sizeof got, got, ref);
    return MUNIT_OK;
}

/**
 * @brief For a fixed seed, the KEM seed-expansion stream is bound to PARAM_K:
 *        it matches SHAKE256(seed || PARAM_K) and differs from SHAKE256(seed || k')
 *        for every other approved RS dimension k'.
 */
static MunitResult test_xof_dom_sep_param_separation(const MunitParameter params[], void *data) {
    (void)params;
    (void)data;

    uint8_t seed[SEED_BYTES];
    munit_rand_memory(sizeof seed, seed);

    uint8_t got[TEST_XOF_OUT_BYTES];
    shake256_xof_ctx ctx;
    xof_init_dom_sep(&ctx, seed, SEED_BYTES, PARAM_K);
    xof_get_bytes(&ctx, got, sizeof got);

    int matched = 0;
    for (size_t i = 0; i < sizeof rs_dimensions / sizeof rs_dimensions[0]; ++i) {
        uint8_t buf[SEED_BYTES + 1];
        memcpy(buf, seed, SEED_BYTES);
        buf[SEED_BYTES] = rs_dimensions[i];

        uint8_t ref[TEST_XOF_OUT_BYTES];
        shake256(ref, sizeof ref, buf, sizeof buf);

        if (rs_dimensions[i] == (uint8_t)PARAM_K) {
            munit_assert_memory_equal(sizeof got, got, ref);
            matched = 1;
        } else {
            munit_assert_memory_not_equal(sizeof got, got, ref);
        }
    }
    munit_assert_int(matched, ==, 1);
    return MUNIT_OK;
}

/**
 * @brief Test registry for the symmetric-primitives suite.
 */
MunitTest symmetric_tests[] = {MUNIT_TEST_ENTRY("xof_init_dom_sep/construction", test_xof_init_dom_sep_construction),
                               MUNIT_TEST_ENTRY("xof_init_dom_sep/param_separation", test_xof_dom_sep_param_separation),
                               MUNIT_TEST_END};
