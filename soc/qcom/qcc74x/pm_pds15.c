/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <string.h>

#include <zephyr/soc/qualcomm/qcc74x_pm_pds15_timer.h>
#include <zephyr/soc/qualcomm/qcc74x_gpio_pds15.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/spinlock.h>
#include <zephyr/logging/log.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_diag.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15_diag.h>
#include <zephyr/soc/qualcomm/qcc74x_ble_lowpower.h>
#include <zephyr/soc/qualcomm/qcc74x_wifi_lowpower.h>
#include <zephyr/sys/util.h>

#include "qcc74x_clock.h"
#include "qcc74x_common.h"
#include "qcc74x_hbn.h"
#include "qcc74x_pm.h"
#include "qc7xx_lp.h"
#include "pm_uart.h"
#include "pm_policy_qcc74x.h"

#ifndef CONFIG_QCC74X_PM_PDS15_LOG_LEVEL
#define CONFIG_QCC74X_PM_PDS15_LOG_LEVEL LOG_LEVEL_NONE
#endif

LOG_MODULE_REGISTER(qcc74x_pm_pds15, CONFIG_QCC74X_PM_PDS15_LOG_LEVEL);

#define QCC74X_PM_PDS15_LOG_DBG(...)                                      \
	do {                                                                  \
		if (IS_ENABLED(CONFIG_QCC74X_PM_PDS15_DEBUG_LOG)) {           \
			LOG_DBG(__VA_ARGS__);                                \
		}                                                             \
	} while (0)
#define QCC74X_PM_PDS15_LOG_INF(...)                                      \
	do {                                                                  \
		if (IS_ENABLED(CONFIG_QCC74X_PM_PDS15_DEBUG_LOG)) {           \
			LOG_INF(__VA_ARGS__);                                \
		}                                                             \
	} while (0)
#define QCC74X_PM_PDS15_LOG_ERR(...)                                      \
	do {                                                                  \
		if (IS_ENABLED(CONFIG_QCC74X_PM_PDS15_DEBUG_LOG)) {           \
			LOG_ERR(__VA_ARGS__);                                \
		}                                                             \
	} while (0)

struct qcc74x_pm_pds15_context {
	struct k_spinlock lock;
	bool initialized;
	bool init_in_progress;
	bool auto_enabled;
	bool auto_lock_released;
	bool auto_entry_pending;
	bool auto_deadline_active;
	bool enter_in_progress;
	bool resume_cleanup_pending;
	bool pds15_enter_committed;
	int init_rc;
	uint64_t timeout_us;
	uint64_t auto_timeout_us;
	uint64_t auto_entry_deadline_us;
	uint64_t auto_deadline_us;
	uint32_t cycle_count;
};

static struct qcc74x_pm_pds15_context pm_pds15_ctx;

static struct k_spinlock pm_pds15_wifi_diag_lock;
static struct qcc74x_pm_pds15_wifi_diag_status pm_pds15_wifi_diag;
static struct k_spinlock pm_pds15_auto_diag_lock;
static struct qcc74x_pm_pds15_auto_diag_status pm_pds15_auto_diag;
static uint64_t pm_pds15_wifi_prepare_requested_us;
static uint64_t pm_pds15_wifi_prepare_effective_us;

struct qcc74x_pm_pds15_pre_entry_result {
	bool ready;
	uint64_t timeout_us;
	enum qcc74x_pm_pds15_auto_diag_reason auto_reason;
	enum qcc74x_pm_pds15_wifi_diag_reason wifi_reason;
};

#define QCC74X_PM_PDS15_FIRST_RTC_EPILOGUE_CNT 180ULL
#include <zephyr/soc/qualcomm/qcc74x_pm_lpfw.h>
#define QCC74X_PM_PDS15_SUBSTATE_ID QCC74X_PM_LPFW_SUBSTATE_ID
static uint64_t pm_pds15_rtc_epilogue_time_max_cnt;

static uint64_t qcc74x_pm_pds15_rtc_count_get(void)
{
	uint32_t rtc_low;
	uint32_t rtc_high;

	HBN_Get_RTC_Timer_Val(&rtc_low, &rtc_high);
	return ((uint64_t)rtc_high << 32) | rtc_low;
}

static int qcc74x_pm_pds15_configure_rtc_wakeup(qc7xx_lp_fw_cfg_t *cfg,
						uint64_t timeout_us)
{
	uint64_t rtc_enter_cnt = qcc74x_pm_pds15_rtc_count_get();
	uint64_t rtc_sleep_cnt = QC7XX_US_TO_PDS_CNT(timeout_us);
	uint64_t ahead_cnt = pm_pds15_rtc_epilogue_time_max_cnt != 0U ?
			     pm_pds15_rtc_epilogue_time_max_cnt :
			     QCC74X_PM_PDS15_FIRST_RTC_EPILOGUE_CNT;

	ahead_cnt += QC7XX_US_TO_PDS_CNT(
		QCC74X_PM_PDS15_WIFI_DIAG_WAKEUP_AHEAD_US);
	if (rtc_sleep_cnt <= ahead_cnt) {
		QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_rtc: sleep too short timeout_us=%llu sleep_cnt=%llu ahead_cnt=%llu",
		       (unsigned long long)timeout_us,
		       (unsigned long long)rtc_sleep_cnt,
		       (unsigned long long)ahead_cnt);
		return -EAGAIN;
	}

	/*
	 * The vendor rtc_wakeup_init() path still uses rtc_timeout_us as the
	 * "RTC wake enabled" guard even when rtc_wakeup_cmp_cnt is absolute.
	 * Keep it nonzero while making the compare count authoritative.
	 */
	cfg->rtc_timeout_us = timeout_us;
	cfg->rtc_wakeup_cmp_cnt = rtc_enter_cnt + rtc_sleep_cnt - ahead_cnt;
	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_rtc: sdk_abs timeout_us=%llu sleep_cnt=%llu ahead_cnt=%llu enter_cnt=%llu cmp_cnt=%llu",
	       (unsigned long long)timeout_us,
	       (unsigned long long)rtc_sleep_cnt,
	       (unsigned long long)ahead_cnt,
	       (unsigned long long)rtc_enter_cnt,
	       (unsigned long long)cfg->rtc_wakeup_cmp_cnt);

	return 0;
}

static void qcc74x_pm_pds15_update_rtc_epilogue(const qc7xx_lp_fw_cfg_t *cfg)
{
	uint64_t rtc_after_cnt;
	uint64_t delta_cnt;

	if (cfg->rtc_wakeup_cmp_cnt == 0U) {
		return;
	}

	rtc_after_cnt = qcc74x_pm_pds15_rtc_count_get();
	if (rtc_after_cnt <= cfg->rtc_wakeup_cmp_cnt) {
		return;
	}

	delta_cnt = rtc_after_cnt - cfg->rtc_wakeup_cmp_cnt;
	if ((delta_cnt > pm_pds15_rtc_epilogue_time_max_cnt) &&
	    (delta_cnt < 300U)) {
		pm_pds15_rtc_epilogue_time_max_cnt = delta_cnt;
		QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_rtc: epilogue_max_cnt=%llu",
		       (unsigned long long)pm_pds15_rtc_epilogue_time_max_cnt);
	}
}

