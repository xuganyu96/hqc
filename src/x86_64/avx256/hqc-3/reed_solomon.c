/**
 * @file reed_solomon.c
 * @brief Constant time implementation of Reed-Solomon codes
 */

#include "reed_solomon.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "crypto_memset.h"
#include "fft.h"
#include "gf.h"
#include "parameters.h"
#ifdef VERBOSE
#include <stdbool.h>
#include <stdio.h>
#endif

/**
 * @brief Number of 16-bit words required to hold a syndrome of length 2·PARAM_DELTA bits
 *
 * For the 256-bit implementation, each word is 16 bits, so we round
 * up the total bit-length (2·PARAM_DELTA) to the next multiple of 16 bits.
 */
#define SYND_SIZE_256 (CEIL_DIVIDE(2 * PARAM_DELTA, 16))

static uint16_t mod(uint16_t i, uint16_t modulus);
static void compute_syndromes(__m256i *syndromes, uint8_t *cdw);
static uint16_t compute_elp(uint16_t *sigma, const uint16_t *syndromes);
static void compute_roots(uint8_t *error, uint16_t *sigma);
static void compute_z_poly(uint16_t *z, const uint16_t *sigma, uint16_t degree, const uint16_t *syndromes);
static void compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error);
static void correct_errors(uint8_t *cdw, const uint16_t *error_values);

/**
 * @brief Precomputed 256-bit vectors of field elements αᵢʲ for GF(2^8) operations (first half).
 *
 * Each entry packs four 64-bit words into a __m256i, corresponding to the
 * powers of the primitive element α raised to the exponents i·j.
 *
 * The table length (45) matches the number of coefficients needed in the
 * αᵢʲ power table for the chosen code parameters.
 */
