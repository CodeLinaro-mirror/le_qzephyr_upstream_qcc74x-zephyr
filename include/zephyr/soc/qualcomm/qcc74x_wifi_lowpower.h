/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_WIFI_LOWPOWER_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_WIFI_LOWPOWER_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/net/wifi.h>
#include <zephyr/sys/util.h>

#include "qc7xx_lp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QCC74X_WIFI_LOWPOWER_NOT_RUN INT32_MIN
#define QCC74X_WIFI_LOWPOWER_WAKEUP_AHEAD_US 5000U

enum qcc74x_wifi_lowpower_reason {
	QCC74X_WIFI_LOWPOWER_REASON_NONE = 0,
	QCC74X_WIFI_LOWPOWER_REASON_WIFI_DISABLED,
	QCC74X_WIFI_LOWPOWER_REASON_NOT_CONNECTED,
	QCC74X_WIFI_LOWPOWER_REASON_PS_NOT_COMPILED,
	QCC74X_WIFI_LOWPOWER_REASON_ACTIVE_CHECK_NOT_RUN,
	QCC74X_WIFI_LOWPOWER_REASON_PS_SLEEP_CHECK_FAILED,
	QCC74X_WIFI_LOWPOWER_REASON_LPFW_BLOCKED,
	QCC74X_WIFI_LOWPOWER_REASON_NEXT_WAKE_TOO_CLOSE,
	QCC74X_WIFI_LOWPOWER_REASON_TWT_NEXT_WAKE_INVALID,
	QCC74X_WIFI_LOWPOWER_REASON_READY,
	QCC74X_WIFI_LOWPOWER_REASON_STA_BUSY,
	QCC74X_WIFI_LOWPOWER_REASON_CONFIG_FAILED,
	QCC74X_WIFI_LOWPOWER_REASON_ENTER_OPS_FAILED,
};

struct qcc74x_wifi_lowpower_ps_detail {
	bool cpu_can_sleep;
	bool queue_sent_empty;
	bool queue_saved_empty;
	bool ps_allowed;
	bool tx_allowed;
	bool machw_allowed;
};

struct qcc74x_wifi_lowpower_status {
	bool sampled;
	bool wifi_config_enabled;
	bool wifi_ps_compiled;
	bool active_check_ran;
	bool sta_connected;
	bool beacon_ready;
	bool tim_wakeup_en;
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
	struct qcc74x_wifi_lowpower_ps_detail ps_detail;
	enum qcc74x_wifi_lowpower_reason reason;
};

struct qcc74x_wifi_lowpower_session {
	bool resume_needed;
	bool tim_wakeup_en;
	struct qcc74x_wifi_lowpower_status status;
};

struct qcc74x_wifi_lowpower_ps_config {
	enum wifi_ps_wakeup_mode wakeup_mode;
	uint16_t listen_interval;
};

#if defined(CONFIG_QCC74X_WIFI_LOW_POWER)
int qcc74x_wifi_lowpower_ps_config_get(
	struct qcc74x_wifi_lowpower_ps_config *config);
void qcc74x_wifi_lowpower_sample(bool active_check,
				 struct qcc74x_wifi_lowpower_status *status);
int qcc74x_wifi_lowpower_can_enter(uint64_t timeout_us,
				   struct qcc74x_wifi_lowpower_status *status);
int qcc74x_wifi_lowpower_prepare(qc7xx_lp_fw_cfg_t *cfg,
				 uint64_t *timeout_us,
				 struct qcc74x_wifi_lowpower_session *session);
void qcc74x_wifi_lowpower_resume(
	const struct qcc74x_wifi_lowpower_session *session,
	qc7xx_lp_fw_cfg_t *cfg);
void qcc74x_wifi_lowpower_get_status(
	struct qcc74x_wifi_lowpower_status *status);
#else
static inline int qcc74x_wifi_lowpower_ps_config_get(
	struct qcc74x_wifi_lowpower_ps_config *config)
{
	if (config == NULL) {
		return -EINVAL;
	}

	config->wakeup_mode = WIFI_PS_WAKEUP_MODE_DTIM;
	config->listen_interval = 0U;
	return -ENOTSUP;
}

static inline void qcc74x_wifi_lowpower_status_init(
	struct qcc74x_wifi_lowpower_status *status)
{
	if (status == NULL) {

		return;
	}

	*status = (struct qcc74x_wifi_lowpower_status){
		.sampled = true,
		.wifi_config_enabled = IS_ENABLED(CONFIG_WIFI_QCC74X),
		.wifi_ps_compiled = false,
		.bssid_rc = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.channel_rc = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.sta_raw_state = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.aid = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.channel = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.twt_flow_count = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.ps_sleep_check_rc = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.lpfw_check_allow = QCC74X_WIFI_LOWPOWER_NOT_RUN,
		.reason = IS_ENABLED(CONFIG_WIFI_QCC74X) ?
			  QCC74X_WIFI_LOWPOWER_REASON_PS_NOT_COMPILED :
			  QCC74X_WIFI_LOWPOWER_REASON_WIFI_DISABLED,
	};
}

static inline void qcc74x_wifi_lowpower_sample(bool active_check,
					       struct qcc74x_wifi_lowpower_status *status)
{
	ARG_UNUSED(active_check);
	qcc74x_wifi_lowpower_status_init(status);
}

static inline int qcc74x_wifi_lowpower_can_enter(uint64_t timeout_us,
						 struct qcc74x_wifi_lowpower_status *status)
{
	ARG_UNUSED(timeout_us);
	qcc74x_wifi_lowpower_status_init(status);
	return 0;
}

static inline int qcc74x_wifi_lowpower_prepare(qc7xx_lp_fw_cfg_t *cfg,
					       uint64_t *timeout_us,
					       struct qcc74x_wifi_lowpower_session *session)
{
	ARG_UNUSED(cfg);
	ARG_UNUSED(timeout_us);
	if (session != NULL) {
		*session = (struct qcc74x_wifi_lowpower_session){ 0 };
		qcc74x_wifi_lowpower_status_init(&session->status);
	}

	return 0;
}

static inline void qcc74x_wifi_lowpower_resume(
	const struct qcc74x_wifi_lowpower_session *session,
	qc7xx_lp_fw_cfg_t *cfg)
{
	ARG_UNUSED(session);
	ARG_UNUSED(cfg);
}

static inline void qcc74x_wifi_lowpower_get_status(
	struct qcc74x_wifi_lowpower_status *status)
{
	qcc74x_wifi_lowpower_status_init(status);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_WIFI_LOWPOWER_H_ */
