/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qualcomm_qc7xx_wifi

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_QCC74X_WIFI_LOW_POWER)
#include <zephyr/soc/qualcomm/qcc74x_wifi_lowpower.h>
#endif

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>
#include <zephyr/net/wifi_nm.h>
#endif

#include "qc7xx_wifi_drv_priv.h"

#if defined(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY)
#include "qc7xx_antenna_diversity.h"
#endif

#include "fhost_wpa.h"
#include "net_al.h"
#include "platform_al.h"
#include "soc_wifi_mgmt.h"
#include "wifi_mgmr.h"
#include "wifi_mgmr_pmk.h"
#include "wireless_api.h"
#include "wireless_config.h"
#include "wl_api.h"
#include <wifi_mgmr_ext.h>

LOG_MODULE_REGISTER(qc7xx_wifi, CONFIG_WIFI_LOG_LEVEL);

#if defined(CONFIG_QCC74X_PM_LPFW)
uint16_t qc7xx_lp_beacon_interval_get(void);
uint8_t qc7xx_lp_beacon_dtim_period_get(void);
#endif

#define TX_BUF_ALLOC_TIMEOUT (-1)
#define DHCPV4_MASK (NET_EVENT_IPV4_DHCP_BOUND | NET_EVENT_IPV4_DHCP_STOP)
#define WEP_PASSWORD_BUF_LEN (13 * 2 + 1)
#define QC7XX_MONITOR_DEFAULT_CHANNEL 6U
#define QC7XX_MONITOR_DEFAULT_FILTER WIFI_PACKET_FILTER_MGMT
#define QC7XX_MONITOR_LOG_LIMIT 32U
#define QC7XX_MONITOR_WIFI_MGMT_TYPE 0U
#define QC7XX_MONITOR_WIFI_CTRL_TYPE 1U
#define QC7XX_MONITOR_WIFI_DATA_TYPE 2U
#define QC7XX_WIFI_PS_TIMEOUT_MIN_MS 10U
#define QC7XX_WIFI_PS_TIMEOUT_MAX_MS 80U
#define QC7XX_WIFI_PS_LISTEN_INTERVAL_MAX 20U
#define QC7XX_WIFI_RECONNECT_DELAY_MS 1000U
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
/*
 * Settling time between the STA associating and announcing the SoftAP channel
 * switch. Also debounces a reconnect or roam, as each one reschedules the work.
 */
#define QC7XX_WIFI_AP_FOLLOW_STA_DELAY_MS 500U
/* Beacons to announce before switching, i.e. the delay in TBTTs. */
#define QC7XX_WIFI_AP_CSA_COUNT 10U
#endif
#define QC7XX_TWT_WAKE_DURATION_256_US 256U
#define QC7XX_TWT_WAKE_DURATION_TU_US 1024U

struct net_if *qc7xx_wifi_iface;
struct qc7xx_wifi_runtime qc7xx_data;

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
struct net_if *qc7xx_wifi_iface_ap;
struct qc7xx_wifi_runtime qc7xx_ap_sta_data;

static struct wifi_ap_sta_info qc7xx_ap_sta_cache[NX_REMOTE_STA_MAX];
static bool qc7xx_ap_sta_cache_valid[NX_REMOTE_STA_MAX];
#endif

static struct net_mgmt_event_callback qc7xx_dhcp_cb;
static struct k_work_delayable qc7xx_wifi_reconnect_work;
static struct k_work_delayable qc7xx_wifi_restore_ps_work;
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
static struct k_work_delayable qc7xx_wifi_ap_follow_sta_work;
#endif
static wifi_mgmr_sta_connect_params_t qc7xx_wifi_reconnect_config;
static bool qc7xx_wifi_reconnect_config_valid;
static bool qc7xx_wifi_low_power_reconnecting;
static wifi_conf_t qc7xx_wifi_conf = {
	.country_code = "CN",
};

extern int fhost_tx_start(net_al_if_t net_if, net_al_tx_t net_buf,
			  cb_fhost_tx cfm_cb, void *cfm_cb_arg);

#if defined(CONFIG_QCC74X_WIFI_HIGH_PERFORMANCE) && CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH > 1
extern int fhost_tx_start_batch(net_al_if_t *net_if, net_al_tx_t *net_buf,
				 uint8_t count);

struct qc7xx_wifi_tx_batch_entry {
	net_al_if_t net_if;
	net_al_tx_t net_buf;
};

K_MUTEX_DEFINE(qc7xx_wifi_tx_batch_lock);
static struct qc7xx_wifi_tx_batch_entry
	qc7xx_wifi_tx_batch[CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH];
static unsigned int qc7xx_wifi_tx_batch_count;

static void qc7xx_wifi_tx_batch_flush_locked(void)
{
	net_al_if_t net_if[CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH];
	net_al_tx_t net_buf[CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH];
	unsigned int count = qc7xx_wifi_tx_batch_count;

	for (unsigned int i = 0; i < count; i++) {
		net_if[i] = qc7xx_wifi_tx_batch[i].net_if;
		net_buf[i] = qc7xx_wifi_tx_batch[i].net_buf;
	}
	qc7xx_wifi_tx_batch_count = 0;

	if (count != 0U) {
		(void)fhost_tx_start_batch(net_if, net_buf, count);
	}
}

static void qc7xx_wifi_tx_batch_flush_work(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&qc7xx_wifi_tx_batch_lock, K_FOREVER);
	qc7xx_wifi_tx_batch_flush_locked();
	k_mutex_unlock(&qc7xx_wifi_tx_batch_lock);
}

K_WORK_DELAYABLE_DEFINE(qc7xx_wifi_tx_batch_work, qc7xx_wifi_tx_batch_flush_work);

static bool qc7xx_wifi_tx_batch_eligible(const uint8_t *frame, size_t len)
{
	size_t ip_offset = 14U;
	uint16_t ethertype;

	if (len < 512U) {
		return false;
	}

	ethertype = sys_get_be16(frame + 12);
	if (ethertype == 0x8100U) {
		ethertype = sys_get_be16(frame + 16);
		ip_offset += 4U;
	}

	if (ethertype == 0x0800U) {
		return frame[ip_offset + 9] == 6U || frame[ip_offset + 9] == 17U;
	}
	if (ethertype == 0x86ddU) {
		return frame[ip_offset + 6] == 6U || frame[ip_offset + 6] == 17U;
	}
	return false;
}

static void qc7xx_wifi_tx_batch_purge(net_al_if_t net_if)
{
	unsigned int dst = 0;

	k_mutex_lock(&qc7xx_wifi_tx_batch_lock, K_FOREVER);
	for (unsigned int i = 0; i < qc7xx_wifi_tx_batch_count; i++) {
		if (qc7xx_wifi_tx_batch[i].net_if == net_if) {
			net_al_tx_buf_free(qc7xx_wifi_tx_batch[i].net_buf);
		} else {
			qc7xx_wifi_tx_batch[dst++] = qc7xx_wifi_tx_batch[i];
		}
	}
	qc7xx_wifi_tx_batch_count = dst;
	if (dst == 0U) {
		(void)k_work_cancel_delayable(&qc7xx_wifi_tx_batch_work);
	} else {
		(void)k_work_reschedule(&qc7xx_wifi_tx_batch_work,
					K_MSEC(CONFIG_QCC74X_WIFI_TX_BATCH_FLUSH_MS));
	}
	k_mutex_unlock(&qc7xx_wifi_tx_batch_lock);
}
#endif

#ifdef CONFIG_WIFI_QCC75X
extern int wifi_mgmr_mac_set(uint8_t mac[6]);
#endif

#if defined(CONFIG_SOC_WIFI_DEBUG_PRINT)
extern void mm_sec_keydump(void);
#endif

static struct net_if *qc7xx_wifi_runtime_to_iface(struct qc7xx_wifi_runtime *data)
{
	if (data == &qc7xx_data) {
		return qc7xx_wifi_iface;
	}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	if (data == &qc7xx_ap_sta_data) {
		return qc7xx_wifi_iface_ap;
	}
#endif

	return NULL;
}

static void qc7xx_wifi_ps_defaults_init(struct qc7xx_wifi_runtime *data)
{
	data->ps.enabled = WIFI_PS_DISABLED;
	data->ps.listen_interval = 0U;
	data->ps.wakeup_mode = WIFI_PS_WAKEUP_MODE_DTIM;
	data->ps.mode = WIFI_PS_MODE_LEGACY;
	data->ps.timeout_ms = 0U;
	data->ps.exit_strategy = WIFI_PS_EXIT_CUSTOM_ALGO;
}

#if defined(CONFIG_QCC74X_WIFI_LOW_POWER)
int qcc74x_wifi_lowpower_ps_config_get(
	struct qcc74x_wifi_lowpower_ps_config *config)
{
	if (config == NULL) {
		return -EINVAL;
	}

	config->wakeup_mode = qc7xx_data.ps.wakeup_mode;
	config->listen_interval = qc7xx_data.ps.listen_interval;

	return 0;
}
#endif

static bool qc7xx_wifi_is_sta_runtime(const struct qc7xx_wifi_runtime *data)
{
	return data == &qc7xx_data;
}

static bool qc7xx_wifi_is_sta_connected(const struct qc7xx_wifi_runtime *data)
{
	return qc7xx_wifi_is_sta_runtime(data) && data->state == QC7XX_STA_CONNECTED;
}

static bool qc7xx_wifi_is_ap_runtime(const struct qc7xx_wifi_runtime *data)
{
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	return data == &qc7xx_ap_sta_data;
#else
	/* Without a second net device the AP shares the STA runtime. */
	return data == &qc7xx_data;
#endif
}

static int qc7xx_wifi_ps_fail(struct wifi_ps_params *params,
			      enum wifi_config_ps_param_fail_reason reason, int err)
{
	params->fail_reason = reason;
	return err;
}

static int qc7xx_wifi_twt_fail(struct wifi_twt_params *params,
			       enum wifi_twt_fail_reason reason, int err)
{
	params->fail_reason = reason;
	return err;
}

static uint64_t qc7xx_wifi_twt_interval_us(const struct twt_status_info *flow)
{
	uint64_t interval;

	if (flow->wake_int_exp >= 63U) {
		return WIFI_MAX_TWT_INTERVAL_US;
	}

	interval = (uint64_t)flow->wake_int_mantissa << flow->wake_int_exp;
	return MIN(interval, (uint64_t)WIFI_MAX_TWT_INTERVAL_US);
}

static uint32_t qc7xx_wifi_twt_wake_interval_us(const struct twt_status_info *flow)
{
	uint32_t unit_us = flow->wake_dur_unit ? QC7XX_TWT_WAKE_DURATION_TU_US :
						 QC7XX_TWT_WAKE_DURATION_256_US;

	return (uint32_t)flow->min_twt_wake_dur * unit_us;
}

static void qc7xx_wifi_twt_flow_to_zephyr(const struct twt_status_info *src,
					  struct wifi_twt_flow_info *dst)
{
	*dst = (struct wifi_twt_flow_info){
		.twt_interval = qc7xx_wifi_twt_interval_us(src),
		.dialog_token = src->dialog_token,
		.flow_id = src->flow_id,
		.negotiation_type = WIFI_TWT_INDIVIDUAL,
		.responder = false,
		.trigger = src->trigger != 0U,
		.implicit = src->implicit != 0U,
		.announce = (src->flow_type == 0U),
		.twt_wake_interval = qc7xx_wifi_twt_wake_interval_us(src),
		.twt_wake_ahead_duration = 0U,
	};
}

static uint64_t qc7xx_wifi_twt_interval_from_ind(const struct twt_setup_ind_info *ind)
{
	uint64_t interval;

	if (ind->wake_int_exp >= 63U) {
		return WIFI_MAX_TWT_INTERVAL_US;
	}

	interval = (uint64_t)ind->wake_int_mantissa << ind->wake_int_exp;
	return MIN(interval, (uint64_t)WIFI_MAX_TWT_INTERVAL_US);
}

static uint32_t qc7xx_wifi_twt_wake_interval_from_ind(const struct twt_setup_ind_info *ind)
{
	uint32_t unit_us = ind->wake_dur_unit ? QC7XX_TWT_WAKE_DURATION_TU_US :
						QC7XX_TWT_WAKE_DURATION_256_US;

	return (uint32_t)ind->min_twt_wake_dur * unit_us;
}

