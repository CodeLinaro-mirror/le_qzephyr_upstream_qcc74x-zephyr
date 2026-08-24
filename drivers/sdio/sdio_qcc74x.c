/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_sdio

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sdio_device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>

#include "qc7xx_sdio2.h"
#include "hardware/sdio2_reg.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sdio_qcc74x, CONFIG_SDIO_LOG_LEVEL);

/* Configuration for a Opal SDIO instance */
struct sdio_qcc74x_config {
	const struct pinctrl_dev_config *pcfg;
	const uint32_t reg_base;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
	void (*irq_init)(void);
	uint32_t irq_num;
	uint32_t reset_irq_num;
};

/* Runtime data for a Opal SDIO instance */
struct sdio_qcc74x_data {
	struct qc7xx_device_s sdio_hal_device;
	sdio_download_callback_t download_callback;
	sdio_upload_callback_t upload_callback;
	sdio_reset_callback_t reset_callback;
};

/**
 * @brief Callback function fired with interrupts.
 *
 * @param arg callback argument, it should be pointer to the device structure.
 * @param irq_event id of event type.
 * @param trans_desc pointer to transmission descriptor.
 */
void sdio2_irq_cb(void *arg, uint32_t irq_event, qc7xx_sdio2_trans_desc_t *trans_desc)
{
	const struct device *dev = (const struct device *)arg;
	struct sdio_qcc74x_data *data = dev->data;
	sdio_transfer_desc_t *desc = (sdio_transfer_desc_t *)trans_desc;

	switch (irq_event) {
	case SDIO2_IRQ_EVENT_DNLD_CPL:
		if (data->download_callback) {
			data->download_callback(dev, desc);
		}
		break;

	case SDIO2_IRQ_EVENT_UPLD_CPL:
		if (data->upload_callback) {
			data->upload_callback(dev, desc);
		}
		break;

	case SDIO2_IRQ_EVENT_SOFT_RESET:
		if (data->reset_callback) {
			data->reset_callback(dev);
		}
		break;

	case SDIO2_IRQ_EVENT_ERR_CRC:
		LOG_ERR("sdio error: CRC ERROR!");
		break;
	case SDIO2_IRQ_EVENT_ERR_ABORT:
		LOG_ERR("sdio error: ABORT!");
		break;
	case SDIO2_IRQ_EVENT_ERR_UNKNOW:
		LOG_ERR("sdio error: UNKNOWN!");
		break;
	default:
		LOG_ERR("undefined sdio2 irq event: %u", irq_event);
		break;
	}
}

/**
 * @brief Helper function to initialize SDIO controller registers
 *
 * @param config Device config
 */
static void sdio_initialize_registers(const struct sdio_qcc74x_config *config)
{
	const uint8_t AUTO_CLEAR_POS = 4U; // auto clear bit position in config2 register
	uint32_t reg_val = 0U;
	uint32_t download_size_max = DT_INST_PROP(0, download_size_max);

	if (download_size_max > 0xff * SDIO2_SIZE_CONSULT_MULTIPLE) {
		/* oversize */
		download_size_max = SDIO2_DEFAULT_SIZE_MAX;
	} else {
		download_size_max =
			download_size_max - (download_size_max % SDIO2_SIZE_CONSULT_MULTIPLE);
	}

	/* clean bitmap (write invalid) */
	sys_write16(0, config->reg_base + SDIO2_RD_BIT_MAP_OFFSET);
	sys_write16(0, config->reg_base + SDIO2_WR_BIT_MAP_OFFSET);

	/* auto clean card2host after cmd53 is completed */
	reg_val = sys_read32(config->reg_base + SDIO2_CONFIG2_OFFSET);
	reg_val |= BIT(AUTO_CLEAR_POS);
	sys_write32(reg_val, config->reg_base + SDIO2_CONFIG2_OFFSET);

	/* unmask the interrupts */
	sys_write8(SDIO2_CCR_CIM_DnLdOvr | SDIO2_CCR_CIM_UpLdOvr | SDIO2_CCR_CIM_Abort,
		   config->reg_base + SDIO2_CARD_INT_MASK_OFFSET);

	/* sdio bootrom/app flag */
	reg_val = sys_read8(config->reg_base + SDIO2_SCRATCH0_2_OFFSET);
	reg_val |= SDIO2_STA_FLAG_RD_LEN_COMPRESS_SUP;
	reg_val |= SDIO2_STA_FLAG_APP_RUN;
	sys_write8(reg_val, config->reg_base + SDIO2_SCRATCH0_2_OFFSET);

	/* slave ready / download max len */
	sys_write8((download_size_max / SDIO2_SIZE_CONSULT_MULTIPLE),
		   config->reg_base + SDIO2_SCRATCH0_1_OFFSET);
}

