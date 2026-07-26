/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_QC7XX_WIFI_DRV_PRIV_H_
#define ZEPHYR_DRIVERS_WIFI_QC7XX_WIFI_DRV_PRIV_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

enum qc7xx_state_flag {
	QC7XX_STA_STOPPED,
	QC7XX_STA_STARTED,
	QC7XX_STA_MONITOR,
	QC7XX_STA_CONNECTING,
	QC7XX_STA_DISCONNECTING,
	QC7XX_STA_CONNECTED,
	QC7XX_AP_STARTED,
	QC7XX_AP_CONNECTED,
	QC7XX_AP_DISCONNECTED,
	QC7XX_AP_STOPPED,
};

struct qc7xx_wifi_status {
	char ssid[WIFI_SSID_MAX_LEN + 1];
	char key[WIFI_PSK_MAX_LEN + 1];
	enum wifi_security_type security;
	enum wifi_mfp_options mfp;
	bool connected;
	uint8_t channel;
	int rssi;
};

struct qc7xx_wifi_ps_state {
	enum wifi_ps enabled;
	unsigned short listen_interval;
	enum wifi_ps_wakeup_mode wakeup_mode;
	enum wifi_ps_mode mode;
	unsigned int timeout_ms;
	enum wifi_ps_exit_strategy exit_strategy;
};

struct qc7xx_wifi_runtime {
	void *fhostvif;
	uint8_t mac_addr[WIFI_MAC_ADDR_LEN];
#if defined(CONFIG_NET_STATISTICS_WIFI)
	struct net_stats_wifi stats;
#endif
	struct qc7xx_wifi_status status;
	struct qc7xx_wifi_ps_state ps;
	scan_result_cb_t scan_cb;
	uint16_t monitor_channel;
	uint16_t monitor_buffer_size;
	enum wifi_frequency_bands monitor_band;
	uint8_t monitor_filter;
	bool monitor_enabled;
	uint32_t monitor_rx_count;
	uint32_t monitor_rx_dropped;
	uint8_t state;
};

extern struct net_if *qc7xx_wifi_iface;
extern struct qc7xx_wifi_runtime qc7xx_data;

#if defined(CONFIG_QC7XX_WIFI_AP_STA_MODE)
extern struct net_if *qc7xx_wifi_iface_ap;
extern struct qc7xx_wifi_runtime qc7xx_ap_sta_data;
#endif

int qc7xx_wifi_dev_init(const struct device *dev);
void qc7xx_wifi_event_handler(uint32_t code, uint32_t code1);

void net_al_tx_buf_init(void);
void *net_al_tx_buf_alloc(int32_t timeout);
void net_al_tx_buf_free(void *buf);
void net_al_tx_buf_set_len(void *buf, int length);

#endif /* ZEPHYR_DRIVERS_WIFI_QC7XX_WIFI_DRV_PRIV_H_ */