static void qc7xx_wifi_raise_twt_setup_event(void)
{
	struct twt_setup_ind_info ind = { 0 };
	struct wifi_twt_params params = { 0 };

	if (qc7xx_wifi_iface == NULL) {
		return;
	}

	if (wifi_mgmr_sta_twt_setup_ind_get(&ind) != 0) {
		LOG_WRN("TWT setup event has no cached indication");
		return;
	}

	if (ind.vif_idx != MGMR_VIF_STA) {
		LOG_WRN("Ignore TWT setup event for fhost VIF %u", ind.vif_idx);
		return;
	}

	params.operation = WIFI_TWT_SETUP;
	params.negotiation_type = WIFI_TWT_INDIVIDUAL;
	params.setup_cmd = (enum wifi_twt_setup_cmd)ind.resp_type;
	params.resp_status = WIFI_TWT_RESP_RECEIVED;
	params.dialog_token = ind.dialog_token;
	params.flow_id = ind.flow_id;
	params.setup.responder = false;
	params.setup.trigger = ind.trigger != 0U;
	params.setup.implicit = ind.implicit != 0U;
	params.setup.announce = (ind.flow_type == 0U);
	params.setup.twt_wake_interval =
		qc7xx_wifi_twt_wake_interval_from_ind(&ind);
	params.setup.twt_interval = qc7xx_wifi_twt_interval_from_ind(&ind);
	params.setup.twt_exponent = ind.wake_int_exp;
	params.setup.twt_mantissa = ind.wake_int_mantissa;

	wifi_mgmt_raise_twt_event(qc7xx_wifi_iface, &params);
}

static int qc7xx_wifi_twt_encode_wake_duration(struct wifi_twt_params *params,
					       twt_setup_params_struct_t *setup)
{
	uint32_t wake_us = params->setup.twt_wake_interval;
	uint32_t dur;

	if (wake_us == 0U) {
		return qc7xx_wifi_twt_fail(params, WIFI_TWT_FAIL_UNSPECIFIED,
					   -EINVAL);
	}

	dur = DIV_ROUND_UP(wake_us, QC7XX_TWT_WAKE_DURATION_256_US);
	if (dur <= UINT8_MAX) {
		setup->wake_dur_unit = false;
		setup->min_twt_wake_dur = (uint8_t)dur;
		return 0;
	}

	dur = DIV_ROUND_UP(wake_us, QC7XX_TWT_WAKE_DURATION_TU_US);
	if (dur <= UINT8_MAX) {
		setup->wake_dur_unit = true;
		setup->min_twt_wake_dur = (uint8_t)dur;
		return 0;
	}

	return qc7xx_wifi_twt_fail(params, WIFI_TWT_FAIL_UNSPECIFIED,
				   -EINVAL);
}

static int qc7xx_wifi_twt_setup(struct wifi_twt_params *params)
{
	twt_setup_params_struct_t setup = { 0 };
	int ret;

	if (params->negotiation_type != WIFI_TWT_INDIVIDUAL) {
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_OPERATION_NOT_SUPPORTED,
					   -ENOTSUP);
	}

	if ((params->setup_cmd != WIFI_TWT_SETUP_CMD_REQUEST) &&
	    (params->setup_cmd != WIFI_TWT_SETUP_CMD_SUGGEST) &&
	    (params->setup_cmd != WIFI_TWT_SETUP_CMD_DEMAND)) {
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_OPERATION_NOT_SUPPORTED,
					   -ENOTSUP);
	}

	if (params->setup.announce || !params->setup.implicit ||
	    params->setup.responder || params->setup.trigger) {
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_OPERATION_NOT_SUPPORTED,
					   -ENOTSUP);
	}

	ret = qc7xx_wifi_twt_encode_wake_duration(params, &setup);
	if (ret != 0) {
		return ret;
	}

	setup.setup_type = (uint8_t)params->setup_cmd;
	setup.flow_type = 1U;
	setup.wake_int_exp = params->setup.twt_exponent;
	setup.wake_int_mantissa = params->setup.twt_mantissa;

	if (wifi_mgmr_sta_twt_setup(&setup) != 0) {
		return qc7xx_wifi_twt_fail(params, WIFI_TWT_FAIL_CMD_EXEC_FAIL,
					   -EIO);
	}

	return 0;
}

static int qc7xx_wifi_twt_teardown(struct wifi_twt_params *params)
{
	twt_teardown_params_struct_t teardown = { 0 };

	if (params->negotiation_type != WIFI_TWT_INDIVIDUAL) {
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_OPERATION_NOT_SUPPORTED,
					   -ENOTSUP);
	}

	if (!params->teardown.teardown_all &&
	    (params->flow_id >= WIFI_MAX_TWT_FLOWS)) {
		return qc7xx_wifi_twt_fail(params, WIFI_TWT_FAIL_INVALID_FLOW_ID,
					   -EINVAL);
	}

	teardown.neg_type = (uint8_t)params->negotiation_type;
	teardown.all_twt = params->teardown.teardown_all ? 1U : 0U;
	teardown.id = params->flow_id;

	if (wifi_mgmr_sta_twt_teardown(&teardown) != 0) {
		params->teardown_status = WIFI_TWT_TEARDOWN_FAILED;
		return qc7xx_wifi_twt_fail(params, WIFI_TWT_FAIL_CMD_EXEC_FAIL,
					   -EIO);
	}

	params->teardown_status = WIFI_TWT_TEARDOWN_SUCCESS;
	return 0;
}

static int qc7xx_wifi_apply_sta_ps(struct qc7xx_wifi_runtime *data,
				   enum wifi_ps enabled)
{
	int ret;

	if (enabled == WIFI_PS_ENABLED) {
		ret = wifi_mgmr_sta_ps_enter();
	} else if (enabled == WIFI_PS_DISABLED) {
		ret = wifi_mgmr_sta_ps_exit();
	} else {
		return -EINVAL;
	}

	if (ret != 0) {
		return -EIO;
	}

	data->ps.enabled = enabled;
	return 0;
}

static struct qc7xx_wifi_runtime *qc7xx_wifi_scan_runtime_get(void)
{
	if (qc7xx_data.scan_cb != NULL) {
		return &qc7xx_data;
	}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	if (qc7xx_ap_sta_data.scan_cb != NULL) {
		return &qc7xx_ap_sta_data;
	}
#endif

	return NULL;
}

static const char *qc7xx_wifi_monitor_itf_get(const struct qc7xx_wifi_runtime *data)
{
	if (data == &qc7xx_data) {
		return "wl0";
	}

	return NULL;
}

static uint16_t qc7xx_wifi_channel_to_freq(uint16_t channel, enum wifi_frequency_bands band)
{
	if (band == WIFI_FREQ_BAND_UNKNOWN) {
		band = channel > 14U ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;
	}

	switch (band) {
	case WIFI_FREQ_BAND_2_4_GHZ:
		return phy_channel_to_freq(PHY_BAND_2G4, channel);
	case WIFI_FREQ_BAND_5_GHZ:
		return phy_channel_to_freq(PHY_BAND_5G, channel);
	default:
		return 0U;
	}
}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
/*
 * The channel the SoftAP is operating on right now.
 *
 * wifi_mgmr_get_ap_info() cannot be used for this: its ucChannel is derived
 * from the requested configuration when the AP starts and is never revisited,
 * so it still reports the original channel after a channel switch. Ask the
 * firmware's VIF instead, which fhost_get_vif_status() reports as its current
 * operating channel.
 *
 * Returns 0 if the channel is unknown, e.g. while the AP is down.
 */
static uint8_t qc7xx_wifi_ap_operating_channel(void)
{
	struct fhost_vif_status status;
	int channel;

	if (fhost_get_vif_status(MGMR_VIF_AP, &status) != 0) {
		return 0U;
	}

	if (status.chan.prim20_freq == 0U) {
		return 0U;
	}

	channel = phy_freq_to_channel(status.chan.band, status.chan.prim20_freq);

	return channel > 0 ? (uint8_t)channel : 0U;
}
#endif

static bool qc7xx_wifi_state_connected(uint8_t state)
{
	return state == QC7XX_STA_CONNECTING || state == QC7XX_STA_CONNECTED ||
	       state == QC7XX_AP_STARTED || state == QC7XX_AP_CONNECTED;
}

static bool qc7xx_wifi_state_active(uint8_t state)
{
	return qc7xx_wifi_state_connected(state) || state == QC7XX_AP_DISCONNECTED;
}

static bool qc7xx_wifi_monitor_frame_allowed(struct qc7xx_wifi_runtime *data,
					     struct qcc74x_frame_info *info)
{
	uint8_t filter = data->monitor_filter;
	uint8_t frame_type;

	if (info->payload == NULL || info->length < sizeof(uint16_t)) {
		return true;
	}

	if (filter == 0U || (filter & WIFI_PACKET_FILTER_ALL) != 0U) {
		return true;
	}

	frame_type = (sys_get_le16(info->payload) >> 2) & 0x3U;
	switch (frame_type) {
	case QC7XX_MONITOR_WIFI_MGMT_TYPE:
		return (filter & WIFI_PACKET_FILTER_MGMT) != 0U;
	case QC7XX_MONITOR_WIFI_CTRL_TYPE:
		return (filter & WIFI_PACKET_FILTER_CTRL) != 0U;
	case QC7XX_MONITOR_WIFI_DATA_TYPE:
		return (filter & WIFI_PACKET_FILTER_DATA) != 0U;
	default:
		return false;
	}
}

static const char *qc7xx_wifi_monitor_frame_type(struct qcc74x_frame_info *info)
{
	uint8_t frame_type;
	uint8_t frame_subtype;

	if (info->payload == NULL || info->length < sizeof(uint16_t)) {
		return "unsupported";
	}

	frame_type = (sys_get_le16(info->payload) >> 2) & 0x3U;
	frame_subtype = (sys_get_le16(info->payload) >> 4) & 0xfU;

	switch (frame_type) {
	case QC7XX_MONITOR_WIFI_MGMT_TYPE:
		switch (frame_subtype) {
		case 4:
			return "probe_req";
		case 5:
			return "probe_resp";
		case 8:
			return "beacon";
		default:
			return "mgmt";
		}
	case QC7XX_MONITOR_WIFI_CTRL_TYPE:
		return "ctrl";
	case QC7XX_MONITOR_WIFI_DATA_TYPE:
		return "data";
	default:
		return "reserved";
	}
}

static void qc7xx_wifi_monitor_cb(struct qcc74x_frame_info *info, void *arg)
{
	struct qc7xx_wifi_runtime *data = arg;

	if (data == NULL || info == NULL) {
		return;
	}

	if (!qc7xx_wifi_monitor_frame_allowed(data, info)) {
		data->monitor_rx_dropped++;
		return;
	}

	data->monitor_rx_count++;
#if defined(CONFIG_NET_STATISTICS_WIFI)
	data->stats.pkts.rx++;
	data->stats.bytes.received += info->length;
#endif

	if (data->monitor_rx_count <= QC7XX_MONITOR_LOG_LIMIT) {
		LOG_INF("Sniffer frame type=%s freq=%u rssi=%d len=%u",
			qc7xx_wifi_monitor_frame_type(info), info->freq, info->rssi, info->length);
	} else if (data->monitor_rx_count == QC7XX_MONITOR_LOG_LIMIT + 1U) {
		LOG_INF("Sniffer frame log limit reached; use wifi packet_filter to adjust capture");
	}
}

static int qc7xx_wifi_monitor_disable(struct qc7xx_wifi_runtime *data)
{
	wifi_mgmr_sniffer_item_t sniffer_item = { 0 };
	const char *itf = qc7xx_wifi_monitor_itf_get(data);

	if (!data->monitor_enabled) {
		return 0;
	}

	if (itf == NULL) {
		return -ENOTSUP;
	}

	sniffer_item.itf = (char *)itf;
	if (wifi_mgmr_sniffer_disable(sniffer_item) != 0) {
		return -EIO;
	}

	data->monitor_enabled = false;
	data->state = QC7XX_STA_STARTED;
	return 0;
}

static int qc7xx_wifi_monitor_enable(struct qc7xx_wifi_runtime *data)
{
	wifi_mgmr_sniffer_item_t sniffer_item = { 0 };
	const char *itf = qc7xx_wifi_monitor_itf_get(data);
	uint16_t channel = data->monitor_channel;
	enum wifi_frequency_bands band = data->monitor_band;
	uint16_t freq;

	if (itf == NULL) {
		return -ENOTSUP;
	}

	if (channel == 0U) {
		channel = QC7XX_MONITOR_DEFAULT_CHANNEL;
		band = WIFI_FREQ_BAND_2_4_GHZ;
	}

	freq = qc7xx_wifi_channel_to_freq(channel, band);
	if (freq == 0U) {
		return -EINVAL;
	}

	if (data->monitor_enabled) {
		int ret = qc7xx_wifi_monitor_disable(data);

		if (ret != 0) {
			return ret;
		}
	}

	sniffer_item.itf = (char *)itf;
	sniffer_item.type = 20U;
	sniffer_item.prim20_freq = freq;
	sniffer_item.cb = qc7xx_wifi_monitor_cb;
	sniffer_item.cb_arg = data;

	if (wifi_mgmr_sniffer_enable(sniffer_item) != 0) {
		return -EIO;
	}

	data->monitor_channel = channel;
	data->monitor_band = band;
	data->monitor_enabled = true;
	data->monitor_rx_count = 0U;
	data->monitor_rx_dropped = 0U;
	data->state = QC7XX_STA_MONITOR;

	LOG_INF("Sniffer enabled on %s channel=%u freq=%u filter=0x%x", itf, channel, freq,
		data->monitor_filter);
	return 0;
}

