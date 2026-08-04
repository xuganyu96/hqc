#include "constants.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ref/gf.h"

#define RS_WORDS_PER_VECTOR 4U
#define RS_VALUES_PER_WORD 4U

static uint16_t alpha_power(size_t exponent);
static size_t beta_exponent(const parameter_set_t *parameter_set, size_t i_msg);
static void build_generator_poly(const parameter_set_t *parameter_set, uint16_t poly[RS_MAX_RS_POLY_SIZE]);
static void build_alpha_ij_table(const parameter_set_t *parameter_set, uint16_t alpha_ij[RS_MAX_ALPHA_IJ_SIZE]);
static void build_eval_alpha_pow_table(const parameter_set_t *parameter_set,
                                       uint16_t eval_alpha_pow[RS_MAX_EVAL_ALPHA_POW_SIZE]);
static void build_beta_inv_pow_lanes_table(const parameter_set_t *parameter_set, size_t positions_blocks,
                                        uint16_t beta_inv_pow_lanes[RS_MAX_BETA_INV_POW_LANES_SIZE]);
static void print_modulus(uint16_t modulus);
static void print_destination_header(const char *symbol_name, const char *primary_path, const char *secondary_path);
static void print_word_array_definition(const char *name, const uint16_t *values, size_t count, size_t columns);
static void print_rs_poly_macro(const uint16_t *coefficients, size_t count, size_t columns);
static void print_matrix_definition(const char *name, const uint16_t *values, size_t rows, size_t cols);
static void print_param_vector_definition(const uint16_t *coefficients, size_t count);
static void print_alpha_ij_vector_definitions(const uint16_t *alpha_ij, size_t rows, size_t cols);
static void print_packed_vector_entry(const uint16_t lanes[RS_LANES_PER_VECTOR]);
static void print_eval_alpha_pow_definition(const parameter_set_t *parameter_set, const uint16_t *eval_alpha_pow);
static void print_beta_inv_pow_lanes_definition(const parameter_set_t *parameter_set, size_t positions_blocks,
                                             const uint16_t *beta_inv_pow_lanes);

static const parameter_set_t PARAMETER_SETS[] = {
    {
        "hqc-1",
        46,
        16,
        15,
        "src/ref/hqc-1/parameters.h",
        "src/x86_64/common/hqc-1/parameters.h",
        "src/ref/hqc-1/reed_solomon.h",
        "src/x86_64/avx256/hqc-1/reed_solomon.c",
    },
    {
        "hqc-3",
        56,
        24,
        16,
        "src/ref/hqc-3/parameters.h",
        "src/x86_64/common/hqc-3/parameters.h",
        "src/ref/hqc-3/reed_solomon.h",
        "src/x86_64/avx256/hqc-3/reed_solomon.c",
    },
    {
        "hqc-5",
        90,
        32,
        29,
        "src/ref/hqc-5/parameters.h",
        "src/x86_64/common/hqc-5/parameters.h",
        "src/ref/hqc-5/reed_solomon.h",
        "src/x86_64/avx256/hqc-5/reed_solomon.c",
    },
};

const parameter_set_t *find_parameter_set(const char *name) {
    size_t count = sizeof(PARAMETER_SETS) / sizeof(PARAMETER_SETS[0]);

    for (size_t index = 0; index < count; ++index) {
        if (strcmp(PARAMETER_SETS[index].name, name) == 0) {
            return &PARAMETER_SETS[index];
        }
    }

    return NULL;
}

const char *supported_parameter_sets(void) {
    return "hqc-1, hqc-3, hqc-5";
}

