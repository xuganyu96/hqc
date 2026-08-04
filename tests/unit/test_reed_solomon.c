/**
 * @file test_reed_solomon.c
 * @brief Intensive tests for the Reed–Solomon (RS) encoder/decoder.
 *
 * Focus: correctness on the *message* part when errors appear in both
 * parity and message, provided the *total* number of symbol errors ≤ t.
 */
#include <inttypes.h>
#include <munit.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "munit_utils.h"
#include "parameters.h"
#include "reed_solomon.c"

#define PARITY_LENGTH (PARAM_N1 - PARAM_K) /* parity length */

/**
 * @brief Print a byte buffer as lowercase hex on a single line.
 *
 * Prints `label`, the buffer length in bytes, then each byte formatted as
 * two hex digits followed by a space, and finally a newline.
 *
 * @param[in] label Human-readable label printed before the bytes (must be non-NULL).
 * @param[in] buf   Pointer to the byte buffer to print (must be non-NULL if @p len > 0).
 * @param[in] len   Number of bytes to print.
 */
static void print_bytes_hex_inline(const char *label, const uint8_t *buf, size_t len) {
    printf("%s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");
}

/**
 * @brief Generate a random non-zero byte using MUnit's PRNG.
 *
 * @return A uniformly random byte in the range [1..255] (never returns 0).
 *
 * @note This is intended for testing only and is not cryptographically secure.
 */
static inline uint8_t rand_nonzero_byte(void) {
    uint8_t b;
    do {
        b = (uint8_t)(munit_rand_uint32() & 0xFF);
    } while (b == 0);
    return b;
}

/**
 * @brief Fill a buffer with random bytes using MUnit's PRNG.
 *
 * @param[out] buf Destination buffer to fill (must be non-NULL).
 * @param[in]  len Number of bytes to write to @p buf.
 *
 * @note This is intended for testing only and is not cryptographically secure.
 */
static void rand_bytes(uint8_t *buf, size_t len) {
    munit_assert_ptr_not_null(buf);
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(munit_rand_uint32() & 0xFF);
}

/**
 * @brief Selects a set of distinct random byte values within a given range.
 *
 * This function uses MUnit's PRNG to draw uniformly random integers in
 * the interval [@p lo, @p hi). It ensures that all returned values are
 * distinct. The chosen values are written into the @p out array.
 *
 * Typical use: selecting distinct error positions in Reed–Solomon tests.
 *
 * @param[out] out   Pointer to an array of at least @p count elements,
 *                   which will be filled with distinct values.
 * @param[in]  count Number of distinct values to generate.
 * @param[in]  lo    Lower bound (inclusive) of the range of candidates.
 * @param[in]  hi    Upper bound (exclusive) of the range of candidates.
 */
static void choose_distinct_in_range(uint8_t *out, size_t count, size_t lo, size_t hi) {
    munit_assert_ptr_not_null(out);
    munit_assert(hi <= 256);
    munit_assert(lo <= hi);
    munit_assert(count <= (hi - lo));

    size_t filled = 0;
    while (filled < count) {
        uint32_t r = munit_rand_uint32();
        size_t pick = lo + (r % (hi - lo));
        bool used = false;
        for (size_t i = 0; i < filled; i++)
            if (out[i] == (uint8_t)pick) {
                used = true;
                break;
            }
        if (!used)
            out[filled++] = (uint8_t)pick;
    }
}

/**
 * @brief Injects random non-zero XOR errors into selected codeword positions.
 *
 * For each position in @p pos, this function flips bits of the corresponding
 * byte in @p codeword using a uniformly random non-zero mask. This ensures
 * each targeted symbol is corrupted, but in a non-predictable way.
 *
 * @param[in,out] codeword  Pointer to the codeword buffer of length @c PARAM_N1.
 * @param[in]     pos       Array of @p count indices into @p codeword where
 *                          errors will be injected.
 * @param[in]     count     Number of error positions (size of @p pos).
 *
 * @note This routine guarantees that each modified symbol changes value
 *       because the injected mask is non-zero.
 */
static void inject_xor_errors(uint8_t *codeword, const uint8_t *pos, size_t count) {
    munit_assert_ptr_not_null(codeword);
    munit_assert_ptr_not_null(pos);
    for (size_t i = 0; i < count; i++) {
        uint8_t p = pos[i];
        munit_assert(p < PARAM_N1);
        uint8_t mask = rand_nonzero_byte();
        codeword[p] ^= mask;
    }
}

