/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Entropy / TRNG
 */

#ifndef QCC74X_PSA_ENTROPY_H
#define QCC74X_PSA_ENTROPY_H

#include <psa/crypto.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get entropy from the QCC74x hardware TRNG.
 *
 * Fills @p output with @p output_size bytes of true random data.
 *
 * @param[out] output     Buffer to fill with random bytes.
 * @param      output_size Number of bytes requested.
 * @param[out] output_length Actual number of bytes written.
 * @return PSA_SUCCESS, PSA_ERROR_NOT_SUPPORTED, or PSA_ERROR_HARDWARE_FAILURE.
 */
psa_status_t qcc74x_transparent_get_entropy(
    uint8_t *output,
    size_t   output_size,
    size_t  *output_length);

#ifdef __cplusplus
}
#endif

#endif /* QCC74X_PSA_ENTROPY_H */