static void qcc74x_pm_pds15_log_lpfw_cfg(const qc7xx_lp_fw_cfg_t *cfg)
{
	uint32_t *const shared_func = iot2lp_para->shared_func_p;
	const uint32_t shared_func4 =
		shared_func != NULL ? (uint32_t)shared_func[4] : 0U;
	const uint32_t shared_func6 =
		shared_func != NULL ? (uint32_t)shared_func[6] : 0U;

	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_lpfw_cfg: tim=%u copy=%u verify=%u rtc_timeout_us=%llu rtc_cmp=%llu "
	       "bssid=%02x:%02x:%02x:%02x:%02x:%02x mac=%02x:%02x:%02x:%02x:%02x:%02x "
	       "channel=%u aid=%u dtim_origin=%u dtim_num=%u bcmc=%u mtimer_min=%u mtimer_max=%u "
	       "bcn_target=%d pack_env=0x%08x buf_addr=0x%08x",
	       cfg->tim_wakeup_en, cfg->lpfw_copy, cfg->lpfw_verify,
	       (unsigned long long)cfg->rtc_timeout_us,
	       (unsigned long long)cfg->rtc_wakeup_cmp_cnt,
	       cfg->bssid[0], cfg->bssid[1], cfg->bssid[2], cfg->bssid[3],
	       cfg->bssid[4], cfg->bssid[5],
	       cfg->mac[0], cfg->mac[1], cfg->mac[2], cfg->mac[3],
	       cfg->mac[4], cfg->mac[5],
	       cfg->channel, cfg->aid, cfg->dtim_origin, cfg->dtim_num,
	       cfg->bcmc_dtim_mode, cfg->mtimer_timeout_mini_us,
	       cfg->mtimer_timeout_max_us, cfg->bcn_target_level,
	       cfg->pack_env, cfg->buf_addr);
	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_lpfw_shared: table=0x%08x calc_delay=0x%08x get_average=0x%08x",
	       (uint32_t)shared_func, shared_func4, shared_func6);
	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_lpfw_app_state: tim_gate=%u tim_wake_enable=%u beacon_interval_tu=%u "
	       "last_bcn_valid=%u last_bcn_rtc_us=%llu last_bcn_delay_us=%d "
	       "last_dtim_count=%u dtim_period=%u continuous_loss=%u loss_level=%d "
	       "wifi_rx_buff=0x%08x",
	       iot2lp_para->tim_wakeup_en, iot2lp_para->tim_wake_enable,
	       iot2lp_para->beacon_interval_tu,
	       iot2lp_para->last_beacon_stamp_rtc_valid,
	       (unsigned long long)iot2lp_para->last_beacon_stamp_rtc_us,
	       iot2lp_para->last_beacon_delay_us,
	       iot2lp_para->last_beacon_dtim_count,
	       iot2lp_para->beacon_dtim_period,
	       iot2lp_para->continuous_loss_cnt,
	       iot2lp_para->bcn_loss_level,
	       (uint32_t)(uintptr_t)iot2lp_para->wifi_rx_buff);
}

static void qcc74x_pm_pds15_log_lpfw_result(const qc7xx_lp_fw_cfg_t *cfg,
					     int fw_rc, uint64_t elapsed_us)
{
	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_lpfw_result: rc=%d wake_reason=0x%08x elapsed_us=%llu "
	       "recv_cnt=%u loss_cnt=%u bcn_target=%d tim=%u",
	       fw_rc, (uint32_t)cfg->wakeup_reason,
	       (unsigned long long)elapsed_us,
	       cfg->lpfw_recv_cnt, cfg->lpfw_loss_cnt,
	       cfg->bcn_target_level, cfg->tim_wakeup_en);
	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_lpfw_app_result: tim_gate=%u tim_wake_enable=%u "
	       "last_bcn_valid=%u last_bcn_rtc_us=%llu last_bcn_delay_us=%d "
	       "last_dtim_count=%u dtim_period=%u continuous_loss=%u loss_level=%d "
	       "lp_bcn_target=%d wifi_rx_buff=0x%08x",
	       iot2lp_para->tim_wakeup_en, iot2lp_para->tim_wake_enable,
	       iot2lp_para->last_beacon_stamp_rtc_valid,
	       (unsigned long long)iot2lp_para->last_beacon_stamp_rtc_us,
	       iot2lp_para->last_beacon_delay_us,
	       iot2lp_para->last_beacon_dtim_count,
	       iot2lp_para->beacon_dtim_period,
	       iot2lp_para->continuous_loss_cnt,
	       iot2lp_para->bcn_loss_level,
	       iot2lp_para->bcn_target_level,
	       (uint32_t)(uintptr_t)iot2lp_para->wifi_rx_buff);
}

static const char *qcc74x_pm_pds15_wifi_diag_reason_name(
	enum qcc74x_pm_pds15_wifi_diag_reason reason)
{
	switch (reason) {
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_WIFI_DISABLED:
		return "wifi_disabled";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_NOT_CONNECTED:
		return "not_connected";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_NOT_COMPILED:
		return "ps_not_compiled";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_ACTIVE_CHECK_NOT_RUN:
		return "active_check_not_run";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_SLEEP_CHECK_FAILED:
		return "ps_sleep_check_failed";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_DTIM_WINDOW_BLOCKED:
		return "dtim_window_blocked";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_TOO_CLOSE:
		return "twt_too_close";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_NEXT_WAKE_INVALID:
		return "twt_next_wake_invalid";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_READY:
		return "ready";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_STA_BUSY:
		return "sta_busy";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_CONFIG_FAILED:
		return "config_failed";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_ENTER_OPS_FAILED:
		return "enter_ops_failed";
	case QCC74X_PM_PDS15_WIFI_DIAG_REASON_NONE:
	default:
		return "none";
	}
}

static enum qcc74x_pm_pds15_wifi_diag_reason
qcc74x_pm_pds15_wifi_diag_reason_from_lowpower(
	enum qcc74x_wifi_lowpower_reason reason)
{
	switch (reason) {
	case QCC74X_WIFI_LOWPOWER_REASON_WIFI_DISABLED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_WIFI_DISABLED;
	case QCC74X_WIFI_LOWPOWER_REASON_NOT_CONNECTED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_NOT_CONNECTED;
	case QCC74X_WIFI_LOWPOWER_REASON_PS_NOT_COMPILED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_NOT_COMPILED;
	case QCC74X_WIFI_LOWPOWER_REASON_ACTIVE_CHECK_NOT_RUN:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_ACTIVE_CHECK_NOT_RUN;
	case QCC74X_WIFI_LOWPOWER_REASON_PS_SLEEP_CHECK_FAILED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_PS_SLEEP_CHECK_FAILED;
	case QCC74X_WIFI_LOWPOWER_REASON_LPFW_BLOCKED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_DTIM_WINDOW_BLOCKED;
	case QCC74X_WIFI_LOWPOWER_REASON_NEXT_WAKE_TOO_CLOSE:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_TOO_CLOSE;
	case QCC74X_WIFI_LOWPOWER_REASON_TWT_NEXT_WAKE_INVALID:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_TWT_NEXT_WAKE_INVALID;
	case QCC74X_WIFI_LOWPOWER_REASON_READY:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_READY;
	case QCC74X_WIFI_LOWPOWER_REASON_STA_BUSY:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_STA_BUSY;
	case QCC74X_WIFI_LOWPOWER_REASON_CONFIG_FAILED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_CONFIG_FAILED;
	case QCC74X_WIFI_LOWPOWER_REASON_ENTER_OPS_FAILED:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_ENTER_OPS_FAILED;
	case QCC74X_WIFI_LOWPOWER_REASON_NONE:
	default:
		return QCC74X_PM_PDS15_WIFI_DIAG_REASON_NONE;
	}
}

void qcc74x_pm_pds15_wifi_diag_get(
	struct qcc74x_pm_pds15_wifi_diag_status *status)
{
	if (status == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&pm_pds15_wifi_diag_lock);

	*status = pm_pds15_wifi_diag;

	k_spin_unlock(&pm_pds15_wifi_diag_lock, key);
}