static enum wifi_security_type qc7xx_wifi_security_to_zephyr(uint8_t soc_security)
{
	switch (soc_security) {
	case WIFI_EVENT_BEACON_IND_AUTH_WPA3_SAE:
		return WIFI_SECURITY_TYPE_SAE;
	case WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK_WPA3_SAE:
		return WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL;
	case WIFI_EVENT_BEACON_IND_AUTH_OPEN:
		return WIFI_SECURITY_TYPE_NONE;
	case WIFI_EVENT_BEACON_IND_AUTH_WEP:
		return WIFI_SECURITY_TYPE_WEP;
	case WIFI_EVENT_BEACON_IND_AUTH_WPA_PSK:
		return WIFI_SECURITY_TYPE_WPA_PSK;
	case WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK:
		return WIFI_SECURITY_TYPE_PSK;
	case WIFI_EVENT_BEACON_IND_AUTH_WPA_WPA2_PSK:
		return WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL;
	default:
		return WIFI_SECURITY_TYPE_UNKNOWN;
	}
}

static enum wifi_link_mode qc7xx_wifi_link_mode_from_phy(int phy_mode)
{
	if (phy_mode == WIFI_MODE_802_11B) {
		return WIFI_1;
	}

	if (phy_mode == (WIFI_MODE_802_11B | WIFI_MODE_802_11G)) {
		return WIFI_3;
	}

	if (phy_mode == (WIFI_MODE_802_11B | WIFI_MODE_802_11G | WIFI_MODE_802_11N_2_4)) {
		return WIFI_4;
	}

	if (phy_mode ==
	    (WIFI_MODE_802_11B | WIFI_MODE_802_11G | WIFI_MODE_802_11N_2_4 |
	     WIFI_MODE_802_11AX_2_4)) {
		return WIFI_6;
	}

	return WIFI_LINK_MODE_UNKNOWN;
}

static int qc7xx_wifi_copy_bytes(char *dst, size_t dst_size, const uint8_t *src, size_t src_len)
{
	if (src == NULL && src_len != 0U) {
		return -EINVAL;
	}

	if (src_len >= dst_size) {
		return -EINVAL;
	}

	if (src_len != 0U) {
		memcpy(dst, src, src_len);
	}

	dst[src_len] = '\0';
	return 0;
}

static void qc7xx_wifi_format_bssid(const uint8_t *bssid, char *buf, size_t buf_size)
{
	if (bssid == NULL || buf == NULL || buf_size < MGMR_BSSID_LEN) {
		return;
	}

	snprintk(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2],
		 bssid[3], bssid[4], bssid[5]);
}

static uint8_t qc7xx_wifi_mfp_to_pmf_cfg(enum wifi_mfp_options mfp, bool encrypted)
{
	switch (mfp) {
	case WIFI_MFP_DISABLE:
		return 0U;
	case WIFI_MFP_OPTIONAL:
		return 1U;
	case WIFI_MFP_REQUIRED:
		return 2U;
	default:
		return encrypted ? 1U : 0U;
	}
}

static enum wifi_mfp_options qc7xx_wifi_pmf_cfg_to_mfp(uint8_t pmf_cfg)
{
	switch (pmf_cfg) {
	case 0U:
		return WIFI_MFP_DISABLE;
	case 1U:
		return WIFI_MFP_OPTIONAL;
	case 2U:
		return WIFI_MFP_REQUIRED;
	default:
		return WIFI_MFP_UNKNOWN;
	}
}

static const char *qc7xx_wifi_security_to_akm(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_WEP:
	case WIFI_SECURITY_TYPE_WEP_OPEN:
	case WIFI_SECURITY_TYPE_WEP_SHARED:
		return "WEP";
	case WIFI_SECURITY_TYPE_WPA_PSK:
		return "WPA";
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_PSK_SHA256:
		return "WPA2";
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_SAE_H2E:
	case WIFI_SECURITY_TYPE_SAE_AUTO:
		return "WPA3";
	case WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL:
		return "WPA2,WPA3";
	default:
		return NULL;
	}
}

static void qc7xx_wifi_bin_to_hex(const uint8_t *in, size_t in_len, char *out)
{
	static const char hex[] = "0123456789ABCDEF";

	for (size_t i = 0; i < in_len; i++) {
		out[i * 2U] = hex[in[i] >> 4];
		out[i * 2U + 1U] = hex[in[i] & 0x0fU];
	}

	out[in_len * 2U] = '\0';
}

static bool qc7xx_wifi_is_hex_string(const char *str)
{
	size_t len = strlen(str);

	if ((len % 2U) != 0U) {
		return false;
	}

	for (size_t i = 0; i < len; i++) {
		char c = str[i];

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		      (c >= 'A' && c <= 'F'))) {
			return false;
		}
	}

	return true;
}

static int qc7xx_wifi_convert_wep_password(const char *in, char *out)
{
	size_t len = strlen(in);

	memset(out, 0, WEP_PASSWORD_BUF_LEN);

	if (len == 5U || len == 13U) {
		qc7xx_wifi_bin_to_hex((const uint8_t *)in, len, out);
		return 0;
	}

	if (len == 10U || len == 26U) {
		if (!qc7xx_wifi_is_hex_string(in)) {
			return -EINVAL;
		}

		memcpy(out, in, len);
		out[len] = '\0';
		return 0;
	}

	return -EINVAL;
}

static void qc7xx_net_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
					 struct net_if *iface)
{
	ARG_UNUSED(cb);

	switch (mgmt_event) {
	case NET_EVENT_IPV4_DHCP_BOUND:
		platform_post_event(EV_WIFI, CODE_WIFI_ON_GOT_IP, 0);
		wifi_mgmt_raise_connect_result_event(iface, 0);
		if (iface == qc7xx_wifi_iface && qc7xx_wifi_low_power_reconnecting) {
			(void)k_work_reschedule(&qc7xx_wifi_restore_ps_work, K_NO_WAIT);
		}
		break;
	default:
		break;
	}
}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE) && defined(CONFIG_NET_DHCPV4_SERVER)
static int qc7xx_wifi_ap_ipv4_config(struct net_if *iface)
{
	struct net_in_addr ap_addr;
	struct net_in_addr netmask;
	struct net_in_addr dhcp_base;
	int ret;

	if (net_addr_pton(NET_AF_INET, CONFIG_QC7XX_WIFI_SOFTAP_IP_ADDRESS, &ap_addr) < 0) {
		LOG_ERR("Invalid CONFIG_QC7XX_WIFI_SOFTAP_IP_ADDRESS");
		return -EINVAL;
	}

	if (net_addr_pton(NET_AF_INET, CONFIG_QC7XX_WIFI_SOFTAP_IP_MASK, &netmask) < 0) {
		LOG_ERR("Invalid CONFIG_QC7XX_WIFI_SOFTAP_IP_MASK");
		return -EINVAL;
	}

	if (net_addr_pton(NET_AF_INET, CONFIG_QC7XX_WIFI_SOFTAP_IP_BASE, &dhcp_base) < 0) {
		LOG_ERR("Invalid CONFIG_QC7XX_WIFI_SOFTAP_IP_BASE");
		return -EINVAL;
	}

	if (net_if_ipv4_addr_add(iface, &ap_addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_ERR("Failed to add SoftAP IPv4 address");
		return -EIO;
	}

	net_if_ipv4_set_gw(iface, &ap_addr);
	if (!net_if_ipv4_set_netmask_by_addr(iface, &ap_addr, &netmask)) {
		LOG_ERR("Failed to set SoftAP IPv4 netmask");
		net_if_ipv4_addr_rm(iface, &ap_addr);
		return -EIO;
	}

	ret = net_dhcpv4_server_start(iface, &dhcp_base);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to start SoftAP DHCPv4 server: %d", ret);
		net_if_ipv4_addr_rm(iface, &ap_addr);
		return -EIO;
	}

	return 0;
}

static void qc7xx_wifi_ap_ipv4_unconfig(struct net_if *iface)
{
	struct net_in_addr ap_addr;

	(void)net_dhcpv4_server_stop(iface);

	if (net_addr_pton(NET_AF_INET, CONFIG_QC7XX_WIFI_SOFTAP_IP_ADDRESS, &ap_addr) == 0) {
		net_if_ipv4_addr_rm(iface, &ap_addr);
	}
}
#endif /* CONFIG_QC7XX_WIFI_AP_STA_MODE && CONFIG_NET_DHCPV4_SERVER */

static int qc7xx_wifi_send(const struct device *dev, struct net_pkt *pkt)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	struct net_if *iface = qc7xx_wifi_runtime_to_iface(data);
#if defined(CONFIG_NET_STATISTICS_WIFI)
	struct net_eth_hdr *hdr = NET_ETH_HDR(pkt);
#endif
	int res;
	int pkt_len;
	void *net_buf = NULL;

	if (iface == NULL) {
		return -EIO;
	}

	if (data == &qc7xx_data && data->state != QC7XX_STA_CONNECTED) {
		return -EIO;
	}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	if (data == &qc7xx_ap_sta_data && data->state != QC7XX_AP_STARTED &&
	    data->state != QC7XX_AP_CONNECTED) {
		return -EIO;
	}
#endif

	pkt_len = net_pkt_get_len(pkt);

	net_buf = net_al_tx_buf_alloc(TX_BUF_ALLOC_TIMEOUT);
	if (net_buf == NULL) {
		k_sleep(K_MSEC(1));
		return -ENOMEM;
	}

	net_al_tx_buf_set_len(net_buf, pkt_len);

	res = net_pkt_read(pkt, net_al_tx_buf_data(net_buf), pkt_len);
	if (res < 0) {
		res = -EIO;
		goto out;
	}

#if defined(CONFIG_QCC74X_WIFI_HIGH_PERFORMANCE) && CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH > 1
	k_mutex_lock(&qc7xx_wifi_tx_batch_lock, K_FOREVER);
	if ((data == &qc7xx_data && data->state != QC7XX_STA_CONNECTED)
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	    || (data == &qc7xx_ap_sta_data && data->state != QC7XX_AP_STARTED &&
		data->state != QC7XX_AP_CONNECTED)
#endif
	) {
		res = -ENOTCONN;
	} else if (qc7xx_wifi_tx_batch_eligible(net_al_tx_buf_data(net_buf), pkt_len)) {
		unsigned int index = qc7xx_wifi_tx_batch_count++;

		qc7xx_wifi_tx_batch[index].net_if = (net_al_if_t)iface;
		qc7xx_wifi_tx_batch[index].net_buf = net_buf;
		if (qc7xx_wifi_tx_batch_count == CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH) {
			qc7xx_wifi_tx_batch_flush_locked();
		} else if (index == 0U) {
			(void)k_work_reschedule(&qc7xx_wifi_tx_batch_work,
						K_MSEC(CONFIG_QCC74X_WIFI_TX_BATCH_FLUSH_MS));
		}
		res = 0;
	} else {
		qc7xx_wifi_tx_batch_flush_locked();
		res = fhost_tx_start((net_al_if_t)iface, net_buf, NULL, NULL);
	}
	k_mutex_unlock(&qc7xx_wifi_tx_batch_lock);
#else
	res = fhost_tx_start((net_al_if_t)iface, net_buf, NULL, NULL);
#endif
	if (res < 0) {
		/* A submitted buffer is released by FHOST, including on rejection. */
		goto out;
	}
	net_buf = NULL;

#if defined(CONFIG_NET_STATISTICS_WIFI)
	data->stats.bytes.sent += pkt_len;
	data->stats.pkts.tx++;

	if (net_eth_is_addr_broadcast(&hdr->dst)) {
		data->stats.broadcast.tx++;
	} else if (net_eth_is_addr_multicast(&hdr->dst)) {
		data->stats.multicast.tx++;
	} else {
		data->stats.unicast.tx++;
	}
#endif
	return 0;

	out:
	if (net_buf != NULL) {
		net_al_tx_buf_free(net_buf);
	}

