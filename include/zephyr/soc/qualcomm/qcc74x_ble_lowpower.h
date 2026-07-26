/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_BLE_LOWPOWER_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_BLE_LOWPOWER_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

struct qcc74x_ble_lowpower_session {
	bool capping;          /* BLE wake horizon was shorter than caller's timeout */
	uint64_t expected_us;  /* expected wake-up horizon in microseconds */
};

#if defined(CONFIG_QCC74X_PM_PDS15_BLE)
int qcc74x_ble_lowpower_prepare(uint64_t *timeout_us,
				struct qcc74x_ble_lowpower_session *session);
void qcc74x_ble_lowpower_crash_check(
	const struct qcc74x_ble_lowpower_session *session,
	uint64_t elapsed_us);
#else
static inline int qcc74x_ble_lowpower_prepare(
	uint64_t *timeout_us,
	struct qcc74x_ble_lowpower_session *session)
{
	ARG_UNUSED(timeout_us);
	if (session != NULL) {
		*session = (struct qcc74x_ble_lowpower_session){ 0 };
	}
	return 0;
}

static inline void qcc74x_ble_lowpower_crash_check(
	const struct qcc74x_ble_lowpower_session *session,
	uint64_t elapsed_us)
{
	ARG_UNUSED(session);
	ARG_UNUSED(elapsed_us);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_BLE_LOWPOWER_H_ */