void qcc74x_pm_pds15_wifi_diag_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_wifi_diag_lock);

	pm_pds15_wifi_diag =
		(struct qcc74x_pm_pds15_wifi_diag_status){ 0 };

	k_spin_unlock(&pm_pds15_wifi_diag_lock, key);
}

static void qcc74x_pm_pds15_wifi_diag_sample_internal(bool active_check,
						      bool log_sample)
{
	struct qcc74x_wifi_lowpower_status wifi_status;
	struct qcc74x_pm_pds15_wifi_diag_status sample = {
		.sampled = true,
		.ps_sleep_check_rc = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.lpfw_check_allow = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.twt_flow_count = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.bssid_rc = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.channel_rc = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.sta_raw_state = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.aid = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
		.channel = QCC74X_PM_PDS15_WIFI_DIAG_NOT_RUN,
	};
	uint32_t reason_count = 0U;

	if (active_check) {
		(void)qcc74x_wifi_lowpower_can_enter(UINT64_MAX, &wifi_status);
	} else {
		qcc74x_wifi_lowpower_get_status(&wifi_status);
		if (!wifi_status.sampled) {
			qcc74x_wifi_lowpower_sample(false, &wifi_status);
		}
	}
	sample.wifi_config_enabled = wifi_status.wifi_config_enabled;
	sample.wifi_ps_compiled = wifi_status.wifi_ps_compiled;
	sample.active_check_ran = wifi_status.active_check_ran;
	sample.sta_connected = wifi_status.sta_connected;
	sample.bssid_rc = wifi_status.bssid_rc;
	sample.channel_rc = wifi_status.channel_rc;
	sample.sta_raw_state = wifi_status.sta_raw_state;
	sample.aid = wifi_status.aid;
	sample.channel = wifi_status.channel;
	sample.twt_flow_count = wifi_status.twt_flow_count;
	sample.ps_sleep_check_rc = wifi_status.ps_sleep_check_rc;
	sample.lpfw_check_allow = wifi_status.lpfw_check_allow;
	sample.next_wakeup_timer_us = wifi_status.next_wakeup_timer_us;
	sample.next_wakeup_timer_valid = wifi_status.next_wakeup_timer_valid;
	sample.ps_cpu_can_sleep = wifi_status.ps_detail.cpu_can_sleep;
	sample.ps_queue_sent_empty = wifi_status.ps_detail.queue_sent_empty;
	sample.ps_queue_saved_empty = wifi_status.ps_detail.queue_saved_empty;
	sample.ps_allowed = wifi_status.ps_detail.ps_allowed;
	sample.tx_allowed = wifi_status.ps_detail.tx_allowed;
	sample.machw_allowed = wifi_status.ps_detail.machw_allowed;
	memcpy(sample.bssid, wifi_status.bssid, sizeof(sample.bssid));
	sample.last_reason =
		qcc74x_pm_pds15_wifi_diag_reason_from_lowpower(
			wifi_status.reason);

	k_spinlock_key_t key = k_spin_lock(&pm_pds15_wifi_diag_lock);

	sample.sample_count = pm_pds15_wifi_diag.sample_count + 1U;
	sample.active_check_count =
		pm_pds15_wifi_diag.active_check_count +
		(sample.active_check_ran ? 1U : 0U);
	sample.connected_count =
		pm_pds15_wifi_diag.connected_count +
		(sample.sta_connected ? 1U : 0U);
	memcpy(sample.reason_counts, pm_pds15_wifi_diag.reason_counts,
	       sizeof(sample.reason_counts));
	if (sample.last_reason < QCC74X_PM_PDS15_WIFI_DIAG_REASON_COUNT) {
		sample.reason_counts[sample.last_reason]++;
		reason_count = sample.reason_counts[sample.last_reason];
	}
	pm_pds15_wifi_diag = sample;

	k_spin_unlock(&pm_pds15_wifi_diag_lock, key);

	if (log_sample && (sample.wifi_config_enabled || active_check)) {
		QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_wifi_diag: sample=%u active=%d wifi=%d ps_compiled=%d connected=%d raw_state=%d "
		       "bssid=%02x:%02x:%02x:%02x:%02x:%02x channel_rc=%d channel=%d aid=%d "
		       "twt_flow=%d ps_check=%d lpfw_allow=%d next_wakeup_us=%u next_valid=%d reason=%s reason_count=%u "
		       "prep_req_us=%llu prep_eff_us=%llu ps_detail=%u/%u/%u/%u/%u/%u",
		       sample.sample_count, sample.active_check_ran,
		       sample.wifi_config_enabled, sample.wifi_ps_compiled,
		       sample.sta_connected, sample.sta_raw_state,
		       sample.bssid[0], sample.bssid[1], sample.bssid[2],
		       sample.bssid[3], sample.bssid[4], sample.bssid[5],
		       sample.channel_rc, sample.channel, sample.aid,
		       sample.twt_flow_count, sample.ps_sleep_check_rc,
		       sample.lpfw_check_allow, sample.next_wakeup_timer_us,
		       sample.next_wakeup_timer_valid,
		       qcc74x_pm_pds15_wifi_diag_reason_name(
			       sample.last_reason),
		       reason_count,
		       (unsigned long long)pm_pds15_wifi_prepare_requested_us,
		       (unsigned long long)pm_pds15_wifi_prepare_effective_us,
		       sample.ps_cpu_can_sleep, sample.ps_queue_sent_empty,
		       sample.ps_queue_saved_empty, sample.ps_allowed,
		       sample.tx_allowed, sample.machw_allowed);
	}
}

void qcc74x_pm_pds15_wifi_diag_sample(bool active_check)
{
	qcc74x_pm_pds15_wifi_diag_sample_internal(active_check, true);
}

static int qcc74x_pm_pds15_wifi_prepare(qc7xx_lp_fw_cfg_t *cfg,
					uint64_t *timeout_us,
					struct qcc74x_wifi_lowpower_session *session)
{
	struct qcc74x_pm_pds15_wifi_diag_status diag;
	uint64_t requested_timeout_us = *timeout_us;
	int rc;

	pm_pds15_wifi_prepare_requested_us = requested_timeout_us;
	pm_pds15_wifi_prepare_effective_us = *timeout_us;

	rc = qcc74x_wifi_lowpower_prepare(cfg, timeout_us, session);
	pm_pds15_wifi_prepare_effective_us = *timeout_us;

	qcc74x_pm_pds15_wifi_diag_sample(false);
	qcc74x_pm_pds15_wifi_diag_get(&diag);

	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_wifi: tim_wakeup_en=%u connected=%d twt_flow=%d "
	       "beacon_ready=%d last_bcn_valid=%u dtim_period=%u",
	       cfg->tim_wakeup_en, diag.sta_connected, diag.twt_flow_count,
	       session->status.beacon_ready,
	       iot2lp_para->last_beacon_stamp_rtc_valid,
	       iot2lp_para->beacon_dtim_period);

	if (diag.last_reason ==
	    QCC74X_PM_PDS15_WIFI_DIAG_REASON_ENTER_OPS_FAILED) {
		QCC74X_PM_PDS15_LOG_ERR("qcc74x_pds15_wifi: connected enter ops failed");
	}
	if (diag.last_reason ==
	    QCC74X_PM_PDS15_WIFI_DIAG_REASON_CONFIG_FAILED) {
		QCC74X_PM_PDS15_LOG_ERR("qcc74x_pds15_wifi: pds wifi config failed");
	}
	if (diag.last_reason ==
	    QCC74X_PM_PDS15_WIFI_DIAG_REASON_DTIM_WINDOW_BLOCKED) {
		QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_wifi: lp fw enter check blocked");
	}
	if (diag.next_wakeup_timer_valid) {
		if (diag.next_wakeup_timer_us <
		    QCC74X_WIFI_LOWPOWER_WAKEUP_AHEAD_US) {
			QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_wifi: next wakeup too close %u us",
			       diag.next_wakeup_timer_us);
		}
	}

	return rc;
}

