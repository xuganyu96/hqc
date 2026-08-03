#include "verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RS_ERROR_BUFFER_SIZE 256U
#define RS_PATH_BUFFER_SIZE 128U
#define RS_WORDS_PER_VECTOR 4U
#define RS_VALUES_PER_WORD 4U

static char *read_text_file(const char *path);
static char *read_repo_text_file(const char *path, char resolved_path[RS_PATH_BUFFER_SIZE]);
static int verify_rs_poly_file(const char *path, const rs_constants_t *constants);
static int verify_alpha_ij_file(const char *path, const rs_constants_t *constants);
static int verify_avx_file(const char *path, const rs_constants_t *constants);
static int parse_rs_poly(const char *text, uint16_t *out, size_t count, char *error, size_t error_size);
static int parse_matrix(const char *text, const char *name, uint16_t *out, size_t rows, size_t cols, char *error,
                        size_t error_size);
static int parse_vector_array(const char *text, const char *name, uint16_t *out, size_t entries, char *error,
                              size_t error_size);
static int compare_array(const uint16_t *expected, const uint16_t *actual, size_t count, const char *name, char *error,
                         size_t error_size);
static int compare_matrix(const uint16_t *expected, const uint16_t *actual, size_t rows, size_t cols, const char *name,
                          char *error, size_t error_size);
static int find_block(const char *text, const char *pattern, const char **start, const char **end, char *error,
                      size_t error_size);
static int next_integer_token(const char **cursor, const char *end, uint64_t *value);
static int parse_value_sequence(const char *start, const char *end, uint16_t *out, size_t count, const char *name,
                                char *error, size_t error_size);

int verify_parameter_set_constants(const rs_constants_t *constants) {
    const parameter_set_t *parameter_set = constants->parameter_set;

    if (verify_rs_poly_file(parameter_set->ref_parameters_path, constants) != 0) {
        return 1;
    }

    if (verify_rs_poly_file(parameter_set->common_parameters_path, constants) != 0) {
        return 1;
    }

    if (verify_alpha_ij_file(parameter_set->ref_reed_solomon_path, constants) != 0) {
        return 1;
    }

    if (verify_avx_file(parameter_set->avx_reed_solomon_path, constants) != 0) {
        return 1;
    }

    printf("verified %s against repository Reed-Solomon constants\n", parameter_set->name);
    return 0;
}

static char *read_text_file(const char *path) {
    FILE *stream = fopen(path, "rb");
    char *text;
    long size;

    if (stream == NULL) {
        return NULL;
    }

    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }

    size = ftell(stream);
    if (size < 0 || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }

    text = malloc((size_t)size + 1U);
    if (text == NULL) {
        fclose(stream);
        return NULL;
    }

    if (fread(text, 1, (size_t)size, stream) != (size_t)size) {
        fclose(stream);
        free(text);
        return NULL;
    }

    fclose(stream);
    text[size] = '\0';
    return text;
}

static char *read_repo_text_file(const char *path, char resolved_path[RS_PATH_BUFFER_SIZE]) {
    char *text = read_text_file(path);

    if (text != NULL) {
        snprintf(resolved_path, RS_PATH_BUFFER_SIZE, "%s", path);
        return text;
    }

    snprintf(resolved_path, RS_PATH_BUFFER_SIZE, "../%s", path);
    text = read_text_file(resolved_path);
    if (text != NULL) {
        return text;
    }

    resolved_path[0] = '\0';
    return NULL;
}

