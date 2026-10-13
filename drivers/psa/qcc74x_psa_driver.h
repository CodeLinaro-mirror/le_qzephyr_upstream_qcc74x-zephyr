/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Crypto Transparent Driver — Master Header
 *
 * Single entry point that includes all sub-module headers.
 * Consumers should include only this header.
 *
 * Sub-modules:
 *   include/qcc74x_psa_init.h     — driver init / deinit
 *   include/qcc74x_psa_cipher.h   — cipher one-shot + streaming
 *   include/qcc74x_psa_entropy.h  — entropy (trng)
 */

#ifndef QCC74X_PSA_DRIVER_H
#define QCC74X_PSA_DRIVER_H

#include "include/qcc74x_psa_init.h"
#include "include/qcc74x_psa_cipher.h"
#include "include/qcc74x_psa_entropy.h"
#include "include/qcc74x_psa_aead.h"
#endif /* QCC74X_PSA_DRIVER_H */