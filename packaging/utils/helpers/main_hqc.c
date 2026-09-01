#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "crypto_memset.h"
#include "hqc-kem.h"
#include "parameters.h"
#include "rng.h"

static void init_randomness(void) {
#ifdef VERBOSE
    unsigned char entropy_input[48] = {0};
    for (int i = 0; i < 48; i++) entropy_input[i] = (unsigned char)i;
    randombytes_init(entropy_input, NULL, 256);
#else
    unsigned char seed[48] = {0};
    size_t filled = 0;
    while (filled < sizeof seed) {
        ssize_t got = syscall(SYS_getrandom, seed + filled, sizeof seed - filled, 0);
        if (got < 0) {
            perror("getrandom");
            exit(EXIT_FAILURE);
        }
        filled += (size_t)got;
    }
    randombytes_init(seed, NULL, 256);
#endif
}

int main(void) {
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
    uint8_t key1[SHARED_SECRET_BYTES_KEM] = {0};
    uint8_t key2[SHARED_SECRET_BYTES_KEM] = {0};

    hqc_kem_keygen(pk, sk);
    hqc_kem_encaps(key1, ct, pk);
    hqc_kem_decaps(key2, sk, ct);

    printf("\n\nsecret1: ");
    for (size_t i = 0; i < SHARED_SECRET_BYTES_KEM; ++i) printf("%02x", key1[i]);

    printf("\nsecret2: ");
    for (size_t i = 0; i < SHARED_SECRET_BYTES_KEM; ++i) printf("%02x", key2[i]);
    printf("\n\n");

    // Zeroize sensitive data
    memset_zero(sk, sizeof sk);
    memset_zero(key1, sizeof key1);
    memset_zero(key2, sizeof key2);

    return 0;
}