/**
 * @brief Convenience wrapper to encode a message into a byte-oriented codeword.
 *
 * This function calls ::reed_solomon_encode and converts the 64-bit word
 * representation to a contiguous byte array for easier testing.
 *
 * @param[in]  m        Input message buffer of size @c PARAM_K bytes.
 * @param[out] cw_bytes Output buffer of size @c PARAM_N1 bytes receiving the
 *                      systematic codeword [parity | message].
 */
static void encode_to_bytes(const uint8_t *m, uint8_t *cw_bytes) {
    uint64_t cw[VEC_N1_SIZE_64] = {0};
    reed_solomon_encode(cw, (const uint64_t *)m);
    memcpy(cw_bytes, cw, PARAM_N1);
}

/**
 * @brief Convenience wrapper to decode a byte-oriented codeword back to a message.
 *
 * This function prepares the codeword in the expected 64-bit word format and
 * calls ::reed_solomon_decode, returning only the message part.
 *
 * @param[in]  cw_bytes Input codeword buffer of size @c PARAM_N1 bytes.
 * @param[out] m_out    Output buffer of size @c PARAM_K bytes receiving the
 *                      decoded message.
 */
static void decode_from_bytes(const uint8_t *cw_bytes, uint8_t *m_out) {
    uint64_t cw_in[VEC_N1_SIZE_64] = {0};
    memcpy(cw_in, cw_bytes, PARAM_N1);
    reed_solomon_decode((uint64_t *)m_out, cw_in);
}

/**
 * @brief Assert that two messages are equal, dumping debug context if not.
 *
 * Compares the reference message @p m_ref against the decoded message @p m_got.
 * If they differ, prints detailed context including:
 *  - the number of errors injected,
 *  - the original (uncorrupted) codeword @p cw,
 *  - and the corrupted codeword @p cw_cor.
 *
 * @param[in] m_ref     Reference message buffer of size @c PARAM_K.
 * @param[in] m_got     Decoded message buffer of size @c PARAM_K.
 * @param[in] cw        Original encoded codeword buffer of size @c PARAM_N1.
 * @param[in] cw_cor    Corrupted codeword buffer of size @c PARAM_N1.
 * @param[in] err_total Total number of errors injected into @p cw_cor.
 */
static void assert_msg_equal(const uint8_t *m_ref, const uint8_t *m_got, const uint8_t *cw, const uint8_t *cw_cor,
                             size_t err_total) {
    if (memcmp(m_ref, m_got, PARAM_K) != 0) {
        printf("\n[FAIL] total_errors=%zu\n", err_total);
        print_bytes_hex_inline("m_ref", m_ref, PARAM_K);
        print_bytes_hex_inline("m_got", m_got, PARAM_K);
        print_bytes_hex_inline("cw   ", cw, PARAM_N1);
        print_bytes_hex_inline("cor  ", cw_cor, PARAM_N1);
        munit_assert_memory_equal(PARAM_K, m_ref, m_got);
    }
}

/**
 * @brief Reed–Solomon zero-error pass-through test.
 *
 * Verifies that encoding and immediately decoding a random message with
 * no errors injected always yields the original message.
 *
 * @param[in]  params (unused) MUnit parameters.
 * @param[in]  ud     (unused) User data pointer.
 *
 * @retval MUNIT_OK if all iterations succeed.
 */
static MunitResult test_decode_succeeds_with_zero_errors(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;
    MUNIT_RANDOMIZED_TEST(1000, {
        uint8_t m[PARAM_K], cw[PARAM_N1], dec[PARAM_K];
        rand_bytes(m, PARAM_K);
        encode_to_bytes(m, cw);
        decode_from_bytes(cw, dec);
        munit_assert_memory_equal(PARAM_K, m, dec);
    });
    return MUNIT_OK;
}

/**
 * @brief Reed–Solomon test: errors confined to parity symbols.
 *
 * Verifies that when up to @c PARAM_T errors occur only in the parity portion of
 * the codeword, the decoder still recovers the message exactly. Since the
 * message part is untouched, correctness requires that decoding ignores
 * parity corruption when within the error-correction capacity.
 *
 * @param[in]  params (unused) MUnit parameters.
 * @param[in]  ud     (unused) User data pointer.
 *
 * @retval MUNIT_OK if all tested cases succeed.
 */
