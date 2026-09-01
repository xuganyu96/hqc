#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hqc-kem.h"
#include "parameters.h"
#include "rng.h"

int test_kem_api(void);

static void init_randomness(void) {
    unsigned char entropy_input[48] = {0};
    for (int i = 0; i < 48; i++) entropy_input[i] = i;
    randombytes_init(entropy_input, NULL, 256);
}

int test_kem_api(void) {
    init_randomness();

#ifdef VERBOSE
    printf("\n*********\n");
    printf("  %s\n", CRYPTO_ALGNAME);
    printf("*********\n");
    printf("\n");
    printf("N: %d   ", PARAM_N);
    printf("N1: %d   ", PARAM_N1);
    printf("N2: %d   ", PARAM_N2);
    printf("OMEGA: %d   ", PARAM_OMEGA);
    printf("OMEGA_R: %d   ", PARAM_OMEGA_R);
    printf("Failure rate: 2^-%d   ", PARAM_DFR_EXP);
    printf("Sec: %d bits", PARAM_SECURITY);
    printf("\n");
#endif

    uint8_t pk[PUBLIC_KEY_BYTES_KEM] = {0};
    uint8_t sk[SECRET_KEY_BYTES_KEM] = {0};
    uint8_t ct[CIPHERTEXT_BYTES_KEM] = {0};
    uint8_t ss1[SHARED_SECRET_BYTES_KEM] = {0};
    uint8_t ss2[SHARED_SECRET_BYTES_KEM] = {0};

    hqc_kem_keygen(pk, sk);
    hqc_kem_encaps(ss1, ct, pk);
    hqc_kem_decaps(ss2, sk, ct);

    if (memcmp(ss1, ss2, SHARED_SECRET_BYTES_KEM)) {
        printf("\n");
        for (size_t i = 0; i < SHARED_SECRET_BYTES_KEM; i++) {
            printf("%02x", ss1[i]);
        }
        printf("\n");
        for (size_t i = 0; i < SHARED_SECRET_BYTES_KEM; i++) {
            printf("%02x", ss2[i]);
        }
        printf("ERROR KEM\n");
        exit(1);
    }

#ifdef VERBOSE
    printf("\n\nss_encaps: ");
    for (size_t i = 0; i < SHARED_SECRET_BYTES_KEM; ++i) printf("%02x", ss1[i]);

    printf("\nss_decaps: ");
    for (size_t i = 0; i < SHARED_SECRET_BYTES_KEM; ++i) printf("%02x", ss2[i]);
    printf("\n\n");
#endif

    return 0;
}

int main(void) {
    if (test_kem_api()) {
        printf("test_kem_api failed\n");
        return 1;
    }
    return 0;
}