static const struct pm_state_info *qcc74x_pm_pds15_state_info_get(void)
{
	const struct pm_state_info *states;
	uint8_t count = pm_state_cpu_get_all(0U, &states);

	for (uint8_t i = 0U; i < count; i++) {
		if ((states[i].state == PM_STATE_SUSPEND_TO_RAM) &&
		    (states[i].substate_id == QCC74X_PM_PDS15_SUBSTATE_ID)) {
			return &states[i];
		}
	}

	return NULL;
}

static uint32_t qcc74x_pm_pds15_state_min_ticks(
	const struct pm_state_info *state)
{
	uint64_t min_us;

	if (state == NULL) {
		return UINT32_MAX;
	}

	min_us = (uint64_t)state->min_residency_us +
		 (uint64_t)state->exit_latency_us;
	if (min_us == 0U) {
		return 0U;
	}

	return k_us_to_ticks_ceil32(min_us);
}

static uint64_t qcc74x_pm_pds15_ticks_to_us(int32_t ticks)
{
	if (ticks == K_TICKS_FOREVER) {
		return UINT64_MAX;
	}

	if (ticks <= 0) {
		return 0U;
	}

	return k_ticks_to_us_floor64((uint64_t)ticks);
}

static bool qcc74x_pm_pds15_policy_state_locked(void)
{
	return pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_RAM,
					      QCC74X_PM_PDS15_SUBSTATE_ID);
}

static bool qcc74x_pm_pds15_auto_expire_if_needed(void)
{
	bool expired = false;
	bool restore_lock = false;
	uint64_t now_us = qc7xx_lp_get_virtual_us();
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	if (pm_pds15_ctx.auto_enabled && pm_pds15_ctx.auto_deadline_active &&
	    (now_us >= pm_pds15_ctx.auto_deadline_us)) {
		pm_pds15_ctx.auto_enabled = false;
		pm_pds15_ctx.auto_deadline_active = false;
		pm_pds15_ctx.auto_deadline_us = 0U;
		pm_pds15_ctx.auto_entry_pending = false;
		pm_pds15_ctx.auto_timeout_us = 0U;
		pm_pds15_ctx.auto_entry_deadline_us = 0U;
		pm_pds15_ctx.timeout_us = 0U;
		restore_lock = pm_pds15_ctx.auto_lock_released;
		pm_pds15_ctx.auto_lock_released = false;
		expired = true;
	}

	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (restore_lock) {
		pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
					 QCC74X_PM_PDS15_SUBSTATE_ID);
	}

	return expired;
}

static bool qcc74x_pm_pds15_auto_deadline_remaining(uint64_t *remaining_us)
{
	bool active;
	uint64_t deadline_us;
	uint64_t now_us = qc7xx_lp_get_virtual_us();
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	active = pm_pds15_ctx.auto_enabled &&
		 pm_pds15_ctx.auto_deadline_active;
	deadline_us = pm_pds15_ctx.auto_deadline_us;

	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (!active) {
		if (remaining_us != NULL) {
			*remaining_us = 0U;
		}
		return false;
	}

	if (remaining_us != NULL) {
		*remaining_us = (now_us < deadline_us) ?
				(deadline_us - now_us) : 0U;
	}

	return true;
}

static uint64_t qcc74x_pm_pds15_deadline_add(uint64_t now_us, uint64_t delta_us)
{
	if ((UINT64_MAX - now_us) < delta_us) {
		return UINT64_MAX;
	}

	return now_us + delta_us;
}

static enum qcc74x_pm_pds15_auto_diag_reason
qcc74x_pm_pds15_auto_diag_classify(
	const struct qcc74x_pm_pds15_auto_diag_status *sample)
{
	if (!sample->auto_config_enabled) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_AUTO_DISABLED;
	}

	if (!sample->initialized) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_NOT_INITIALIZED;
	}

	if (sample->policy_lock_active || !sample->state_available) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_STATE_LOCKED;
	}

	if ((sample->timeout_us == 0U) || (sample->timeout_us == UINT64_MAX)) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_TIMEOUT_TOO_SHORT;
	}

	if ((sample->ticks != K_TICKS_FOREVER) &&
	    ((sample->ticks <= 0) ||
	     ((sample->min_residency_ticks != UINT32_MAX) &&
	      ((uint32_t)sample->ticks < sample->min_residency_ticks)))) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_TIMEOUT_TOO_SHORT;
	}

	if (sample->enter_busy) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_ENTER_BUSY;
	}

	if (sample->resume_pending) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_RESUME_PENDING;
	}

	if ((sample->wifi_reason != QCC74X_PM_PDS15_WIFI_DIAG_REASON_NONE) &&
	    (sample->wifi_reason != QCC74X_PM_PDS15_WIFI_DIAG_REASON_WIFI_DISABLED) &&
	    (sample->wifi_reason != QCC74X_PM_PDS15_WIFI_DIAG_REASON_READY) &&
	    (sample->wifi_reason !=
	     QCC74X_PM_PDS15_WIFI_DIAG_REASON_NOT_CONNECTED)) {
		return QCC74X_PM_PDS15_AUTO_DIAG_REASON_WIFI_NOT_READY;
	}

	return QCC74X_PM_PDS15_AUTO_DIAG_REASON_READY;
}

static uint64_t qcc74x_pm_pds15_timeout_from_policy_ticks(int32_t ticks,
							  bool *ready)
{
	uint64_t timeout_us = qcc74x_pm_pds15_ticks_to_us(ticks);
	uint64_t remaining_us = 0U;

	if (ready != NULL) {
		*ready = false;
	}

	if (qcc74x_pm_pds15_auto_deadline_remaining(&remaining_us)) {
		if (remaining_us == 0U) {
			(void)qcc74x_pm_pds15_auto_expire_if_needed();
			return 0U;
		}

		if ((timeout_us == UINT64_MAX) || (remaining_us < timeout_us)) {
			timeout_us = remaining_us;
		}
	}

	if ((timeout_us == 0U) || (timeout_us == UINT64_MAX)) {
		return 0U;
	}

	if (ready != NULL) {
		*ready = true;
	}

	return timeout_us;
}

