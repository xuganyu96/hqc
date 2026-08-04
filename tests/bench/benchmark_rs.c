#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "parameters.h"
#include "reed_solomon.h"

#define NB_TEST    100
#define NB_SAMPLES 1000

static inline uint64_t cpucyclesStart(void) {
    unsigned hi, lo;
    __asm__ __volatile__(
        "CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r"(hi), "=r"(lo)
        :
        : "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)lo) ^ (((uint64_t)hi) << 32);
}

static inline uint64_t cpucyclesStop(void) {
    unsigned hi, lo;
    __asm__ __volatile__(
        "RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t"
        : "=r"(hi), "=r"(lo)
        :
        : "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)lo) ^ (((uint64_t)hi) << 32);
}

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Fill buffer from getrandom(); fallback to /dev/urandom; last resort time^pid */
static void rand_bytes(uint8_t *buf, size_t len) {
    ssize_t r = syscall(SYS_getrandom, buf, len, 0);
    if (r == (ssize_t)len)
        return;

    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(buf, 1, len, f);
        fclose(f);
        if (got == len)
            return;
    }
    /* very weak fallback to avoid zeros */
    uint32_t x = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    for (size_t i = 0; i < len; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        buf[i] = (uint8_t)x;
    }
}

/* Inject up to t random byte errors in the **message portion** only */
static void inject_message_errors(uint8_t *cw_bytes, size_t count) {
    if (count == 0)
        return;
    if (count > PARAM_T)
        count = PARAM_T;

    const size_t lo = PARAM_N1 - PARAM_K; /* start of message in systematic codeword */
    /* choose distinct positions */
    uint8_t used[PARAM_K] = {0};
    size_t placed = 0;
    while (placed < count) {
        uint8_t r;
        rand_bytes(&r, 1);
        size_t idx = lo + (r % PARAM_K);
        size_t rel = idx - lo;
        if (used[rel])
            continue;
        used[rel] = 1;
        /* non-zero mask */
        uint8_t m;
        do {
            rand_bytes(&m, 1);
        } while (m == 0);
        cw_bytes[idx] ^= m;
        placed++;
    }
}

/* Convenience wrappers to call your RS API on byte arrays */
static void rs_encode_bytes(const uint8_t *m, uint8_t *cw) {
    uint64_t cw_words[VEC_N1_SIZE_64] = {0};
    reed_solomon_encode(cw_words, (const uint64_t *)m);
    memcpy(cw, cw_words, PARAM_N1);
}

static void rs_decode_bytes(const uint8_t *cw, uint8_t *m_out) {
    uint64_t cw_words[VEC_N1_SIZE_64] = {0};
    memcpy(cw_words, cw, PARAM_N1);
    reed_solomon_decode((uint64_t *)m_out, cw_words);
}

int main(void) {
    uint8_t msg[PARAM_K];
    uint8_t cw_clean[PARAM_N1];
    uint8_t cw_corrupt[PARAM_N1];
    uint8_t dec[PARAM_K];

    /* Accumulators */
    uint64_t enc_cycles_total = 0, dec_cycles_total = 0, dec_err_cycles_total = 0;
    uint64_t enc_ns_total = 0, dec_ns_total = 0, dec_err_ns_total = 0;

    /* Warm-ups (RS has no keygen; warm caches & JIT) */
    for (size_t i = 0; i < NB_TEST; ++i) {
        rand_bytes(msg, sizeof msg);
        rs_encode_bytes(msg, cw_clean);
        rs_decode_bytes(cw_clean, dec);
    }

    /* --------- Encode bench --------- */
    for (size_t s = 0; s < NB_SAMPLES; ++s) {
        uint64_t cyc_acc = 0, ns_acc = 0;
        for (size_t i = 0; i < NB_TEST; ++i) {
            rand_bytes(msg, sizeof msg);

            uint64_t c1 = cpucyclesStart();
            uint64_t t1 = get_time_ns();
            rs_encode_bytes(msg, cw_clean);
            uint64_t t2 = get_time_ns();
            uint64_t c2 = cpucyclesStop();

            cyc_acc += (c2 - c1);
            ns_acc += (t2 - t1);
        }
        enc_cycles_total += cyc_acc / NB_TEST;
        enc_ns_total += ns_acc / NB_TEST;
    }

    /* --------- Decode bench (no errors) --------- */
    for (size_t s = 0; s < NB_SAMPLES; ++s) {
        uint64_t cyc_acc = 0, ns_acc = 0;
        for (size_t i = 0; i < NB_TEST; ++i) {
            rand_bytes(msg, sizeof msg);
            rs_encode_bytes(msg, cw_clean);

            uint64_t c1 = cpucyclesStart();
            uint64_t t1 = get_time_ns();
            rs_decode_bytes(cw_clean, dec);
            uint64_t t2 = get_time_ns();
            uint64_t c2 = cpucyclesStop();

            cyc_acc += (c2 - c1);
            ns_acc += (t2 - t1);

            /* sanity */
            if (memcmp(dec, msg, PARAM_K) != 0) {
                fprintf(stderr, "Decode mismatch in clean path!\n");
                return 1;
            }
        }
        dec_cycles_total += cyc_acc / NB_TEST;
        dec_ns_total += ns_acc / NB_TEST;
    }

    /* --------- Decode bench (with errors on message; count = t) --------- */
    for (size_t s = 0; s < NB_SAMPLES; ++s) {
        uint64_t cyc_acc = 0, ns_acc = 0;
        for (size_t i = 0; i < NB_TEST; ++i) {
            rand_bytes(msg, sizeof msg);
            rs_encode_bytes(msg, cw_clean);
            memcpy(cw_corrupt, cw_clean, PARAM_N1);
            inject_message_errors(cw_corrupt, PARAM_T); /* worst-case within capacity */

            uint64_t c1 = cpucyclesStart();
            uint64_t t1 = get_time_ns();
            rs_decode_bytes(cw_corrupt, dec);
            uint64_t t2 = get_time_ns();
            uint64_t c2 = cpucyclesStop();

            cyc_acc += (c2 - c1);
            ns_acc += (t2 - t1);

            /* sanity */
            if (memcmp(dec, msg, PARAM_K) != 0) {
                fprintf(stderr, "Decode mismatch in error path!\n");
                return 1;
            }
        }
        dec_err_cycles_total += cyc_acc / NB_TEST;
        dec_err_ns_total += ns_acc / NB_TEST;
    }

    /* Averages */
    double enc_cycles_avg = (double)enc_cycles_total / NB_SAMPLES;
    double dec_cycles_avg = (double)dec_cycles_total / NB_SAMPLES;
    double dec_err_cycles_avg = (double)dec_err_cycles_total / NB_SAMPLES;

    double enc_ms = (double)enc_ns_total / NB_SAMPLES / 1e6;
    double dec_ms = (double)dec_ns_total / NB_SAMPLES / 1e6;
    double dec_err_ms = (double)dec_err_ns_total / NB_SAMPLES / 1e6;

    printf("\n--- Reed–Solomon Benchmark ---\n");
    printf("Params: N=%d, K=%d, t=%d\n", PARAM_N1, PARAM_K, PARAM_T);
    printf("Encode        : %.0f cycles, %.3f ms\n", enc_cycles_avg, enc_ms);
    printf("Decode (clean): %.0f cycles, %.3f ms\n", dec_cycles_avg, dec_ms);
    printf("Decode (t err): %.0f cycles, %.3f ms\n", dec_err_cycles_avg, dec_err_ms);
    printf("\n");

    return 0;
}