static int verify_rs_poly_file(const char *path, const rs_constants_t *constants) {
    char resolved_path[RS_PATH_BUFFER_SIZE];
    char error[RS_ERROR_BUFFER_SIZE];
    uint16_t actual[RS_MAX_RS_POLY_SIZE] = {0};
    char *text = read_repo_text_file(path, resolved_path);
    int status;

    if (text == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    status = parse_rs_poly(text, actual, constants->generator_poly_count, error, sizeof(error));
    if (status == 0) {
        status = compare_array(constants->generator_poly, actual, constants->generator_poly_count, "RS_POLY_COEFS", error,
                               sizeof(error));
    }

    if (status != 0) {
        fprintf(stderr, "%s: %s\n", resolved_path, error);
    }

    free(text);
    return status;
}

static int verify_alpha_ij_file(const char *path, const rs_constants_t *constants) {
    char resolved_path[RS_PATH_BUFFER_SIZE];
    char error[RS_ERROR_BUFFER_SIZE];
    uint16_t actual[RS_MAX_ALPHA_IJ_SIZE] = {0};
    char *text = read_repo_text_file(path, resolved_path);
    int status;

    if (text == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    status = parse_matrix(text, "alpha_ij_pow", actual, constants->alpha_ij_rows, constants->alpha_ij_cols, error,
                          sizeof(error));
    if (status == 0) {
        status = compare_matrix(constants->alpha_ij, actual, constants->alpha_ij_rows, constants->alpha_ij_cols,
                                "alpha_ij_pow", error, sizeof(error));
    }

    if (status != 0) {
        fprintf(stderr, "%s: %s\n", resolved_path, error);
    }

    free(text);
    return status;
}

static int verify_avx_file(const char *path, const rs_constants_t *constants) {
    char resolved_path[RS_PATH_BUFFER_SIZE];
    char error[RS_ERROR_BUFFER_SIZE];
    uint16_t expected_param[RS_MAX_PARAM_SIZE] = {0};
    uint16_t actual_param[RS_MAX_PARAM_SIZE] = {0};
    uint16_t expected_alpha_ij_vector[RS_MAX_ALPHA_IJ_VECTOR_SIZE] = {0};
    uint16_t actual_alpha_ij_vector[RS_MAX_ALPHA_IJ_VECTOR_SIZE] = {0};
    size_t array_count = (constants->alpha_ij_rows + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR;
    size_t param_entries = (constants->generator_poly_count + RS_LANES_PER_VECTOR - 1U) / RS_LANES_PER_VECTOR;
    char *text = read_repo_text_file(path, resolved_path);
    int status;

    if (text == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    pack_param_vectors(constants->generator_poly, constants->generator_poly_count, expected_param);
    status = parse_vector_array(text, "param256", actual_param, param_entries, error, sizeof(error));
    if (status == 0) {
        status = compare_array(expected_param, actual_param, param_entries * RS_LANES_PER_VECTOR, "param256", error,
                               sizeof(error));
    }

    if (status != 0) {
        fprintf(stderr, "%s: %s\n", resolved_path, error);
        free(text);
        return status;
    }

    for (size_t array_index = 0; array_index < array_count; ++array_index) {
        char name[32];

        pack_alpha_ij_vectors(constants->alpha_ij, constants->alpha_ij_rows, constants->alpha_ij_cols,
                              array_index * RS_LANES_PER_VECTOR, expected_alpha_ij_vector);
        snprintf(name, sizeof(name), "alpha_ij256_%zu", array_index + 1U);

        status = parse_vector_array(text, name, actual_alpha_ij_vector, constants->alpha_ij_cols, error, sizeof(error));
        if (status == 0) {
            status = compare_array(expected_alpha_ij_vector, actual_alpha_ij_vector,
                                   constants->alpha_ij_cols * RS_LANES_PER_VECTOR, name, error, sizeof(error));
        }

        if (status != 0) {
            fprintf(stderr, "%s: %s\n", resolved_path, error);
            free(text);
            return status;
        }
    }

    free(text);
    return 0;
}

static int parse_rs_poly(const char *text, uint16_t *out, size_t count, char *error, size_t error_size) {
    const char *start = strstr(text, "#define RS_POLY_COEFS");
    const char *end;

    if (start == NULL) {
        snprintf(error, error_size, "failed to locate RS_POLY_COEFS");
        return 1;
    }

    start += strlen("#define RS_POLY_COEFS");
    end = strstr(start, "///<");
    if (end == NULL) {
        snprintf(error, error_size, "failed to find end of RS_POLY_COEFS");
        return 1;
    }

    return parse_value_sequence(start, end, out, count, "RS_POLY_COEFS", error, error_size);
}

static int parse_matrix(const char *text, const char *name, uint16_t *out, size_t rows, size_t cols, char *error,
                        size_t error_size) {
    char pattern[64];
    const char *start;
    const char *end;

    snprintf(pattern, sizeof(pattern), "static const uint16_t %s[", name);
    if (find_block(text, pattern, &start, &end, error, error_size) != 0) {
        return 1;
    }

    return parse_value_sequence(start, end, out, rows * cols, name, error, error_size);
}

static int parse_vector_array(const char *text, const char *name, uint16_t *out, size_t entries, char *error,
                              size_t error_size) {
    char pattern[64];
    const char *start;
    const char *end;
    const char *cursor;

    snprintf(pattern, sizeof(pattern), "static const __m256i %s[", name);
    if (find_block(text, pattern, &start, &end, error, error_size) != 0) {
        return 1;
    }

    cursor = start;
    for (size_t entry = 0; entry < entries; ++entry) {
        uint64_t words[RS_WORDS_PER_VECTOR];

        for (size_t word = 0; word < RS_WORDS_PER_VECTOR; ++word) {
            int status = next_integer_token(&cursor, end, &words[word]);
            if (status <= 0) {
                snprintf(error, error_size, "%s is missing packed word %zu", name, entry * RS_WORDS_PER_VECTOR + word);
                return 1;
            }
        }

        for (size_t word = 0; word < RS_WORDS_PER_VECTOR; ++word) {
            for (size_t value = 0; value < RS_VALUES_PER_WORD; ++value) {
                out[entry * RS_LANES_PER_VECTOR + word * RS_VALUES_PER_WORD + value] =
                    (uint16_t)((words[word] >> (16U * value)) & 0xFFFFU);
            }
        }
    }

    {
        uint64_t value;
        int status = next_integer_token(&cursor, end, &value);
        if (status == 1) {
            snprintf(error, error_size, "%s has extra packed values", name);
            return 1;
        }
    }

    return 0;
}

static int compare_array(const uint16_t *expected, const uint16_t *actual, size_t count, const char *name, char *error,
                         size_t error_size) {
    for (size_t index = 0; index < count; ++index) {
        if (expected[index] != actual[index]) {
            snprintf(error, error_size, "%s[%zu] mismatch: expected %u, got %u", name, index, expected[index],
                     actual[index]);
            return 1;
        }
    }

    return 0;
}

static int compare_matrix(const uint16_t *expected, const uint16_t *actual, size_t rows, size_t cols, const char *name,
                          char *error, size_t error_size) {
    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            size_t index = row * cols + col;
            if (expected[index] != actual[index]) {
                snprintf(error, error_size, "%s[%zu][%zu] mismatch: expected %u, got %u", name, row, col,
                         expected[index], actual[index]);
                return 1;
            }
        }
    }

    return 0;
}

static int find_block(const char *text, const char *pattern, const char **start, const char **end, char *error,
                      size_t error_size) {
    const char *anchor = strstr(text, pattern);
    const char *equal;
    const char *open_brace;
    const char *close_brace;

    if (anchor == NULL) {
        snprintf(error, error_size, "failed to locate %s", pattern);
        return 1;
    }

    equal = strchr(anchor, '=');
    if (equal == NULL) {
        snprintf(error, error_size, "failed to find initializer for %s", pattern);
        return 1;
    }

    open_brace = strchr(equal, '{');
    close_brace = strstr(equal, "};");
    if (open_brace == NULL || close_brace == NULL || open_brace >= close_brace) {
        snprintf(error, error_size, "failed to find data block for %s", pattern);
        return 1;
    }

    *start = open_brace;
    *end = close_brace;
    return 0;
}

static int next_integer_token(const char **cursor, const char *end, uint64_t *value) {
    const char *start = *cursor;
    char *next;

    while (start < end && (*start < '0' || *start > '9')) {
        ++start;
    }

    if (start >= end) {
        return 0;
    }

    *value = strtoull(start, &next, 0);
    if (next == start || next > end) {
        return -1;
    }

    *cursor = next;
    return 1;
}

static int parse_value_sequence(const char *start, const char *end, uint16_t *out, size_t count, const char *name,
                                char *error, size_t error_size) {
    const char *cursor = start;

    for (size_t index = 0; index < count; ++index) {
        uint64_t value;
        int status = next_integer_token(&cursor, end, &value);

        if (status <= 0) {
            snprintf(error, error_size, "%s is missing value %zu", name, index);
            return 1;
        }

        out[index] = (uint16_t)value;
    }

    {
        uint64_t value;
        int status = next_integer_token(&cursor, end, &value);
        if (status == 1) {
            snprintf(error, error_size, "%s has extra values", name);
            return 1;
        }
    }

    return 0;
}