#if defined(CONFIG_NET_STATISTICS_WIFI)
	data->stats.errors.tx++;
#endif
	return res;
}
static int qc7xx_wifi_disconnect(const struct device *dev)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	uint8_t previous_state = data->state;
	bool disconnecting = data == &qc7xx_data &&
		(previous_state == QC7XX_STA_CONNECTING || previous_state == QC7XX_STA_CONNECTED);

	if (data == &qc7xx_data) {
		qc7xx_wifi_low_power_reconnecting = false;
		qc7xx_wifi_reconnect_config_valid = false;
		(void)k_work_cancel_delayable(&qc7xx_wifi_reconnect_work);
		(void)k_work_cancel_delayable(&qc7xx_wifi_restore_ps_work);
		if (IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT) &&
		    wifi_mgmr_sta_autoconnect_disable() != 0) {
			LOG_WRN("Failed to disable Wi-Fi auto reconnect during disconnect");
		}
	}

	if (disconnecting) {
		data->state = QC7XX_STA_DISCONNECTING;
		data->conn_requested = false;
#if defined(CONFIG_QCC74X_WIFI_HIGH_PERFORMANCE) && CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH > 1
		qc7xx_wifi_tx_batch_purge((net_al_if_t)qc7xx_wifi_iface);
#endif
	}

	if (wifi_sta_disconnect() != 0) {
		if (disconnecting) {
			data->state = previous_state;
		}
		return -EAGAIN;
	}
	if (previous_state == QC7XX_STA_CONNECTING && data->state == QC7XX_STA_DISCONNECTING) {
		data->state = QC7XX_STA_STARTED;
		wifi_mgmt_raise_disconnect_result_event(qc7xx_wifi_iface, 0);
	}

	return 0;
}
static void qc7xx_wifi_reconnect_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	if (!qc7xx_wifi_low_power_reconnecting ||
	    !qc7xx_wifi_reconnect_config_valid ||
	    qc7xx_data.state == QC7XX_STA_CONNECTED) {
		return;
	}

	qc7xx_data.state = QC7XX_STA_CONNECTING;
	ret = wifi_mgmr_sta_connect(&qc7xx_wifi_reconnect_config);
	if (ret != 0) {
		qc7xx_data.state = QC7XX_STA_STARTED;
		(void)k_work_reschedule(&qc7xx_wifi_reconnect_work,
					K_MSEC(QC7XX_WIFI_RECONNECT_DELAY_MS));
	}
}

static void qc7xx_wifi_restore_ps_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!qc7xx_wifi_low_power_reconnecting ||
	    qc7xx_data.state != QC7XX_STA_CONNECTED) {
		return;
	}

	if (qc7xx_wifi_apply_sta_ps(&qc7xx_data, WIFI_PS_ENABLED) != 0) {
		LOG_WRN("Failed to restore Wi-Fi power save after DHCP; retrying");
		(void)k_work_reschedule(&qc7xx_wifi_restore_ps_work,
					K_MSEC(QC7XX_WIFI_RECONNECT_DELAY_MS));
		return;
	}

	qc7xx_wifi_low_power_reconnecting = false;
}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
/*
 * Align the SoftAP with the STA channel once the STA associates.
 *
 * wifi_mgmr_ap_start() already follows the STA channel, but only when the STA is
 * connected at the time the AP comes up. Starting the AP first leaves the two on
 * separate channels and the firmware time-shares between them (MCC), with no
 * corrective path of its own, so announce a channel switch here.
 *
 * Runs on the system work queue rather than from the event handler, as
 * fhost_wpa_execute_cmd() blocks until the supplicant has run the command and
 * the handler's work queue also carries the remaining Wi-Fi events.
 */
static void qc7xx_wifi_ap_follow_sta_work_handler(struct k_work *work)
{
	int sta_channel = 0;
	uint8_t ap_channel;
	uint16_t freq;
	int ret;

	ARG_UNUSED(work);

	if (qc7xx_data.state != QC7XX_STA_CONNECTED) {
		return;
	}

	if (qc7xx_ap_sta_data.state != QC7XX_AP_STARTED &&
	    qc7xx_ap_sta_data.state != QC7XX_AP_CONNECTED &&
	    qc7xx_ap_sta_data.state != QC7XX_AP_DISCONNECTED) {
		return;
	}

	if (wifi_mgmr_sta_channel_get(&sta_channel) != 0 || sta_channel <= 0) {
		LOG_WRN("No STA channel to align the SoftAP with");
		return;
	}

	/*
	 * Must be the operating channel rather than the configured one, so that
	 * a reconnect onto the channel the AP already follows does not announce
	 * a second, redundant switch.
	 */
	ap_channel = qc7xx_wifi_ap_operating_channel();
	if (ap_channel == 0U) {
		LOG_WRN("No SoftAP channel to compare against");
		return;
	}

	if ((int)ap_channel == sta_channel) {
		return;
	}

	/*
	 * A CSA moves the AP within its band; it cannot move it across bands,
	 * and associated stations need not support the target band at all.
	 */
	if ((sta_channel > 14) != (ap_channel > 14U)) {
		LOG_WRN("Not moving the SoftAP from channel %u to STA channel %d: "
			"different band",
			ap_channel, sta_channel);
		return;
	}

	freq = qc7xx_wifi_channel_to_freq((uint16_t)sta_channel, WIFI_FREQ_BAND_UNKNOWN);
	if (freq == 0U) {
		LOG_WRN("No frequency for STA channel %d", sta_channel);
		return;
	}

	/* hostapd parses "CHAN_SWITCH <cs_count> <freq>", in MHz. */
	ret = fhost_wpa_execute_cmd(MGMR_VIF_AP, NULL, NULL, 2000, "CHAN_SWITCH %u %u",
				    QC7XX_WIFI_AP_CSA_COUNT, (unsigned int)freq);
	if (ret != 0) {
		LOG_WRN("Failed to move the SoftAP from channel %u to %d: %d", ap_channel,
			sta_channel, ret);
		return;
	}

	LOG_INF("SoftAP following STA to channel %d (%u MHz)", sta_channel, freq);
}
#endif

static int qc7xx_wifi_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	wifi_mgmr_sta_connect_params_t wifi_config;
	const char *akm;
	char bssid_str[MGMR_BSSID_LEN] = { 0 };
	char wep_key[WEP_PASSWORD_BUF_LEN] = { 0 };
	bool encrypted = true;
	uint8_t pmf_cfg;
	int ret;

	if (data != &qc7xx_data) {
		return -ENOTSUP;
	}

	if (data->monitor_enabled) {
		return -EBUSY;
	}

	if (data->state == QC7XX_STA_CONNECTING || data->state == QC7XX_STA_CONNECTED) {
		return -EALREADY;
	}

	qc7xx_wifi_low_power_reconnecting = false;
	(void)k_work_cancel_delayable(&qc7xx_wifi_reconnect_work);
	(void)k_work_cancel_delayable(&qc7xx_wifi_restore_ps_work);

	memset(&wifi_config, 0, sizeof(wifi_config));

	ret = qc7xx_wifi_copy_bytes(data->status.ssid, sizeof(data->status.ssid), params->ssid,
				    params->ssid_length);
	if (ret < 0) {
		return ret;
	}

	ret = qc7xx_wifi_copy_bytes(wifi_config.ssid, sizeof(wifi_config.ssid), params->ssid,
				    params->ssid_length);
	if (ret < 0) {
		return ret;
	}

	wifi_config.ssid_len = params->ssid_length;

	akm = qc7xx_wifi_security_to_akm(params->security);
	switch (params->security) {
	case WIFI_SECURITY_TYPE_NONE:
		encrypted = false;
		data->status.security = WIFI_SECURITY_TYPE_NONE;
		break;
	case WIFI_SECURITY_TYPE_WEP:
	case WIFI_SECURITY_TYPE_WEP_OPEN:
	case WIFI_SECURITY_TYPE_WEP_SHARED:
		if (qc7xx_wifi_copy_bytes(data->status.key, sizeof(data->status.key), params->psk,
					  params->psk_length) < 0) {
			return -EINVAL;
		}

		if (qc7xx_wifi_convert_wep_password(data->status.key, wep_key) < 0) {
			return -EINVAL;
		}

		if (qc7xx_wifi_copy_bytes(wifi_config.key, sizeof(wifi_config.key),
					  (const uint8_t *)wep_key, strlen(wep_key)) < 0) {
			return -EINVAL;
		}

		wifi_config.key_len = strlen(wifi_config.key);
		data->status.security = WIFI_SECURITY_TYPE_WEP;
		break;
	case WIFI_SECURITY_TYPE_WPA_PSK:
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_PSK_SHA256:
	case WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL:
		if (qc7xx_wifi_copy_bytes(data->status.key, sizeof(data->status.key), params->psk,
					  params->psk_length) < 0) {
			return -EINVAL;
		}

		if (qc7xx_wifi_copy_bytes(wifi_config.key, sizeof(wifi_config.key), params->psk,
					  params->psk_length) < 0) {
			return -EINVAL;
		}

		wifi_config.key_len = params->psk_length;
		wifi_config.wpa_flags = FHOST_WPA_SSID_PREFER_WPA2_TO_WPA3;
		data->status.security = params->security;
		break;
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_SAE_H2E:
	case WIFI_SECURITY_TYPE_SAE_AUTO: {
		const uint8_t *password = params->sae_password != NULL ? params->sae_password :
									 params->psk;
		size_t password_len = params->sae_password != NULL ? params->sae_password_length :
									 params->psk_length;

		if (qc7xx_wifi_copy_bytes(data->status.key, sizeof(data->status.key), password,
					  password_len) < 0) {
			return -EINVAL;
		}

		if (qc7xx_wifi_copy_bytes(wifi_config.key, sizeof(wifi_config.key), password,
					  password_len) < 0) {
			return -EINVAL;
		}

		wifi_config.key_len = password_len;
		data->status.security = WIFI_SECURITY_TYPE_SAE;
		break;
	}
	default:
		LOG_ERR("Unsupported security type: %d", params->security);
		return -EINVAL;
	}

	if (akm != NULL) {
		ret = qc7xx_wifi_copy_bytes(wifi_config.akm_str, sizeof(wifi_config.akm_str),
					    (const uint8_t *)akm, strlen(akm));
		if (ret < 0) {
			return ret;
		}

		wifi_config.akm_len = strlen(wifi_config.akm_str);
	}

	if (!memcmp(params->bssid, (uint8_t[WIFI_MAC_ADDR_LEN]){ 0 }, WIFI_MAC_ADDR_LEN)) {
		bssid_str[0] = '\0';
	} else {
		qc7xx_wifi_format_bssid(params->bssid, bssid_str, sizeof(bssid_str));
		ret = qc7xx_wifi_copy_bytes(wifi_config.bssid_str, sizeof(wifi_config.bssid_str),
					    (const uint8_t *)bssid_str, strlen(bssid_str));
		if (ret < 0) {
			return ret;
		}
	}

	pmf_cfg = qc7xx_wifi_mfp_to_pmf_cfg(params->mfp, encrypted);

	wifi_config.pmf_cfg = pmf_cfg;
	data->status.mfp = qc7xx_wifi_pmf_cfg_to_mfp(pmf_cfg);
	wifi_config.use_dhcp = 0;
	wifi_config.quick_connect = 0;
	if (data->ps.wakeup_mode == WIFI_PS_WAKEUP_MODE_LISTEN_INTERVAL) {
		if (data->ps.listen_interval == 0U) {
			return -EINVAL;
		}

		ret = wifi_mgmr_sta_set_listen_itv(data->ps.listen_interval);
		if (ret != 0) {
			return -EIO;
		}

		wifi_config.listen_interval = data->ps.listen_interval;
	} else {
		ret = wifi_mgmr_sta_set_listen_itv(0U);
		if (ret != 0) {
			return -EIO;
		}

		wifi_config.listen_interval = 0U;
	}

	if (params->timeout > 0) {
		wifi_config.timeout_ms = params->timeout * MSEC_PER_SEC;
	}

	data->status.channel = params->channel == WIFI_CHANNEL_ANY ? 0U : params->channel;
	if (params->channel == WIFI_CHANNEL_ANY) {
		wifi_config.freq1 = phy_channel_to_freq(PHY_BAND_2G4, 0);
		wifi_config.freq2 = wifi_config.freq1;
	} else {
		uint8_t band = params->channel > 14U ? PHY_BAND_5G : PHY_BAND_2G4;

		wifi_config.freq1 = phy_channel_to_freq(band, params->channel);
		wifi_config.freq2 = 0U;
	}

	data->state = QC7XX_STA_CONNECTING;
	data->status.connected = false;
	data->conn_requested = true;

	if (IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT)) {
		ret = wifi_mgmr_sta_autoconnect_enable();
		if (ret != 0) {
			data->state = QC7XX_STA_STARTED;
			data->status.mfp = WIFI_MFP_DISABLE;
			data->conn_requested = false;
			return -EIO;
		}
	}

	ret = wifi_mgmr_sta_connect(&wifi_config);
	if (ret != 0) {
		data->state = QC7XX_STA_STARTED;
		data->status.mfp = WIFI_MFP_DISABLE;
		data->conn_requested = false;
		return -EIO;
	}

	qc7xx_wifi_reconnect_config = wifi_config;
	qc7xx_wifi_reconnect_config_valid = true;

	return 0;
}