static const __m256i alpha_ij256_1[55] = {
    {0x0011000f00050003, 0x001a00ff00550033, 0x00a100960072002e, 0x005f0035001300f8},
    {0x001a005500110005, 0x005f001300a10072, 0x00f7009500d80038, 0x00e50066001e0006},
    {0x00a1002e0055000f, 0x00f7007300380035, 0x003700340066000a, 0x005300ab00d90026},
    {0x005f00a1001a0011, 0x00e5001e00f700d8, 0x00530090006a0037, 0x004c00d3004f0014},
    {0x00d8003500720033, 0x006a00e400660002, 0x00d300cc000400ab, 0x00830008004d00d4},
    {0x00f7003800a10055, 0x005300d900370066, 0x0062006700d30044, 0x00b500b3006b0078},
    {0x001e0073001300ff, 0x004f00f500d900e4, 0x006b0028004d00cd, 0x00fe001d005700ce},
    {0x00e500f7005f001a, 0x004c004f0053006a, 0x00b5008100830062, 0x00fb002f00fe000b},
    {0x0037000a0038002e, 0x006200cd004400ab, 0x00bb00f900b3009e, 0x00c300c200600092},
    {0x006a006600d80072, 0x0083004d00d30004, 0x002f0061001000b3, 0x009f004000fa003a},
    {0x0090003400950096, 0x00810028006700cc, 0x00d20071006100f9, 0x009b00ef002c0041},
    {0x0053003700f700a1, 0x00b5006b006200d3, 0x00c300d2002f00bb, 0x00fc00e80082009c},
    {0x00140026000600f8, 0x000b00ce007800d4, 0x009c0041003a0092, 0x004500630065009d},
    {0x004f00d9001e0013, 0x00fe0057006b004d, 0x0082002c00fa0060, 0x0012004a00a50065},
    {0x00d300ab00660035, 0x002f001d00b30008, 0x00e800ef004000c2, 0x00390036004a0063},
    {0x004c005300e5005f, 0x00fb00fe00b50083, 0x00fc009b009f00c3, 0x0003003900120045},
    {0x00e0000c005c00e1, 0x005d00ec005000bd, 0x00b000b100bc00ed, 0x00e10001000d0051},
    {0x0062004400370038, 0x00c3006000bb00b3, 0x00a8000700e800d5, 0x0034003500c70085},
    {0x001800d100eb0048, 0x005b006d002b009a, 0x002900cf00c500df, 0x00f50066009600fd},
    {0x008300d3006a00d8, 0x009f00fa002f0010, 0x003900c6001b00e8, 0x00d400ab00020033},
    {0x006b00cd00d90073, 0x0082003d0060001d, 0x00c7008c004a0043, 0x009e00d300260038},
    {0x0081006700900095, 0x009b002c00d20061, 0x002e00dd00c60007, 0x00c40008003c00aa},
    {0x0049003b00e600a4, 0x00ea00ba0032007d, 0x00730052008d00ca, 0x001900b300a90090},
    {0x00b50062005300f7, 0x00fc008200c3002f, 0x0034002e003900a8, 0x0016001d009e00d1},
    {0x0010000800040002, 0x001b008000400020, 0x00ab00d8006c0036, 0x005e002f009a004d},
    {0x000b007800140006, 0x00450065009c003a, 0x00d100aa00330085, 0x00ba00c200d600d0},
    {0x00bb009e0044000a, 0x00a8004300d500c2, 0x003b00d90035007c, 0x00b6004000ae00b5},
    {0x00fe006b004f001e, 0x001200a5008200fa, 0x009e003c000200c7, 0x001f00ef00e700d6},
    {0x0087007f00680022, 0x008f00990089005e, 0x00db004c00e40055, 0x00cf00e800c00093},
    {0x002f00b300d30066, 0x0039004a00e80040, 0x001d000800ab0035, 0x0036006300ef00c2},
    {0x00e900db00b200aa, 0x001c00e3006f0074, 0x009200dc00cc00f7, 0x004b004a00580047},
    {0x00fb00b5004c00e5, 0x0003001200fc009f, 0x001600c400d40034, 0x00050036001f00ba},
    {0x00d200f900670034, 0x002e008c000700ef, 0x001500bb000800d9, 0x0038003900ca0089},
    {0x005d005000e0005c, 0x00e1000d00b000bc, 0x00ed00ec00bd000c, 0x005c0001005100b1},
    {0x00fa001d004d00e4, 0x00020097004a0080, 0x00ef003a009a00d3, 0x000400350094001b},
    {0x00c300bb00620037, 0x003400c700a800e8, 0x00b60015001d003b, 0x006700660024008b},
    {0x004700a300f10059, 0x00be001100f30025, 0x0043005b007d0078, 0x00b900ab001a005a},
    {0x005b002b001800eb, 0x00f50096002900c5, 0x002d00640020007f, 0x005700d300950017},
    {0x009c009200780026, 0x00d1003800850063, 0x008b008900c200b5, 0x002b0008003700c7},
    {0x009f002f0083006a, 0x00d400020039001b, 0x00360025005e001d, 0x003a00b300040072},
    {0x00ac00ae00b900be, 0x00a600e500a200cb, 0x00f200a50074002b, 0x00e2001d004c00a4},
    {0x00820060006b00d9, 0x009e002600c7004a, 0x002400ca00ef00ae, 0x00d5002f00780037},
    {0x007a001600dc0070, 0x009800e6000f0091, 0x0055003e008000d2, 0x00c100c2004900f5},
    {0x009b00d200810090, 0x00c4003c002e00c6, 0x0038007b00250015, 0x00210040002700b2},
    {0x00e800c200b300ab, 0x001d00d300350036, 0x0066003900630040, 0x004a00ef002f0008},
    {0x00ea0032004900e6, 0x001900a90073008d, 0x00d900b400cb0075, 0x005a00e800b70081},
    {0x00c8001500760031, 0x007100f1000a0094, 0x004400ff00910082, 0x00dd006300470030},
    {0x00fc00c300b50053, 0x0016009e00340039, 0x00670038003600b6, 0x000f004a00d5002b},
    {0x00a5003d005700f5, 0x00e7008100260097, 0x007800220094006f, 0x00480036009b00a0},
    {0x001b004000100004, 0x005e009a00ab006c, 0x00b3006a00970063, 0x00e4003900c500fa},
    {0x00b000ed0050000c, 0x00ed0050000c0001, 0x0050000c000100b0, 0x000c000100b000ed},
    {0x0045009c000b0014, 0x00ba00d600d10033, 0x002b00b20072008b, 0x00a90035003e002a},
    {0x007900750027003c, 0x009d008700cd0072, 0x006000a600d800f3, 0x00d00066008f0058},
    {0x00a800d500bb0044, 0x00b600ae003b0035, 0x0032006b0066008c, 0x00f900ab00a200fc},
    {0x00c600ef006100cc, 0x0025003a000800d8, 0x0040009a006a0039, 0x007d00d3003300cb},
};

