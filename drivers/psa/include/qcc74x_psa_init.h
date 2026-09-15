/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Init / Deinit
 */

#ifndef QCC74X_PSA_INIT_H
#define QCC74X_PSA_INIT_H

#include <psa/crypto.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the QCC74x PSA transparent driver.
 *
 * Must be called once before any other driver entry point.
 * Registers internal mutexes and acquires references to the
 * QCC74x SEC engine hardware instances (AES, SHA, TRNG).
 *
 * @return PSA_SUCCESS on success, or a PSA error code.
 */
psa_status_t qcc74x_psa_driver_init(void);

/**
 * @brief Deinitialize the QCC74x PSA transparent driver.
 *
 * Releases hardware references and destroys internal mutexes.
 * After this call, no driver entry point may be invoked until
 * qcc74x_psa_driver_init() is called again.
 */
void qcc74x_psa_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* QCC74X_PSA_INIT_H */