void build_rs_constants(const parameter_set_t *parameter_set, rs_constants_t *constants) {
    memset(constants, 0, sizeof(*constants));

    constants->parameter_set = parameter_set;
    constants->generator_poly_count = (size_t)(2U * parameter_set->delta + 1U);
    constants->alpha_ij_rows = (size_t)(2U * parameter_set->delta);
    constants->alpha_ij_cols = (size_t)(parameter_set->n1 - 1U);
    constants->positions_blocks = (size_t)((parameter_set->k + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR);

    build_generator_poly(parameter_set, constants->generator_poly);
    build_alpha_ij_table(parameter_set, constants->alpha_ij);
    build_eval_alpha_pow_table(parameter_set, constants->eval_alpha_pow);
    build_beta_inv_pow_lanes_table(parameter_set, constants->positions_blocks, constants->beta_inv_pow_lanes);
}

int print_gf_256_field_info(void) {
    printf("modulus: 0x%03X (", RS_FIELD_POLY);
    print_modulus(RS_FIELD_POLY);
    printf(")\n");
    printf("primitive byte: 0x%02X\n", gf_alpha);
    printf("multiplicative order of primitive byte: %u\n", RS_FIELD_ORDER);
    printf("\n");
    print_word_array_definition("gf_exp", gf_exp, sizeof(gf_exp) / sizeof(gf_exp[0]), 16);
    printf("\n");
    print_word_array_definition("gf_log", gf_log, sizeof(gf_log) / sizeof(gf_log[0]), 16);

    return 0;
}

void print_parameter_set_constants(const rs_constants_t *constants) {
    const parameter_set_t *parameter_set = constants->parameter_set;

    printf("/* %s: modulus=0x%03X, primitive=0x%02X, n1=%u, k=%u, delta=%u */\n", parameter_set->name, RS_FIELD_POLY,
           gf_alpha, parameter_set->n1, parameter_set->k, parameter_set->delta);
    print_destination_header("RS_POLY_COEFS", parameter_set->ref_parameters_path, parameter_set->common_parameters_path);
    print_rs_poly_macro(constants->generator_poly, constants->generator_poly_count, 16);
    printf("\n");
    print_destination_header("alpha_ij_pow", parameter_set->ref_reed_solomon_path, NULL);
    print_matrix_definition("alpha_ij_pow", constants->alpha_ij, constants->alpha_ij_rows, constants->alpha_ij_cols);
    printf("\n");
    print_destination_header("alpha_ij_vecs_* and rs_generator_vecs", parameter_set->avx_reed_solomon_path, NULL);
    print_alpha_ij_vector_definitions(constants->alpha_ij, constants->alpha_ij_rows, constants->alpha_ij_cols);
    print_param_vector_definition(constants->generator_poly, constants->generator_poly_count);
    printf("\n");
    print_destination_header("eval_alpha_pow", parameter_set->ref_reed_solomon_path, NULL);
    print_eval_alpha_pow_definition(parameter_set, constants->eval_alpha_pow);
    printf("\n");
    print_destination_header("beta_inv_pow_lanes", parameter_set->avx_reed_solomon_path, NULL);
    print_beta_inv_pow_lanes_definition(parameter_set, constants->positions_blocks, constants->beta_inv_pow_lanes);
    printf("\n");
}

void pack_param_vectors(const uint16_t *coefficients, size_t count, uint16_t *packed) {
    size_t entry_count = (count + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR;

    memset(packed, 0, entry_count * RS_LANES_PER_VECTOR * sizeof(*packed));
    memcpy(packed, coefficients, count * sizeof(*coefficients));
}

void pack_alpha_ij_vectors(const uint16_t *alpha_ij, size_t rows, size_t cols, size_t row_start, uint16_t *packed) {
    memset(packed, 0, cols * RS_LANES_PER_VECTOR * sizeof(*packed));

    for (size_t col = 0; col < cols; ++col) {
        for (size_t lane = 0; lane < RS_LANES_PER_VECTOR; ++lane) {
            size_t row = row_start + lane;
            if (row < rows) {
                packed[col * RS_LANES_PER_VECTOR + lane] = alpha_ij[row * cols + col];
            }
        }
    }
}

static uint16_t alpha_power(size_t exponent) {
    return gf_exp[exponent % RS_FIELD_ORDER];
}

/*
 * beta = alpha^(-pos) is the evaluation point for message position i_msg, where
 * pos = (n1 - k) + i_msg is the corresponding codeword position. Returns the
 * exponent e_beta such that beta = alpha^e_beta (0 <= e_beta < RS_FIELD_ORDER).
 */
static size_t beta_exponent(const parameter_set_t *parameter_set, size_t i_msg) {
    size_t pos = (size_t)(parameter_set->n1 - parameter_set->k) + i_msg;
    size_t neg_pos = pos % RS_FIELD_ORDER;

    return (neg_pos == 0) ? 0 : (RS_FIELD_ORDER - neg_pos);
}

static void build_generator_poly(const parameter_set_t *parameter_set, uint16_t poly[RS_MAX_RS_POLY_SIZE]) {
    size_t current_degree = 0;

    memset(poly, 0, RS_MAX_RS_POLY_SIZE * sizeof(*poly));
    poly[0] = 1;

    for (size_t exponent = 1; exponent <= (size_t)(2U * parameter_set->delta); ++exponent) {
        uint16_t root = alpha_power(exponent);

        poly[current_degree + 1U] = 1;
        for (size_t index = current_degree; index > 0; --index) {
            poly[index] = (uint16_t)(gf_mul(poly[index], root) ^ poly[index - 1]);
        }
        poly[0] = gf_mul(poly[0], root);
        current_degree += 1U;
    }
}

static void build_alpha_ij_table(const parameter_set_t *parameter_set, uint16_t alpha_ij[RS_MAX_ALPHA_IJ_SIZE]) {
    size_t rows = (size_t)(2U * parameter_set->delta);
    size_t cols = (size_t)(parameter_set->n1 - 1U);

    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            alpha_ij[row * cols + col] = alpha_power((row + 1U) * (col + 1U));
        }
    }
}

