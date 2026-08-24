/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_rtc

#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include "rtc_utils.h"

#include <hardware/rtc_reg.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rtc_qcc74x, CONFIG_RTC_LOG_LEVEL);

#define TIMEGM_RTC_TIME_MASK                                                                       \
	(RTC_ALARM_TIME_MASK_SECOND | RTC_ALARM_TIME_MASK_MINUTE | RTC_ALARM_TIME_MASK_HOUR |      \
	 RTC_ALARM_TIME_MASK_MONTHDAY | RTC_ALARM_TIME_MASK_MONTH | RTC_ALARM_TIME_MASK_YEAR)
#define HBN_RTC_TIME_H_MASK  (0xffU)
#define HBN_RTC_TIME_FREQ_HZ (32768U)
#define HBN_RTC_TIME_MAX     (0xffffffffffULL)

/**
 * @brief Opal RTC driver instance config
 */
struct rtc_qcc74x_config {
	uint32_t base; /**< Register base address. */
};

/**
 * @brief QCC74x RTC driver instance data
 */
struct rtc_qcc74x_data {
	bool rtc_time_is_set;  /**< True if RTC time is set. */
	time_t posix_time_ref; /**< Posix time reference at RTC time setting time point. */
	uint64_t cnt_ref;      /**< Counter reference at RTC time setting time point. */
};

/**
 * @brief Convert counter value to seconds
 *
 * @param cnt Counter value.
 * @return time_t Counter value in seconds.
 */
static inline time_t counter_value_to_seconds(uint64_t cnt)
{
	return cnt / HBN_RTC_TIME_FREQ_HZ;
}

/**
 * @brief Read counter value register
 *
 * @param base Device register base address.
 * @return uint64_t Counter value.
 */
static uint64_t counter_value_read(uint32_t base)
{
	/* Trigger RTC value read. */
	uint32_t reg_val = sys_read32(base + HBN_RTC_TIME_H_OFFSET);
	reg_val |= HBN_RTC_TIME_LATCH;
	sys_write32(reg_val, base + HBN_RTC_TIME_H_OFFSET);
	reg_val &= ~HBN_RTC_TIME_LATCH;
	sys_write32(reg_val, base + HBN_RTC_TIME_H_OFFSET);

	/* Read RTC value. */
	uint64_t time_l = sys_read32(base + HBN_RTC_TIME_L_OFFSET);
	uint64_t time_h = sys_read32(base + HBN_RTC_TIME_H_OFFSET) & HBN_RTC_TIME_H_MASK;

	return ((time_h << 32) | time_l);
}

/**
 * @brief Update reference time point values in case of counter wrap-over
 *
 * This function is needed to support RTC counter wrapping over more than once.
 * 40-bit counter updated in 32768 Hz wraps every 388 days.
 *
 * @param data QC7xx RTC instance data.
 * @param cnt Current counter value.
 */
static void ref_values_update(struct rtc_qcc74x_data *data, uint64_t cnt)
{
	if (cnt < HBN_RTC_TIME_FREQ_HZ) {
		/* Counter has not run >= 1 seconds from wrap over. Do not update
		 * reference values yet since posix_time_ref resolution is 1 sec
		 * and second fraction needs to be subtracted from the counter
		 * reference value. */
		return;
	}

	if (cnt < data->cnt_ref) {
		LOG_INF("ref_values_update, cnt: %lld", cnt);
		LOG_INF("ref_values_update, before update, cnt_ref: %lld, posix_time_ref: %lld",
			data->cnt_ref, data->posix_time_ref);
		/* Counter has wrapped over. Update reference values. */
		uint64_t cnt_delta = cnt + HBN_RTC_TIME_MAX + 1 - data->cnt_ref;
		data->posix_time_ref += counter_value_to_seconds(cnt_delta);
		/* Subtract the part that is < 1 sec since posix_time_ref
		 * resolution is 1 sec. */
		data->cnt_ref = cnt - (cnt % HBN_RTC_TIME_FREQ_HZ);
		LOG_INF("ref_values_update, after update, cnt_ref: %lld, posix_time_ref: %lld",
			data->cnt_ref, data->posix_time_ref);
	}
}

