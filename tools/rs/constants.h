#ifndef HQC_TOOLS_RS_H
#define HQC_TOOLS_RS_H

#include <stddef.h>
#include <stdint.h>

#define RS_FIELD_POLY 0x11BU
#define RS_FIELD_ORDER 255U
#define RS_LANES_PER_VECTOR 16U
#define RS_MAX_DELTA 29U
#define RS_MAX_N1 90U
#define RS_MAX_RS_POLY_SIZE (2U * RS_MAX_DELTA + 1U)
#define RS_MAX_ALPHA_IJ_ROWS (2U * RS_MAX_DELTA)
#define RS_MAX_ALPHA_IJ_COLS (RS_MAX_N1 - 1U)
#define RS_MAX_ALPHA_IJ_SIZE (RS_MAX_ALPHA_IJ_ROWS * RS_MAX_ALPHA_IJ_COLS)
#define RS_MAX_PARAM_SIZE (((RS_MAX_RS_POLY_SIZE + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR) * RS_LANES_PER_VECTOR)
#define RS_MAX_ALPHA_IJ_VECTOR_SIZE (RS_LANES_PER_VECTOR * RS_MAX_ALPHA_IJ_COLS)

typedef struct {
    const char *name;
    uint16_t n1;
    uint16_t k;
    uint16_t delta;
    const char *ref_parameters_path;
    const char *common_parameters_path;
    const char *ref_reed_solomon_path;
    const char *avx_reed_solomon_path;
} parameter_set_t;

typedef struct {
    const parameter_set_t *parameter_set;
    size_t generator_poly_count;
    size_t alpha_ij_rows;
    size_t alpha_ij_cols;
    uint16_t generator_poly[RS_MAX_RS_POLY_SIZE];
    uint16_t alpha_ij[RS_MAX_ALPHA_IJ_SIZE];
} rs_constants_t;

const parameter_set_t *find_parameter_set(const char *name);
const char *supported_parameter_sets(void);
void build_rs_constants(const parameter_set_t *parameter_set, rs_constants_t *constants);
int print_gf_256_field_info(void);
void print_parameter_set_constants(const rs_constants_t *constants);
void pack_param_vectors(const uint16_t *coefficients, size_t count, uint16_t *packed);
void pack_alpha_ij_vectors(const uint16_t *alpha_ij, size_t rows, size_t cols, size_t row_start, uint16_t *packed);

#endif