static void qc7xx_wifi_scan_item_cb(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
	struct net_if *iface = env;
	struct qc7xx_wifi_runtime *data = arg;
	struct wifi_scan_result res = { 0 };
	size_t ssid_len;

	if (data == NULL || data->scan_cb == NULL || item == NULL) {
		return;
	}

	ssid_len = strnlen(item->ssid, WIFI_SSID_MAX_LEN);
	res.ssid_length = ssid_len;
	if (ssid_len != 0U) {
		memcpy(res.ssid, item->ssid, ssid_len);
	}
	res.ssid[ssid_len] = '\0';
	res.rssi = item->rssi;
	res.channel = item->channel;
	res.band = item->channel > 14U ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;
	res.security = qc7xx_wifi_security_to_zephyr(item->auth);
	memcpy(res.mac, item->bssid, WIFI_MAC_ADDR_LEN);
	res.mac_length = WIFI_MAC_ADDR_LEN;

	data->scan_cb(iface, 0, &res);

	/* Ensure notifications get delivered promptly. */
	k_yield();
}

static int qc7xx_wifi_scan_complete(void)
{
	struct qc7xx_wifi_runtime *data = qc7xx_wifi_scan_runtime_get();
	struct net_if *iface;

	if (data == NULL || data->scan_cb == NULL) {
		return 0;
	}

	iface = qc7xx_wifi_runtime_to_iface(data);
	if (iface == NULL) {
		data->scan_cb = NULL;
		return -EIO;
	}

	(void)wifi_mgmr_scan_ap_all(iface, data, qc7xx_wifi_scan_item_cb);
	data->scan_cb(iface, 0, NULL);
	data->scan_cb = NULL;

	if (data == &qc7xx_data && data->state == QC7XX_STA_STARTED) {
		data->status.connected = false;
	}

	return 0;
}

static int qc7xx_wifi_scan(const struct device *dev, struct wifi_scan_params *params,
			   scan_result_cb_t cb)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	wifi_mgmr_scan_params_t config;

	if (data != &qc7xx_data) {
		return -ENOTSUP;
	}

	if (data->monitor_enabled) {
		return -EBUSY;
	}

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	memset(&config, 0, sizeof(config));
	data->scan_cb = cb;

	if (params != NULL) {
		size_t chan_count = 0U;

		config.passive = (params->scan_type == WIFI_SCAN_TYPE_PASSIVE);
		config.duration = config.passive ? params->dwell_time_passive :
						params->dwell_time_active;

		if (params->ssids[0] != NULL) {
			size_t ssid_len = strnlen(params->ssids[0], MGMR_SSID_LEN);

			memcpy(config.ssid_array, params->ssids[0], ssid_len);
			config.ssid_length = ssid_len;
		}

		for (size_t i = 0; i < ARRAY_SIZE(params->band_chan) &&
				   chan_count < ARRAY_SIZE(config.channels);
		     i++) {
			if (params->band_chan[i].channel == 0U) {
				continue;
			}

			config.channels[chan_count++] = params->band_chan[i].channel;
		}

		config.channels_cnt = chan_count;
	}

	if (wifi_mgmr_sta_scan(&config) != 0) {
		data->scan_cb = NULL;
		return -EAGAIN;
	}

	if (data->state == QC7XX_STA_STOPPED) {
		data->state = QC7XX_STA_STARTED;
	}

	return 0;
}

static int qc7xx_wifi_set_power_save(const struct device *dev, struct wifi_ps_params *params)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	int ret;

	if (!qc7xx_wifi_is_sta_runtime(data)) {
		return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_OPERATION_NOT_SUPPORTED,
					  -ENOTSUP);
	}

	switch (params->type) {
	case WIFI_PS_PARAM_STATE:
		if (!qc7xx_wifi_is_sta_connected(data)) {
			return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_DEVICE_NOT_CONNECTED,
						  -ENOTCONN);
		}
		if (qc7xx_wifi_low_power_reconnecting && params->enabled == WIFI_PS_ENABLED) {
			return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_UNSPECIFIED,
						  -EBUSY);
		}
		if (qc7xx_wifi_low_power_reconnecting && params->enabled == WIFI_PS_DISABLED) {
			qc7xx_wifi_low_power_reconnecting = false;
			(void)k_work_cancel_delayable(&qc7xx_wifi_restore_ps_work);
		}

		if (IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT) &&
		    params->enabled == WIFI_PS_ENABLED) {
			ret = wifi_mgmr_sta_autoconnect_disable();
			if (ret != 0) {
				return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_CMD_EXEC_FAIL,
							  -EIO);
			}
		}

		ret = qc7xx_wifi_apply_sta_ps(data, params->enabled);
		if (ret == 0 && IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT) &&
		    params->enabled == WIFI_PS_DISABLED) {
			ret = wifi_mgmr_sta_autoconnect_enable();
		}
		if (ret != 0 && IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT) &&
		    params->enabled == WIFI_PS_ENABLED) {
			(void)wifi_mgmr_sta_autoconnect_enable();
		}
		if (ret == -EINVAL) {
			return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_UNSPECIFIED,
						  -EINVAL);
		}
		if (ret != 0) {
			return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_CMD_EXEC_FAIL,
						  ret);
		}

		return 0;
	case WIFI_PS_PARAM_WAKEUP_MODE:
		if (params->wakeup_mode == WIFI_PS_WAKEUP_MODE_DTIM) {
			ret = wifi_mgmr_sta_set_listen_itv(0U);
			if (ret != 0) {
				return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_CMD_EXEC_FAIL,
							  -EIO);
			}

			data->ps.wakeup_mode = WIFI_PS_WAKEUP_MODE_DTIM;
			return 0;
		}

		if (params->wakeup_mode == WIFI_PS_WAKEUP_MODE_LISTEN_INTERVAL) {
			if ((data->ps.listen_interval == 0U) ||
			    (data->ps.listen_interval > QC7XX_WIFI_PS_LISTEN_INTERVAL_MAX)) {
				return qc7xx_wifi_ps_fail(
					params,
					WIFI_PS_PARAM_LISTEN_INTERVAL_RANGE_INVALID,
					-EINVAL);
			}

			ret = wifi_mgmr_sta_set_listen_itv(data->ps.listen_interval);
			if (ret != 0) {
				return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_CMD_EXEC_FAIL,
							  -EIO);
			}

			data->ps.wakeup_mode = WIFI_PS_WAKEUP_MODE_LISTEN_INTERVAL;
			return 0;
		}

		return qc7xx_wifi_ps_fail(params,
					  WIFI_PS_PARAM_FAIL_OPERATION_NOT_SUPPORTED,
					  -ENOTSUP);
	case WIFI_PS_PARAM_LISTEN_INTERVAL:
		if ((params->listen_interval == 0U) ||
		    (params->listen_interval > QC7XX_WIFI_PS_LISTEN_INTERVAL_MAX)) {
			return qc7xx_wifi_ps_fail(params,
						  WIFI_PS_PARAM_LISTEN_INTERVAL_RANGE_INVALID,
						  -EINVAL);
		}

		data->ps.listen_interval = params->listen_interval;
		if (data->ps.wakeup_mode == WIFI_PS_WAKEUP_MODE_LISTEN_INTERVAL) {
			ret = wifi_mgmr_sta_set_listen_itv(data->ps.listen_interval);
			if (ret != 0) {
				return qc7xx_wifi_ps_fail(params,
							  WIFI_PS_PARAM_FAIL_CMD_EXEC_FAIL,
							  -EIO);
			}
		}

		return 0;
	case WIFI_PS_PARAM_TIMEOUT:
		if (params->timeout_ms < QC7XX_WIFI_PS_TIMEOUT_MIN_MS ||
		    params->timeout_ms > QC7XX_WIFI_PS_TIMEOUT_MAX_MS) {
			return qc7xx_wifi_ps_fail(params,
						  WIFI_PS_PARAM_FAIL_OPERATION_NOT_SUPPORTED,
						  -ENOTSUP);
		}

		ret = wifi_mgmr_sta_ps_active_time(params->timeout_ms);
		if (ret != 0) {
			return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_CMD_EXEC_FAIL,
						  -EIO);
		}

		data->ps.timeout_ms = params->timeout_ms;
		return 0;
	case WIFI_PS_PARAM_MODE:
		if (params->mode != WIFI_PS_MODE_LEGACY) {
			return qc7xx_wifi_ps_fail(params,
						  WIFI_PS_PARAM_FAIL_OPERATION_NOT_SUPPORTED,
						  -ENOTSUP);
		}

		data->ps.mode = WIFI_PS_MODE_LEGACY;
		return 0;
	case WIFI_PS_PARAM_EXIT_STRATEGY:
		if (params->exit_strategy != WIFI_PS_EXIT_CUSTOM_ALGO) {
			return qc7xx_wifi_ps_fail(params,
						  WIFI_PS_PARAM_FAIL_OPERATION_NOT_SUPPORTED,
						  -ENOTSUP);
		}

		data->ps.exit_strategy = WIFI_PS_EXIT_CUSTOM_ALGO;
		return 0;
	default:
		return qc7xx_wifi_ps_fail(params, WIFI_PS_PARAM_FAIL_OPERATION_NOT_SUPPORTED,
					  -ENOTSUP);
	}
}

static int qc7xx_wifi_get_power_save_config(const struct device *dev, struct wifi_ps_config *config)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	struct twt_status_info twt_status[WIFI_MAX_TWT_FLOWS];
	uint8_t twt_num = 0U;

	if (!qc7xx_wifi_is_sta_runtime(data)) {
		return -ENOTSUP;
	}

	memset(config, 0, sizeof(*config));
	config->ps_params.enabled = qc7xx_wifi_is_sta_connected(data) ? data->ps.enabled :
								 WIFI_PS_DISABLED;
	config->ps_params.listen_interval = data->ps.listen_interval;
	config->ps_params.wakeup_mode = data->ps.wakeup_mode;
	config->ps_params.mode = data->ps.mode;
	config->ps_params.timeout_ms = data->ps.timeout_ms;
	config->ps_params.exit_strategy = data->ps.exit_strategy;

	if (qc7xx_wifi_is_sta_connected(data) &&
	    (wifi_mgmr_sta_twt_statusget(twt_status, &twt_num) == 0)) {
		config->num_twt_flows = MIN(twt_num, WIFI_MAX_TWT_FLOWS);
		for (int i = 0; i < config->num_twt_flows; i++) {
			qc7xx_wifi_twt_flow_to_zephyr(&twt_status[i],
						      &config->twt_flows[i]);
		}
	}

	return 0;
}

static int qc7xx_wifi_set_twt(const struct device *dev,
			      struct wifi_twt_params *params)
{
	struct qc7xx_wifi_runtime *data = dev->data;

	if (!qc7xx_wifi_is_sta_runtime(data)) {
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_OPERATION_NOT_SUPPORTED,
					   -ENOTSUP);
	}

	if (!qc7xx_wifi_is_sta_connected(data)) {
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_DEVICE_NOT_CONNECTED,
					   -ENOTCONN);
	}

	switch (params->operation) {
	case WIFI_TWT_SETUP:
		return qc7xx_wifi_twt_setup(params);
	case WIFI_TWT_TEARDOWN:
		return qc7xx_wifi_twt_teardown(params);
	default:
		return qc7xx_wifi_twt_fail(params,
					   WIFI_TWT_FAIL_OPERATION_NOT_SUPPORTED,
					   -ENOTSUP);
	}
}

