/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_reset

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_SOC_SERIES_QCC74X
#include <qcc74x_glb.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <qcc75x_glb.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCR3XX
#include <qcr373_glb.h>
#endif

/* Register for logging */
LOG_MODULE_REGISTER(reset_qcc74x, CONFIG_RESET_LOG_LEVEL);

/**
 * @brief Reset device configuration (base address from device tree)
 *
 */
struct reset_qcc74x_config {
	uint32_t base;
};

/**
 * @brief Init reset controller driver.
 *
 * @param dev reset device
 * @return int always zero
 */
static int reset_qcc74x_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	LOG_DBG("Reset controller initialized");

	return 0;
}

static int reset_qcc74x_status(const struct device *dev, uint32_t id, uint8_t *status)
{
	const struct reset_qcc74x_config *config = dev->config;
	uint32_t tmpVal = 0U;
	uint32_t bit = 0U;
	uint32_t regAddr = 0U;

	LOG_DBG("Getting reset status id: %u", id);

	if (!status) {
		return -EINVAL;
	}

	if (id < 32) {
		bit = id;
		regAddr = config->base + GLB_SWRST_CFG0_OFFSET;
	} else if (id < 64) {
		bit = id - 32;
		regAddr = config->base + GLB_SWRST_CFG1_OFFSET;
	} else if (id < 96) {
		bit = id - 64;
		regAddr = config->base + GLB_SWRST_CFG2_OFFSET;
	}

	tmpVal = sys_read32(regAddr);
	*status = (tmpVal >> bit) & 1;

	return 0;
}

static int reset_qcc74x_line_assert(const struct device *dev, uint32_t id)
{
	const struct reset_qcc74x_config *config = dev->config;
	uint32_t tmpVal = 0U;
	uint32_t bit = 0U;
	uint32_t regAddr = 0U;

	LOG_DBG("Assert reset id: %u", id);

	if (id < 32) {
		bit = id;
		regAddr = config->base + GLB_SWRST_CFG0_OFFSET;
	} else if (id < 64) {
		bit = id - 32;
		regAddr = config->base + GLB_SWRST_CFG1_OFFSET;
	} else if (id < 96) {
		bit = id - 64;
		regAddr = config->base + GLB_SWRST_CFG2_OFFSET;
	}

	tmpVal = sys_read32(regAddr);
	tmpVal |= (1 << bit);
	sys_write32(tmpVal, regAddr);

	return 0;
}

static int reset_qcc74x_line_deassert(const struct device *dev, uint32_t id)
{
	const struct reset_qcc74x_config *config = dev->config;
	uint32_t tmpVal = 0U;
	uint32_t bit = 0U;
	uint32_t regAddr = 0U;

	LOG_DBG("Deassert reset id: %u", id);

	if (id < 32) {
		bit = id;
		regAddr = config->base + GLB_SWRST_CFG0_OFFSET;
	} else if (id < 64) {
		bit = id - 32;
		regAddr = config->base + GLB_SWRST_CFG1_OFFSET;
	} else if (id < 96) {
		bit = id - 64;
		regAddr = config->base + GLB_SWRST_CFG2_OFFSET;
	}

	tmpVal = sys_read32(regAddr);
	tmpVal &= ~(1 << bit);
	sys_write32(tmpVal, regAddr);

	return 0;
}

static int reset_qcc74x_line_toggle(const struct device *dev, uint32_t id)
{
	(void)reset_qcc74x_line_assert(dev, id);
	(void)reset_qcc74x_line_deassert(dev, id);

	return 0;
}

/**
 * @brief Construct a new device api object for reset controller
 *
 */
static DEVICE_API(reset, reset_qcc74x_driver_api) = {
	.status = reset_qcc74x_status,
	.line_assert = reset_qcc74x_line_assert,
	.line_deassert = reset_qcc74x_line_deassert,
	.line_toggle = reset_qcc74x_line_toggle,
};

/**
 * @brief Set base address in configuration to devicetree value
 *
 */
static const struct reset_qcc74x_config reset_qcc74x_config = {
	.base = DT_REG_ADDR(DT_INST_PARENT(0)),
};

/**
 * @brief Declare reset driver instance
 *
 */
DEVICE_DT_INST_DEFINE(0, reset_qcc74x_init, NULL, NULL, &reset_qcc74x_config, PRE_KERNEL_1,
		      CONFIG_RESET_INIT_PRIORITY, &reset_qcc74x_driver_api);