/**
 * @brief Initializes the SDIO device.
 *
 * Configures the SDIO hardware, including pinctrl and interrupts.
 *
 * @param dev Pointer to the SDHC device structure.
 * @return 0 on success, negative error code on failure
 */
static int sdio_qcc74x_init(const struct device *dev)
{
	int ret = 0;

	const struct sdio_qcc74x_config *config = dev->config;
	struct sdio_qcc74x_data *data = dev->data;
	LOG_DBG("%s initialization started", dev->name);

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

	if (ret < 0) {
		LOG_ERR("Pinctrl apply state failed, ret: %d", ret);
		return ret;
	}

	ret = clock_control_on(config->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for SDIO driver is already enabled");
		} else {
			LOG_ERR("Failed to enable SDIO clock: %d", ret);
			return ret;
		}
	}

	ret = reset_line_toggle_dt(&config->reset);
	if (ret < 0) {
		LOG_ERR("SDIO reset line toggle failed: %d", ret);
		return -EIO;
	}

	sdio_initialize_registers(config);

	config->irq_init();
	qc7xx_sdio2_irq_attach(&data->sdio_hal_device, sdio2_irq_cb, (void *)dev);
	return ret;
}

/**
 * @brief Interrupt service routine for completion of SDIO transfers.
 *
 * @param dev Pointer to the SDHC device structure.
 */
static void sdio_qcc74x_isr(const struct device *dev)
{
	const struct sdio_qcc74x_config *config = dev->config;
	struct sdio_qcc74x_data *data = dev->data;
	qc7xx_sdio2_isr(config->irq_num, &data->sdio_hal_device);
}

/**
 * @brief Interrupt service routine for SDIO soft reset.
 *
 * @param dev Pointer to the SDHC device structure.
 */
static void sdio_qcc74x_reset_isr(const struct device *dev)
{
	LOG_DBG("Soft reset");
#if defined(SDIO2_SOFT_RST_INT_SUP)
	const struct sdio_qcc74x_config *config = dev->config;
	struct sdio_qcc74x_data *data = dev->data;
	qc7xx_sdio2_reset_isr(config->reset_irq_num, &data->sdio_hal_device);
#endif
}

static void sdio_qcc74x_set_download_callback(const struct device *dev,
					    sdio_download_callback_t callback)
{
	struct sdio_qcc74x_data *data = dev->data;
	data->download_callback = callback;
}

static void sdio_qcc74x_set_upload_callback(const struct device *dev, sdio_upload_callback_t callback)
{
	struct sdio_qcc74x_data *data = dev->data;
	data->upload_callback = callback;
}

static void sdio_qcc74x_set_reset_callback(const struct device *dev, sdio_reset_callback_t callback)
{
	struct sdio_qcc74x_data *data = dev->data;
	data->reset_callback = callback;
}

static int sdio_qcc74x_dnld_port_push(const struct device *dev, sdio_transfer_desc_t *trans_desc)
{
	struct sdio_qcc74x_data *data = dev->data;
	return qc7xx_sdio2_dnld_port_push(&data->sdio_hal_device,
					  (qc7xx_sdio2_trans_desc_t *)trans_desc);
}

static int sdio_qcc74x_upld_port_push(const struct device *dev, sdio_transfer_desc_t *trans_desc)
{
	struct sdio_qcc74x_data *data = dev->data;
	return qc7xx_sdio2_upld_port_push(&data->sdio_hal_device,
					  (qc7xx_sdio2_trans_desc_t *)trans_desc);
}