static void qcc74x_pm_pds15_auto_diag_update(int32_t ticks,
					     uint64_t *timeout_us_out,
					     bool *timeout_ready_out)
{
	struct qcc74x_pm_pds15_auto_diag_status sample = {
		.sampled = true,
		.auto_config_enabled = IS_ENABLED(CONFIG_QCC74X_PM_PDS15_POLICY),
		.ticks = ticks,
	};
	const struct pm_state_info *state = qcc74x_pm_pds15_state_info_get();
	struct qcc74x_pm_pds15_wifi_diag_status wifi_diag;
	uint32_t reason_count = 0U;
	uint64_t now_us;
	bool timeout_ready = false;
	k_spinlock_key_t key;

	(void)qcc74x_pm_pds15_auto_expire_if_needed();
	now_us = qc7xx_lp_get_virtual_us();

	key = k_spin_lock(&pm_pds15_ctx.lock);
	sample.auto_enabled = pm_pds15_ctx.auto_enabled;
	sample.initialized = pm_pds15_ctx.initialized;
	sample.enter_busy = pm_pds15_ctx.enter_in_progress;
	sample.resume_pending = pm_pds15_ctx.resume_cleanup_pending;
	sample.auto_deadline_active = pm_pds15_ctx.auto_deadline_active;
	if (pm_pds15_ctx.auto_deadline_active) {
		if (now_us >= pm_pds15_ctx.auto_deadline_us) {
			sample.auto_deadline_expired = true;
			sample.auto_remaining_us = 0U;
		} else {
			sample.auto_remaining_us =
				pm_pds15_ctx.auto_deadline_us - now_us;
		}
	}
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	sample.policy_lock_active = qcc74x_pm_pds15_policy_state_locked();
	sample.state_available =
		(state != NULL) &&
		pm_policy_state_is_available(PM_STATE_SUSPEND_TO_RAM,
					     QCC74X_PM_PDS15_SUBSTATE_ID);
	sample.min_residency_ticks =
		qcc74x_pm_pds15_state_min_ticks(state);
	sample.timeout_us =
		qcc74x_pm_pds15_timeout_from_policy_ticks(ticks,
							  &timeout_ready);

	if (sample.auto_config_enabled && sample.initialized &&
	    !sample.policy_lock_active &&
	    sample.state_available && timeout_ready && !sample.enter_busy &&
	    !sample.resume_pending) {
		qcc74x_pm_pds15_wifi_diag_sample_internal(true, false);
	}

	qcc74x_pm_pds15_wifi_diag_get(&wifi_diag);
	sample.wifi_reason = wifi_diag.sampled ? wifi_diag.last_reason :
			      QCC74X_PM_PDS15_WIFI_DIAG_REASON_NONE;
	sample.last_reason = qcc74x_pm_pds15_auto_diag_classify(&sample);

	key = k_spin_lock(&pm_pds15_auto_diag_lock);
	sample.sample_count = pm_pds15_auto_diag.sample_count + 1U;
	sample.ready_count = pm_pds15_auto_diag.ready_count +
			     (sample.last_reason ==
			      QCC74X_PM_PDS15_AUTO_DIAG_REASON_READY ? 1U : 0U);
	memcpy(sample.reason_counts, pm_pds15_auto_diag.reason_counts,
	       sizeof(sample.reason_counts));
	if (sample.last_reason < QCC74X_PM_PDS15_AUTO_DIAG_REASON_COUNT) {
		sample.reason_counts[sample.last_reason]++;
		reason_count = sample.reason_counts[sample.last_reason];
	}
	pm_pds15_auto_diag = sample;
	k_spin_unlock(&pm_pds15_auto_diag_lock, key);

	if (timeout_us_out != NULL) {
		*timeout_us_out = sample.timeout_us;
	}
	if (timeout_ready_out != NULL) {
		*timeout_ready_out = timeout_ready;
	}

	ARG_UNUSED(reason_count);
}

void qcc74x_pm_pds15_auto_diag_sample(void)
{
	qcc74x_pm_pds15_auto_diag_update(K_TICKS_FOREVER, NULL, NULL);
}

void qcc74x_pm_pds15_auto_diag_get(
	struct qcc74x_pm_pds15_auto_diag_status *status)
{
	if (status == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&pm_pds15_auto_diag_lock);

	*status = pm_pds15_auto_diag;

	k_spin_unlock(&pm_pds15_auto_diag_lock, key);
}

void qcc74x_pm_pds15_auto_diag_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_auto_diag_lock);

	pm_pds15_auto_diag =
		(struct qcc74x_pm_pds15_auto_diag_status){ 0 };

	k_spin_unlock(&pm_pds15_auto_diag_lock, key);
}

static bool qcc74x_pm_pds15_pre_entry_check(
	int32_t ticks, struct qcc74x_pm_pds15_pre_entry_result *result)
{
	struct qcc74x_pm_pds15_auto_diag_status diag;
	bool timeout_ready = false;
	uint64_t timeout_us;
	bool ready;

	qcc74x_pm_pds15_auto_diag_update(ticks, &timeout_us, &timeout_ready);
	qcc74x_pm_pds15_auto_diag_get(&diag);

	ready = timeout_ready &&
		(diag.last_reason == QCC74X_PM_PDS15_AUTO_DIAG_REASON_READY);

	if (result != NULL) {
		*result = (struct qcc74x_pm_pds15_pre_entry_result){
			.ready = ready,
			.timeout_us = ready ? timeout_us : 0U,
			.auto_reason = diag.last_reason,
			.wifi_reason = diag.wifi_reason,
		};
	}

	return ready;
}

bool qcc74x_pm_pds15_can_auto_enter(int32_t ticks)
{
	struct qcc74x_pm_pds15_pre_entry_result result;
	bool can_enter;
	uint64_t entry_deadline_us = 0U;
	k_spinlock_key_t key;

	can_enter = qcc74x_pm_pds15_pre_entry_check(ticks, &result);
	if (can_enter) {
		entry_deadline_us =
			qcc74x_pm_pds15_deadline_add(qc7xx_lp_get_virtual_us(),
						     result.timeout_us);
	}

	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.auto_entry_pending = can_enter;
	pm_pds15_ctx.auto_timeout_us = can_enter ? result.timeout_us : 0U;
	pm_pds15_ctx.auto_entry_deadline_us =
		can_enter ? entry_deadline_us : 0U;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	return can_enter;
}

void qcc74x_pm_pds15_record_auto_reject(int32_t ticks)
{
	k_spinlock_key_t key;

	/* Also clear requests made through the official long-lived CPU unlock. */
	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.auto_entry_pending = false;
	pm_pds15_ctx.auto_timeout_us = 0U;
	pm_pds15_ctx.auto_entry_deadline_us = 0U;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (qcc74x_pm_pds15_auto_is_enabled()) {
		qcc74x_pm_pds15_auto_diag_update(ticks, NULL, NULL);
	}
}

int qcc74x_pm_pds15_policy_lock_set(bool lock)
{
	if (lock) {
		k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

		pm_pds15_ctx.auto_entry_pending = false;
		pm_pds15_ctx.auto_timeout_us = 0U;
		pm_pds15_ctx.auto_entry_deadline_us = 0U;
		k_spin_unlock(&pm_pds15_ctx.lock, key);
	}

	if (lock) {
		if (!qcc74x_pm_pds15_policy_state_locked()) {
			pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
						 QCC74X_PM_PDS15_SUBSTATE_ID);
		}
	} else {
		if (qcc74x_pm_pds15_policy_state_locked()) {
			pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM,
						 QCC74X_PM_PDS15_SUBSTATE_ID);
		}
	}

	return 0;
}

bool qcc74x_pm_pds15_policy_lock_is_active(void)
{
	return qcc74x_pm_pds15_policy_state_locked();
}

static int qcc74x_pm_pds15_policy_lock_init(void)
{
	pm_pds15_ctx.auto_enabled = false;
	pm_pds15_ctx.auto_lock_released = false;
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
				 QCC74X_PM_PDS15_SUBSTATE_ID);

	return 0;
}

SYS_INIT(qcc74x_pm_pds15_policy_lock_init, PRE_KERNEL_2, 0);

static uint64_t qcc74x_pm_pds15_timeout_until_deadline(uint64_t deadline_us,
						       bool *ready)
{
	uint64_t now_us;

	if (ready != NULL) {
		*ready = false;
	}

	now_us = qc7xx_lp_get_virtual_us();
	if (now_us >= deadline_us) {
		return 0U;
	}

	if (ready != NULL) {
		*ready = true;
	}

	return deadline_us - now_us;
}

static void qcc74x_pm_pds15_auto_entry_skip_cleanup(void)
{
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	pm_pds15_ctx.enter_in_progress = false;
	pm_pds15_ctx.pds15_enter_committed = false;
	pm_pds15_ctx.auto_entry_pending = false;
	pm_pds15_ctx.auto_timeout_us = 0U;
	pm_pds15_ctx.auto_entry_deadline_us = 0U;
	pm_pds15_ctx.resume_cleanup_pending = false;
	pm_pds15_ctx.timeout_us = 0U;

	k_spin_unlock(&pm_pds15_ctx.lock, key);
	qcc74x_pm_policy_entry_rejected();
}