static void build_eval_alpha_pow_table(const parameter_set_t *parameter_set,
                                       uint16_t eval_alpha_pow[RS_MAX_EVAL_ALPHA_POW_SIZE]) {
    size_t k = parameter_set->k;
    size_t t = parameter_set->delta;

    for (size_t i_msg = 0; i_msg < k; ++i_msg) {
        size_t e_beta = beta_exponent(parameter_set, i_msg);

        for (size_t j = 1; j <= t; ++j) {
            eval_alpha_pow[i_msg * t + (j - 1U)] = alpha_power(e_beta * j);
        }
    }
}

static void build_beta_inv_pow_lanes_table(const parameter_set_t *parameter_set, size_t positions_blocks,
                                        uint16_t beta_inv_pow_lanes[RS_MAX_BETA_INV_POW_LANES_SIZE]) {
    size_t k = parameter_set->k;
    size_t t = parameter_set->delta;

    for (size_t j = 0; j <= t; ++j) {
        for (size_t block = 0; block < positions_blocks; ++block) {
            for (size_t lane = 0; lane < RS_LANES_PER_VECTOR; ++lane) {
                size_t i_msg = block * RS_LANES_PER_VECTOR + lane;
                size_t index = (j * positions_blocks + block) * RS_LANES_PER_VECTOR + lane;
                uint16_t value;

                if (i_msg >= k) {
                    value = 0;
                } else if (j == 0) {
                    value = 1;
                } else {
                    value = alpha_power(beta_exponent(parameter_set, i_msg) * j);
                }

                beta_inv_pow_lanes[index] = value;
            }
        }
    }
}

static void print_modulus(uint16_t modulus) {
    bool first = true;

    for (int degree = 15; degree >= 0; --degree) {
        if ((modulus & (1U << degree)) == 0) {
            continue;
        }

        if (!first) {
            printf(" + ");
        }

        if (degree == 0) {
            printf("1");
        } else if (degree == 1) {
            printf("x");
        } else {
            printf("x^%d", degree);
        }

        first = false;
    }
}

static void print_destination_header(const char *symbol_name, const char *primary_path, const char *secondary_path) {
    printf("/* Paste %s into:\n", symbol_name);
    printf(" *   %s\n", primary_path);
    if (secondary_path != NULL) {
        printf(" *   %s\n", secondary_path);
    }
    printf(" */\n");
}

static void print_word_array_definition(const char *name, const uint16_t *values, size_t count, size_t columns) {
    printf("static const uint16_t %s[%zu] = {\n", name, count);

    for (size_t start = 0; start < count; start += columns) {
        size_t end = start + columns;
        if (end > count) {
            end = count;
        }

        printf("    ");
        for (size_t index = start; index < end; ++index) {
            if (index != start) {
                printf(", ");
            }
            printf("%u", values[index]);
        }
        printf(",\n");
    }

    printf("};\n");
}

static void print_rs_poly_macro(const uint16_t *coefficients, size_t count, size_t columns) {
    printf("#define RS_POLY_COEFS \\\n");

    for (size_t start = 0; start < count; start += columns) {
        size_t end = start + columns;
        if (end > count) {
            end = count;
        }

        printf("    ");
        for (size_t index = start; index < end; ++index) {
            if (index != start) {
                printf(", ");
            }
            printf("%u", coefficients[index]);
        }

        if (end != count) {
            printf(" \\\n");
        } else {
            printf("  ///< Coefficients of the Reed-Solomon generator polynomial\n");
        }
    }
}

