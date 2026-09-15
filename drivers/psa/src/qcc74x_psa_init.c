/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Init / Deinit implementation
 */

#include "../include/qcc74x_psa_init.h"
#include <zephyr/kernel.h>

/* Module-level mutexes — one per hardware engine; defined here */
K_MUTEX_DEFINE(qcc74x_psa_sha_mutex);
K_MUTEX_DEFINE(qcc74x_psa_aes_mutex);

psa_status_t qcc74x_psa_driver_init(void)
{
    /* Mutexes are statically initialized via K_MUTEX_DEFINE.
     * Hardware device references are acquired lazily on first use.
     * No additional initialization is needed. */
    return PSA_SUCCESS;
}

void qcc74x_psa_driver_deinit(void)
{
    /* Statically defined mutexes have no runtime deinit needed.
     * Reserved for future cleanup. */
}