static MunitResult test_decode_succeeds_with_parity_errors_up_to_t(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;
    MUNIT_RANDOMIZED_TEST(1000, {
        uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
        rand_bytes(m, PARAM_K);
        encode_to_bytes(m, cw);
        memcpy(cor, cw, PARAM_N1);

        size_t e_par = (PARAM_T < (PARAM_N1 - PARAM_K)) ? PARAM_T : (PARAM_N1 - PARAM_K);
        uint8_t pos[PARITY_LENGTH];
        choose_distinct_in_range(pos, e_par, 0, (PARAM_N1 - PARAM_K));
        inject_xor_errors(cor, pos, e_par);

        decode_from_bytes(cor, dec);
        assert_msg_equal(m, dec, cw, cor, e_par);
    });
    return MUNIT_OK;
}

/**
 * @brief Reed–Solomon test: errors confined to message symbols.
 *
 * Verifies that when up to @c PARAM_T errors occur inside the message portion of
 * the codeword (the last @c PARAM_K symbols), the decoder recovers the original
 * message exactly.
 *
 * @param[in]  params (unused) MUnit parameters.
 * @param[in]  ud     (unused) User data pointer.
 *
 * @retval MUNIT_OK if all tested cases succeed.
 */
static MunitResult test_decode_succeeds_with_message_errors_up_to_t(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;

    MUNIT_RANDOMIZED_TEST(1000, {
        uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
        rand_bytes(m, PARAM_K);
        encode_to_bytes(m, cw);
        memcpy(cor, cw, PARAM_N1);

        size_t e_msg = (PARAM_T < PARAM_K) ? PARAM_T : PARAM_K;
        uint8_t pos[PARAM_K];
        choose_distinct_in_range(pos, e_msg, (PARAM_N1 - PARAM_K), PARAM_N1);
        inject_xor_errors(cor, pos, e_msg);

        decode_from_bytes(cor, dec);
        assert_msg_equal(m, dec, cw, cor, e_msg);
    });
    return MUNIT_OK;
}

/**
 * @brief Reed–Solomon test: mixed error distribution across parity and message.
 *
 * Ensures that when the total number of symbol errors across both the
 * parity region [0 .. PARAM_N1-PARAM_K-1] and the message region [PARAM_N1-PARAM_K .. PARAM_N1-1] is at
 * most the error-correction capacity @c PARAM_T, the decoder recovers the
 * original message exactly.
 *
 * @param[in]  params (unused) MUnit parameters.
 * @param[in]  ud     (unused) User data pointer.
 *
 * @retval MUNIT_OK if all tested cases succeed.
 */
static MunitResult test_decode_succeeds_for_all_message_parity_splits_up_to_t(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;

    MUNIT_RANDOMIZED_TEST(1000, {
        for (size_t total = 1; total <= PARAM_T; ++total) {
            /* iterate splits m = message errors, p = parity errors */
            for (size_t m_err = 0; m_err <= total; ++m_err) {
                size_t p_err = total - m_err;
                if (m_err > PARAM_K || p_err > PARITY_LENGTH)
                    continue;

                uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
                rand_bytes(m, PARAM_K);
                encode_to_bytes(m, cw);
                memcpy(cor, cw, PARAM_N1);

                uint8_t pos_m[PARAM_K], pos_p[PARITY_LENGTH];
                if (m_err) {
                    choose_distinct_in_range(pos_m, m_err, PARITY_LENGTH, PARAM_N1);
                    inject_xor_errors(cor, pos_m, m_err);
                }
                if (p_err) {
                    choose_distinct_in_range(pos_p, p_err, 0, PARITY_LENGTH);
                    inject_xor_errors(cor, pos_p, p_err);
                }

                decode_from_bytes(cor, dec);
                assert_msg_equal(m, dec, cw, cor, total);
            }
        }
    });
    return MUNIT_OK;
}

/**
 * @brief Boundary test with errors at the junction of parity and message.
 *
 * This test injects exactly two symbol errors:
 *  - One at position PARAM_N1 - PARAM_K - 1 (the last parity symbol).
 *  - One at position PARAM_N1 - PARAM_K (the first message symbol).
 *
 * @param params (unused) MUnit test parameters.
 * @param ud     (unused) User data pointer.
 * @return MUNIT_OK if the message is recovered correctly; otherwise an assertion
 *         failure occurs.
 */