static void print_matrix_definition(const char *name, const uint16_t *values, size_t rows, size_t cols) {
    printf("static const uint16_t %s[%zu][%zu] = {\n", name, rows, cols);

    for (size_t row = 0; row < rows; ++row) {
        printf("    {");
        for (size_t col = 0; col < cols; ++col) {
            if (col != 0) {
                printf(", ");
            }
            printf("%u", values[row * cols + col]);
        }
        printf("},\n");
    }

    printf("};\n");
}

static void print_param_vector_definition(const uint16_t *coefficients, size_t count) {
    size_t entry_count = (count + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR;
    uint16_t packed[RS_MAX_PARAM_SIZE] = {0};

    printf("static const __m256i rs_generator_vecs[%zu] = {\n", entry_count);
    pack_param_vectors(coefficients, count, packed);

    for (size_t entry = 0; entry < entry_count; ++entry) {
        print_packed_vector_entry(&packed[entry * RS_LANES_PER_VECTOR]);
    }

    printf("};\n");
}

static void print_alpha_ij_vector_definitions(const uint16_t *alpha_ij, size_t rows, size_t cols) {
    size_t array_count = (rows + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR;

    for (size_t array_index = 0; array_index < array_count; ++array_index) {
        uint16_t packed[RS_MAX_ALPHA_IJ_VECTOR_SIZE] = {0};

        printf("static const __m256i alpha_ij_vecs_%zu[%zu] = {\n", array_index + 1U, cols);
        pack_alpha_ij_vectors(alpha_ij, rows, cols, array_index * RS_LANES_PER_VECTOR, packed);

        for (size_t col = 0; col < cols; ++col) {
            print_packed_vector_entry(&packed[col * RS_LANES_PER_VECTOR]);
        }

        printf("};\n\n");
    }
}

static void print_packed_vector_entry(const uint16_t lanes[RS_LANES_PER_VECTOR]) {
    uint64_t words[RS_WORDS_PER_VECTOR] = {0};

    for (size_t word_index = 0; word_index < RS_WORDS_PER_VECTOR; ++word_index) {
        uint64_t word = 0;
        for (size_t value_index = 0; value_index < RS_VALUES_PER_WORD; ++value_index) {
            size_t lane_index = word_index * RS_VALUES_PER_WORD + value_index;
            word |= ((uint64_t)lanes[lane_index]) << (16U * value_index);
        }
        words[word_index] = word;
    }

    printf("    {0x%016llx, 0x%016llx, 0x%016llx, 0x%016llx},\n", (unsigned long long)words[0],
           (unsigned long long)words[1], (unsigned long long)words[2], (unsigned long long)words[3]);
}

static void print_eval_alpha_pow_definition(const parameter_set_t *parameter_set, const uint16_t *eval_alpha_pow) {
    size_t k = parameter_set->k;
    size_t t = parameter_set->delta;

    printf("static const uint8_t eval_alpha_pow[PARAM_K][PARAM_T] = {\n");
    for (size_t i_msg = 0; i_msg < k; ++i_msg) {
        size_t e_beta = beta_exponent(parameter_set, i_msg);

        printf("    /* row %zu: beta = alpha^%zu */ {", i_msg, e_beta);
        for (size_t j = 0; j < t; ++j) {
            printf("0x%02X%s", eval_alpha_pow[i_msg * t + j], (j + 1U == t) ? "" : ", ");
        }
        printf("}%s\n", (i_msg + 1U == k) ? "};" : ",");
    }
}

static void print_beta_inv_pow_lanes_definition(const parameter_set_t *parameter_set, size_t positions_blocks,
                                             const uint16_t *beta_inv_pow_lanes) {
    size_t t = parameter_set->delta;

    printf("static const uint16_t beta_inv_pow_lanes[PARAM_T + 1][POSITIONS_BLOCKS][GF_LANES] = {\n");
    for (size_t j = 0; j <= t; ++j) {
        printf("    {// power j = %zu\n", j);
        for (size_t block = 0; block < positions_blocks; ++block) {
            const uint16_t *row = &beta_inv_pow_lanes[(j * positions_blocks + block) * RS_LANES_PER_VECTOR];

            printf("     {");
            for (size_t lane = 0; lane < RS_LANES_PER_VECTOR; ++lane) {
                printf("%u%s", row[lane], (lane + 1U == RS_LANES_PER_VECTOR) ? "" : ", ");
            }
            printf("}%s", (block + 1U == positions_blocks) ? "}" : ",\n");
        }
        printf("%s\n", (j == t) ? "};" : ",");
    }
}