/**
 * @brief Precomputed 256-bit vectors of field elements αᵢʲ for GF(2^8) operations (second half).
 *
 * Same format as alpha_ij256_1, providing the remaining set of powers required
 * for optimized Reed–Solomon arithmetic on 256-bit words with AVX2.
 */
static const __m256i alpha_ij256_2[55] = {
    {0x00d80048003800e1, 0x00f700a400950073, 0x001e000a00060002, 0x00e500aa00660022},
    {0x006a00eb0037005c, 0x005300e6009000d9, 0x004f004400140004, 0x004c00b200d30068},
    {0x00d300d10044000c, 0x0062003b006700cd, 0x006b009e00780008, 0x00b500db00b3007f},
    {0x00830018006200e0, 0x00b500490081006b, 0x00fe00bb000b0010, 0x00fb00e9002f0087},
    {0x0010009a00b300bd, 0x002f007d0061001d, 0x00fa00c2003a0020, 0x009f00740040005e},
    {0x002f002b00bb0050, 0x00c3003200d20060, 0x008200d5009c0040, 0x00fc006f00e80089},
    {0x00fa006d006000ec, 0x008200ba002c003d, 0x00a5004300650080, 0x001200e3004a0099},
    {0x009f005b00c3005d, 0x00fc00ea009b0082, 0x001200a80045001b, 0x0003001c0039008f},
    {0x00e800df00d500ed, 0x00a800ca00070043, 0x00c7007c00850036, 0x003400f700350055},
    {0x001b00c500e800bc, 0x0039008d00c6004a, 0x000200350033006c, 0x00d400cc00ab00e4},
    {0x00c600cf000700b1, 0x002e005200dd008c, 0x003c00d900aa00d8, 0x00c400dc0008004c},
    {0x0039002900a800b0, 0x00340073002e00c7, 0x009e003b00d100ab, 0x00160092001d00db},
    {0x003300fd00850051, 0x00d1009000aa0038, 0x00d600b500d0004d, 0x00ba004700c20093},
    {0x0002009600c7000d, 0x009e00a9003c0026, 0x00e700ae00d6009a, 0x001f005800ef00c0},
    {0x00ab006600350001, 0x001d00b3000800d3, 0x00ef004000c2002f, 0x0036004a006300e8},
    {0x00d400f5003400e1, 0x0016001900c4009e, 0x001f00b600ba005e, 0x0005004b003600cf},
    {0x00bd00e0000c005c, 0x00ed005d00ec0050, 0x005100b000b100bc, 0x005c00e10001000d},
    {0x001d007f003b000c, 0x00b60075001500ae, 0x0024008c008b0063, 0x00670053006600a1},
    {0x00200027007f00e0, 0x002d0065006400c3, 0x0095000f001700c6, 0x0057008800d300be},
    {0x005e0020001d00bd, 0x003600cb002500ef, 0x0004006600720097, 0x003a006100b3004d},
    {0x00ef00c300ae0050, 0x0024002900ca006f, 0x007800d100370035, 0x00d50015002f00f9},
    {0x00250064001500ec, 0x003800b4007b00ca, 0x0027006b00b2006a, 0x0021007a004000fb},
    {0x00cb00650075005d, 0x00d9004800b40029, 0x00b700a3008100d4, 0x005a009900e800bf},
    {0x0036002d00b600ed, 0x006700d900380024, 0x00d50032002b00b3, 0x000f0085004a006f},
    {0x009700c6006300bc, 0x00b300d4006a0035, 0x00c500ef00fa007d, 0x00e4007200390091},
    {0x00720017008b00b1, 0x002b008100b20037, 0x003e00fc002a00fa, 0x00a900be00350084},
    {0x0066000f008c00b0, 0x003200a3006b00d1, 0x00a2004200fc00ef, 0x00f9006200ab0038},
    {0x0004009500240051, 0x00d500b700270078, 0x004800a2003e00c5, 0x004e00f000080031},
    {0x004d00be00a1000d, 0x006f00bf00fb00f9, 0x0031003800840091, 0x006400b7001d0018},
    {0x00b300d300660001, 0x004a00e80040002f, 0x000800ab00350039, 0x006300ef00c2001d},
    {0x00610088005300e1, 0x00850099007a0015, 0x00f0006200be0072, 0x00ee00a500ef00b7},
    {0x003a00570067005c, 0x000f005a002100d5, 0x004e00f900a900e4, 0x001100ee00630064},
    {0x00400092006b000c, 0x000a002400a800af, 0x0075006000db00d3, 0x0037000f003600fc},
    {0x00bc005d005000e0, 0x000c00e1000d00b0, 0x00b100ed00ec00bd, 0x00e0005c00010051},
    {0x00c50074002f00bd, 0x0008006a00330036, 0x009100e8005e0061, 0x001000d40066006c},
    {0x004a008900320050, 0x00f900cd000a00a2, 0x007c00ca00df00c2, 0x00d200b500d300f7},
    {0x008d001f009c00ec, 0x00ae00dc00f500a1, 0x005f008500f4009f, 0x00ac00a000b3003c},
    {0x006c00790089005d, 0x003d00d600d70034, 0x00900055000e0025, 0x00a500da002f00b9},
    {0x0035008c00fc00ed, 0x00df00d200db0044, 0x006200340024004a, 0x00290043004000a3},
    {0x00e4006c004a00bc, 0x00630074007d0008, 0x001000d300d80094, 0x003300c600e800fa},
    {0x00cc0013002900b1, 0x004200c100e700b5, 0x00fb007f00e60033, 0x005900b4004a009d},
    {0x0008003400a200b0, 0x007c002d00750092, 0x009c002b00620066, 0x003b000a00390007},
    {0x009a0014002e0051, 0x00a1001200230032, 0x00ea0015005700cc, 0x00300068003500ee},
    {0x007d00d7000a000d, 0x003700fd00990075, 0x0079008200e90083, 0x006d009800ab0003},
    {0x00c200b300ab0001, 0x00d30035003600e8, 0x003900630040001d, 0x00ef002f00080066},
    {0x007400d600cd00e1, 0x006b00eb00fd002d, 0x00f800f3009b003a, 0x00f400c0001d00b8},
    {0x008000fb009e005c, 0x00bb006e001300f3, 0x00be002400770074, 0x007b006500c20081},
    {0x0063003d00f9000c, 0x00d2006b0037007c, 0x003b0073002900e8, 0x0055008b00ef0092},
    {0x0091002a009200e0, 0x009c00690068002e, 0x00c4005300f600cb, 0x00eb0084006300e2},
    {0x0094002500c200bd, 0x00e8003a00830066, 0x002000080002008d, 0x004d00d800360080},
    {0x000100b000ed0050, 0x00b000ed0050000c, 0x00ed0050000c0001, 0x0050000c000100b0},
    {0x00d8000e00df00ec, 0x0029009b00e90062, 0x0023001600280002, 0x00b700b90066008a},
    {0x006a00dd0043005d, 0x00c7000900e200db, 0x00cf009c00f00004, 0x002a001900d300ff},
    {0x00d3005500ca00ed, 0x007300f30082002b, 0x00f200af00160008, 0x000700c300b30037},
    {0x00830002003600bc, 0x00ab009700c500c2, 0x0072004a00740010, 0x008d0080002f00d4},
};

