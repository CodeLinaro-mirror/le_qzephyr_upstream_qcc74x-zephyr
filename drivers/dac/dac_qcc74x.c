/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_dac

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>

#include <hardware/dac_reg.h>
#if defined(CONFIG_DAC_QCC74X_DMA)
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dac/dac_qcc74x.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dac_qcc74x);

/* DAC clock div definition */
#define DAC_CLK_DIV_16 0
#define DAC_CLK_DIV_32 1
#define DAC_CLK_DIV_64 3
#define DAC_CLK_DIV_1  4

/* DAC resolution supported in Opal family */
#define DAC_RESOLUTION 12

/* DAC driver configuration */
struct dac_qcc74x_cfg {
	const struct pinctrl_dev_config *pcfg; /**< Pin control config. */
	uint32_t base_reg_gpip;                /**< GPIP reg base address. */
	uint32_t base_reg_glb;                 /**< GLB reg base address. */
	uint8_t voltage_range;                 /**< DAC output voltage range. */
	uint8_t clk_div;                       /**< Clock divider for DAC mode. */
#if defined(CONFIG_DAC_QCC74X_DMA)
	const struct device *dma_dev; /**< DMA device. */
	const uint8_t dma_channel;    /**< DMA channel. */
	const uint32_t dma_slot;      /**< DMA slot. */
	const uint32_t per_fifo_addr; /**< DMA channel source address. */
#endif
};

/**
 * @brief System clock settle.
 */
static void system_clock_settle(void)
{
	__asm__ volatile(".rept 15 ; nop ; .endr");
}

/**
 * @brief         Set Opal DAC DMA format.
 *
 * @param dev     Pointer to device structure.
 * @param format  DMA data format.
 */
void dac_qcc74x_set_dma_format(const struct device *dev, uint8_t format)
{
	const struct dac_qcc74x_cfg *cfg = dev->config;
	uint32_t tmp_val = 0U;

	tmp_val = sys_read32(cfg->base_reg_glb + GPIP_GPDAC_DMA_CONFIG_OFFSET);
	tmp_val &= ~GPIP_GPDAC_DMA_FORMAT_MASK;
	tmp_val |= (format << GPIP_GPDAC_DMA_FORMAT_SHIFT);
	sys_write32(tmp_val, cfg->base_reg_glb + GPIP_GPDAC_DMA_CONFIG_OFFSET);
}

/**
 * @brief Enable/disable Opal DAC source from DMA.
 *
 * @param dev     Pointer to device structure.
 * @param enable  Enable/disable DAC DMA data source.
 */
void dac_qcc74x_link_tx_dma(const struct device *dev, bool enable)
{
	const struct dac_qcc74x_cfg *cfg = dev->config;
	uint32_t tmp_val1 = 0U;
	uint32_t tmp_val2 = 0U;
	uint8_t flag = 0U;

	tmp_val1 = sys_read32(cfg->base_reg_gpip + GPIP_GPDAC_CONFIG_OFFSET);
	tmp_val1 &= ~GPIP_GPDAC_CH_A_SEL_MASK;
	tmp_val1 &= ~GPIP_GPDAC_CH_B_SEL_MASK;

	tmp_val2 = sys_read32(cfg->base_reg_gpip + GPIP_GPDAC_DMA_CONFIG_OFFSET);
	tmp_val2 &= ~GPIP_GPDAC_DMA_FORMAT_MASK;
	if (enable) {
		if (sys_read32(cfg->base_reg_glb + GLB_GPDAC_ACTRL_OFFSET) &
		    (GLB_GPDAC_A_EN | GLB_GPDAC_IOA_EN)) {
			tmp_val1 |= (1U << GPIP_GPDAC_CH_A_SEL_SHIFT);
			flag++;
		}

		if (sys_read32(cfg->base_reg_glb + GLB_GPDAC_BCTRL_OFFSET) &
		    (GLB_GPDAC_B_EN | GLB_GPDAC_IOB_EN)) {
			tmp_val1 |= (1U << GPIP_GPDAC_CH_B_SEL_SHIFT);
			flag++;
		}

		if (flag == 1) {
			tmp_val2 |= (0U << GPIP_GPDAC_DMA_FORMAT_SHIFT);
		} else {
			tmp_val2 |= (1U << GPIP_GPDAC_DMA_FORMAT_SHIFT);
		}
		tmp_val2 |= GPIP_GPDAC_DMA_TX_EN;
	} else {
		tmp_val2 &= ~GPIP_GPDAC_DMA_TX_EN;
	}
	sys_write32(tmp_val2, cfg->base_reg_gpip + GPIP_GPDAC_DMA_CONFIG_OFFSET);
	sys_write32(tmp_val1, cfg->base_reg_gpip + GPIP_GPDAC_CONFIG_OFFSET);
}

