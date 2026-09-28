/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_SOC_QUALCOMM_QCC74X_PM_LPFW_H_
#define ZEPHYR_SOC_QUALCOMM_QCC74X_PM_LPFW_H_

#include <stdbool.h>

/* Internal API names retain PDS15 for compatibility; the PM state does not. */
#if defined(CONFIG_QCC74X_PM_PDS1)
#define QCC74X_PM_LPFW_SUBSTATE_ID 1U
bool qcc74x_psram_retention_ready(void);
#else
#define QCC74X_PM_LPFW_SUBSTATE_ID 15U
#endif

#endif
