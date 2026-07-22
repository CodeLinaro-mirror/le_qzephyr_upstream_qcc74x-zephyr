/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_mdio

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/net/ethernet.h>

#include <qc7xx_emac.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mdio_qcc74x, CONFIG_MDIO_LOG_LEVEL);

/** @brief Timeout for PHY operations in microseconds */
#define PHY_OPERATION_TIMEOUT_US 1000

/**
 * @brief MDIO device data structure
 * @details Contains runtime data for the MDIO driver instance
 */
struct mdio_qcc74x_data {
	struct k_sem sem;               /**< Semaphore for synchronizing MDIO access */
	uint8_t instance_id;            /**< Instance ID of the MDIO device */
	struct qc7xx_device_s mdio_dev; /**< QC7xx device instance */
};

/**
 * @brief MDIO device configuration structure
 * @details Contains configuration data for the MDIO driver
 */
struct mdio_qcc74x_config {
	const struct pinctrl_dev_config *pcfg; /**< Pin control configuration */
	uint32_t clk_div;                      /**< MDIO clock divider value */
};

static int mdio_qcc74x_read(const struct device *dev, uint8_t prtad, uint8_t regad, uint16_t *data)
{
	struct mdio_qcc74x_data *dev_data = dev->data;

	k_sem_take(&dev_data->sem, K_FOREVER);
	qc7xx_emac_md_read(&dev_data->mdio_dev, prtad, regad, data);
	k_sem_give(&dev_data->sem);

	return 0;
}

static int mdio_qcc74x_write(const struct device *dev, uint8_t prtad, uint8_t regad, uint16_t data)
{
	struct mdio_qcc74x_data *dev_data = dev->data;

	k_sem_take(&dev_data->sem, K_FOREVER);
	qc7xx_emac_md_write(&dev_data->mdio_dev, prtad, regad, data);
	k_sem_give(&dev_data->sem);

	return 0;
}

/**
 * @brief Initialize the MDIO device
 * @param dev Pointer to the device structure
 * @return 0 on success, negative error code on failure
 * @retval -1 Failed to get MDIO device
 * @retval -EIO Failed to apply pin control configuration
 */
static int mdio_qcc74x_initialize(const struct device *dev)
{
	const struct mdio_qcc74x_config *dev_cfg = dev->config;
	struct mdio_qcc74x_data *dev_data = dev->data;
	uint8_t instance = dev_data->instance_id;
	struct qc7xx_emac_config_s emac_cfg = {
		.mac_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.md_clk_div = dev_cfg->clk_div,
		.min_frame_len = NET_ETH_MINIMAL_FRAME_SIZE + 4,
		.max_frame_len = NET_ETH_MAX_FRAME_SIZE + 4,
	};
	int res;

	LOG_DBG("MDIO %d initialize, mdio clock div=%d", instance, dev_cfg->clk_div);

	dev_data->mdio_dev.idx = instance;

	switch (instance) {
	case 0:
#if DT_NODE_EXISTS(DT_NODELABEL(emac0))
		dev_data->mdio_dev.reg_base = DT_REG_ADDR(DT_NODELABEL(emac0));
		dev_data->mdio_dev.irq_num = DT_IRQN(DT_NODELABEL(emac0));
#else
		return -ENODEV;
#endif
		break;
	case 1:
#if DT_NODE_EXISTS(DT_NODELABEL(emac1))
		dev_data->mdio_dev.reg_base = DT_REG_ADDR(DT_NODELABEL(emac1));
		dev_data->mdio_dev.irq_num = DT_IRQN(DT_NODELABEL(emac1));
#else
		return -ENODEV;
#endif
		break;

	default:
		return -EINVAL;
	}

	LOG_DBG("MDIO %d reg_base=0x%x, irq_num=%d", instance, dev_data->mdio_dev.reg_base,
		dev_data->mdio_dev.irq_num);

	qc7xx_emac_init(&dev_data->mdio_dev, &emac_cfg);

	k_sem_init(&dev_data->sem, 1, 1);

	res = pinctrl_apply_state(dev_cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (res != 0) {
		LOG_ERR("MDIO init pin ctrl fail");
		return -EIO;
	}

	LOG_DBG("MDIO init success");

	return 0;
}

/**
 * @brief MDIO driver API structure
 * @details Defines the function pointers for MDIO operations
 */
static DEVICE_API(mdio, mdio_qcc74x_driver_api) = {
	.read = mdio_qcc74x_read,
	.write = mdio_qcc74x_write,
};

/**
 * @brief Macro to define MDIO configuration for a specific instance
 * @param idx Instance index
 */
#define MDIO_QCC74X_CONFIG(idx)                                                                      \
	static const struct mdio_qcc74x_config mdio_qcc74x_config_##idx = {                            \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),                                       \
		.clk_div = DT_INST_PROP(idx, clk_div),                                             \
	};

/**
 * @brief Macro to define MDIO device instance
 * @param idx Instance index
 */
#define MDIO_QCC74X_DEVICE(idx)                                                                      \
	PINCTRL_DT_INST_DEFINE(idx);                                                               \
	MDIO_QCC74X_CONFIG(idx);                                                                     \
	static struct mdio_qcc74x_data mdio_qcc74x_data##idx = {                                       \
		.instance_id = idx,                                                                \
		.mdio_dev = {NULL, 0, 0xFF, 0, 0, 0, NULL},                                        \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, &mdio_qcc74x_initialize, NULL, &mdio_qcc74x_data##idx,              \
			      &mdio_qcc74x_config_##idx, POST_KERNEL, CONFIG_MDIO_INIT_PRIORITY,     \
			      &mdio_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MDIO_QCC74X_DEVICE)