static int dac_qcc74x_write_value(const struct device *dev, uint8_t channel, uint32_t value)
{
	const struct dac_qcc74x_cfg *cfg = dev->config;
	uint32_t tmp_val = 0U;

	if (channel > 1) {
		LOG_ERR("Only 2 channels are supported for Opal");
		return -EINVAL;
	}

	tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_DATA_OFFSET);

	if (channel == 0) {
		tmp_val &= ~GLB_GPDAC_A_DATA_MASK;
		tmp_val |= (value << GLB_GPDAC_A_DATA_SHIFT);
	} else if (channel == 1) {
		tmp_val &= ~GLB_GPDAC_B_DATA_MASK;
		tmp_val |= (value << GLB_GPDAC_B_DATA_SHIFT);
	}
	sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_DATA_OFFSET);
	LOG_DBG("Wrote value: %d to dac channel: %d.", value, channel);
	return 0;
}

#if defined(CONFIG_DAC_QCC74X_DMA)
static int dac_qcc74x_write_dma(const struct device *dev, uint16_t len, uint32_t *source_address)
{
	const struct dac_qcc74x_cfg *conf = dev->config;
	struct dma_block_config dma_blk = {0};
	struct dma_config dma_cfg = {0};
	int ret = 0;

	dma_blk.source_address = (uint32_t)source_address;
	dma_blk.dest_address = conf->per_fifo_addr;
	dma_blk.block_size = len;

	dma_cfg.dma_slot = conf->dma_slot;
	dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	dma_cfg.source_data_size = 4;
	dma_cfg.dest_data_size = 4;
	dma_cfg.source_burst_length = 1;
	dma_cfg.dest_burst_length = 1;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;

	ret = dma_config(conf->dma_dev, conf->dma_channel, &dma_cfg);
	if (ret) {
		LOG_ERR("Error configuring DMA: %d", ret);
		return ret;
	}

	ret = dma_start(conf->dma_dev, conf->dma_channel);
	if (ret) {
		LOG_ERR("Error starting DMA: %d", ret);
		return ret;
	}
	LOG_DBG("Wrote : %d values to dac using DMA from: %x.", len, *source_address);
	return 0;
}
#endif

static int dac_qcc74x_channel_setup(const struct device *dev,
				  const struct dac_channel_cfg *channel_cfg)
{
	const struct dac_qcc74x_cfg *cfg = dev->config;
	uint32_t tmp_val = 0U;

	if (channel_cfg->resolution != DAC_RESOLUTION) {
		LOG_ERR("Wrong DAC resolution");
		return -EINVAL;
	}

	if (channel_cfg->channel_id > 1) {
		LOG_ERR("Only 2 channels are supported for Opal");
		return -EINVAL;
	}

	if (channel_cfg->channel_id == 0) {
		tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_ACTRL_OFFSET);
		tmp_val |= (GLB_GPDAC_A_EN | GLB_GPDAC_IOA_EN);
		tmp_val &= ~(GLB_GPDAC_A_RNG_MASK);
		tmp_val |= (cfg->voltage_range << GLB_GPDAC_A_RNG_SHIFT);
		sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_ACTRL_OFFSET);
	} else if (channel_cfg->channel_id == 1) {
		tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_BCTRL_OFFSET);
		tmp_val |= (GLB_GPDAC_B_EN | GLB_GPDAC_IOB_EN);
		tmp_val &= ~(GLB_GPDAC_B_RNG_MASK);
		tmp_val |= (cfg->voltage_range << GLB_GPDAC_B_RNG_SHIFT);
		sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_BCTRL_OFFSET);
	}
#if defined(CONFIG_DAC_QCC74X_DMA)
	dac_qcc74x_link_tx_dma(dev, true);
