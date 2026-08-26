/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_PDS15_DIAG_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_PDS15_DIAG_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum qcc74x_pm_pds15_wifi_diag_reason {
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_NONE = 0,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_WIFI_DISABLED = 1,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_NOT_CONNECTED = 2,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_NOT_COMPILED = 3,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_ACTIVE_CHECK_NOT_RUN = 4,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_SLEEP_CHECK_FAILED = 5,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_DTIM_WINDOW_BLOCKED = 6,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_TOO_CLOSE = 7,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_NEXT_WAKE_INVALID = 8,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_READY = 9,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_STA_BUSY = 10,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_CONFIG_FAILED = 11,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_ENTER_OPS_FAILED = 12,
	QCC74X_PM_PDS15_WIFI_DIAG_REASON_COUNT,
};

#define QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN INT32_MIN
#define QCC74X_PM_PDS15_WIFI_DIAG_WAKEUP_AHEAD_US 5000U

struct qcc74x_pm_pds15_wifi_diag_status {
	bool sampled;
	bool wifi_config_enabled;
	bool wifi_ps_compiled;
	bool active_check_ran;
	bool sta_connected;
	bool ps_cpu_can_sleep;
	bool ps_queue_sent_empty;
	bool ps_queue_saved_empty;
	bool ps_allowed;
	bool tx_allowed;
	bool machw_allowed;
	uint32_t sample_count;
	uint32_t active_check_count;
	uint32_t connected_count;
	uint32_t reason_counts[QCC74X_PM_PDS15_WIFI_DIAG_REASON_COUNT];
	enum qcc74x_pm_pds15_wifi_diag_reason last_reason;
	int32_t bssid_rc;
	int32_t channel_rc;
	int32_t sta_raw_state;
	int32_t aid;
	int32_t channel;
	int32_t twt_flow_count;
	int32_t ps_sleep_check_rc;
	int32_t lpfw_check_allow;
	uint32_t next_wakeup_timer_us;
	bool next_wakeup_timer_valid;
	uint8_t bssid[6];
};

struct qcc74x_pm_pds15_ble_diag_status {
	bool sampled;                   /* prepare has been called at least once */
	bool bt_ready;                  /* bt_is_ready() at last sample */
	uint32_t sample_count;          /* total ble_prepare calls */
	uint32_t allowed_count;         /* sleep allowed (BLE idle or no cap) */
	uint32_t capped_count;          /* sleep capped by BLE wake time */
	uint32_t blocked_count;         /* BLE busy, EBUSY returned */
	uint32_t crash_recovery_count;  /* GLB BTDM reset triggered */
	int32_t  last_sleep_rtc;        /* btble_controller_sleep return value */
	uint64_t last_capped_us;        /* most recent BLE-imposed timeout cap */
	uint64_t last_crash_elapsed_us; /* elapsed at last crash trigger */
	uint64_t last_crash_expected_us;
};

enum qcc74x_pm_pds15_auto_diag_reason {
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_NONE = 0,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_AUTO_DISABLED = 1,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_NOT_INITIALIZED = 2,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_STATE_LOCKED = 3,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_TIMEOUT_TOO_SHORT = 4,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_ENTER_BUSY = 5,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_RESUME_PENDING = 6,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_WIFI_NOT_READY = 7,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_READY = 8,
	QCC74X_PM_PDS15_AUTO_DIAG_REASON_COUNT,
};

struct qcc74x_pm_pds15_auto_diag_status {
	bool sampled;
	bool auto_config_enabled;
	bool auto_enabled;
	bool policy_lock_active;
	bool state_available;
	bool initialized;
	bool enter_busy;
	bool resume_pending;
	bool auto_deadline_active;
	bool auto_deadline_expired;
	int32_t ticks;
	uint32_t min_residency_ticks;
	uint64_t timeout_us;
	uint64_t auto_remaining_us;
	uint32_t sample_count;
	uint32_t ready_count;
	uint32_t reason_counts[QCC74X_PM_PDS15_AUTO_DIAG_REASON_COUNT];
	enum qcc74x_pm_pds15_auto_diag_reason last_reason;
	enum qcc74x_pm_pds15_wifi_diag_reason wifi_reason;
};

#ifdef CONFIG_QCC74X_PM_PDS15
void qcc74x_pm_pds15_wifi_diag_sample(bool active_check);
void qcc74x_pm_pds15_wifi_diag_get(
	struct qcc74x_pm_pds15_wifi_diag_status *status);
void qcc74x_pm_pds15_wifi_diag_reset(void);
void qcc74x_pm_pds15_auto_diag_sample(void);
void qcc74x_pm_pds15_auto_diag_get(
	struct qcc74x_pm_pds15_auto_diag_status *status);
void qcc74x_pm_pds15_auto_diag_reset(void);
void qcc74x_pm_pds15_ble_diag_get(
	struct qcc74x_pm_pds15_ble_diag_status *status);
void qcc74x_pm_pds15_ble_diag_reset(void);
void qcc74x_pm_pds15_ble_inject_crash(void);
#else
static inline void qcc74x_pm_pds15_wifi_diag_sample(bool active_check)
{
	ARG_UNUSED(active_check);
}

static inline void qcc74x_pm_pds15_wifi_diag_get(
	struct qcc74x_pm_pds15_wifi_diag_status *status)
{
	if (status != NULL) {
		*status =
			(struct qcc74x_pm_pds15_wifi_diag_status){ 0 };
	}
}

static inline void qcc74x_pm_pds15_wifi_diag_reset(void)
{
}

static inline void qcc74x_pm_pds15_auto_diag_sample(void)
{
}

static inline void qcc74x_pm_pds15_auto_diag_get(
	struct qcc74x_pm_pds15_auto_diag_status *status)
{
	if (status != NULL) {
		*status =
			(struct qcc74x_pm_pds15_auto_diag_status){ 0 };
	}
}

static inline void qcc74x_pm_pds15_auto_diag_reset(void)
{
}

static inline void qcc74x_pm_pds15_ble_diag_get(
	struct qcc74x_pm_pds15_ble_diag_status *status)
{
	if (status != NULL) {
		*status = (struct qcc74x_pm_pds15_ble_diag_status){ 0 };
	}
}

static inline void qcc74x_pm_pds15_ble_diag_reset(void)
{
}

static inline void qcc74x_pm_pds15_ble_inject_crash(void)
{
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_PDS15_DIAG_H_ */
