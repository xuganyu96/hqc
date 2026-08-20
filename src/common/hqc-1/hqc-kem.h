/**
 * @file hqc-kem.h
 * @brief NIST KEM API used by the HQC-KEM IND-CCA2 scheme
 */

#ifndef HQC_KEM_H
#define HQC_KEM_H

#include <stdint.h>

#define CRYPTO_ALGNAME "HQC-1"

#define CRYPTO_SECRETKEYBYTES  2321U
#define CRYPTO_PUBLICKEYBYTES  2241U
#define CRYPTO_BYTES           32U
#define CRYPTO_CIPHERTEXTBYTES 4433U

/**
 * @brief Generate a KEM keypair.
 *
 * @param[out] ek_kem Encapsulation key buffer (size CRYPTO_PUBLICKEYBYTES).
 * @param[out] dk_kem Decapsulation key buffer (size CRYPTO_SECRETKEYBYTES).
 *
 * @details The encapsulation key is the PKE public key.
 *          The decapsulation key is serialized as:
 *          @code
 *          dk_kem = [ ek_kem | dk_pke | sigma | seed_kem ]
 *          @endcode
 *          with:
 *          - ek_kem: PKE public key (encapsulation key)
 *          - dk_pke: PKE secret key (decryption key)
 *          - sigma: implicit rejection value derived from `seed_kem` via the HQC XOF
 *          - seed_kem: KEM master seed from which `seed_pke` and `sigma` are derived
 *
 * @return 0 on success, -1 on randombytes failure.
 */
int8_t hqc_kem_keygen(uint8_t *ek_kem, uint8_t *dk_kem);

/**
 * @brief Encapsulate a shared secret using the public key.
 *
 * @param[out] c_kem Ciphertext buffer (size CRYPTO_CIPHERTEXTBYTES).
 * @param[out] K     Shared secret buffer (size CRYPTO_BYTES).
 * @param[in]  ek_kem Encapsulation key buffer (size CRYPTO_PUBLICKEYBYTES).
 *
 * @details The encapsulation key is the PKE public key.
 *
 * @return 0 on success, -1 on randombytes failure.
 */
int8_t hqc_kem_encaps(uint8_t *c_kem, uint8_t *K, const uint8_t *ek_kem);

/**
 * @brief Decapsulate a shared secret using the secret key.
 *
 * @param[out] K_prime Shared secret buffer (size CRYPTO_BYTES).
 * @param[in]  dk_kem  Decapsulation key buffer (size CRYPTO_SECRETKEYBYTES).
 * @param[in]  c_kem   Ciphertext buffer (size CRYPTO_CIPHERTEXTBYTES).
 *
 * @details The decapsulation key is serialized as:
 *          @code
 *          dk_kem = [ ek_kem | dk_pke | sigma | seed_kem ]
 *          @endcode
 *          with:
 *          - ek_kem: PKE public key (encapsulation key)
 *          - dk_pke: PKE secret key (decryption key)
 *          - sigma: implicit rejection value derived from `seed_kem` via the HQC XOF
 *          - seed_kem: KEM master seed from which `seed_pke` and `sigma` are derived
 *
 * @return 0 on success.
 */
int8_t hqc_kem_decaps(uint8_t *K_prime, const uint8_t *dk_kem, const uint8_t *c_kem);

#endif  // HQC_KEM_H