#endif
	return 0;
}

/**
 * @brief Initialize Opal DAC driver instance.
 *
 * @param  dev    Pointer to device structure.
 * @retval 0      On success.
 */
static int dac_qcc74x_init(const struct device *dev)
{
	const struct dac_qcc74x_cfg *cfg = dev->config;
	uint32_t tmp_val = 0U;
	uint8_t clk_div = cfg->clk_div;
	int err = 0;

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		LOG_ERR("DAC pinctrl setup failed (%d)", err);
		return err;
	}

	/* reset */
	tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);
	tmp_val &= ~GLB_GPDACA_RSTN_ANA;
	sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);

	system_clock_settle();

	tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);
	tmp_val &= ~GLB_GPDACB_RSTN_ANA;
	sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);

	system_clock_settle();

	tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);
	tmp_val |= GLB_GPDACA_RSTN_ANA;
	tmp_val |= GLB_GPDACB_RSTN_ANA;
	sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);

	system_clock_settle();

	/* internal reference */
	tmp_val = sys_read32(cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);
	tmp_val &= ~GLB_GPDAC_REF_SEL;
	sys_write32(tmp_val, cfg->base_reg_glb + GLB_GPDAC_CTRL_OFFSET);

	tmp_val = 0U;
	tmp_val |= GPIP_GPDAC_EN;

	tmp_val |= (clk_div << GPIP_GPDAC_MODE_SHIFT);
	/* Choose channel source as reg */
	tmp_val &= ~GPIP_GPDAC_CH_A_SEL_MASK;
	tmp_val &= ~GPIP_GPDAC_CH_B_SEL_MASK;

	sys_write32(tmp_val, cfg->base_reg_gpip + GPIP_GPDAC_CONFIG_OFFSET);

	/* no DMA as default*/
	tmp_val = sys_read32(cfg->base_reg_gpip + GPIP_GPDAC_DMA_CONFIG_OFFSET);
	tmp_val &= ~GPIP_GPDAC_DMA_TX_EN;
	sys_write32(tmp_val, cfg->base_reg_gpip + GPIP_GPDAC_DMA_CONFIG_OFFSET);

	return 0;
}

#if defined(CONFIG_DAC_QCC74X_DMA)
static DEVICE_API(dac_qcc74x, dac_driver_api) = {
	.base.channel_setup = dac_qcc74x_channel_setup,
	.base.write_value = dac_qcc74x_write_value,
	.write_dma = dac_qcc74x_write_dma,
};
#else
static DEVICE_API(dac, dac_driver_api) = {
	.channel_setup = dac_qcc74x_channel_setup,
	.write_value = dac_qcc74x_write_value,
};
#endif

#if defined(CONFIG_DAC_QCC74X_DMA)
#define DAC_QCC74X_DMA_CONF_INIT(n)                                                                  \
	.dma_dev = (DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_IDX(n, 0))),                                \
	.dma_channel = (DT_INST_DMAS_CELL_BY_IDX(n, 0, channel)),                                  \
	.dma_slot = (DT_INST_DMAS_CELL_BY_IDX(n, 0, slot)),                                        \
	.per_fifo_addr = (DT_INST_DMAS_CELL_BY_IDX(n, 0, per_fifo_addr)),
#else
#define DAC_QCC74X_DMA_CONF_INIT(n)
#endif

#define DAC_QCC74X_DEVICE(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
                                                                                                   \
	static const struct dac_qcc74x_cfg dac_qcc74x_config_##n = {                                   \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.base_reg_gpip = DT_INST_REG_ADDR_BY_IDX(n, 0),                                    \
		.base_reg_glb = DT_INST_REG_ADDR_BY_IDX(n, 1),                                     \
		.voltage_range = DT_PROP(DT_DRV_INST(n), voltage_range),                           \
		.clk_div = DT_PROP(DT_DRV_INST(n), clk_div),                                       \
		DAC_QCC74X_DMA_CONF_INIT(n)};                                                        \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, dac_qcc74x_init, NULL, NULL, &dac_qcc74x_config_##n, POST_KERNEL,     \
			      CONFIG_DAC_INIT_PRIORITY, &dac_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DAC_QCC74X_DEVICE)