/**
 * Coefficients of polynomial G
 * stored in 256-bit values
 **/
static const __m256i param256[3] = {
    {0x001e009100b40038, 0x003c001c00690083, 0x00b800ce0021006b, 0x00e7009900f9009b},
    {0x000100e7000b0070, 0x003e004700c90090, 0x00f6008e00960008, 0x0096001a00d60021},
    {0x0000000000000001, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000},
};

/**
 * Returns i modulo the given modulus.
 * i must be less than 2*modulus.
 * Therefore, the return value is either i or i-modulus.
 * @returns i mod (modulus)
 * @param[in] i The integer whose modulo is taken
 * @param[in] modulus The modulus
 */
static uint16_t mod(uint16_t i, uint16_t modulus) {
    uint16_t tmp = i - modulus;

    // mask = 0xffff if(i < PARAM_GF_MUL_ORDER)
    int16_t mask = -(tmp >> 15);

    return tmp + (mask & modulus);
}

/**
 * @brief Computes the generator polynomial of the primitive Reed-Solomon code with given parameters.
 *
 * Code length is 2^m-1. <br>
 * PARAM_DELTA is the targeted correction capacity of the code
 * and receives the real correction capacity (which is at least equal to the target). <br>
 * gf_exp and gf_log are arrays giving antilog and log of GF(2^m) elements.
 *
 * @param[out] poly Array of size (2*PARAM_DELTA + 1) receiving the coefficients of the generator polynomial
 */