static int qcc74x_pm_pds15_calibrate_rc32k(void)
{
	const uint32_t sample_ms = 50U;
	const uint32_t max_attempts = 8U;
	int last_ppm = 1000000;

	for (uint32_t attempt = 1U; attempt <= max_attempts; attempt++) {
		unsigned int key;
		uint32_t rtc_low0;
		uint32_t rtc_high0;
		uint32_t rtc_low1;
		uint32_t rtc_high1;
		uint32_t rtc_us;
		uint32_t mtimer_us;
		uint64_t mtimer0;
		uint64_t mtimer1;

		key = irq_lock();
		mtimer0 = CPU_Get_MTimer_US();
		HBN_Get_RTC_Timer_Val(&rtc_low0, &rtc_high0);
		irq_unlock(key);

		arch_delay_ms(sample_ms);

		key = irq_lock();
		mtimer1 = CPU_Get_MTimer_US();
		HBN_Get_RTC_Timer_Val(&rtc_low1, &rtc_high1);
		irq_unlock(key);

		if ((mtimer1 <= mtimer0) || (rtc_high1 != rtc_high0) ||
		    (rtc_low1 <= rtc_low0)) {
			continue;
		}

		mtimer_us = (uint32_t)(mtimer1 - mtimer0);
		rtc_us = QC7XX_PDS_CNT_TO_US(rtc_low1 - rtc_low0);
		last_ppm = qc7xx_lp_rtc_rc32k_coarse_adj(mtimer_us, rtc_us);

		if ((last_ppm <= 2000) && (last_ppm >= -2000)) {
			qc7xx_lp_set_32k_clock_ready(1);
			return 0;
		}

		arch_delay_ms(5);
	}

	qc7xx_lp_set_32k_clock_ready(0);
	return (last_ppm == 1000000) ? -EIO : -ERANGE;
}

static int qcc74x_pm_pds15_init_once(void)
{
	int rc = 0;
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	if (pm_pds15_ctx.initialized) {
		k_spin_unlock(&pm_pds15_ctx.lock, key);
		return 0;
	}

	if (pm_pds15_ctx.init_in_progress) {
		k_spin_unlock(&pm_pds15_ctx.lock, key);
		return -EINPROGRESS;
	}

	pm_pds15_ctx.init_in_progress = true;
	pm_pds15_ctx.init_rc = -EINPROGRESS;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

#ifdef CONFIG_QCC74X_PM_PDS1
	if (!qcc74x_psram_retention_ready()) {
		rc = -ENOTSUP;
		goto out;
	}
#endif

	/*
	 * The vendor LPFW PDS15 flow assumes the RTC counter is already running
	 * and the LP 32k domain has been declared usable.
	 * BLE low power requires XTAL32K (±20 ppm) for clock accuracy;
	 * without BLE, RC32K (±1500 ppm) is sufficient and saves power.
	 */
#if defined(CONFIG_QCC74X_PM_PDS15_BLE)
	qc7xx_lp_rtc_use_xtal32K();
#else
	qc7xx_lp_rtc_use_rc32k();
#endif
	HBN_Enable_RTC_Counter();
#if !defined(CONFIG_QCC74X_PM_PDS15_BLE)
	pm_rc32k_auto_cal_init();

	if (qcc74x_pm_pds15_calibrate_rc32k() != 0) {
		rc = -EIO;
		goto out;
	}
#else
	qc7xx_lp_set_32k_clock_ready(1);
#endif

	rc = qc7xx_lp_init();
	if (rc != 0) {
		goto out;
	}

	qc7xx_lp_fw_init();

out:
	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.initialized = (rc == 0);
	pm_pds15_ctx.init_in_progress = false;
	pm_pds15_ctx.init_rc = rc;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	return rc;
}

int qcc74x_pm_pds15_init(void)
{
	return qcc74x_pm_pds15_init_once();
}

#if defined(CONFIG_QCC74X_PM_PDS15_AUTO_INIT)
static void qcc74x_pm_pds15_auto_init_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int rc = qcc74x_pm_pds15_init();

	if (rc == 0) {
		QCC74X_PM_PDS15_LOG_INF("qcc74x_pds15: auto init ok");
	} else {
		QCC74X_PM_PDS15_LOG_ERR("qcc74x_pds15: auto init failed rc=%d",
		       rc);
	}
}

static K_WORK_DELAYABLE_DEFINE(qcc74x_pm_pds15_auto_init_work,
			       qcc74x_pm_pds15_auto_init_work_handler);

static int qcc74x_pm_pds15_auto_init(void)
{
	(void)k_work_schedule(&qcc74x_pm_pds15_auto_init_work, K_MSEC(100));

	return 0;
}

SYS_INIT(qcc74x_pm_pds15_auto_init, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);
#endif

static int qcc74x_pm_pds15_require_initialized(void)
{
	int rc;
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	/*
	 * During PDS15 bring-up, LPFW init is an explicit shell-driven step.
	 * The runtime deep-sleep path should consume that prepared state only.
	 */
	if (pm_pds15_ctx.initialized) {
		rc = 0;
	} else if (pm_pds15_ctx.init_in_progress) {
		rc = -EINPROGRESS;
	} else if (pm_pds15_ctx.init_rc != 0) {
		rc = pm_pds15_ctx.init_rc;
	} else {
		rc = -EAGAIN;
	}

	k_spin_unlock(&pm_pds15_ctx.lock, key);
	return rc;
}

bool qcc74x_pm_pds15_is_initialized(void)
{
	struct qcc74x_pm_pds15_status status;

	qcc74x_pm_pds15_get_status(&status);

	return status.initialized;
}

void qcc74x_pm_pds15_get_status(struct qcc74x_pm_pds15_status *status)
{
	bool policy_lock_active;

	if (status == NULL) {
		return;
	}

	policy_lock_active = qcc74x_pm_pds15_policy_state_locked();

	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	*status = (struct qcc74x_pm_pds15_status){
		.initialized = pm_pds15_ctx.initialized,
		.init_in_progress = pm_pds15_ctx.init_in_progress,
		.auto_enabled = pm_pds15_ctx.auto_enabled,
		.policy_lock_active = policy_lock_active,
		.auto_entry_pending = pm_pds15_ctx.auto_entry_pending,
		.auto_deadline_active = pm_pds15_ctx.auto_deadline_active,
		.init_rc = pm_pds15_ctx.init_rc,
		.timeout_us = pm_pds15_ctx.timeout_us,
		.auto_timeout_us = pm_pds15_ctx.auto_timeout_us,
		.auto_deadline_us = pm_pds15_ctx.auto_deadline_us,
		.cycle_count = pm_pds15_ctx.cycle_count,
	};

	k_spin_unlock(&pm_pds15_ctx.lock, key);
}

