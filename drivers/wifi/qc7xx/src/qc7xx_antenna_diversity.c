/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qualcomm_qcc74x_antenna_diversity

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <antenna_al.h>

#include "qc7xx_antenna_diversity.h"

#if !DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
#error "QCC74x antenna diversity requires an enabled devicetree RF-switch node"
#endif

#define ANTENNA_DIVERSITY_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(DT_DRV_COMPAT)
#define ANTENNA_DIVERSITY_COUNT 2U

LOG_MODULE_REGISTER(qc7xx_antenna_diversity, CONFIG_WIFI_LOG_LEVEL);

static const struct gpio_dt_spec antenna_select =
	GPIO_DT_SPEC_GET(ANTENNA_DIVERSITY_NODE, select_gpios);

static int qc7xx_antenna_hal_init(void)
{
	if (!gpio_is_ready_dt(&antenna_select)) {
		LOG_ERR("Antenna select GPIO is not ready");
		return -ENODEV;
	}

	/* Logical active selects antenna 0; devicetree describes the polarity. */
	return gpio_pin_configure_dt(&antenna_select, GPIO_OUTPUT_ACTIVE);
}

static int qc7xx_antenna_hal_switch(antenna_id_t antenna)
{
	if (antenna >= ANTENNA_DIVERSITY_COUNT) {
		return -EINVAL;
	}

	return gpio_pin_set_dt(&antenna_select, antenna == ANTENNA_0);
}

static uint8_t qc7xx_antenna_hal_get_count(void)
{
	return ANTENNA_DIVERSITY_COUNT;
}

static void qc7xx_antenna_hal_deinit(void)
{
}

static const antenna_hal_ops_t qc7xx_antenna_ops = {
	.init = qc7xx_antenna_hal_init,
	.switch_antenna = qc7xx_antenna_hal_switch,
	.get_antenna_count = qc7xx_antenna_hal_get_count,
	.deinit = qc7xx_antenna_hal_deinit,
	.log = NULL,
	.dynamic_div_enabled = IS_ENABLED(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_DYNAMIC),
	.static_div_enabled = IS_ENABLED(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_STATIC),
};

int qc7xx_antenna_diversity_init(void)
{
	int ret = antenna_hal_init(&qc7xx_antenna_ops);

	if (ret != 0) {
		LOG_ERR("Failed to initialize antenna diversity: %d", ret);
		return ret < 0 ? ret : -EIO;
	}

	LOG_INF("Antenna diversity initialized: static=%u dynamic=%u GPIO=%u",
		IS_ENABLED(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_STATIC),
		IS_ENABLED(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_DYNAMIC),
		antenna_select.pin);
	return 0;
}