void compute_generator_poly(uint16_t *poly) {
    poly[0] = 1;
    int tmp_degree = 0;

    for (uint16_t i = 1; i < (2 * PARAM_DELTA + 1); ++i) {
        for (size_t j = tmp_degree; j; --j) {
            poly[j] = gf_exp[mod(gf_log[poly[j]] + i, PARAM_GF_MUL_ORDER)] ^ poly[j - 1];
        }

        poly[0] = gf_exp[mod(gf_log[poly[0]] + i, PARAM_GF_MUL_ORDER)];
        poly[++tmp_degree] = 1;
    }

    printf("\n");
    for (int i = 0; i < (PARAM_G); ++i) {
        printf("%d, ", poly[i]);
    }
    printf("\n");
}

/**
 * @brief Encodes a message message of PARAM_K bits to a Reed-Solomon codeword codeword of PARAM_N1 bytes
 *
 * Following @cite lin1983error (Chapter 4 - Cyclic Codes),
 * We perform a systematic encoding using a linear (PARAM_N1 - PARAM_K)-stage shift register
 * with feedback connections based on the generator polynomial PARAM_RS_POLY of the Reed-Solomon code.
 *
 * @param[out] cdw Array of size VEC_N1_SIZE_64 receiving the encoded message
 * @param[in] msg Array of size VEC_K_SIZE_64 storing the message
 */
void reed_solomon_encode(uint64_t *cdw, const uint64_t *msg) {
    uint8_t gate_value = 0;

    union {
        uint16_t arr16[16 * CEIL_DIVIDE(PARAM_G, 16)];
        __m256i dummy;
    } tmp = {0};

    uint8_t msg_bytes[PARAM_K] = {0};
    uint8_t cdw_bytes[PARAM_N1] = {0};
    __m256i *tmp256 = (__m256i *)tmp.arr16;
    uint16_t *PARAM_RS_POLY = (uint16_t *)param256;

    memcpy(msg_bytes, msg, PARAM_K);

    for (int16_t i = PARAM_K - 1; i >= 0; --i) {
        gate_value = msg_bytes[i] ^ cdw_bytes[PARAM_N1 - PARAM_K - 1];

        __m256i gate256 = _mm256_set1_epi16(gate_value);
        tmp256[0] = gf_mul_vect(gate256, param256[0]);
        tmp256[1] = gf_mul_vect(gate256, param256[1]);

        for (size_t j = 32; j < PARAM_G; ++j) {
            tmp.arr16[j] = gf_mul(gate_value, PARAM_RS_POLY[j]);
        }

        for (size_t k = PARAM_N1 - PARAM_K - 1; k; --k) {
            cdw_bytes[k] = cdw_bytes[k - 1] ^ tmp.arr16[k];
        }

        cdw_bytes[0] = tmp.arr16[0];
    }

    memcpy(cdw_bytes + PARAM_N1 - PARAM_K, msg_bytes, PARAM_K);
    memcpy(cdw, cdw_bytes, PARAM_N1);
}

/**
 * @brief Computes 2 * PARAM_DELTA syndromes
 *
 * @param[out] syndromes256 Array of size 2 * PARAM_DELTA receiving the computed syndromes
 * @param[in] cdw Array of size PARAM_N1 storing the received vector
 */
void compute_syndromes(__m256i *syndromes256, uint8_t *cdw) {
    syndromes256[0] = _mm256_set1_epi16(cdw[0]);

    for (size_t i = 0; i < PARAM_N1 - 1; ++i) {
        syndromes256[0] ^= gf_mul_vect(_mm256_set1_epi16(cdw[i + 1]), alpha_ij256_1[i]);
    }

    syndromes256[1] = _mm256_set1_epi16(cdw[0]);

    for (size_t i = 0; i < PARAM_N1 - 1; ++i) {
        syndromes256[1] ^= gf_mul_vect(_mm256_set1_epi16(cdw[i + 1]), alpha_ij256_2[i]);
    }
}