static int sdio_qcc74x_dnld_port_pop(const struct device *dev, sdio_transfer_desc_t *trans_desc)
{
	struct sdio_qcc74x_data *data = dev->data;
	return qc7xx_sdio2_dnld_port_pop(&data->sdio_hal_device,
					 (qc7xx_sdio2_trans_desc_t *)trans_desc);
}

static int sdio_qcc74x_upld_port_pop(const struct device *dev, sdio_transfer_desc_t *trans_desc)
{
	struct sdio_qcc74x_data *data = dev->data;
	return qc7xx_sdio2_upld_port_pop(&data->sdio_hal_device,
					 (qc7xx_sdio2_trans_desc_t *)trans_desc);
}

static int sdio_qcc74x_feature_control(const struct device *dev, int cmd, uintptr_t arg)
{
	struct sdio_qcc74x_data *data = dev->data;
	return qc7xx_sdio2_feature_control(&data->sdio_hal_device, cmd, arg);
}

static void sdio_qcc74x_re_initialize(const struct device *dev)
{
	const struct sdio_qcc74x_config *config = dev->config;
	struct sdio_qcc74x_data *data = dev->data;

	qc7xx_sdio2_deinit(&data->sdio_hal_device);

	sdio_initialize_registers(config);
	/* deinit disables IRQs */
	config->irq_init();
	qc7xx_sdio2_irq_attach(&data->sdio_hal_device, sdio2_irq_cb, (void *)dev);
}

static DEVICE_API(sdio,
		  sdio_qcc74x_driver_api) = {.dnld_port_push = sdio_qcc74x_dnld_port_push,
					   .upld_port_push = sdio_qcc74x_upld_port_push,
					   .dnld_port_pop = sdio_qcc74x_dnld_port_pop,
					   .upld_port_pop = sdio_qcc74x_upld_port_pop,
					   .feature_control = sdio_qcc74x_feature_control,
					   .set_download_callback = sdio_qcc74x_set_download_callback,
					   .set_upload_callback = sdio_qcc74x_set_upload_callback,
					   .set_reset_callback = sdio_qcc74x_set_reset_callback,
					   .re_initialize = sdio_qcc74x_re_initialize};

#define SDIO_DEVICE(inst)                                                                          \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static void sdio_qcc74x_irq_init##inst(void)                                                 \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN_BY_IDX(inst, 0), DT_INST_IRQ_BY_IDX(inst, 0, priority),   \
			    sdio_qcc74x_isr, DEVICE_DT_INST_GET(inst), 0);                           \
		IRQ_CONNECT(DT_INST_IRQN_BY_IDX(inst, 1), DT_INST_IRQ_BY_IDX(inst, 1, priority),   \
			    sdio_qcc74x_reset_isr, DEVICE_DT_INST_GET(inst), 0);                     \
		irq_enable(DT_INST_IRQN_BY_IDX(inst, 0));                                          \
		irq_enable(DT_INST_IRQN_BY_IDX(inst, 1));                                          \
	};                                                                                         \
	static struct sdio_qcc74x_data sdio_qcc74x_data_##inst = {                                     \
		.sdio_hal_device = {.name = "",                                                    \
				    .reg_base = DT_INST_REG_ADDR(inst),                            \
				    .irq_num = DT_INST_IRQN(inst),                                 \
				    .idx = inst},                                                  \
		.download_callback = NULL,                                                         \
		.upload_callback = NULL,                                                           \
		.reset_callback = NULL,                                                            \
	};                                                                                         \
	static const struct sdio_qcc74x_config sdio_qcc74x_config##inst = {                            \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.reg_base = DT_INST_REG_ADDR(inst),                                                \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                             \
		.irq_init = sdio_qcc74x_irq_init##inst,                                              \
		.irq_num = DT_INST_IRQN_BY_IDX(inst, 0),                                           \
		.reset_irq_num = DT_INST_IRQN_BY_IDX(inst, 1)};                                    \
	DEVICE_DT_INST_DEFINE(inst, sdio_qcc74x_init, NULL, &sdio_qcc74x_data_##inst,                  \
			      &sdio_qcc74x_config##inst, POST_KERNEL, CONFIG_SDIO_INIT_PRIORITY,     \
			      &sdio_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SDIO_DEVICE)
