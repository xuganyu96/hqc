#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef AVX2_MODE
#include <immintrin.h>
#endif

#include "crypto_memset.h"
#include "parameters.h"

#ifndef RS_IMPL_INCLUDE
#error "RS_IMPL_INCLUDE must be defined to the path of the variant reed_solomon.c"
#endif

#include RS_IMPL_INCLUDE

#ifdef AVX2_MODE
#ifndef SYND_SIZE_256
#define SYND_SIZE_256 (CEIL_DIVIDE(2 * PARAM_T, 16))
#endif
#endif

static inline void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
    size_t i = 0;
    while (isspace((unsigned char)s[i])) i++;
    if (i)
        memmove(s, s + i, strlen(s + i) + 1);
}

static inline int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

static int hexbyte_to_u8(const char *s, uint8_t *out) {
    unsigned v;
    if (sscanf(s, "%2x", &v) != 1)
        return 0;
    *out = (uint8_t)v;
    return 1;
}

static int parse_hex_bytes_line(const char *line, const char *key, uint8_t *out, size_t out_len) {
    // expects: "key = aa bb cc ..."
    size_t klen = strlen(key);
    if (!starts_with(line, key))
        return 0;
    const char *p = line + klen;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p != '=')
        return 0;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;

    size_t i = 0;
    while (*p && i < out_len) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1]))
            break;
        if (!hexbyte_to_u8(p, &out[i]))
            break;
        i++;
        p += 2;
        while (*p == ' ' || *p == '\t') p++;
    }
    for (; i < out_len; i++) out[i] = 0;
    return 1;
}

static void write_hex_bytes(FILE *fp, const char *key, const uint8_t *buf, size_t len) {
    fprintf(fp, "%s = ", key);
    for (size_t i = 0; i < len; i++) {
        fprintf(fp, "%02x%s", buf[i], (i + 1 == len) ? "" : " ");
    }
    fputc('\n', fp);
}

static void write_u16_lowbytes_padded(FILE *fp, const char *key, const uint16_t *arr, size_t want) {
    fprintf(fp, "%s = ", key);
    for (size_t i = 0; i < want; i++) {
        uint8_t b = arr ? (uint8_t)(arr[i] & 0xFF) : 0;
        fprintf(fp, "%02x%s", b, (i + 1 == want) ? "" : " ");
    }
    fputc('\n', fp);
}

static void write_mask_00_01(FILE *fp, const char *key, const uint8_t *mask, size_t len) {
    fprintf(fp, "%s = ", key);
    for (size_t i = 0; i < len; i++) {
        fprintf(fp, "%02x%s", mask[i] ? 0x01 : 0x00, (i + 1 == len) ? "" : " ");
    }
    fputc('\n', fp);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/rs.tv\n", argv[0]);
        return 2;
    }
    const char *tv_path = argv[1];

    FILE *fin = fopen(tv_path, "r");
    if (!fin) {
        perror("fopen .tv");
        return 1;
    }

    FILE *fout = fopen("rs.rsp", "w");
    if (!fout) {
        perror("fopen rs.rsp");
        fclose(fin);
        return 1;
    }

    char line[4096];
    int in_record = 0;
    int have_msg = 0, have_cor = 0;

    uint8_t msg[PARAM_K];
    uint8_t corrupted[PARAM_N1];

    size_t rec_count = 0;

    char scenario_label[128] = "";
    int scenario_total = 0;
    int scenario_idx = 0;

    while (fgets(line, sizeof line, fin)) {
        trim(line);
        if (line[0] == 0)
            continue;

        if (starts_with(line, "# ===== BEGIN SCENARIO:")) {
            char label[128] = "";
            int count = 0;
            if (sscanf(line, "# ===== BEGIN SCENARIO: %127s (count=%d)", label, &count) == 2) {
                snprintf(scenario_label, sizeof scenario_label, "%s", label);
                scenario_total = count;
                scenario_idx = 0;
            }
            fprintf(fout, "%s\n", line);
            continue;
        }
        if (starts_with(line, "# ===== END SCENARIO:")) {
            fprintf(fout, "%s\n\n", line);
            continue;
        }

        if (starts_with(line, "---")) {
            in_record = 1;
            have_msg = have_cor = 0;
            fprintf(fout, "---\n");
            if (scenario_label[0] != '\0') {
                scenario_idx++;
                fprintf(fout, "# sample %d/%d\n", scenario_idx, scenario_total);
            }
            continue;
        }
        if (!in_record)
            continue;

        if (starts_with(line, "message")) {
            if (!parse_hex_bytes_line(line, "message", msg, PARAM_K)) {
                fprintf(stderr, "[warn] bad message line\n");
                continue;
            }
            have_msg = 1;
            continue;
        }
        if (starts_with(line, "corrupted")) {
            if (!parse_hex_bytes_line(line, "corrupted", corrupted, PARAM_N1)) {
                fprintf(stderr, "[warn] bad corrupted line\n");
                continue;
            }
            have_cor = 1;
            continue;
        }

        if (have_msg && have_cor) {
            uint8_t cw_encoded[PARAM_N1] = {0};
            uint8_t decoded_msg[PARAM_K] = {0};
            uint16_t sigma[PARAM_T + 1] = {0};
            uint8_t error_positions_msg[PARAM_K] = {0};

            reed_solomon_encode((uint64_t *)cw_encoded, (const uint64_t *)msg);
            uint8_t cdw_work[PARAM_N1];
            memcpy(cdw_work, corrupted, PARAM_N1);

#ifdef AVX2_MODE
            __m256i syndrome_vecs[SYND_SIZE_256];
            uint16_t *syndromes = (uint16_t *)syndrome_vecs;
            compute_syndromes(syndrome_vecs, cdw_work);
#else
            uint16_t syndromes_u16[2 * PARAM_T] = {0};
            uint16_t *syndromes = syndromes_u16;
            compute_syndromes(syndromes_u16, cdw_work);
#endif

            compute_error_locator_polynomial(sigma, syndromes);
            compute_error_positions(error_positions_msg, sigma);
            correct_errors(decoded_msg, cdw_work, syndromes, sigma, error_positions_msg);

            write_hex_bytes(fout, "message", msg, PARAM_K);
            write_hex_bytes(fout, "codeword", cw_encoded, PARAM_N1);
            write_hex_bytes(fout, "corrupted", corrupted, PARAM_N1);

            uint8_t s8[2 * PARAM_T] = {0};
            for (int i = 0; i < 2 * PARAM_T; i++) s8[i] = (uint8_t)(syndromes[i] & 0xFF);

            write_hex_bytes(fout, "syndromes", s8, 2 * PARAM_T);
            write_u16_lowbytes_padded(fout, "sigma", sigma, PARAM_T + 1);
            write_mask_00_01(fout, "error_positions", error_positions_msg, PARAM_K);
            write_hex_bytes(fout, "decoded", decoded_msg, PARAM_K);

            have_msg = have_cor = 0;
            rec_count++;
            if ((rec_count % 1000) == 0) {
                fprintf(stderr, "[info] processed %zu records\n", rec_count);
            }
        }
    }

    fclose(fin);
    fclose(fout);
    fprintf(stderr, "[done] rs.rsp written (%zu records)\n", rec_count);
    return 0;
}