static void qc7xx_sta_wifi_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct qc7xx_wifi_runtime *dev_data = dev->data;
	struct ethernet_context *eth_ctx = net_if_l2_data(iface);

	eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;
	qc7xx_wifi_ps_defaults_init(dev_data);

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	{
		struct wifi_nm_instance *nm = wifi_nm_get_instance("qc7xx_wifi_nm");

		if (dev_data == &qc7xx_data) {
			qc7xx_wifi_iface = iface;
			dev_data->state = QC7XX_STA_STOPPED;
			wifi_mgmr_sta_mac_get(dev_data->mac_addr);
			net_if_set_link_addr(iface, dev_data->mac_addr, WIFI_MAC_ADDR_LEN,
					     NET_LINK_ETHERNET);
			wifi_nm_register_mgd_type_iface(nm, WIFI_TYPE_STA, qc7xx_wifi_iface);
		} else if (dev_data == &qc7xx_ap_sta_data) {
			qc7xx_wifi_iface_ap = iface;
			dev_data->state = QC7XX_AP_STOPPED;
			wifi_mgmr_ap_mac_get(dev_data->mac_addr);
			net_if_set_link_addr(iface, dev_data->mac_addr, WIFI_MAC_ADDR_LEN,
					     NET_LINK_ETHERNET);
			wifi_nm_register_mgd_type_iface(nm, WIFI_TYPE_SAP, qc7xx_wifi_iface_ap);
		}
	}
#else
	qc7xx_wifi_iface = iface;
	dev_data->state = QC7XX_STA_STOPPED;
	wifi_mgmr_sta_mac_get(dev_data->mac_addr);
	net_if_set_link_addr(iface, dev_data->mac_addr, WIFI_MAC_ADDR_LEN, NET_LINK_ETHERNET);
#endif

	ethernet_init(iface);
	net_if_carrier_off(iface);
}

int qc7xx_wifi_dev_init(const struct device *dev)
{
	int ret;

	ARG_UNUSED(dev);
	k_work_init_delayable(&qc7xx_wifi_reconnect_work, qc7xx_wifi_reconnect_work_handler);
	k_work_init_delayable(&qc7xx_wifi_restore_ps_work, qc7xx_wifi_restore_ps_work_handler);
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	k_work_init_delayable(&qc7xx_wifi_ap_follow_sta_work,
			      qc7xx_wifi_ap_follow_sta_work_handler);
#endif

	net_al_tx_buf_init();

#if defined(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY)
	ret = qc7xx_antenna_diversity_init();
	if (ret != 0) {
		return ret;
	}
#endif

	ret = soc_wifi_init();
	if (ret != 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_QC7XX_WIFI_STA_AUTO_DHCPV4)) {
		net_mgmt_init_event_callback(&qc7xx_dhcp_cb, qc7xx_net_mgmt_event_handler,
					     DHCPV4_MASK);
		net_mgmt_add_event_callback(&qc7xx_dhcp_cb);
	}

	return 0;
}

static int qc7xx_wifi_ap_enable(const struct device *dev, struct wifi_connect_req_params *params)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	wifi_mgmr_ap_params_t config;
	char ssid[WIFI_SSID_MAX_LEN + 1] = { 0 };
	char key[WIFI_PSK_MAX_LEN + 1] = { 0 };
	const char *akm = qc7xx_wifi_security_to_akm(params->security);
	const uint8_t *password = params->psk;
	size_t password_len = params->psk_length;
	bool secure_ap = true;

	if (qc7xx_data.monitor_enabled) {
		return -EBUSY;
	}

	memset(&config, 0, sizeof(config));

	if (qc7xx_wifi_copy_bytes(ssid, sizeof(ssid), params->ssid, params->ssid_length) < 0) {
		return -EINVAL;
	}

	if (qc7xx_wifi_copy_bytes(data->status.ssid, sizeof(data->status.ssid), params->ssid,
				  params->ssid_length) < 0) {
		return -EINVAL;
	}

	config.ssid = ssid;
	data->status.channel = params->channel == WIFI_CHANNEL_ANY ? 6U : params->channel;

	switch (params->security) {
	case WIFI_SECURITY_TYPE_NONE:
		config.key = NULL;
		secure_ap = false;
		data->status.security = WIFI_SECURITY_TYPE_NONE;
		break;
	case WIFI_SECURITY_TYPE_WPA_PSK:
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_PSK_SHA256:
	case WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL:
		break;
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_SAE_H2E:
	case WIFI_SECURITY_TYPE_SAE_AUTO:
		if (params->sae_password != NULL) {
			password = params->sae_password;
			password_len = params->sae_password_length;
		}
		break;
	default:
		return -EINVAL;
	}

	if (secure_ap) {
		if (password_len == 0U) {
			return -EINVAL;
		}

		if (qc7xx_wifi_copy_bytes(key, sizeof(key), password, password_len) < 0) {
			return -EINVAL;
		}

		if (qc7xx_wifi_copy_bytes(data->status.key, sizeof(data->status.key), password,
					  password_len) < 0) {
			return -EINVAL;
		}

		config.key = key;
	}

	config.akm = (char *)akm;
	data->status.security = params->security;
	config.channel = data->status.channel;
	config.use_dhcpd = false;
	config.hidden_ssid = params->ignore_broadcast_ssid;

	switch (params->bandwidth) {
	case WIFI_FREQ_BANDWIDTH_20MHZ:
		config.type = 0U;
		wifi_mgmr_set_ht40_enable(0U);
		break;
	case WIFI_FREQ_BANDWIDTH_40MHZ:
		config.type = 1U;
		wifi_mgmr_set_ht40_enable(1U);
		break;
	case WIFI_FREQ_BANDWIDTH_UNKNOWN:
		config.type = 0U;
		wifi_mgmr_set_ht40_enable(0U);
		break;
	default:
		return -ENOTSUP;
	}

	if (wifi_mgmr_ap_start(&config) != 0) {
		return -EIO;
	}

	return 0;
}

static int qc7xx_wifi_ap_disable(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (wifi_mgmr_ap_stop() != 0) {
		return -EAGAIN;
	}

	return 0;
}

static int qc7xx_wifi_status(const struct device *dev, struct wifi_iface_status *status)
{
	struct qc7xx_wifi_runtime *data = dev->data;

	memset(status, 0, sizeof(*status));

	switch (data->state) {
	case QC7XX_STA_STOPPED:
	case QC7XX_AP_STOPPED:
		status->state = WIFI_STATE_INACTIVE;
		break;
	case QC7XX_STA_STARTED:
	case QC7XX_STA_MONITOR:
		status->state = data->scan_cb != NULL ? WIFI_STATE_SCANNING : WIFI_STATE_DISCONNECTED;
		break;
	case QC7XX_STA_CONNECTING:
		status->state = WIFI_STATE_ASSOCIATING;
		break;
	case QC7XX_STA_DISCONNECTING:
		status->state = WIFI_STATE_DISCONNECTED;
		break;
	case QC7XX_STA_CONNECTED:
		status->state = WIFI_STATE_COMPLETED;
		break;
	case QC7XX_AP_STARTED:
	case QC7XX_AP_CONNECTED:
		status->state = WIFI_SAP_IFACE_ENABLED;
		break;
	case QC7XX_AP_DISCONNECTED:
		status->state = WIFI_SAP_IFACE_ENABLED;
		break;
	default:
		status->state = WIFI_STATE_UNKNOWN;
		break;
	}

	memcpy(status->ssid, data->status.ssid, sizeof(data->status.ssid));
	status->ssid_len = strnlen(data->status.ssid, WIFI_SSID_MAX_LEN);
	status->band = WIFI_FREQ_BAND_2_4_GHZ;
	status->link_mode = WIFI_LINK_MODE_UNKNOWN;
	status->mfp = data->status.connected ? data->status.mfp : WIFI_MFP_DISABLE;
	status->security = data->status.security;

	if (data == &qc7xx_data) {
		int phy_mode;
		wifi_mgmr_connect_ind_stat_info_t wifi_mgmr_ind_stat = { 0 };

		status->iface_mode = WIFI_MODE_INFRA;
		wifi_mgmr_sta_rssi_get(&status->rssi);
		wifi_mgmr_sta_get_bssid((uint8_t *)status->bssid);

		if (status->state == WIFI_STATE_COMPLETED) {
			wifi_mgmr_sta_channel_get((int *)&status->channel);
		} else if (data->monitor_enabled) {
			status->channel = data->monitor_channel;
		}

		status->band = status->channel > 14U ? WIFI_FREQ_BAND_5_GHZ :
							WIFI_FREQ_BAND_2_4_GHZ;
		phy_mode = wifi_mgmr_get_mode(0U);
		status->link_mode = qc7xx_wifi_link_mode_from_phy(phy_mode);
#if defined(CONFIG_QCC74X_PM_LPFW)
		status->dtim_period = qc7xx_lp_beacon_dtim_period_get();
		status->beacon_interval = qc7xx_lp_beacon_interval_get();
#else
		status->dtim_period = 0U;
		status->beacon_interval = 0U;
#endif
		status->twt_capable =
			(status->state == WIFI_STATE_COMPLETED) &&
			(status->link_mode >= WIFI_6) &&
			(wifi_mgmr_twt_support() == 1);
		status->current_phy_tx_rate = 0.0f;

		if (wifi_mgmr_sta_connect_ind_stat_get(&wifi_mgmr_ind_stat) == 0) {
			status->security = qc7xx_wifi_security_to_zephyr(wifi_mgmr_ind_stat.security);
		}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	} else if (data == &qc7xx_ap_sta_data) {
		int phy_mode;
		wifi_mgmr_ap_info_t ap = { 0 };
		uint8_t channel;

		status->iface_mode = WIFI_MODE_AP;
		memcpy(status->bssid, data->mac_addr, WIFI_MAC_ADDR_LEN);
		status->channel = data->status.channel;

		phy_mode = wifi_mgmr_get_mode(1U);
		status->link_mode = qc7xx_wifi_link_mode_from_phy(phy_mode);
		status->dtim_period = 2U;
		status->beacon_interval = 100U;
		status->twt_capable = false;
		status->current_phy_tx_rate = 0.0f;

		if (wifi_mgmr_get_ap_info(&ap) == 0) {
			status->security = qc7xx_wifi_security_to_zephyr(ap.ucSecurity);
			if (ap.ucChannel != 0U) {
				status->channel = ap.ucChannel;
			}
		}

		/* Prefer the operating channel, which differs from the one above
		 * once the AP has followed the STA through a channel switch.
		 */
		channel = qc7xx_wifi_ap_operating_channel();
		if (channel != 0U) {
			status->channel = channel;
		}

		status->band = status->channel > 14U ? WIFI_FREQ_BAND_5_GHZ :
							WIFI_FREQ_BAND_2_4_GHZ;
#endif
	} else {
		status->iface_mode = WIFI_MODE_UNKNOWN;
	}

	return 0;
}

#if defined(CONFIG_NET_STATISTICS_WIFI)
static int qc7xx_wifi_stats(const struct device *dev, struct net_stats_wifi *stats)
{
	struct qc7xx_wifi_runtime *data = dev->data;

	*stats = data->stats;
	stats->overrun_count = data->stats.errors.rx + data->stats.errors.tx;

	return 0;
}

static int qc7xx_wifi_reset_stats(const struct device *dev)
{
	struct qc7xx_wifi_runtime *data = dev->data;

	memset(&data->stats, 0, sizeof(data->stats));
	data->monitor_rx_count = 0U;
	data->monitor_rx_dropped = 0U;

	return 0;
}
#endif

static int qc7xx_wifi_mode(const struct device *dev, struct wifi_mode_info *mode)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	int ret;

	if (data != &qc7xx_data || mode == NULL) {
		return -ENOTSUP;
	}

	if (mode->oper == WIFI_MGMT_GET) {
		mode->mode = data->monitor_enabled ? WIFI_MONITOR_MODE : WIFI_STA_MODE;
		return 0;
	}

	if ((mode->mode != WIFI_STA_MODE) && (mode->mode != WIFI_MONITOR_MODE)) {
		return -ENOTSUP;
	}

	if (mode->mode == WIFI_MONITOR_MODE) {
		if (data->scan_cb != NULL || qc7xx_wifi_state_active(data->state)
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
		    || qc7xx_wifi_state_active(qc7xx_ap_sta_data.state)
#endif
		) {
			LOG_ERR("Cannot enable monitor mode while Wi-Fi is scanning, connected or AP is active");
			return -EBUSY;
		}

		if (data->monitor_filter == 0U) {
			data->monitor_filter = QC7XX_MONITOR_DEFAULT_FILTER;
		}

		ret = qc7xx_wifi_monitor_enable(data);
		if (ret != 0) {
			return ret;
		}

		return 0;
	}

	ret = qc7xx_wifi_monitor_disable(data);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int qc7xx_wifi_channel(const struct device *dev, struct wifi_channel_info *channel)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	enum wifi_frequency_bands band;
	uint16_t freq;

	if (data != &qc7xx_data || channel == NULL) {
		return -ENOTSUP;
	}

	if (channel->oper == WIFI_MGMT_GET) {
		channel->channel = data->monitor_channel == 0U ? QC7XX_MONITOR_DEFAULT_CHANNEL :
								  data->monitor_channel;
		channel->band = data->monitor_band == WIFI_FREQ_BAND_UNKNOWN ?
				      WIFI_FREQ_BAND_2_4_GHZ :
				      data->monitor_band;
		return 0;
	}

	if (data->scan_cb != NULL ||
	    (!data->monitor_enabled && qc7xx_wifi_state_active(data->state))) {
		return -EBUSY;
	}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	if (qc7xx_wifi_state_active(qc7xx_ap_sta_data.state)) {
		return -EBUSY;
	}
