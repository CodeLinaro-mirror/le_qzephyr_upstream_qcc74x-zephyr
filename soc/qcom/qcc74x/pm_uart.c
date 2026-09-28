/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_qc7xx.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15_diag.h>
#include "pm_uart.h"

#define UART_DEVICE(node) DEVICE_DT_GET(node),
static const struct device *const uart_devices[] = {
	DT_FOREACH_STATUS_OKAY(qcom_qcc74x_uart, UART_DEVICE)
};

static int qcc74x_pm_uart_check(bool require_suspended)
{
	for (size_t n = 0; n < ARRAY_SIZE(uart_devices); n++) {
		const struct device *dev = uart_devices[n];
		enum pm_device_state state;

		if (!device_is_ready(dev) || pm_device_runtime_is_enabled(dev) ||
		    pm_device_is_busy(dev) || pm_device_wakeup_is_enabled(dev) ||
		    pm_device_state_get(dev, &state) != 0 ||
		    (state != PM_DEVICE_STATE_SUSPENDED &&
		     (require_suspended || state != PM_DEVICE_STATE_ACTIVE))) {
			return -EBUSY;
		}
	}
	return 0;
}

int qcc74x_pm_uart_preflight(void)
{
	return qcc74x_pm_uart_check(false);
}

/* The system manager may have skipped a UART since preflight. */
int qcc74x_pm_uart_check_suspended(void)
{
	return qcc74x_pm_uart_check(true);
}

void qcc74x_pm_pds15_uart_diag_get(struct qcc74x_pm_uart_diag *status)
{
	unsigned int key = irq_lock();

	*status = (struct qcc74x_pm_uart_diag){0};
	for (size_t n = 0; n < ARRAY_SIZE(uart_devices); n++) {
		struct uart_qc7xx_pm_stats stats;

		uart_qc7xx_pm_stats_get(uart_devices[n], &stats);
		status->suspends += stats.suspends;
		status->resumes += stats.resumes;
		status->rejected += stats.rejected;
		status->retries += stats.retries;
		status->failures += stats.failures;
		status->active |= stats.suspended;
		if (stats.last_rc != 0) {
			status->last_rc = stats.last_rc;
			status->last_device = uart_devices[n]->name;
		}
	}
	irq_unlock(key);
}

FUNC_NORETURN void qcc74x_pm_uart_fail(int rc)
{
	ARG_UNUSED(rc);
	(void)irq_lock();
	/* Foundation recovery failed: do not access UART registers or unmask IRQs. */
	for (;;) {
		arch_nop();
	}
}