/**
 * @brief Computes the error locator polynomial (ELP) sigma
 *
 * This is a constant time implementation of Berlekamp's algorithm (see @cite lin1983error (Chapter 6 - BCH Codes). <br>
 * We use the letter p for rho which is initialized at -1. <br>
 * The array X_sigma_p represents the polynomial X^(mu-rho)*sigma_p(X). <br>
 * Instead of maintaining a list of sigmas, we update in place both sigma and X_sigma_p. <br>
 * sigma_copy serves as a temporary save of sigma in case X_sigma_p needs to be updated. <br>
 * We can properly correct only if the degree of sigma does not exceed PARAM_DELTA.
 * This means only the first PARAM_DELTA + 1 coefficients of sigma are of value
 * and we only need to save its first PARAM_DELTA - 1 coefficients.
 *
 * @returns the degree of the ELP sigma
 * @param[out] sigma Array of size (at least) PARAM_DELTA receiving the ELP
 * @param[in] syndromes Array of size (at least) 2*PARAM_DELTA storing the syndromes
 */
static uint16_t compute_elp(uint16_t *sigma, const uint16_t *syndromes) {
    uint16_t deg_sigma = 0;
    uint16_t deg_sigma_p = 0;
    uint16_t deg_sigma_copy = 0;
    uint16_t sigma_copy[PARAM_DELTA + 1] = {0};
    uint16_t X_sigma_p[PARAM_DELTA + 1] = {0, 1};
    uint16_t pp = (uint16_t)-1;  // 2*rho
    uint16_t d_p = 1;
    uint16_t d = syndromes[0];

    uint16_t mask1, mask2, mask12;
    uint16_t deg_X, deg_X_sigma_p;
    uint16_t dd;
    uint16_t mu;

    uint16_t i;

    sigma[0] = 1;
    for (mu = 0; (mu < (2 * PARAM_DELTA)); ++mu) {
        // Save sigma in case we need it to update X_sigma_p
        memcpy(sigma_copy, sigma, 2 * (PARAM_DELTA));
        deg_sigma_copy = deg_sigma;

        dd = gf_mul(d, gf_inverse(d_p));

        for (i = 1; (i <= mu + 1) && (i <= PARAM_DELTA); ++i) {
            sigma[i] ^= gf_mul(dd, X_sigma_p[i]);
        }

        deg_X = mu - pp;
        deg_X_sigma_p = deg_X + deg_sigma_p;

        // mask1 = 0xffff if(d != 0) and 0 otherwise
        mask1 = -((uint16_t)-d >> 15);

        // mask2 = 0xffff if(deg_X_sigma_p > deg_sigma) and 0 otherwise
        mask2 = -((uint16_t)(deg_sigma - deg_X_sigma_p) >> 15);

        // mask12 = 0xffff if the deg_sigma increased and 0 otherwise
        volatile uint16_t mask12__ = mask1 & mask2;
        mask12 = mask12__;
        deg_sigma ^= mask12 & (deg_X_sigma_p ^ deg_sigma);

        if (mu == (2 * PARAM_DELTA - 1)) {
            break;
        }

        pp ^= mask12 & (mu ^ pp);
        d_p ^= mask12 & (d ^ d_p);
        for (i = PARAM_DELTA; i; --i) {
            X_sigma_p[i] = (mask12 & sigma_copy[i - 1]) ^ (~mask12 & X_sigma_p[i - 1]);
        }

        deg_sigma_p ^= mask12 & (deg_sigma_copy ^ deg_sigma_p);
        d = syndromes[mu + 1];

        for (i = 1; (i <= mu + 1) && (i <= PARAM_DELTA); ++i) {
            d ^= gf_mul(sigma[i], syndromes[mu + 1 - i]);
        }
    }

    return deg_sigma;
}

/**
 * @brief Computes the error polynomial error from the error locator polynomial sigma
 *
 * See function fft for more details.
 *
 * @param[out] error Array of 2^PARAM_M elements receiving the error polynomial
 * @param[in] sigma Array of 2^PARAM_FFT elements storing the error locator polynomial
 */
static void compute_roots(uint8_t *error, uint16_t *sigma) {
    uint16_t w[1 << PARAM_M] = {0};

    fft(w, sigma, PARAM_DELTA + 1);
    fft_retrieve_error_poly(error, w);
}

