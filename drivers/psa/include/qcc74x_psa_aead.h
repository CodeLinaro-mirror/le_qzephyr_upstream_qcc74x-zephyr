/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — AEAD one-shot (AES-GCM with HW AES-ECB + SW GHASH)
 *
 * Implements PSA AEAD encrypt/decrypt one-shot entry points using the
 * existing QCC74x transparent cipher driver (HW AES-ECB) for the AES-CTR
 * keystream generation and a software GHASH (GF(2^128)) for the authentication
 * tag. This provides HW-accelerated AES-GCM without requiring native GCM
 * hardware support from the SEC Engine.
 */

#ifndef QCC74X_PSA_AEAD_H
#define QCC74X_PSA_AEAD_H

#include <psa/crypto.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief QCC74x HW accelerated AEAD one-shot encrypt (AES-GCM only).
 *
 * Performs AES-GCM encryption using HW AES-ECB for the AES-CTR keystream
 * and a software GF(2^128) GHASH for authentication.
 *
 * Supported algorithms: PSA_ALG_GCM only.
 * Other algorithms return PSA_ERROR_NOT_SUPPORTED (fallback to software).
 *
 * @param[in]  attributes           Key attributes
 * @param[in]  key_buffer           Key material buffer
 * @param[in]  key_buffer_size      Key material size in bytes
 * @param[in]  alg                  AEAD algorithm (PSA_ALG_GCM)
 * @param[in]  nonce                Nonce / IV
 * @param[in]  nonce_length         Nonce length (12 bytes recommended for GCM)
 * @param[in]  additional_data      Additional authenticated data (AAD)
 * @param[in]  additional_data_length Length of AAD
 * @param[in]  plaintext            Plaintext input
 * @param[in]  plaintext_length     Length of plaintext
 * @param[out] ciphertext           Output ciphertext buffer
 * @param[in]  ciphertext_size      Size of ciphertext buffer
 * @param[out] ciphertext_length    Actual ciphertext length on success
 *
 * @retval PSA_SUCCESS              Encryption successful
 * @retval PSA_ERROR_NOT_SUPPORTED  Algorithm not supported (fallback to SW)
 * @retval PSA_ERROR_INVALID_ARGUMENT Invalid parameters
 * @retval PSA_ERROR_HARDWARE_FAILURE HW AES engine error
 */
psa_status_t qcc74x_aead_encrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *ciphertext, size_t ciphertext_size,
    size_t *ciphertext_length);

/**
 * @brief QCC74x HW accelerated AEAD one-shot decrypt (AES-GCM only).
 *
 * Performs AES-GCM decryption and authentication. The authentication tag
 * is expected as the last @p tag_length bytes of the ciphertext input.
 *
 * @param[in]  attributes           Key attributes
 * @param[in]  key_buffer           Key material buffer (or HW_KEY_MARKER)
 * @param[in]  key_buffer_size      Key material size in bytes
 * @param[in]  alg                  AEAD algorithm (PSA_ALG_GCM)
 * @param[in]  nonce                Nonce / IV
 * @param[in]  nonce_length         Nonce length
 * @param[in]  additional_data      Additional authenticated data
 * @param[in]  additional_data_length Length of AAD
 * @param[in]  ciphertext           Ciphertext + tag input
 * @param[in]  ciphertext_length    Length of ciphertext + tag
 * @param[out] plaintext            Output plaintext buffer
 * @param[in]  plaintext_size       Size of plaintext buffer
 * @param[out] plaintext_length     Actual plaintext length on success
 *
 * @retval PSA_SUCCESS              Decryption and authentication successful
 * @retval PSA_ERROR_NOT_SUPPORTED  Algorithm not supported (fallback to SW)
 * @retval PSA_ERROR_INVALID_ARGUMENT Invalid parameters or authentication failed
 * @retval PSA_ERROR_HARDWARE_FAILURE HW AES engine error
 */
psa_status_t qcc74x_aead_decrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *ciphertext, size_t ciphertext_length,
    uint8_t *plaintext, size_t plaintext_size,
    size_t *plaintext_length);

#ifdef __cplusplus
}
#endif

#endif /* QCC74X_PSA_AEAD_H */