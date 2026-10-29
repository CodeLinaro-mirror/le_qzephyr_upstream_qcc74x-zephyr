/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SERIAL_UART_QC7XX_H_
#define ZEPHYR_INCLUDE_DRIVERS_SERIAL_UART_QC7XX_H_

#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/soc/qualcomm/qcc74x_pm_lpfw.h>
#ifdef CONFIG_QCC74X_PM_LPFW
/* Read-only diagnostics. Device lifecycle belongs to System Device PM. */
struct uart_qc7xx_pm_stats {
	uint32_t suspends;
	uint32_t resumes;
	uint32_t rejected;
	uint32_t retries;
	uint32_t failures;
	int last_rc;
	bool suspended;
};

void uart_qc7xx_pm_stats_get(const struct device *dev,
			   struct uart_qc7xx_pm_stats *stats);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SERIAL_UART_QC7XX_H_ */