#endif

	band = channel->band;
	if (band == WIFI_FREQ_BAND_UNKNOWN) {
		band = channel->channel > 14U ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;
	}

	freq = qc7xx_wifi_channel_to_freq(channel->channel, band);
	if (freq == 0U) {
		return -EINVAL;
	}

	data->monitor_channel = channel->channel;
	data->monitor_band = band;

	if (data->monitor_enabled) {
		return qc7xx_wifi_monitor_enable(data);
	}

	return 0;
}

static int qc7xx_wifi_filter(const struct device *dev, struct wifi_filter_info *filter)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	uint8_t supported_filters = WIFI_PACKET_FILTER_ALL | WIFI_PACKET_FILTER_MGMT |
				    WIFI_PACKET_FILTER_DATA | WIFI_PACKET_FILTER_CTRL;

	if (data != &qc7xx_data || filter == NULL) {
		return -ENOTSUP;
	}

	if (filter->oper == WIFI_MGMT_GET) {
		filter->filter = data->monitor_filter == 0U ? QC7XX_MONITOR_DEFAULT_FILTER :
							       data->monitor_filter;
		filter->buffer_size = data->monitor_buffer_size;
		return 0;
	}

	if ((filter->filter & ~supported_filters) != 0U) {
		return -EINVAL;
	}

	if (filter->filter != 0U) {
		data->monitor_filter = filter->filter;
	}

	data->monitor_buffer_size = filter->buffer_size;
	return 0;
}

static int qc7xx_wifi_get_version(const struct device *dev, struct wifi_version *params)
{
	ARG_UNUSED(dev);

	if (params == NULL) {
		return -EINVAL;
	}

	params->drv_version = "qc7xx";
	params->fw_version = WIRELESS_VERSION;

	return 0;
}

static int8_t qc7xx_pwrlimits_max(const struct reg_power_limits_t *rule)
{
	const int8_t limits[] = {
		rule->limits.b_dsss, rule->limits.b_cck, rule->limits.g,
		rule->limits.n20, rule->limits.n40,
		rule->limits.ax20, rule->limits.ax40,
	};
	int8_t max = limits[0];

	for (size_t i = 1U; i < ARRAY_SIZE(limits); i++) {
		max = MAX(max, limits[i]);
	}

	return max;
}

static int8_t qc7xx_wl_pwrlim_max(uint8_t ch)
{
	const struct wl_param_pwrlimit_t *lim;
	int8_t max;

	if (wl_cfg == NULL || ch == 0U || ch > NUM_WLAN_CHANNELS) {
		return -1;
	}

	lim = &wl_cfg->param.pwrlim[ch - 1U];
	if (!lim->en) {
		return -1;
	}

	max = lim->b_dsss;
	max = MAX(max, lim->b_cck);
	max = MAX(max, lim->g);
	max = MAX(max, lim->n20);
	max = MAX(max, lim->n40);
	max = MAX(max, lim->ax20);
	max = MAX(max, lim->ax40);

	return max;
}

/*
 * Report the regulatory transmit power ceiling for one channel, in dBm.
 *
 * Preference order, most to least specific: the per-channel limits loaded from
 * the RF TLV area of flash, the per-channel rules of the board regulatory
 * database, then that database's per-domain ceiling. Returns 0 when none of
 * them has an answer, which the caller reports as unknown.
 */
static uint8_t qc7xx_reg_max_power(const char *country, uint8_t ch)
{
	const struct reg_db_t *db = wireless_regdb_get();
	const struct reg_domain_info_t *dom = NULL;
	reg_domain_t domain = REG_DOMAIN_ROW;
	int8_t max;

	max = qc7xx_wl_pwrlim_max(ch);
	if (max >= 0) {
		return (uint8_t)max;
	}

	if (db == NULL) {
		return 0U;
	}

	for (uint8_t i = 0U; i < db->num_country_maps; i++) {
		if (db->country_reg_map[i].country_code[0] == country[0] &&
		    db->country_reg_map[i].country_code[1] == country[1]) {
			domain = db->country_reg_map[i].domain;
			break;
		}
	}

	for (uint8_t i = 0U; i < db->num_reg_domains; i++) {
		if (db->reg_domains[i].domain == domain) {
			dom = &db->reg_domains[i];
			break;
		}
	}

	if (dom == NULL) {
		return 0U;
	}

	for (uint8_t i = 0U; i < dom->num_cpl_rules; i++) {
		if (dom->cpl_rules[i].channel == ch) {
			max = qc7xx_pwrlimits_max(&dom->cpl_rules[i]);
			return max > 0 ? (uint8_t)max : 0U;
		}
	}

	return dom->max_power > 0 ? (uint8_t)dom->max_power : 0U;
}

static int qc7xx_wifi_reg_domain(const struct device *dev, struct wifi_reg_domain *reg_domain)
{
	char country[3] = { 0 };
	uint8_t c24 = 0U;
	uint8_t c5 = 0U;

	ARG_UNUSED(dev);

	if (reg_domain == NULL) {
		return -EINVAL;
	}

	if (reg_domain->oper == WIFI_MGMT_SET) {
		country[0] = reg_domain->country_code[0];
		country[1] = reg_domain->country_code[1];

		return wifi_mgmr_set_country_code(country) == 0 ? 0 : -EIO;
	}

	if (wifi_mgmr_get_country_code(country) != 0) {
		return -EIO;
	}

	reg_domain->country_code[0] = country[0];
	reg_domain->country_code[1] = country[1];

	if (reg_domain->chan_info == NULL) {
		reg_domain->num_channels = 0U;
		return 0;
	}

	if (wifi_mgmr_get_channel_nums(country, &c24, &c5) != 0) {
		reg_domain->num_channels = 0U;
		return 0;
	}

	/*
	 * The stack only exposes a channel count per band, not a per-channel
	 * table, and every 2.4 GHz regulatory domain it knows about starts at
	 * channel 1 and is contiguous. Passive-only and DFS do not apply to
	 * 2.4 GHz, so they are always reported clear.
	 */
	for (uint8_t ch = 1U; ch <= c24; ch++) {
		struct wifi_reg_chan_info *info = &reg_domain->chan_info[ch - 1U];

		info->center_frequency = ch == 14U ? 2484U : 2407U + (ch * 5U);
		info->supported = 1U;
		info->max_power = qc7xx_reg_max_power(country, ch);
		info->passive_only = 0U;
		info->dfs = 0U;
	}

	reg_domain->num_channels = c24;

	return 0;
}

static int qc7xx_wifi_pmksa_flush(const struct device *dev)
{
	ARG_UNUSED(dev);

	wifi_mgmr_pmk_cache_entry_invalidate();

	return 0;
}

static int qc7xx_wifi_wps_config(const struct device *dev, struct wifi_wps_config_params *params)
{
	struct qc7xx_wifi_runtime *data = dev->data;

	if (params == NULL) {
		return -EINVAL;
	}

	if (!qc7xx_wifi_is_sta_runtime(data)) {
		return -ENOTSUP;
	}

	/* The stack only implements the push-button method. */
	if (params->oper != WIFI_WPS_PBC) {
		return -ENOTSUP;
	}

	if (data->monitor_enabled) {
		return -EBUSY;
	}

	if (wifi_mgmr_sta_wps_pbc(WIFI_EVENT_BEACON_IND_AUTH_WPA2_PSK) != 0) {
		return -EIO;
	}

	return 0;
}

static int qc7xx_wifi_ap_sta_disconnect(const struct device *dev, const uint8_t *mac)
{
	struct qc7xx_wifi_runtime *data = dev->data;

	if (mac == NULL) {
		return -EINVAL;
	}

	if (!qc7xx_wifi_is_ap_runtime(data)) {
		return -ENOTSUP;
	}

	/*
	 * wifi_mgmr_ap_sta_delete() is indexed, so walk the station table to
	 * translate the address the caller gave us.
	 */
	for (uint8_t idx = 0U; idx < NX_REMOTE_STA_MAX; idx++) {
		struct wifi_sta_basic_info info = { 0 };

		if (wifi_mgmr_ap_sta_info_get(&info, idx) != 0) {
			continue;
		}

		if (info.is_used == 0U ||
		    memcmp(info.sta_mac, mac, WIFI_MAC_ADDR_LEN) != 0) {
			continue;
		}

		return wifi_mgmr_ap_sta_delete(info.sta_idx) == 0 ? 0 : -EIO;
	}

	return -ENOENT;
}

static int qc7xx_wifi_ap_config_params(const struct device *dev,
				       struct wifi_ap_config_params *params)
{
	struct qc7xx_wifi_runtime *data = dev->data;
	uint32_t handled = 0U;

	if (params == NULL) {
		return -EINVAL;
	}

	if (!qc7xx_wifi_is_ap_runtime(data)) {
		return -ENOTSUP;
	}

	if ((params->type & WIFI_AP_CONFIG_PARAM_MAX_NUM_STA) != 0U) {
		if (params->max_num_sta == 0U || params->max_num_sta > NX_REMOTE_STA_MAX) {
			return -EINVAL;
		}

		if (wifi_mgmr_conf_max_sta((uint8_t)params->max_num_sta) != 0) {
			return -EIO;
		}

		handled |= WIFI_AP_CONFIG_PARAM_MAX_NUM_STA;
	}

	if ((params->type & WIFI_AP_CONFIG_PARAM_BANDWIDTH) != 0U) {
		uint8_t ht40;

		switch (params->bandwidth) {
		case WIFI_FREQ_BANDWIDTH_20MHZ:
			ht40 = 0U;
			break;
		case WIFI_FREQ_BANDWIDTH_40MHZ:
			ht40 = 1U;
			break;
		default:
			return -ENOTSUP;
		}

		if (wifi_mgmr_set_ht40_enable(ht40) != 0) {
			return -EIO;
		}

		handled |= WIFI_AP_CONFIG_PARAM_BANDWIDTH;
	}

	/*
	 * max_inactivity is only accepted by wifi_mgmr_ap_start(), so it cannot
	 * be retuned on a running AP. HT/VHT capability strings need hostapd.
	 */
	if ((params->type & ~handled) != 0U) {
		return -ENOTSUP;
	}

	return 0;
}

static void qc7xx_wifi_handle_sta_connect_event(void *event_data)
{
	int ret;

	ARG_UNUSED(event_data);

	qc7xx_data.state = QC7XX_STA_CONNECTED;
	qc7xx_data.status.connected = true;
	(void)k_work_cancel_delayable(&qc7xx_wifi_reconnect_work);

	/* In AP+STA mode the AP interface is registered first.  Make the
	 * connected STA interface the default so socket traffic is routed to
	 * the uplink interface rather than the inactive AP interface. */
	net_if_set_default(qc7xx_wifi_iface);

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	/* An AP started before the STA associated is left on its own channel. */
	(void)k_work_reschedule(&qc7xx_wifi_ap_follow_sta_work,
				K_MSEC(QC7XX_WIFI_AP_FOLLOW_STA_DELAY_MS));
#endif

#if defined(CONFIG_QC7XX_WIFI_STA_AUTO_DHCPV4)
	if (qc7xx_wifi_low_power_reconnecting) {
		ret = qc7xx_wifi_apply_sta_ps(&qc7xx_data, WIFI_PS_DISABLED);
		if (ret != 0) {
			LOG_WRN("Failed to disable Wi-Fi power save before DHCP: %d", ret);
		}
		net_dhcpv4_restart(qc7xx_wifi_iface);
	} else {
		net_dhcpv4_start(qc7xx_wifi_iface);
	}
#else
	wifi_mgmt_raise_connect_result_event(qc7xx_wifi_iface, 0);
	if (qc7xx_wifi_low_power_reconnecting) {
		ret = qc7xx_wifi_apply_sta_ps(&qc7xx_data, WIFI_PS_ENABLED);
		if (ret != 0) {
			LOG_ERR("Failed to restore Wi-Fi power save after reconnect: %d", ret);
		}
		qc7xx_wifi_low_power_reconnecting = false;
	}
#endif
}