/**
 * @brief Computes the polynomial z(x)
 *
 * See @cite lin1983error (Chapter 6 - BCH Codes) for more details.
 *
 * @param[out] z Array of PARAM_DELTA + 1 elements receiving the polynomial z(x)
 * @param[in] sigma Array of 2^PARAM_FFT elements storing the error locator polynomial
 * @param[in] degree Integer that is the degree of polynomial sigma
 * @param[in] syndromes Array of 2 * PARAM_DELTA storing the syndromes
 */
static void compute_z_poly(uint16_t *z, const uint16_t *sigma, uint16_t degree, const uint16_t *syndromes) {
    size_t i, j;
    uint16_t mask;

    z[0] = 1;

    for (i = 1; i < PARAM_DELTA + 1; ++i) {
        mask = -((uint16_t)(i - degree - 1) >> 15);
        z[i] = mask & sigma[i];
    }

    z[1] ^= syndromes[0];

    for (i = 2; i <= PARAM_DELTA; ++i) {
        mask = -((uint16_t)(i - degree - 1) >> 15);
        z[i] ^= mask & syndromes[i - 1];

        for (j = 1; j < i; ++j) {
            z[i] ^= mask & gf_mul(sigma[j], syndromes[i - j - 1]);
        }
    }
}

/**
 * @brief Computes the error values
 *
 * See @cite lin1983error (Chapter 6 - BCH Codes) for more details.
 *
 * @param[out] error_values Array of PARAM_DELTA elements receiving the error values
 * @param[in] z Array of PARAM_DELTA + 1 elements storing the polynomial z(x)
 * @param[in] error Array of PARAM_DELTA elements storing the errors positions
 */
static void compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error) {
    uint16_t beta_j[PARAM_DELTA] = {0};
    uint16_t e_j[PARAM_DELTA] = {0};

    uint16_t delta_counter;
    uint16_t delta_real_value;
    uint16_t found;
    uint16_t mask1;
    uint16_t mask2;
    uint16_t tmp1;
    uint16_t tmp2;
    uint16_t inverse;
    uint16_t inverse_power_j;

    // Compute the beta_{j_i} page 31 of the documentation
    delta_counter = 0;
    for (size_t i = 0; i < PARAM_N1; i++) {
        found = 0;
        mask1 = (uint16_t)(-((int32_t)error[i]) >> 31);  // error[i] != 0
        for (size_t j = 0; j < PARAM_DELTA; j++) {
            mask2 = ~((uint16_t)(-((int32_t)j ^ delta_counter) >> 31));  // j == delta_counter
            beta_j[j] += mask1 & mask2 & gf_exp[i];
            found += mask1 & mask2 & 1;
        }
        delta_counter += found;
    }
    delta_real_value = delta_counter;

    // Compute the e_{j_i} page 31 of the documentation
    for (size_t i = 0; i < PARAM_DELTA; ++i) {
        tmp1 = 1;
        tmp2 = 1;
        inverse = gf_inverse(beta_j[i]);
        inverse_power_j = 1;

        for (size_t j = 1; j <= PARAM_DELTA; ++j) {
            inverse_power_j = gf_mul(inverse_power_j, inverse);
            tmp1 ^= gf_mul(inverse_power_j, z[j]);
        }
        for (size_t k = 1; k < PARAM_DELTA; ++k) {
            tmp2 = gf_mul(tmp2, (1 ^ gf_mul(inverse, beta_j[(i + k) % PARAM_DELTA])));
        }
        mask1 = (uint16_t)(((int16_t)i - delta_real_value) >> 15);  // i < delta_real_value
        e_j[i] = mask1 & gf_mul(tmp1, gf_inverse(tmp2));
    }

    // Place the delta e_{j_i} values at the right coordinates of the output vector
    delta_counter = 0;
    for (size_t i = 0; i < PARAM_N1; ++i) {
        found = 0;
        mask1 = (uint16_t)(-((int32_t)error[i]) >> 31);  // error[i] != 0
        for (size_t j = 0; j < PARAM_DELTA; j++) {
            mask2 = ~((uint16_t)(-((int32_t)j ^ delta_counter) >> 31));  // j == delta_counter
            error_values[i] += mask1 & mask2 & e_j[j];
            found += mask1 & mask2 & 1;
        }
        delta_counter += found;
    }
}

/**
 * @brief Correct the errors
 *
 * @param[out] cdw Array of PARAM_N1 elements receiving the corrected vector
 * @param[in] error_values Array of PARAM_DELTA elements storing the error values
 */
