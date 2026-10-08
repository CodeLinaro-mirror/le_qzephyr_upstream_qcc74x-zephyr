/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_HBN_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_HBN_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum qcc74x_hbn_acomp_trigger {
	QCC74X_HBN_ACOMP_TRIGGER_FALLING = 1,
	QCC74X_HBN_ACOMP_TRIGGER_RISING = 2,
	QCC74X_HBN_ACOMP_TRIGGER_BOTH = 3,
};

struct qcc74x_hbn_acomp_wakeup {
	bool enabled;
	uint8_t input_pin;
	enum qcc74x_hbn_acomp_trigger trigger;
};

struct qcc74x_hbn_config {
	uint64_t timeout_ms;
	struct qcc74x_hbn_acomp_wakeup acomp;
};

struct qcc74x_hbn_wake_info {
	/**
	 * True when the retained Boot2 parameter block is available.
	 *
	 * Use woke_from_hbn to identify the wake source before interpreting the
	 * retained parameter fields.
	 */
	bool valid;
	/** True when Boot2 published HBN_STATUS_WAKEUP_FLAG. */
	bool woke_from_hbn;
	uint32_t raw_hbn_status;
	uint32_t wakeup_reason;
	uint64_t requested_sleep_cycles;
	uint64_t rtc_wakeup_count;
	uint8_t boot2_return;
	uint8_t wakeup_io_bits;
	uint8_t wakeup_io_edge_bits;
	uint8_t wakeup_acomp_bits;
	uint8_t wakeup_acomp_edge_bits;
};

/** Configure the next explicit sys_poweroff() as a QCC74x HBN entry. */
int qcc74x_hbn_configure(const struct qcc74x_hbn_config *config);

/** Return the HBN information captured before normal platform initialization. */
int qcc74x_hbn_wake_info_get(struct qcc74x_hbn_wake_info *info);

/** Clear the captured information and its retained Boot2 source. */
void qcc74x_hbn_wake_info_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_HBN_H_ */
