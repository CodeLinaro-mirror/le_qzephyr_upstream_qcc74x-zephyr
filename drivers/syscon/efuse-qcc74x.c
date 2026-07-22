/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_efuse

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/syscon.h>

#include <qc7xx_ef_ctrl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(efuse_qcc74x, CONFIG_SYSCON_LOG_LEVEL);

struct efuse_qcc74x_config {
	size_t size;
};

static int efuse_qcc74x_read(const struct device *dev, uint16_t reg, uint32_t *val)
{
	const struct efuse_qcc74x_config *config = dev->config;

	if (!val) {
		return -EINVAL;
	}

	if (reg >= (config->size - sizeof(uint32_t))) {
		return -EINVAL;
	}

	/* The parameters reg and val must be 4‑byte aligned.*/
	if (!IS_ALIGNED(reg, sizeof(uint32_t))) {
		return -EINVAL;
	}

	if (!IS_ALIGNED(val, sizeof(uint32_t))) {
		return -EINVAL;
	}

	qc7xx_ef_ctrl_read_direct(NULL, reg, val, 1, 1);
	return 0;
}

static int efuse_qcc74x_write(const struct device *dev, uint16_t reg, uint32_t val)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(reg);
	ARG_UNUSED(val);
	return -ENOTSUP;
}

static int efuse_qcc74x_size(const struct device *dev, size_t *size)
{
	const struct efuse_qcc74x_config *config = dev->config;

	if (!size) {
		return -EINVAL;
	}

	*size = config->size;
	return 0;
}

static int efuse_qcc74x_get_base(const struct device *dev, uintptr_t *addr)
{
	ARG_UNUSED(dev);

	if (!addr) {
		return -EINVAL;
	}

	*addr = 0;
	return 0;
}

static DEVICE_API(syscon, efuse_qcc74x_api) = {
	.read = efuse_qcc74x_read,
	.write = efuse_qcc74x_write,
	.get_size = efuse_qcc74x_size,
	.get_base = efuse_qcc74x_get_base,
};

static const struct efuse_qcc74x_config efuse_config = {
	.size = DT_INST_PROP(0, size),
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, &efuse_config, POST_KERNEL, CONFIG_SYSCON_INIT_PRIORITY,
		      &efuse_qcc74x_api);
