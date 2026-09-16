/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Entropy / TRNG implementation
 */

#include "../include/qcc74x_psa_entropy.h"
#include <zephyr/kernel.h>
#include <string.h>

#include "mbedtls/platform_util.h"
#include "qc7xx_core.h"
#include "qc7xx_sec_trng.h"

/* TRNG hardware always produces 32-byte blocks */
#define TRNG_BLOCK_SIZE 32

psa_status_t qcc74x_transparent_get_entropy(
    uint8_t *output,
    size_t   output_size,
    size_t  *output_length)
{
    struct qc7xx_device_s *dev;
    uint8_t tmp[TRNG_BLOCK_SIZE];
    size_t offset = 0;
    int ret;

    if (output == NULL || output_length == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    *output_length = 0;

    if (output_size == 0) {
        return PSA_SUCCESS;
    }

    dev = qc7xx_device_get_by_name(QC7XX_NAME_SEC_TRNG);
    if (!dev) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    while (offset < output_size) {
        (void)memset(tmp, 0, sizeof(tmp));
        ret = qc7xx_trng_read(dev, tmp);
        if (ret != 0) {
            mbedtls_platform_zeroize(tmp, sizeof(tmp));
            *output_length = offset;
            return PSA_ERROR_HARDWARE_FAILURE;
        }
        size_t copy = (output_size - offset < TRNG_BLOCK_SIZE)
                      ? (output_size - offset) : TRNG_BLOCK_SIZE;
        memcpy(output + offset, tmp, copy);
        offset += copy;
    }

    /* Wipe the TRNG output buffer after use */
    mbedtls_platform_zeroize(tmp, sizeof(tmp));

    *output_length = output_size;
    return PSA_SUCCESS;
}