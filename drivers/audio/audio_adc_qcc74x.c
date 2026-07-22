/*
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_audio_adc

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/audio/audio_adc.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <qc7xx_auadc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(audio_adc_qcc74x, CONFIG_AUDIO_ADC_LOG_LEVEL);

/**
 * @brief Opal audio ADC driver instance DMA config.
 */
struct audio_adc_qcc74x_dma_config {
	const struct device *dma_dev; /**< DMA device. */
	const uint8_t dma_channel;    /**< DMA channel. */
	const uint32_t dma_slot;      /**< DMA slot. */
	const uint32_t per_fifo_addr; /**< DMA channel source address. */
};

/**
 * @brief Opal audio ADC driver instance config.
 */
struct audio_adc_qcc74x_config {
	const uint32_t base;                               /**< Register base address. */
	const struct pinctrl_dev_config *pincfg;           /**< Pin control configuration. */
	const struct audio_adc_qcc74x_dma_config dma_config; /**< DMA configuration. */
	const struct device *clock_dev;                    /**< Clock device used */
	const clock_control_subsys_t clock_id;             /**< Peripheral ID */
	const uint8_t clk_div;                             /**< Clock division value */
};

/**
 * @brief Opal audio ADC driver instance data.
 */
struct audio_adc_qcc74x_data {
};

static int audio_adc_qcc74x_configure(const struct device *dev, const struct audio_adc_cfg *cfg)
{
	const struct audio_adc_qcc74x_config *conf = dev->config;
	struct qc7xx_device_s auadc_hd = {.reg_base = conf->base};
	int ret = 0;

	struct qc7xx_auadc_init_config_s auadc_init_cfg = {
		.input_mode = AUADC_INPUT_MODE_ADC,
		.fifo_threshold = 3,
	};

	switch (cfg->sample_width) {
	case AUDIO_PCM_WIDTH_16_BITS:
		auadc_init_cfg.data_format = AUADC_DATA_FORMAT_16BIT;
		break;
	case AUDIO_PCM_WIDTH_20_BITS:
		auadc_init_cfg.data_format = AUADC_DATA_FORMAT_20BIT;
		break;
	case AUDIO_PCM_WIDTH_24_BITS:
		auadc_init_cfg.data_format = AUADC_DATA_FORMAT_24BIT;
		break;
	case AUDIO_PCM_WIDTH_32_BITS:
		auadc_init_cfg.data_format = AUADC_DATA_FORMAT_32BIT;
		break;
	default:
		LOG_ERR("Unsupported sample width: %d bits", cfg->sample_width);
		return -ENOTSUP;
	}

	switch (cfg->sampling_rate) {
	case AUDIO_PCM_RATE_8K:
		auadc_init_cfg.sampling_rate = AUADC_SAMPLING_RATE_8K;
		break;
	case AUDIO_PCM_RATE_16K:
		auadc_init_cfg.sampling_rate = AUADC_SAMPLING_RATE_16K;
		break;
	case AUDIO_PCM_RATE_24K:
		auadc_init_cfg.sampling_rate = AUADC_SAMPLING_RATE_24K;
		break;
	case AUDIO_PCM_RATE_32K:
		auadc_init_cfg.sampling_rate = AUADC_SAMPLING_RATE_32K;
		break;
	case AUDIO_PCM_RATE_48K:
		auadc_init_cfg.sampling_rate = AUADC_SAMPLING_RATE_48K;
		break;
	default:
		LOG_ERR("Unsupported sampling rate: %d Hz", cfg->sampling_rate);
		return -ENOTSUP;
	}

	qc7xx_auadc_init(&auadc_hd, &auadc_init_cfg);

	struct qc7xx_auadc_adc_init_config_s auadc_analog_init_cfg = {
		.auadc_analog_en = true,
		.adc_mode = AUADC_ADC_MODE_AUDIO,
		.adc_pga_posi_ch = cfg->input_positive,
		.adc_pga_nega_ch = cfg->input_negative,
		.adc_pga_gain = cfg->gain_db};

	if (cfg->coupling_mode == AC) {
		auadc_analog_init_cfg.adc_pga_mode = cfg->differential
							     ? AUADC_ADC_PGA_MODE_AC_DIFFER
							     : AUADC_ADC_PGA_MODE_AC_SINGLE;
	} else {
		auadc_analog_init_cfg.adc_pga_mode = cfg->differential
							     ? AUADC_ADC_PGA_MODE_DC_DIFFER
							     : AUADC_ADC_PGA_MODE_DC_SINGLE;
	}

	qc7xx_auadc_adc_init(&auadc_hd, &auadc_analog_init_cfg);

	qc7xx_auadc_link_rxdma(&auadc_hd, true);

	struct dma_block_config dma_blk = {0};
	dma_blk.source_address = conf->dma_config.per_fifo_addr;
	dma_blk.dest_address = (uint32_t)cfg->buffer;
	dma_blk.block_size = cfg->buffer_size;
	dma_blk.source_reload_en = 1;
	dma_blk.dest_reload_en = 1;

	struct dma_config dma_cfg = {0};
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_slot = conf->dma_config.dma_slot;
	dma_cfg.complete_callback_en = 0;
	dma_cfg.error_callback_dis = 0;
	dma_cfg.source_data_size = 2;
	dma_cfg.dest_data_size = 2;
	dma_cfg.source_burst_length = 4;
	dma_cfg.dest_burst_length = 4;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;

	ret = dma_config(conf->dma_config.dma_dev, conf->dma_config.dma_channel, &dma_cfg);
	if (ret < 0) {
		LOG_ERR("Error configuring DMA channel %d: %d", conf->dma_config.dma_channel, ret);
		ret = -EINVAL;
		return ret;
	}

	return 0;
}

