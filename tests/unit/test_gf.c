/**
 * @file test_gf.c
 * @brief Unit test for GF(2^8) reduction with the field defined by @c PARAM_GF_POLY.
 *
 * @details
 * - The test exhaustively checks all 2^16 inputs.
 * - The reduction taps (feedback positions) are provided by
 *   ::gf_reduction_taps and interpreted relative to @c PARAM_M = 8.
 * - The reference routine reduces with the polynomial @c PARAM_GF_POLY
 *   (0x11B), shifting and xoring for each high bit.
 *
 * @see gf.h, parameters.h, munit.h
 */

#include <inttypes.h>
#include <munit.h>
#include <string.h>
#include "gf.h"
#include "munit_utils.h"
#include "parameters.h"

#ifdef REF_MODE
void gf_carryless_mul(uint8_t* c, uint8_t a, uint8_t b);
#endif

/**
 * @brief Reduce a 16-bit value into GF(2^m) with XOR/shift feedback.
 *
 * @param x 16-bit input to reduce.
 * @return The value reduced modulo the field polynomial (in the low @c PARAM_M bits).
 *
 */
uint16_t gf_reduce(uint16_t x);

/**
 * @brief Feedback tap positions (in descending order) used by ::gf_reduce.
 *
 */
static const uint8_t gf_reduction_taps[] = {4, 3, 1};

uint16_t gf_reduce(uint16_t x) {
    uint64_t mod;
    const int reduction_steps = 2;           /* For deg(x) = 2 * (PARAM_M - 1), reduce twice */
    const size_t gf_reduction_tap_count = 3; /* Number of feedback positions */

    for (int i = 0; i < reduction_steps; ++i) {
        mod = x >> PARAM_M;      /* Extract upper bits */
        x &= (1 << PARAM_M) - 1; /* Keep lower bits */
        x ^= mod;                /* Pre-XOR with no shift */

        uint16_t z1 = 0;
        for (size_t j = gf_reduction_tap_count; j; --j) {
            uint16_t z2 = gf_reduction_taps[j - 1];
            uint16_t dist = z2 - z1;
            mod <<= dist;
            x ^= mod;
            z1 = z2;
        }
    }
    return x;
}

/**
 * @brief Reference GF(2^8) reduction using explicit polynomial long division.
 *
 * @param x 16-bit value to reduce.
 * @return The reduced 8-bit value (low byte of @p x after reduction).
 */
static uint8_t ref_reduce(uint16_t x) {
    const uint16_t poly = PARAM_GF_POLY;
    for (int bit = 15; bit >= 8; --bit) {
        if (x & (1U << bit)) {
            x ^= poly << (bit - 8);
        }
    }
    return (uint8_t)x;
}

/**
 * @brief Reference carryless product of two GF(2^8) elements before reduction.
 *
 * @param a Left operand.
 * @param b Right operand.
 * @return Polynomial product over GF(2) with degree at most 14.
 */
static uint16_t ref_carryless_mul(uint8_t a, uint8_t b) {
    uint16_t product = 0;

    for (unsigned int bit = 0; bit < 8; ++bit) {
        if ((b >> bit) & 1U) {
            product ^= (uint16_t)a << bit;
        }
    }

    return product;
}

/**
 * @brief Reference GF(2^8) multiplication using polynomial multiplication and reduction.
 *
 * @param a Left operand.
 * @param b Right operand.
 * @return Product reduced modulo @c PARAM_GF_POLY.
 */
static uint8_t ref_mul(uint8_t a, uint8_t b) {
    return ref_reduce(ref_carryless_mul(a, b));
}

/**
 * @brief Reference exponentiation in GF(2^8).
 *
 * @param a Base element.
 * @param exponent Non-negative exponent.
 * @return a raised to @p exponent in GF(2^8).
 */
static uint8_t ref_pow(uint8_t a, uint16_t exponent) {
    uint8_t result = 1;

    while (exponent > 0) {
        if (exponent & 1U) {
            result = ref_mul(result, a);
        }
        exponent >>= 1;
        if (exponent > 0) {
            a = ref_mul(a, a);
        }
    }

    return result;
}

/**
 * @brief MUnit test case: exhaustive equivalence of ::gf_reduce and ::ref_reduce.
 *
 * Iterates over all 2^16 inputs and asserts that both reducers produce identical
 * outputs for each input.
 *
 * @param params    Unused MUnit parameters.
 * @param user_data Unused user data pointer.
 * @return @c MUNIT_OK on success, or triggers an assertion failure otherwise.
 *
 * @test
 * The test compares:
 * @code
 * expected = ref_reduce(i);
 * actual   = gf_reduce(i);
 * @endcode
 * for all 0 <= i < 65536.
 */
