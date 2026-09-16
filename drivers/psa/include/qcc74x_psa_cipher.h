/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Cipher one-shot + streaming
 */

#ifndef QCC74X_PSA_CIPHER_H
#define QCC74X_PSA_CIPHER_H

#include <psa/crypto.h>
#include "qcc74x_psa_driver_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot encrypt */
psa_status_t qcc74x_transparent_cipher_encrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *iv, size_t iv_length,
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_size, size_t *output_length);

/* Streaming encrypt setup */
psa_status_t qcc74x_transparent_cipher_encrypt_setup(
    qcc74x_transparent_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg);

/* Streaming decrypt setup */
psa_status_t qcc74x_transparent_cipher_decrypt_setup(
    qcc74x_transparent_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg);

/* Set IV */
psa_status_t qcc74x_transparent_cipher_set_iv(
    qcc74x_transparent_cipher_operation_t *operation,
    const uint8_t *iv, size_t iv_length);

/* Update — process data */
psa_status_t qcc74x_transparent_cipher_update(
    qcc74x_transparent_cipher_operation_t *operation,
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_size, size_t *output_length);

/* Finish — finalize and zeroize key */
psa_status_t qcc74x_transparent_cipher_finish(
    qcc74x_transparent_cipher_operation_t *operation,
    uint8_t *output, size_t output_size, size_t *output_length);

/* Abort — zeroize context */
psa_status_t qcc74x_transparent_cipher_abort(
    qcc74x_transparent_cipher_operation_t *operation);

#ifdef __cplusplus
}
#endif

#endif /* QCC74X_PSA_CIPHER_H */