static MunitResult test_decode_succeeds_with_errors_at_parity_message_boundary(const MunitParameter params[],
                                                                               void *ud) {
    (void)params;
    (void)ud;
    MUNIT_RANDOMIZED_TEST(1000, {
        uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
        rand_bytes(m, PARAM_K);
        encode_to_bytes(m, cw);
        memcpy(cor, cw, PARAM_N1);

        uint8_t pos[2] = {(uint8_t)(PARITY_LENGTH - 1), (uint8_t)PARITY_LENGTH};
        inject_xor_errors(cor, pos, 2); /* total=2 ≤ t assumed by parameters */

        decode_from_bytes(cor, dec);
        assert_msg_equal(m, dec, cw, cor, 2);
    });
    return MUNIT_OK;
}

/**
 * @brief Worst-case error distribution with total exactly PARAM_T.
 *
 * This test stresses the decoder by injecting exactly PARAM_T errors,
 * the maximum guaranteed correction capacity. Errors are placed at random
 * positions across the entire codeword (both parity and message), ensuring
 * no duplication. The split between parity and message errors is random.
 *
 * @param params (unused) MUnit test parameters.
 * @param ud     (unused) User data pointer.
 * @return MUNIT_OK if the message is fully recovered; otherwise triggers
 *         an assertion failure.
 */
static MunitResult test_decode_succeeds_with_exact_t_total_errors(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;

    MUNIT_RANDOMIZED_TEST(1000, {
        uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
        rand_bytes(m, PARAM_K);
        encode_to_bytes(m, cw);
        memcpy(cor, cw, PARAM_N1);

        /* draw m_err in [max(0, t-PARITY_LENGTH) .. min(t, PARAM_K)] so p_err fits inside PARITY_LENGTH */
        size_t lo_m = (PARAM_T > PARITY_LENGTH) ? (PARAM_T - PARITY_LENGTH) : 0;
        size_t hi_m = (PARAM_T < PARAM_K) ? PARAM_T : PARAM_K;
        if (lo_m > hi_m)
            lo_m = hi_m;
        size_t m_err = lo_m + (munit_rand_uint32() % (hi_m - lo_m + 1));
        size_t p_err = PARAM_T - m_err;

        uint8_t pos_m[PARAM_K], pos_p[PARITY_LENGTH];
        if (m_err) {
            choose_distinct_in_range(pos_m, m_err, PARITY_LENGTH, PARAM_N1);
            inject_xor_errors(cor, pos_m, m_err);
        }
        if (p_err) {
            choose_distinct_in_range(pos_p, p_err, 0, PARITY_LENGTH);
            inject_xor_errors(cor, pos_p, p_err);
        }

        decode_from_bytes(cor, dec);
        assert_msg_equal(m, dec, cw, cor, PARAM_T);
    });
    return MUNIT_OK;
}

/**
 * @brief Negative test: inject PARAM_T + 1 errors, exceeding correction capacity.
 *
 * This test ensures the decoder behaves safely (no crashes, no buffer
 * overflows) when the number of symbol errors exceeds the guaranteed
 * correction limit (PARAM_T). Specifically:
 *  - A random message of length PARAM_K is encoded.
 *  - Exactly PARAM_T+1 errors are injected at distinct positions
 *    across the full codeword.
 *  - The decoder is run on the corrupted codeword.
 *
 * @param params (unused) MUnit test parameters.
 * @param ud     (unused) User data pointer.
 * @return MUNIT_OK always, as long as no crash occurs.
 */
static MunitResult test_decode_beyond_t_does_not_crash(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;

    MUNIT_RANDOMIZED_TEST(1000, {
        uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
        rand_bytes(m, PARAM_K);
        encode_to_bytes(m, cw);
        memcpy(cor, cw, PARAM_N1);

        size_t total = PARAM_T + 1;

        /* Pick a feasible split */
        size_t hi_m = (total < PARAM_K) ? total : PARAM_K;
        size_t m_err = (hi_m > 0) ? (munit_rand_uint32() % (hi_m + 1)) : 0;
        size_t p_err = total - m_err;
        if (p_err > PARITY_LENGTH) { /* force inside PARITY_LENGTH */
            p_err = PARITY_LENGTH;
            m_err = total - p_err;
            if (m_err > PARAM_K)
                m_err = PARAM_K;
        }

        uint8_t pos_m[PARAM_K], pos_p[PARITY_LENGTH];
        if (m_err) {
            choose_distinct_in_range(pos_m, m_err, PARITY_LENGTH, PARAM_N1);
            inject_xor_errors(cor, pos_m, m_err);
        }
        if (p_err) {
            choose_distinct_in_range(pos_p, p_err, 0, PARITY_LENGTH);
            inject_xor_errors(cor, pos_p, p_err);
        }

        /* Just ensure it runs; don’t assert equality (beyond capacity). */
        decode_from_bytes(cor, dec);

        /* Optional: if it did match, great; otherwise we accept mismatch. */
        // if (memcmp(m, dec, PARAM_K) != 0) printf("(as expected) fail for total=%zu\n", total);
    });
    return MUNIT_OK;
}

