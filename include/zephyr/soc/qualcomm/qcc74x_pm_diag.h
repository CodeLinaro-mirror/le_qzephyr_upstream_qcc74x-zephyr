/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_DIAG_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_DIAG_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum qcc74x_pm_deep_skip_reason {
	QCC74X_PM_DEEP_SKIP_REASON_NONE = 0,
	QCC74X_PM_DEEP_SKIP_REASON_NOT_ARMED = 1,
	QCC74X_PM_DEEP_SKIP_REASON_ENTER_BUSY = 2,
	QCC74X_PM_DEEP_SKIP_REASON_DEADLINE_REACHED = 3,
	QCC74X_PM_DEEP_SKIP_REASON_NEXT_TIMEOUT_IMMEDIATE = 4,
	QCC74X_PM_DEEP_SKIP_REASON_NO_WAKE_SOURCE = 5,
	QCC74X_PM_DEEP_SKIP_REASON_WIFI_BLOCKED = 6,
	QCC74X_PM_DEEP_SKIP_REASON_BLE_BLOCKED = 7,
	QCC74X_PM_DEEP_SKIP_REASON_COUNT,
};

struct qcc74x_pm_diag_stats {
	uint32_t state_set_calls;
	uint32_t exit_post_ops_calls;
	uint32_t shallow_idle_calls;
	uint32_t deep_sleep_calls;
	uint32_t unsupported_state_calls;
	uint32_t deep_prepare_calls;
	uint32_t deep_skip_calls;
	uint32_t deep_skip_reason_counts[QCC74X_PM_DEEP_SKIP_REASON_COUNT];
	uint32_t deep_resume_success;
	uint32_t deep_resume_failures;
	uint32_t last_state;
	uint32_t last_substate;
	uint32_t last_deep_skip_reason;
	int32_t last_deep_return_code;
	int32_t last_uart_resume_rc;
	uint32_t last_wake_reason;
	uint64_t last_entry_cycle;
	uint64_t last_exit_cycle;
	uint64_t last_deep_requested_us;
	uint64_t last_deep_elapsed_us;
};

#ifdef CONFIG_QCC74X_PM_DIAGNOSTICS
void qcc74x_pm_diag_get(struct qcc74x_pm_diag_stats *stats);
void qcc74x_pm_diag_reset(void);
void qcc74x_pm_diag_record_deep_prepare(uint64_t timeout_us);
void qcc74x_pm_diag_record_deep_skip(
	enum qcc74x_pm_deep_skip_reason reason);
void qcc74x_pm_diag_record_deep_result(int rc, uint64_t elapsed_us,
					 uint32_t wake_reason,
					 int uart_resume_rc);
#else
static inline void qcc74x_pm_diag_get(struct qcc74x_pm_diag_stats *stats)
{
	if (stats != NULL) {
		*stats = (struct qcc74x_pm_diag_stats){ 0 };
	}
}

static inline void qcc74x_pm_diag_reset(void)
{
}

static inline void qcc74x_pm_diag_record_deep_prepare(uint64_t timeout_us)
{
	ARG_UNUSED(timeout_us);
}

static inline void qcc74x_pm_diag_record_deep_skip(
	enum qcc74x_pm_deep_skip_reason reason)
{
	ARG_UNUSED(reason);
}

static inline void qcc74x_pm_diag_record_deep_result(int rc, uint64_t elapsed_us,
						      uint32_t wake_reason,
						      int uart_resume_rc)
{
	ARG_UNUSED(rc);
	ARG_UNUSED(elapsed_us);
	ARG_UNUSED(wake_reason);
	ARG_UNUSED(uart_resume_rc);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_DIAG_H_ */