int qcc74x_pm_pds15_auto_enable(bool enable)
{
	int rc;
	bool release_lock = false;
	bool restore_lock = false;
	bool lock_needed = false;
	k_spinlock_key_t key;

	if (!IS_ENABLED(CONFIG_QCC74X_PM_PDS15_POLICY)) {
		return -ENOTSUP;
	}

	if (enable) {
		rc = qcc74x_pm_pds15_require_initialized();
		if (rc != 0) {
			return rc;
		}
	}

	key = k_spin_lock(&pm_pds15_ctx.lock);
	restore_lock = !enable && pm_pds15_ctx.auto_lock_released;
	pm_pds15_ctx.auto_enabled = enable;
	pm_pds15_ctx.auto_deadline_active = false;
	pm_pds15_ctx.auto_deadline_us = 0U;
	pm_pds15_ctx.auto_entry_pending = false;
	pm_pds15_ctx.auto_timeout_us = 0U;
	pm_pds15_ctx.auto_entry_deadline_us = 0U;
	pm_pds15_ctx.timeout_us = 0U;
	pm_pds15_ctx.cycle_count = 0U;
	if (!enable) {
		pm_pds15_ctx.auto_lock_released = false;
	}
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (restore_lock) {
		pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
					 QCC74X_PM_PDS15_SUBSTATE_ID);
	}

	if (enable) {
		key = k_spin_lock(&pm_pds15_ctx.lock);
		release_lock = !pm_pds15_ctx.auto_lock_released &&
			       qcc74x_pm_pds15_policy_state_locked();
		k_spin_unlock(&pm_pds15_ctx.lock, key);

		if (release_lock) {
			pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM,
						 QCC74X_PM_PDS15_SUBSTATE_ID);
			key = k_spin_lock(&pm_pds15_ctx.lock);
			pm_pds15_ctx.auto_lock_released = true;
			k_spin_unlock(&pm_pds15_ctx.lock, key);
		}
	} else {
		lock_needed = !qcc74x_pm_pds15_policy_state_locked();
		if (lock_needed) {
			pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM,
						 QCC74X_PM_PDS15_SUBSTATE_ID);
		}
	}

	return 0;
}

int qcc74x_pm_pds15_auto_arm_for(uint64_t duration_us, bool *out_release_pending)
{
	int rc;
	uint64_t deadline_us;
	bool release_lock = false;
	k_spinlock_key_t key;

	if (out_release_pending != NULL) {
		*out_release_pending = false;
	}

	if (!IS_ENABLED(CONFIG_QCC74X_PM_PDS15_POLICY)) {
		return -ENOTSUP;
	}

	if (duration_us == 0U) {
		return qcc74x_pm_pds15_auto_enable(true);
	}

	rc = qcc74x_pm_pds15_require_initialized();
	if (rc != 0) {
		return rc;
	}

	deadline_us = qcc74x_pm_pds15_deadline_add(qc7xx_lp_get_virtual_us(),
						   duration_us);

	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.auto_enabled = true;
	pm_pds15_ctx.auto_deadline_active = true;
	pm_pds15_ctx.auto_deadline_us = deadline_us;
	pm_pds15_ctx.auto_entry_pending = false;
	pm_pds15_ctx.auto_timeout_us = 0U;
	pm_pds15_ctx.auto_entry_deadline_us = 0U;
	pm_pds15_ctx.timeout_us = 0U;
	pm_pds15_ctx.cycle_count = 0U;
	release_lock = !pm_pds15_ctx.auto_lock_released &&
		       qcc74x_pm_pds15_policy_state_locked();
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (out_release_pending != NULL) {
		*out_release_pending = release_lock;
	}

	return 0;
}

/*
 * releasing the policy lock synchronously (as rev 20 did)
 * only protects the confirmation shell_print() that runs before this call —
 * it does not protect the shell's own prompt-redraw print that unconditionally
 * follows every command, via the identical ring-buffer/ISR TX path. Deferring
 * the actual release onto the system work queue, well outside the shell
 * thread's own execution, ensures PDS15 cannot become policy-eligible until
 * both prints have had time to fully drain, regardless of how many prints
 * the shell core decides to emit after this command handler returns.
 */
#define QCC74X_PM_PDS15_AUTO_RELEASE_DEFER_MS 200

static void qcc74x_pm_pds15_auto_release_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	bool release_lock;
	k_spinlock_key_t key;

	key = k_spin_lock(&pm_pds15_ctx.lock);
	release_lock = !pm_pds15_ctx.auto_lock_released &&
		       qcc74x_pm_pds15_policy_state_locked();
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (release_lock) {
		pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_RAM,
					 QCC74X_PM_PDS15_SUBSTATE_ID);
		key = k_spin_lock(&pm_pds15_ctx.lock);
		pm_pds15_ctx.auto_lock_released = true;
		k_spin_unlock(&pm_pds15_ctx.lock, key);
	}
}

static K_TIMER_DEFINE(qcc74x_pm_pds15_auto_release_timer,
		      qcc74x_pm_pds15_auto_release_timer_handler, NULL);

void qcc74x_pm_pds15_auto_release_pending_lock(void)
{
	k_timer_start(&qcc74x_pm_pds15_auto_release_timer,
		     K_MSEC(QCC74X_PM_PDS15_AUTO_RELEASE_DEFER_MS), K_NO_WAIT);
}

int qcc74x_pm_pds15_auto_enable_for(uint64_t duration_us)
{
	bool release_pending = false;
	int rc = qcc74x_pm_pds15_auto_arm_for(duration_us, &release_pending);

	if (release_pending) {
		qcc74x_pm_pds15_auto_release_pending_lock();
	}

	return rc;
}