/**
 * @brief Exhaustive sweep of mixed error distributions for i = 1..PARAM_T.
 *
 * This test systematically explores all possible splits of i errors
 * between the parity and message regions, for every i from 1 up to
 * the correction capacity PARAM_T.
 *
 * For each (i, parity_errors, message_errors) combination:
 *  - A random message of length PARAM_K is generated.
 *  - It is encoded into a codeword of length PARAM_N1.
 *  - parity_errors are injected into the parity region [0 .. PARAM_N1 - PARAM_K - 1].
 *  - message_errors are injected into the message region [PARAM_N1 - PARAM_K .. PARAM_N1 - 1].
 *  - The codeword is decoded.
 *  - The decoded message is asserted to match the original.
 *
 * @param params (unused) MUnit test parameters.
 * @param ud     (unused) User data pointer.
 *
 * @return MUNIT_OK if all combinations succeed.
 */
static MunitResult test_decode_succeeds_for_all_weights_up_to_t_mixed_splits(const MunitParameter params[], void *ud) {
    (void)params;
    (void)ud;

    for (size_t i = 1; i <= PARAM_T; ++i) {
        /* try several random compositions for each i */
        for (int trial = 0; trial < 8; ++trial) {
            uint8_t m[PARAM_K], cw[PARAM_N1], cor[PARAM_N1], dec[PARAM_K];
            rand_bytes(m, PARAM_K);
            encode_to_bytes(m, cw);
            memcpy(cor, cw, PARAM_N1);

            size_t hi_m = (i < PARAM_K) ? i : PARAM_K;
            /* choose m_err uniformly from [0..min(i,PARAM_K)] but also ensure p_err fits */
            size_t m_err = munit_rand_uint32() % (hi_m + 1);
            size_t p_err = i - m_err;
            if (p_err > PARITY_LENGTH) {
                p_err = PARITY_LENGTH;
                m_err = i - p_err;
            }

            uint8_t pos_m[PARAM_K], pos_p[PARITY_LENGTH];
            if (m_err) {
                choose_distinct_in_range(pos_m, m_err, PARITY_LENGTH, PARAM_N1);
                inject_xor_errors(cor, pos_m, m_err);
            }
            if (p_err) {
                choose_distinct_in_range(pos_p, p_err, 0, PARITY_LENGTH);
                inject_xor_errors(cor, pos_p, p_err);
            }

            decode_from_bytes(cor, dec);
            assert_msg_equal(m, dec, cw, cor, i);
        }
    }
    return MUNIT_OK;
}

/**
 * @brief Test registry for Vectors routines.
 */
MunitTest rs_tests[] = {
    MUNIT_TEST_ENTRY("zero errors", test_decode_succeeds_with_zero_errors),
    MUNIT_TEST_ENTRY("parity-only (<=t)", test_decode_succeeds_with_parity_errors_up_to_t),
    MUNIT_TEST_ENTRY("message-only (<=t)", test_decode_succeeds_with_message_errors_up_to_t),
    MUNIT_TEST_ENTRY("mixed sum<=t", test_decode_succeeds_for_all_message_parity_splits_up_to_t),
    MUNIT_TEST_ENTRY("boundary n-k-1 & n-k", test_decode_succeeds_with_errors_at_parity_message_boundary),
    MUNIT_TEST_ENTRY("t (random split)", test_decode_succeeds_with_exact_t_total_errors),
    MUNIT_TEST_ENTRY("over capacity (t+1)", test_decode_beyond_t_does_not_crash),
    MUNIT_TEST_ENTRY("sweep with mixed splits", test_decode_succeeds_for_all_weights_up_to_t_mixed_splits),
    MUNIT_TEST_END};