static MunitResult test_gf_reduce(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    for (uint32_t i = 0; i < (1U << 16); ++i) {
        uint8_t expected = ref_reduce((uint16_t)i);
        uint8_t actual = gf_reduce((uint16_t)i);
        munit_assert_uint8(actual, ==, expected);
    }
    return MUNIT_OK;
}

/**
 * @brief MUnit test case: exhaustive equivalence of ::gf_mul and ::ref_mul.
 *
 * Checks all 256 x 256 input pairs.
 *
 * @param params    Unused MUnit parameters.
 * @param user_data Unused user data pointer.
 * @return @c MUNIT_OK on success.
 */
static MunitResult test_gf_mul(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    for (uint16_t a = 0; a < (1U << PARAM_M); ++a) {
        for (uint16_t b = 0; b < (1U << PARAM_M); ++b) {
            uint8_t expected = ref_mul((uint8_t)a, (uint8_t)b);
            uint8_t actual = (uint8_t)gf_mul(a, b);
            munit_assert_uint8(actual, ==, expected);
        }
    }

    return MUNIT_OK;
}

#ifdef REF_MODE
/**
 * @brief MUnit test case: exhaustive equivalence of ::gf_carryless_mul and ::ref_carryless_mul.
 *
 * Checks all 256 x 256 input pairs and validates the unreduced 16-bit polynomial product.
 *
 * @param params    Unused MUnit parameters.
 * @param user_data Unused user data pointer.
 * @return @c MUNIT_OK on success.
 */
static MunitResult test_gf_carryless_mul(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    for (uint16_t a = 0; a < (1U << PARAM_M); ++a) {
        for (uint16_t b = 0; b < (1U << PARAM_M); ++b) {
            uint8_t c[2] = {0};
            uint16_t expected = ref_carryless_mul((uint8_t)a, (uint8_t)b);

            gf_carryless_mul(c, (uint8_t)a, (uint8_t)b);

            uint16_t actual = (uint16_t)c[0] | ((uint16_t)c[1] << 8);
            munit_assert_uint16(actual, ==, expected);
        }
    }

    return MUNIT_OK;
}
#endif

/**
 * @brief MUnit test case: exhaustive equivalence of ::gf_square and squaring via ::ref_mul.
 *
 * @param params    Unused MUnit parameters.
 * @param user_data Unused user data pointer.
 * @return @c MUNIT_OK on success.
 */
static MunitResult test_gf_square(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    for (uint16_t a = 0; a < (1U << PARAM_M); ++a) {
        uint8_t expected = ref_mul((uint8_t)a, (uint8_t)a);
        uint8_t actual = (uint8_t)gf_square(a);
        munit_assert_uint8(actual, ==, expected);
    }

    return MUNIT_OK;
}

/**
 * @brief MUnit test case: exhaustive equivalence of ::gf_inverse and exponentiation by 254.
 *
 * For each non-zero element a, the inverse is checked against a^254 and against
 * the multiplicative identity relation a * a^{-1} = 1. Zero is expected to map
 * to zero by the current implementation convention.
 *
 * @param params    Unused MUnit parameters.
 * @param user_data Unused user data pointer.
 * @return @c MUNIT_OK on success.
 */
static MunitResult test_gf_inverse(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    munit_assert_uint8((uint8_t)gf_inverse(0), ==, 0);

    for (uint16_t a = 1; a < (1U << PARAM_M); ++a) {
        uint8_t inverse = (uint8_t)gf_inverse(a);
        uint8_t expected = ref_pow((uint8_t)a, 254);

        munit_assert_uint8(inverse, ==, expected);
        munit_assert_uint8((uint8_t)gf_mul(a, inverse), ==, 1);
    }

    return MUNIT_OK;
}

/**
 * @brief Test registry for Galois Field routines.
 */
MunitTest gf_tests[] = {
    MUNIT_TEST_ENTRY("gf_reduce", test_gf_reduce),
#ifdef REF_MODE
    MUNIT_TEST_ENTRY("gf_carryless_mul", test_gf_carryless_mul),
#endif
    MUNIT_TEST_ENTRY("gf_mul", test_gf_mul),
    MUNIT_TEST_ENTRY("gf_square", test_gf_square),
    MUNIT_TEST_ENTRY("gf_inverse", test_gf_inverse),
    MUNIT_TEST_END,
};
