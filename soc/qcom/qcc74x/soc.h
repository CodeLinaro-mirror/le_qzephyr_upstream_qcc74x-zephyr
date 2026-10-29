/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* This file is needed since some Zephyr subsystems include <soc.h>. */

#ifndef ZEPHYR_SOC_QCOM_QCC74X_SOC_H_
#define ZEPHYR_SOC_QCOM_QCC74X_SOC_H_

#include <stdint.h>

struct boot_rsp;

int qcc74x_mcuboot_prepare_boot(const struct boot_rsp *rsp, uintptr_t *start);
void qcc74x_mcuboot_boot_image(uintptr_t start);

#ifdef CONFIG_QCC74X_PM_PDS1
/* Read-only boot check; -EAGAIN means the controller is not initialized yet. */
int qcc74x_psram_check_config(void);
#endif

#endif /* ZEPHYR_SOC_QCOM_QCC74X_SOC_H_ */