/**
 * @brief Calculate counter value delta between current time point and reference
 *
 * @param data QC7xx RTC instance data.
 * @param base Device register base address.
 * @return uint64_t Counter value between current time point value and reference
 *                  time point.
 */
static uint64_t counter_delta_calculate(struct rtc_qcc74x_data *data, uint32_t base)
{
	uint64_t cnt = counter_value_read(base);
	ref_values_update(data, cnt);

	if (cnt < data->cnt_ref) {
		/* Counter has wrapped over. */
		cnt += HBN_RTC_TIME_MAX + 1;
	}

	return cnt - data->cnt_ref;
}

static int rtc_qcc74x_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
	if (!timeptr || !rtc_utils_validate_rtc_time(timeptr, TIMEGM_RTC_TIME_MASK)) {
		LOG_ERR("Invalid timeptr.");
		return -EINVAL;
	}

	struct rtc_qcc74x_data *const data = dev->data;
	const struct rtc_qcc74x_config *const cfg = dev->config;
	data->cnt_ref = counter_value_read(cfg->base);
	data->posix_time_ref = timeutil_timegm((struct tm *)timeptr);
	data->rtc_time_is_set = true;
	LOG_INF("rtc_qcc74x_set_time, cnt_ref: %lld,  posix_time_ref: %lld", data->cnt_ref,
		data->posix_time_ref);

	return 0;
}

static int rtc_qcc74x_get_time(const struct device *const dev, struct rtc_time *timeptr)
{
	if (!timeptr) {
		LOG_ERR("Invalid timeptr.");
		return -EINVAL;
	}

	struct rtc_qcc74x_data *const data = dev->data;

	if (!data->rtc_time_is_set) {
		LOG_ERR("RTC time is not set.");
		return -ENODATA;
	}

	const struct rtc_qcc74x_config *const cfg = dev->config;
	uint64_t cnt_delta = counter_delta_calculate(data, cfg->base);
	time_t t = data->posix_time_ref + counter_value_to_seconds(cnt_delta);
	struct tm *tm_ptr = gmtime_r(&t, (struct tm *)timeptr);
	if (!tm_ptr) {
		LOG_ERR("Error in gmtime_r().");
		return -EINVAL;
	}
	timeptr->tm_nsec = 0;
	timeptr->tm_isdst = -1;

	return 0;
}

/**
 * @brief Initialize Opal RTC counter
 *
 * QC7xx hardware has one counter that is used for RTC. The counter is used
 * through hibernation (HBN) registers.
 *
 * @param dev Pointer to device structure.
 * @retval 0 if successful.
 */
static int rtc_qcc74x_init(const struct device *dev)
{
	const struct rtc_qcc74x_config *const cfg = dev->config;

	uint32_t reg_val = sys_read32(cfg->base + HBN_CTL_OFFSET);
	reg_val |= HBN_RTC_ENABLE;
	sys_write32(reg_val, cfg->base + HBN_CTL_OFFSET);

	return 0;
}

static DEVICE_API(rtc, rtc_qcc74x_driver_api) = {
	.set_time = rtc_qcc74x_set_time,
	.get_time = rtc_qcc74x_get_time,
};

#define RTC_QCC74X_DEVICE(n)                                                                         \
	static struct rtc_qcc74x_data rtc_qcc74x_data_##n;                                             \
	static const struct rtc_qcc74x_config rtc_qcc74x_config_##n = {.base = DT_INST_REG_ADDR(n)};   \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, rtc_qcc74x_init, NULL, &rtc_qcc74x_data_##n, &rtc_qcc74x_config_##n,    \
			      POST_KERNEL, CONFIG_RTC_INIT_PRIORITY, &rtc_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RTC_QCC74X_DEVICE)