static void qc7xx_wifi_handle_sta_disconnect_event(void *event_data)
{
	uint8_t reason = (uint32_t)event_data;
	bool disconnecting = qc7xx_data.state == QC7XX_STA_DISCONNECTING;
	bool reconnect_from_ps = qc7xx_data.ps.enabled == WIFI_PS_ENABLED;

	(void)k_work_cancel_delayable(&qc7xx_wifi_restore_ps_work);

#if defined(CONFIG_QCC74X_WIFI_HIGH_PERFORMANCE) && CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH > 1
	qc7xx_wifi_tx_batch_purge((net_al_if_t)qc7xx_wifi_iface);
#endif

	qc7xx_data.status.connected = false;
	qc7xx_data.status.mfp = WIFI_MFP_DISABLE;
	qc7xx_data.ps.enabled = WIFI_PS_DISABLED;
	wifi_mgmr_sta_twt_cache_clear();

	if (qc7xx_data.state == QC7XX_STA_CONNECTED || disconnecting) {
#if defined(CONFIG_QC7XX_WIFI_STA_AUTO_DHCPV4)
		net_dhcpv4_stop(qc7xx_wifi_iface);
#endif
		wifi_mgmt_raise_disconnect_result_event(qc7xx_wifi_iface, 0);
	} else {
		wifi_mgmt_raise_disconnect_result_event(qc7xx_wifi_iface, -1);
	}

	switch (reason) {
	case 2:  /* WIFI_REASON_AUTH_EXPIRE */
	case 14: /* WIFI_REASON_MIC_FAILURE */
	case 15: /* WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT */
	case 202: /* WIFI_REASON_AUTH_FAIL */
	case 204: /* WIFI_REASON_HANDSHAKE_TIMEOUT */
		LOG_DBG("STA authentication failure: %u", reason);
		break;
	case 201: /* WIFI_REASON_NO_AP_FOUND */
		LOG_DBG("AP not found");
		break;
	default:
		LOG_DBG("Disconnect reason: %u", reason);
		break;
	}

	if (disconnecting) {
		qc7xx_data.conn_requested = false;
		qc7xx_data.state = QC7XX_STA_STARTED;
	} else if ((reconnect_from_ps || qc7xx_wifi_low_power_reconnecting) &&
		   reason != 8U && qc7xx_wifi_reconnect_config_valid) {
		/*
		 * Run the next connect after the disconnect event has completed so
		 * the previous connection's cleanup finishes first.
		 */
		qc7xx_wifi_low_power_reconnecting = true;
		qc7xx_data.state = QC7XX_STA_STARTED;
		(void)k_work_reschedule(&qc7xx_wifi_reconnect_work,
					K_MSEC(QC7XX_WIFI_RECONNECT_DELAY_MS));
	} else if (IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT) && reason != 8U &&
		   qc7xx_data.conn_requested) {
		qc7xx_data.state = QC7XX_STA_CONNECTING;
		wifi_mgmr_sta_autoconnect_enable();
	} else {
		qc7xx_wifi_low_power_reconnecting = false;
		/* Nothing requested: staying at CONNECTING would reject every
		 * later connect request with -EALREADY.
		 */
		qc7xx_data.conn_requested = false;
		if (IS_ENABLED(CONFIG_SOC_WIFI_STA_RECONNECT)) {
			wifi_mgmr_sta_autoconnect_disable();
		}
		qc7xx_data.state = QC7XX_STA_STARTED;
	}
}

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
static void qc7xx_wifi_ap_sta_cache_reset(void)
{
	memset(qc7xx_ap_sta_cache, 0, sizeof(qc7xx_ap_sta_cache));
	memset(qc7xx_ap_sta_cache_valid, 0, sizeof(qc7xx_ap_sta_cache_valid));
}

static void qc7xx_wifi_ap_sta_info_init(struct wifi_ap_sta_info *sta_info,
					const struct wifi_sta_basic_info *stai)
{
	memset(sta_info, 0, sizeof(*sta_info));

	sta_info->link_mode = WIFI_LINK_MODE_UNKNOWN;
	sta_info->twt_capable = false;
	sta_info->mac_length = WIFI_MAC_ADDR_LEN;
	memcpy(sta_info->mac, stai->sta_mac, WIFI_MAC_ADDR_LEN);
}

static void qc7xx_wifi_handle_ap_connect_event(void *event_data)
{
	uint8_t idx = (uint32_t)event_data;
	struct wifi_sta_basic_info stai = { 0 };
	struct wifi_ap_sta_info sta_info = { 0 };

	if (idx >= ARRAY_SIZE(qc7xx_ap_sta_cache) ||
	    wifi_mgmr_ap_sta_info_get(&stai, idx) != 0 || !stai.is_used) {
		LOG_WRN("AP station add event has no station info for index %u", idx);
		return;
	}

	qc7xx_wifi_ap_sta_info_init(&sta_info, &stai);
	qc7xx_ap_sta_cache[idx] = sta_info;
	qc7xx_ap_sta_cache_valid[idx] = true;

	wifi_mgmt_raise_ap_sta_connected_event(qc7xx_wifi_iface_ap, &sta_info);
}

static void qc7xx_wifi_handle_ap_disconnect_event(void *event_data)
{
	uint8_t idx = (uint32_t)event_data;
	struct wifi_ap_sta_info sta_info = { 0 };

	if (idx >= ARRAY_SIZE(qc7xx_ap_sta_cache) || !qc7xx_ap_sta_cache_valid[idx]) {
		LOG_WRN("AP station delete event has no cached station info for index %u", idx);
	} else {
		sta_info = qc7xx_ap_sta_cache[idx];
		memset(&qc7xx_ap_sta_cache[idx], 0, sizeof(qc7xx_ap_sta_cache[idx]));
		qc7xx_ap_sta_cache_valid[idx] = false;
	}

	wifi_mgmt_raise_ap_sta_disconnected_event(qc7xx_wifi_iface_ap, &sta_info);
}
#endif

void qc7xx_wifi_event_handler(uint32_t code, uint32_t code1)
{
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	struct net_if *iface_ap = qc7xx_wifi_iface_ap;
	struct qc7xx_wifi_runtime *ap_data = &qc7xx_ap_sta_data;
#else
	struct net_if *iface_ap = qc7xx_wifi_iface;
	struct qc7xx_wifi_runtime *ap_data = &qc7xx_data;
#endif

	switch (code) {
	case CODE_WIFI_ON_INIT_DONE:
		LOG_DBG("Wi-Fi init done");
		wifi_mgmr_init(&qc7xx_wifi_conf);
		break;
	case CODE_WIFI_ON_MGMR_DONE:
		LOG_DBG("Wi-Fi manager ready");
		break;
	case CODE_WIFI_ON_SCAN_DONE:
		LOG_DBG("Wi-Fi scan done");
		(void)qc7xx_wifi_scan_complete();
		break;
	case CODE_WIFI_ON_CONNECTED:
		LOG_DBG("Wi-Fi connected");
#if defined(CONFIG_SOC_WIFI_DEBUG_PRINT)
		mm_sec_keydump();
#endif
		qc7xx_wifi_handle_sta_connect_event((void *)code1);
		break;
	case CODE_WIFI_ON_GOT_IP:
		LOG_DBG("Wi-Fi got IPv4");
		break;
	case CODE_WIFI_ON_DISCONNECT:
		LOG_DBG("Wi-Fi disconnected");
		qc7xx_wifi_handle_sta_disconnect_event((void *)code1);
		break;
	case CODE_WIFI_ON_TWT_SETUP:
		LOG_DBG("Wi-Fi TWT setup response: %u", code1);
		qc7xx_wifi_raise_twt_setup_event();
		break;
	case CODE_WIFI_ON_AP_STARTED:
		LOG_DBG("AP started");
		ap_data->state = QC7XX_AP_STARTED;
		ap_data->status.connected = false;
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
		qc7xx_wifi_ap_sta_cache_reset();
#endif
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE) && defined(CONFIG_NET_DHCPV4_SERVER)
		if (qc7xx_wifi_ap_ipv4_config(iface_ap) != 0) {
			wifi_mgmt_raise_ap_enable_result_event(iface_ap, WIFI_STATUS_AP_FAIL);
			break;
		}
#endif
		net_eth_carrier_on(iface_ap);
		wifi_mgmt_raise_ap_enable_result_event(iface_ap, WIFI_STATUS_AP_SUCCESS);
		break;
	case CODE_WIFI_ON_AP_STOPPED:
		LOG_DBG("AP stopped");
		ap_data->state = QC7XX_AP_STOPPED;
#if defined(CONFIG_QCC74X_WIFI_HIGH_PERFORMANCE) && CONFIG_QCC74X_WIFI_TX_BATCH_DEPTH > 1
		qc7xx_wifi_tx_batch_purge((net_al_if_t)iface_ap);
#endif
		ap_data->status.connected = false;
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE) && defined(CONFIG_NET_DHCPV4_SERVER)
		qc7xx_wifi_ap_ipv4_unconfig(iface_ap);
#endif
		net_eth_carrier_off(iface_ap);
		wifi_mgmt_raise_ap_disable_result_event(iface_ap, WIFI_STATUS_AP_SUCCESS);
		break;
#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
	case CODE_WIFI_ON_AP_STA_ADD:
		LOG_DBG("AP station added: %u", code1);
		ap_data->state = QC7XX_AP_CONNECTED;
		ap_data->status.connected = true;
		qc7xx_wifi_handle_ap_connect_event((void *)code1);
		break;
	case CODE_WIFI_ON_AP_STA_DEL:
		LOG_DBG("AP station removed: %u", code1);
		ap_data->state = QC7XX_AP_DISCONNECTED;
		ap_data->status.connected = false;
		qc7xx_wifi_handle_ap_disconnect_event((void *)code1);
		break;
#endif
	default:
		LOG_DBG("Unhandled Wi-Fi event: %u", code);
		break;
	}
}

const struct wifi_mgmt_ops qc7xx_wifi_mgmt = {
	.scan = qc7xx_wifi_scan,
	.connect = qc7xx_wifi_connect,
	.disconnect = qc7xx_wifi_disconnect,
	.ap_enable = qc7xx_wifi_ap_enable,
	.ap_disable = qc7xx_wifi_ap_disable,
	.iface_status = qc7xx_wifi_status,
	.set_power_save = qc7xx_wifi_set_power_save,
	.get_power_save_config = qc7xx_wifi_get_power_save_config,
	.set_twt = qc7xx_wifi_set_twt,
#if defined(CONFIG_NET_STATISTICS_WIFI)
	.get_stats = qc7xx_wifi_stats,
	.reset_stats = qc7xx_wifi_reset_stats,
#endif
	.filter = qc7xx_wifi_filter,
	.mode = qc7xx_wifi_mode,
	.channel = qc7xx_wifi_channel,
	.get_version = qc7xx_wifi_get_version,
	.reg_domain = qc7xx_wifi_reg_domain,
	.pmksa_flush = qc7xx_wifi_pmksa_flush,
	.wps_config = qc7xx_wifi_wps_config,
	.ap_sta_disconnect = qc7xx_wifi_ap_sta_disconnect,
	.ap_config_params = qc7xx_wifi_ap_config_params,
};

const struct net_wifi_mgmt_offload qc7xx_api = {
	.wifi_iface.iface_api.init = qc7xx_sta_wifi_init,
	.wifi_iface.send = qc7xx_wifi_send,
	.wifi_mgmt_api = &qc7xx_wifi_mgmt,
};

static int qc7xx_wifi_dev_init_pri(const struct device *dev)
{
#ifdef CONFIG_WIFI_QCC75X
	uint8_t mac[6] = { 0x22, 0x22, 0x22, 0x88, 0x88, 0x88 };

	wifi_mgmr_mac_set(mac);
#endif

	return qc7xx_wifi_dev_init(dev);
}

NET_DEVICE_DT_INST_DEFINE(0, qc7xx_wifi_dev_init_pri, NULL, &qc7xx_data, NULL,
			  CONFIG_WIFI_INIT_PRIORITY, &qc7xx_api, ETHERNET_L2,
			  NET_L2_GET_CTX_TYPE(ETHERNET_L2), NET_ETH_MTU);

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
NET_DEVICE_DT_INST_DEFINE(1, NULL, NULL, &qc7xx_ap_sta_data, NULL, CONFIG_WIFI_INIT_PRIORITY,
			  &qc7xx_api, ETHERNET_L2, NET_L2_GET_CTX_TYPE(ETHERNET_L2),
			  NET_ETH_MTU);

DEFINE_WIFI_NM_INSTANCE(qc7xx_wifi_nm, &qc7xx_wifi_mgmt);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));
#endif