static void correct_errors(uint8_t *cdw, const uint16_t *error_values) {
    for (size_t i = 0; i < PARAM_N1; ++i) {
        cdw[i] ^= error_values[i];
    }
}

/**
 * @brief Decodes the received word
 *
 * This function relies on six steps:
 * -# Compute the 2·PARAM_DELTA syndromes.
 * -# Compute the error-locator polynomial σ(x).
 * -# Use an additive FFT to find the roots of σ(x) (the error locations) and take their inverses.
 * -# Compute the error-evaluator polynomial z(x).
 * -# Compute the error values at each located position.
 * -# Correct the received polynomial by subtracting the error values.
 *
 * For a more complete picture on Reed-Solomon decoding, see Shu. Lin and Daniel J. Costello in Error Control Coding:
 * Fundamentals and Applications @cite lin1983error
 *
 * @param[out] msg Array of size VEC_K_SIZE_64 receiving the decoded message
 * @param[in] cdw Array of size VEC_N1_SIZE_64 storing the received word
 */
void reed_solomon_decode(uint64_t *msg, uint64_t *cdw) {
    uint8_t cdw_bytes[PARAM_N1] = {0};
    __m256i syndromes256[SYND_SIZE_256];
    uint16_t *syndromes = (uint16_t *)syndromes256;
    uint16_t sigma[1 << PARAM_FFT] = {0};
    uint8_t error[1 << PARAM_M] = {0};
    uint16_t z[PARAM_N1] = {0};
    uint16_t error_values[PARAM_N1] = {0};
    uint16_t deg;

    // Copy the vector in an array of bytes
    memcpy(cdw_bytes, cdw, PARAM_N1);

    // Calculate the 2*PARAM_DELTA syndromes
    compute_syndromes(syndromes256, cdw_bytes);

    // Compute the error locator polynomial sigma
    // Sigma's degree is at most PARAM_DELTA but the FFT requires the extra room
    deg = compute_elp(sigma, syndromes);

    // Compute the error polynomial error
    compute_roots(error, sigma);

    // Compute the polynomial z(x)
    compute_z_poly(z, sigma, deg, syndromes);

    // Compute the error values
    compute_error_values(error_values, z, error);

    // Correct the errors
    correct_errors(cdw_bytes, error_values);

    // Retrieve the message from the decoded codeword
    memcpy(msg, cdw_bytes + (PARAM_G - 1), PARAM_K);

#ifdef VERBOSE
    printf("\n\nThe syndromes: ");
    for (size_t i = 0; i < 2 * PARAM_DELTA; ++i) {
        printf("%u ", syndromes[i]);
    }
    printf("\n\nThe error locator polynomial: sigma(x) = ");
    bool first_coeff = true;
    if (sigma[0]) {
        printf("%u", sigma[0]);
        first_coeff = false;
    }
    for (size_t i = 1; i < (1 << PARAM_FFT); ++i) {
        if (sigma[i] == 0)
            continue;
        if (!first_coeff)
            printf(" + ");
        first_coeff = false;
        if (sigma[i] != 1)
            printf("%u ", sigma[i]);
        if (i == 1)
            printf("x");
        else
            printf("x^%zu", i);
    }
    if (first_coeff)
        printf("0");

    printf("\n\nThe polynomial: z(x) = ");
    bool first_coeff_1 = true;
    if (z[0]) {
        printf("%u", z[0]);
        first_coeff_1 = false;
    }
    for (size_t i = 1; i < (PARAM_DELTA + 1); ++i) {
        if (z[i] == 0)
            continue;
        if (!first_coeff_1)
            printf(" + ");
        first_coeff_1 = false;
        if (z[i] != 1)
            printf("%u ", z[i]);
        if (i == 1)
            printf("x");
        else
            printf("x^%zu", i);
    }
    if (first_coeff_1)
        printf("0");

    printf("\n\nThe pairs of (error locator numbers, error values): ");
    size_t j = 0;
    for (size_t i = 0; i < PARAM_N1; ++i) {
        if (error[i]) {
            printf("(%zu, %d) ", i, error_values[j]);
            j++;
        }
    }
    printf("\n");
#endif

    // Zeroize sensitive data
    memset_zero(cdw_bytes, sizeof cdw_bytes);
}
