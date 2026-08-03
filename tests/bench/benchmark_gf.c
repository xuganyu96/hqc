#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "gf.h"

#define NB_WARMUP         100
#define NB_SAMPLES        1000
#define GF_CARDINALITY    256
#define GF_NONZERO_COUNT  (GF_CARDINALITY - 1)
#define GF_MUL_BATCH_SIZE ((uint64_t)GF_CARDINALITY * GF_CARDINALITY)
#define GF_INV_BATCH_SIZE ((uint64_t)GF_NONZERO_COUNT)

inline static uint64_t cpucyclesStart(void) {
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

inline static uint64_t cpucyclesStop(void) {
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
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline uint16_t rotl16(uint16_t x) {
    return (uint16_t)((x << 1) | (x >> 15));
}

static __attribute__((noinline)) uint16_t benchmark_mul_batch(uint8_t offset) {
    uint16_t acc = (uint16_t)(offset | 1U);

    for (uint16_t a = 0; a < GF_CARDINALITY; ++a) {
        uint16_t aa = (uint8_t)(a + offset);

        for (uint16_t b = 0; b < GF_CARDINALITY; ++b) {
            acc = rotl16(acc) ^ gf_mul(aa, (uint8_t)(b + offset));
        }
        acc ^= aa;
    }

    return acc;
}

static __attribute__((noinline)) uint16_t benchmark_inverse_batch(uint8_t offset) {
    uint16_t acc = (uint16_t)(offset | 1U);

    for (uint16_t a = 1; a < GF_CARDINALITY; ++a) {
        uint16_t aa = a + offset;

        if (aa >= GF_CARDINALITY) {
            aa -= GF_NONZERO_COUNT;
        }

        acc = rotl16(acc) ^ gf_inverse(aa);
    }

    return acc;
}

int main(void) {
    uint64_t t1, t2, ns1, ns2;
    uint64_t mul_cycles_total = 0, inv_cycles_total = 0;
    uint64_t mul_ns_total = 0, inv_ns_total = 0;
    uint16_t seed = (uint16_t)get_time_ns();
    uint16_t mul_checksum = 0;
    uint16_t inv_checksum = 0;

    if (seed == 0) {
        seed = 1;
    }

    // warm-up
    for (size_t i = 0; i < NB_WARMUP; ++i) {
        (void)benchmark_mul_batch((uint8_t)(seed + i));
    }
    for (size_t i = 0; i < NB_SAMPLES; ++i) {
        uint8_t offset = (uint8_t)(seed + i);

        t1 = cpucyclesStart();
        ns1 = get_time_ns();
        uint16_t mul_sample = benchmark_mul_batch(offset);
        ns2 = get_time_ns();
        t2 = cpucyclesStop();

        mul_checksum ^= mul_sample;
        mul_cycles_total += (t2 - t1);
        mul_ns_total += (ns2 - ns1);
    }

    // warm-up
    for (size_t i = 0; i < NB_WARMUP; ++i) {
        (void)benchmark_inverse_batch((uint8_t)((seed + i) % GF_NONZERO_COUNT));
    }
    for (size_t i = 0; i < NB_SAMPLES; ++i) {
        uint8_t offset = (uint8_t)((seed + i) % GF_NONZERO_COUNT);

        t1 = cpucyclesStart();
        ns1 = get_time_ns();
        uint16_t inv_sample = benchmark_inverse_batch(offset);
        ns2 = get_time_ns();
        t2 = cpucyclesStop();

        inv_checksum ^= inv_sample;
        inv_cycles_total += (t2 - t1);
        inv_ns_total += (ns2 - ns1);
    }

    if ((mul_checksum == 0xFFFFU) && (inv_checksum == 0xFFFFU)) {
        fputs("unexpected GF benchmark checksum\n", stderr);
        return 1;
    }

    double mul_cycles_avg = (double)mul_cycles_total / (NB_SAMPLES * GF_MUL_BATCH_SIZE);
    double inv_cycles_avg = (double)inv_cycles_total / (NB_SAMPLES * GF_INV_BATCH_SIZE);
    double mul_ns_avg = (double)mul_ns_total / (NB_SAMPLES * GF_MUL_BATCH_SIZE);
    double inv_ns_avg = (double)inv_ns_total / (NB_SAMPLES * GF_INV_BATCH_SIZE);

    printf("\n--- HQC GF Benchmark ---\n");
    printf("GF mul     : %.2f cycles, %.2f ns\n", mul_cycles_avg, mul_ns_avg);
    printf("GF inverse : %.2f cycles, %.2f ns\n", inv_cycles_avg, inv_ns_avg);
    printf("\n");

    return 0;
}