static int audio_adc_qcc74x_start(const struct device *dev)
{
	const struct audio_adc_qcc74x_config *conf = dev->config;
	struct qc7xx_device_s auadc_hd = {.reg_base = conf->base};
	int ret = 0;

	ret = dma_start(conf->dma_config.dma_dev, conf->dma_config.dma_channel);
	if (ret < 0) {
		LOG_ERR("Error starting DMA channel %d: %d", conf->dma_config.dma_channel, ret);
		ret = -EINVAL;
		return ret;
	}

	qc7xx_auadc_feature_control(&auadc_hd, AUADC_CMD_RECORD_START, 0);

	return 0;
}

static int audio_adc_qcc74x_stop(const struct device *dev)
{
	const struct audio_adc_qcc74x_config *conf = dev->config;
	struct qc7xx_device_s auadc_hd = {.reg_base = conf->base};
	int ret = 0;

	qc7xx_auadc_feature_control(&auadc_hd, AUADC_CMD_RECORD_STOP, 0);

	ret = dma_stop(conf->dma_config.dma_dev, conf->dma_config.dma_channel);
	if (ret < 0) {
		LOG_ERR("Error stopping DMA channel %d: %d", conf->dma_config.dma_channel, ret);
		ret = -EINVAL;
		return ret;
	}

	return 0;
}

/**
 * @brief Initialize Opal audio ADC.
 *
 * @param dev Pointer to device structure.
 * @retval 0 if successful.
 */
static int audio_adc_qcc74x_init(const struct device *dev)
{
	const struct audio_adc_qcc74x_config *conf = dev->config;
	int ret = 0;
	struct qcc74x_clk_settings audio_clk_settings = {
		.enable = CLOCK_ENABLE,
		.clk_div = conf->clk_div,
	};

	ret = pinctrl_apply_state(conf->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Failed to apply pinctrl state: %d", ret);
		return ret;
	}

	ret = clock_control_configure(conf->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)conf->clock_id,
				      (void *)&audio_clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure Audio ADC clock: %d", ret);
	}

	return 0;
}

static DEVICE_API(audio_adc, audio_adc_qcc74x_api) = {
	.configure = audio_adc_qcc74x_configure,
	.start = audio_adc_qcc74x_start,
	.stop = audio_adc_qcc74x_stop,
};

#define AUDIO_ADC_QCC74X_DMA_CONF(inst)                                                              \
	{                                                                                          \
		.dma_dev = (DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_IDX(inst, 0))),                     \
		.dma_channel = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, channel)),                       \
		.dma_slot = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, slot)),                             \
		.per_fifo_addr = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, per_fifo_addr)),               \
	}

#define AUDIO_ADC_QCC74X_DEVICE(inst)                                                                \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static const struct audio_adc_qcc74x_config audio_adc_qcc74x_config_##inst = {                 \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                    \
		.dma_config = AUDIO_ADC_QCC74X_DMA_CONF(inst),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clk_div = DT_PROP(DT_DRV_INST(inst), clk_div),                                    \
	};                                                                                         \
                                                                                                   \
	static struct audio_adc_qcc74x_data audio_adc_qcc74x_data_##inst = {};                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &audio_adc_qcc74x_init, NULL, &audio_adc_qcc74x_data_##inst,       \
			      &audio_adc_qcc74x_config_##inst, POST_KERNEL,                          \
			      CONFIG_AUDIO_ADC_INIT_PRIORITY, &audio_adc_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(AUDIO_ADC_QCC74X_DEVICE)
