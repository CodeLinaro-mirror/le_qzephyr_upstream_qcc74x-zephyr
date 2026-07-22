/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_trng

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/clock_control.h>

#ifdef CONFIG_SOC_SERIES_QCC75X
#include "qcc75x_glb.h"
#endif
#include <zephyr/drivers/clock_control/qcc7xx_clk_sec_ll.h>
#include <qc7xx_sec_trng.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(entropy_qcc74x, CONFIG_ENTROPY_LOG_LEVEL);

#define ENTROPY_BUFF_SIZE 32

/* Configuration for an entropy instance */
struct entropy_qcc74x_config {
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	uint8_t clock_source;
};

static int entropy_qcc74x_get_entropy(const struct device *dev, uint8_t *buf, uint16_t len)
{
	int retv = 0;
	uint8_t *buf_bytes = buf;
	uint8_t data[ENTROPY_BUFF_SIZE] = {0};
	uint32_t to_copy = 0U;

	/* Enable TRNG access */
#ifdef CONFIG_SOC_SERIES_QCC75X
	if (GLB_CORE_ID_AP == GLB_Get_Core_Type()) {
		qc7xx_group0_request_trng_access(NULL);
	} else {
		qc7xx_group1_request_trng_access(NULL);
	}
#else
	qc7xx_group0_request_trng_access(NULL);
#endif

	while (len > 0) {
		retv = qc7xx_trng_read(NULL, data);
		if (retv != 0) {
			LOG_ERR("Failed to read entropy. Bytes missed: %u", len);
			return retv;
		}
		to_copy = MIN(sizeof(data), len);

		memcpy(buf_bytes, data, to_copy);
		buf_bytes += to_copy;
		len -= to_copy;
	}

	/* Release TRNG access */
#ifdef CONFIG_SOC_SERIES_QCC75X
	if (GLB_CORE_ID_AP == GLB_Get_Core_Type()) {
		qc7xx_group0_release_trng_access(NULL);
	} else {
		qc7xx_group1_release_trng_access(NULL);
	}
#else
	qc7xx_group0_release_trng_access(NULL);
#endif

	LOG_DBG("Entropy read was succesfull");

	return 0;
}

/**
 * @brief Initialize the entropy device.
 *
 * @param dev Pointer to the device structure.
 * @return int 0 on success, negative error code on failure.
 */
static int entropy_qcc74x_init(const struct device *dev)
{
	const struct entropy_qcc74x_config *cfg = dev->config;
	int ret = 0;

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("Clock control device not ready");
		return -ENODEV;
	}

	/* Select clock source */
	ret = clock_control_configure(cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)cfg->clock_id,
				      (void *)&cfg->clock_source);
	if (ret != 0) {
		LOG_ERR("Failed to configure TRNG clock with source: %u err: %d", cfg->clock_source,
			ret);
		return ret;
	}

	/* Enable clock */
	ret = clock_control_on(cfg->clock_dev, (clock_control_subsys_t)(uintptr_t)cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for TRNG driver is already enabled");
		} else {
			LOG_ERR("Failed to enable TRNG clock: %d", ret);
			return ret;
		}
	}

	return 0;
}

static DEVICE_API(entropy, entropy_qcc74x_api_funcs) = {.get_entropy = entropy_qcc74x_get_entropy};

#define ENTROPY_QCC74X_DEVICE(idx)                                                                   \
	static const struct entropy_qcc74x_config entropy_qcc74x_dev_config_##idx = {                  \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                              \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(idx, id),                  \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(idx, source),                         \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, entropy_qcc74x_init, NULL, NULL, &entropy_qcc74x_dev_config_##idx,  \
			      PRE_KERNEL_1, CONFIG_ENTROPY_INIT_PRIORITY,                          \
			      &entropy_qcc74x_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(ENTROPY_QCC74X_DEVICE)