bool qcc74x_pm_pds15_auto_is_enabled(void)
{
	bool enabled;
	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	enabled = pm_pds15_ctx.auto_enabled;

	k_spin_unlock(&pm_pds15_ctx.lock, key);

	return enabled;
}
int qcc74x_pm_pds15_enter(uint8_t substate_id)
{
	uint64_t timeout_us = 0U;
	uint64_t entry_deadline_us = 0U;
	uint64_t effective_timeout_us;
	bool auto_entry = false;
	bool should_enter = false;
	uint64_t before_us;
	uint64_t elapsed_us;
	uint32_t wake_reason = 0U;
	uint64_t gpio_wake_bits = 0U;
	uint64_t gpio_wake_edge_bits = 0U;
	int gpio_resume_rc = 0;
	int fw_rc;
	int rc;
	struct qcc74x_wifi_lowpower_session wifi_session = { 0 };
	struct qcc74x_ble_lowpower_session ble_session = { 0 };
	qc7xx_lp_fw_cfg_t cfg = {
		.tim_wakeup_en = 0U,
		/*
		 * Boot auto-init loads and verifies LPFW before automatic PDS15
		 * entry. Re-copying/re-hashing it on every later PDS15 entry is
		 * what currently trips the vendor multi-cycle instability after
		 * wake, so the runtime path should consume the prepared image only.
		 */
		.lpfw_copy = 0U,
		.lpfw_verify = 0U,
		.dtim_origin = QC7XX_DTIM_NUM,
	};

	if (substate_id != QCC74X_PM_PDS15_SUBSTATE_ID) {
		return -ENOTSUP;
	}
	rc = qcc74x_pm_pds15_require_initialized();
	if (rc != 0) {
		return rc;
	}

	k_spinlock_key_t key = k_spin_lock(&pm_pds15_ctx.lock);

	if (pm_pds15_ctx.enter_in_progress) {
		k_spin_unlock(&pm_pds15_ctx.lock, key);
		qcc74x_pm_diag_record_deep_skip(
			QCC74X_PM_DEEP_SKIP_REASON_ENTER_BUSY);
		return -EBUSY;
	}

	pm_pds15_ctx.pds15_enter_committed = false;
	if (pm_pds15_ctx.auto_entry_pending &&
	    (pm_pds15_ctx.auto_timeout_us != 0U)) {
		auto_entry = true;
		timeout_us = pm_pds15_ctx.auto_timeout_us;
		entry_deadline_us = pm_pds15_ctx.auto_entry_deadline_us;
		pm_pds15_ctx.auto_entry_pending = false;
		pm_pds15_ctx.auto_timeout_us = 0U;
		pm_pds15_ctx.auto_entry_deadline_us = 0U;
		pm_pds15_ctx.timeout_us = timeout_us;
		pm_pds15_ctx.cycle_count++;
		pm_pds15_ctx.enter_in_progress = true;
	}

	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (!auto_entry) {
		qcc74x_pm_policy_entry_rejected();
		qcc74x_pm_diag_record_deep_skip(
			QCC74X_PM_DEEP_SKIP_REASON_NOT_ARMED);
		return -EAGAIN;
	}

	effective_timeout_us =
		qcc74x_pm_pds15_timeout_until_deadline(entry_deadline_us,
						       &should_enter);
	if (!should_enter) {
		qcc74x_pm_pds15_auto_entry_skip_cleanup();
		qcc74x_pm_diag_record_deep_skip(
			QCC74X_PM_DEEP_SKIP_REASON_NEXT_TIMEOUT_IMMEDIATE);
		return -EAGAIN;
	}

	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.timeout_us = effective_timeout_us;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	rc = qcc74x_pm_uart_check_suspended();
	if (rc != 0) {
		qcc74x_pm_pds15_auto_entry_skip_cleanup();
		return rc;
	}

	rc = gpio_qc7xx_pds15_prepare();
	if (rc != 0) {
		qcc74x_pm_pds15_auto_entry_skip_cleanup();
		if (rc == -EAGAIN) {
			qcc74x_pm_diag_record_deep_skip(
				QCC74X_PM_DEEP_SKIP_REASON_GPIO_BLOCKED);
			return -EAGAIN;
		}

		QCC74X_PM_PDS15_LOG_ERR(
			"qcc74x_pds15_prepare: GPIO wake config rc=%d", rc);
		return rc;
	}

	/* BLE first: btble_controller_sleep(0) is a pure query (~200 µs).
	 * If BLE is active, skip the expensive WiFi prepare/resume entirely.
	 */
	rc = qcc74x_ble_lowpower_prepare(&effective_timeout_us, &ble_session);
	if (rc != 0) {
		qcc74x_pm_pds15_auto_entry_skip_cleanup();
		qcc74x_pm_diag_record_deep_skip(
			QCC74X_PM_DEEP_SKIP_REASON_BLE_BLOCKED);
		return -EAGAIN;
	}

	rc = qcc74x_pm_pds15_wifi_prepare(&cfg, &effective_timeout_us,
					  &wifi_session);
	if (rc != 0) {
		qcc74x_pm_pds15_auto_entry_skip_cleanup();
		qcc74x_pm_diag_record_deep_skip(
			QCC74X_PM_DEEP_SKIP_REASON_WIFI_BLOCKED);
		return -EAGAIN;
	}

	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.timeout_us = effective_timeout_us;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	rc = qcc74x_pm_pds15_configure_rtc_wakeup(&cfg, effective_timeout_us);
	if (rc != 0) {
		qcc74x_pm_pds15_auto_entry_skip_cleanup();
		qcc74x_pm_diag_record_deep_skip(
			QCC74X_PM_DEEP_SKIP_REASON_NEXT_TIMEOUT_IMMEDIATE);
		return -EAGAIN;
	}

	qcc74x_pm_diag_record_deep_prepare(effective_timeout_us);

	/*
	 * Preserve unannounced time across the MTIMER reset. Neither capture
	 * nor rebase may dispatch callbacks while system-managed UARTs sleep;
	 * the timer ISR announces after the PM core has restored devices.
	 */
	qcc74x_pm_pds15_timer_flush_elapsed();

	before_us = qc7xx_lp_get_virtual_us();
	qcc74x_pm_pds15_log_lpfw_cfg(&cfg);
	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.pds15_enter_committed = true;
	k_spin_unlock(&pm_pds15_ctx.lock, key);
	fw_rc = qc7xx_lp_fw_enter(&cfg);
	elapsed_us = qc7xx_lp_get_virtual_us() - before_us;
	if (fw_rc >= 0) {
		qcc74x_pm_pds15_timer_deep_sleep_rebase(elapsed_us);
	}
	qcc74x_pm_pds15_log_lpfw_result(&cfg, fw_rc, elapsed_us);
	if (fw_rc >= 0) {
		wake_reason = (uint32_t)fw_rc;
		/*
		 * LPFW reports RTC TIMEOUT for BLE-initiated wake (BLE schedules
		 * its wakeup via the RTC compare register). Surface the BLE bit
		 * so downstream diagnostics correctly attribute the wake.
		 */
		if ((wake_reason & LPFW_WAKEUP_TIME_OUT) && ble_session.capping) {
			wake_reason |= LPFW_WAKEUP_BLE;
		}
		qcc74x_ble_lowpower_crash_check(&ble_session, elapsed_us);
		qcc74x_pm_pds15_update_rtc_epilogue(&cfg);
		gpio_resume_rc = gpio_qc7xx_pds15_resume(&gpio_wake_bits,
							  &gpio_wake_edge_bits);
		if (gpio_resume_rc != 0) {
			QCC74X_PM_PDS15_LOG_ERR(
				"qcc74x_pds15_resume: GPIO restore rc=%d",
				gpio_resume_rc);
		}
		if (wifi_session.resume_needed) {
			qcc74x_wifi_lowpower_resume(&wifi_session, &cfg);
		}


		key = k_spin_lock(&pm_pds15_ctx.lock);
		pm_pds15_ctx.enter_in_progress = false;
		pm_pds15_ctx.auto_entry_pending = false;
		pm_pds15_ctx.auto_timeout_us = 0U;
		pm_pds15_ctx.auto_entry_deadline_us = 0U;
		pm_pds15_ctx.resume_cleanup_pending = true;
		k_spin_unlock(&pm_pds15_ctx.lock, key);

		qcc74x_pm_diag_record_deep_result(0, elapsed_us, wake_reason,
						  gpio_wake_bits,
						  gpio_wake_edge_bits);
		(void)qcc74x_pm_pds15_auto_expire_if_needed();
		return 0;
	}

	key = k_spin_lock(&pm_pds15_ctx.lock);
	pm_pds15_ctx.auto_entry_pending = false;
	pm_pds15_ctx.auto_timeout_us = 0U;
	pm_pds15_ctx.auto_entry_deadline_us = 0U;
	pm_pds15_ctx.enter_in_progress = false;
	pm_pds15_ctx.resume_cleanup_pending = false;
	pm_pds15_ctx.pds15_enter_committed = false;
	pm_pds15_ctx.timeout_us = 0U;
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	qcc74x_pm_diag_record_deep_result(fw_rc, elapsed_us, wake_reason,
					  gpio_wake_bits, gpio_wake_edge_bits);

	return fw_rc;
}

void qcc74x_pm_pds15_resume_complete(uint8_t substate_id)
{
	k_spinlock_key_t key;
	bool cleanup;

	if (substate_id != QCC74X_PM_PDS15_SUBSTATE_ID) {
		return;
	}

	key = k_spin_lock(&pm_pds15_ctx.lock);
	if (!pm_pds15_ctx.pds15_enter_committed) {
		k_spin_unlock(&pm_pds15_ctx.lock, key);
		return;
	}

	pm_pds15_ctx.pds15_enter_committed = false;
	cleanup = pm_pds15_ctx.resume_cleanup_pending;
	if (cleanup) {
		pm_pds15_ctx.resume_cleanup_pending = false;
		pm_pds15_ctx.auto_entry_pending = false;
		pm_pds15_ctx.auto_timeout_us = 0U;
		pm_pds15_ctx.auto_entry_deadline_us = 0U;
		pm_pds15_ctx.timeout_us = 0U;
	}
	k_spin_unlock(&pm_pds15_ctx.lock, key);

	if (cleanup) {
		QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_resume: session complete");
		gpio_qc7xx_pds15_dispatch_wake();
	}
}
