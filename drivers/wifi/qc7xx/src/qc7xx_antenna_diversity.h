/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_DRIVERS_WIFI_QC7XX_ANTENNA_DIVERSITY_H_
#define ZEPHYR_DRIVERS_WIFI_QC7XX_ANTENNA_DIVERSITY_H_

int qc7xx_antenna_diversity_init(void);

#if defined(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_SHELL)
#include <zephyr/shell/shell.h>

extern const union shell_cmd_entry qc7xx_antenna_shell_subcmds;
#endif

#endif /* ZEPHYR_DRIVERS_WIFI_QC7XX_ANTENNA_DIVERSITY_H_ */
