/*
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_audio_dac

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/audio/audio_dac.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <qc7xx_audac.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(audio_dac_qcc74x, CONFIG_AUDIO_DAC_LOG_LEVEL);

/**
 * @brief Opal audio DAC driver instance DMA config.
 */
struct audio_dac_qcc74x_dma_config {
	const struct device *dma_dev; /**< DMA device. */
	const uint8_t dma_channel;    /**< DMA channel. */
	const uint32_t dma_slot;      /**< DMA slot. */
	const uint32_t per_fifo_addr; /**< DMA channel source address. */
};

/**
 * @brief Opal audio DAC driver instance config.
 */
struct audio_dac_qcc74x_config {
	uint32_t base;                                     /**< Register base address. */
	const struct pinctrl_dev_config *pincfg;           /**< Pin control configuration. */
	const struct audio_dac_qcc74x_dma_config dma_config; /**< DMA configuration. */
	const struct device *clock_dev;                    /**< Clock device used */
	const clock_control_subsys_t clock_id;             /**< Peripheral ID */
	const uint8_t clk_div;                             /**< Clock division value */
};

/**
 * @brief Opal audio DAC driver instance data.
 */
struct audio_dac_qcc74x_data {
};

static int audio_dac_qcc74x_configure(const struct device *dev, const struct audio_dac_cfg *cfg)
{
	const struct audio_dac_qcc74x_config *conf = dev->config;
	struct qc7xx_device_s audac_hd = {.reg_base = conf->base};
	int ret;

	struct qc7xx_audac_init_config_s audac_init_cfg = {.output_mode = AUDAC_OUTPUT_MODE_PWM,
							   .source_channels_num =
								   AUDAC_SOURCE_CHANNEL_SINGLE,
							   .mixer_mode = AUDAC_MIXER_MODE_ONLY_L,
							   .fifo_threshold = 3};

	switch (cfg->sample_width) {
	case AUDIO_PCM_WIDTH_16_BITS:
		audac_init_cfg.data_format = AUDAC_DATA_FORMAT_16BIT;
		break;
	case AUDIO_PCM_WIDTH_20_BITS:
		audac_init_cfg.data_format = AUDAC_DATA_FORMAT_20BIT;
		break;
	case AUDIO_PCM_WIDTH_24_BITS:
		audac_init_cfg.data_format = AUDAC_DATA_FORMAT_24BIT;
		break;
	case AUDIO_PCM_WIDTH_32_BITS:
		audac_init_cfg.data_format = AUDAC_DATA_FORMAT_32BIT;
		break;
	default:
		LOG_ERR("Unsupported sample width: %d bits", cfg->sample_width);
		return -ENOTSUP;
	}

	switch (cfg->sampling_rate) {
	case AUDIO_PCM_RATE_8K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_8K;
		break;
	case AUDIO_PCM_RATE_16K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_16K;
		break;
	case AUDIO_PCM_RATE_22P05K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_22P05K;
		break;
	case AUDIO_PCM_RATE_24K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_24K;
		break;
	case AUDIO_PCM_RATE_32K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_32K;
		break;
	case AUDIO_PCM_RATE_44P1K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_44P1K;
		break;
	case AUDIO_PCM_RATE_48K:
		audac_init_cfg.sampling_rate = AUDAC_SAMPLING_RATE_48K;
		break;
	default:
		LOG_ERR("Unsupported sampling rate: %d Hz", cfg->sampling_rate);
		return -ENOTSUP;
	}

	qc7xx_audac_init(&audac_hd, &audac_init_cfg);

	qc7xx_audac_feature_control(&audac_hd, AUDAC_CMD_SET_VOLUME_VAL, (size_t)(-5 * 2));
	struct qc7xx_audac_volume_config_s audac_volume_cfg = {
		.mute_ramp_en = true,
		.mute_up_ramp_rate = AUDAC_RAMP_RATE_FS_32,
		.mute_down_ramp_rate = AUDAC_RAMP_RATE_FS_8,
		.volume_update_mode = AUDAC_VOLUME_UPDATE_MODE_RAMP,
		.volume_ramp_rate = AUDAC_RAMP_RATE_FS_128,
		.volume_zero_cross_timeout = AUDAC_RAMP_RATE_FS_128,
	};
	qc7xx_audac_volume_init(&audac_hd, &audac_volume_cfg);

	qc7xx_audac_link_rxdma(&audac_hd, true);

	struct dma_block_config dma_blk = {0};
	dma_blk.source_address = (uint32_t)cfg->buffer;
	dma_blk.dest_address = conf->dma_config.per_fifo_addr;
	dma_blk.block_size = cfg->buffer_size;
	dma_blk.source_reload_en = 1;
	dma_blk.dest_reload_en = 1;

	struct dma_config dma_cfg = {0};
	dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	dma_cfg.dma_slot = conf->dma_config.dma_slot;
	dma_cfg.complete_callback_en = 1;
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

static int audio_dac_qcc74x_start(const struct device *dev)
{
	const struct audio_dac_qcc74x_config *conf = dev->config;
	struct qc7xx_device_s audac_hd = {.reg_base = conf->base};
	int ret = 0;

	ret = dma_start(conf->dma_config.dma_dev, conf->dma_config.dma_channel);
	if (ret < 0) {
		LOG_ERR("Error starting DMA channel %d: %d", conf->dma_config.dma_channel, ret);
		ret = -EINVAL;
		return ret;
	}

	/* Calling AUDAC_CMD_PLAY_START when playback is ongoing stops playback
	 * for some reason. Call AUDAC_CMD_PLAY_STOP always first as a workaround. */
	qc7xx_audac_feature_control(&audac_hd, AUDAC_CMD_PLAY_STOP, 0);
	qc7xx_audac_feature_control(&audac_hd, AUDAC_CMD_PLAY_START, 0);

	return 0;
}

static int audio_dac_qcc74x_stop(const struct device *dev)
{
	const struct audio_dac_qcc74x_config *conf = dev->config;
	struct qc7xx_device_s audac_hd = {.reg_base = conf->base};
	int ret = 0;

	qc7xx_audac_feature_control(&audac_hd, AUDAC_CMD_PLAY_STOP, 0);

	ret = dma_stop(conf->dma_config.dma_dev, conf->dma_config.dma_channel);
	if (ret < 0) {
		LOG_ERR("Error stopping DMA channel %d: %d", conf->dma_config.dma_channel, ret);
		ret = -EINVAL;
		return ret;
	}

	return 0;
}

/**
 * @brief Initialize Opal audio DAC.
 *
 * @param dev Pointer to device structure.
 * @retval 0 if successful.
 */
static int audio_dac_qcc74x_init(const struct device *dev)
{
	const struct audio_dac_qcc74x_config *conf = dev->config;
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
		LOG_ERR("Failed to configure Audio DAC clock: %d", ret);
	}

	return 0;
}

static DEVICE_API(audio_dac, audio_dac_qcc74x_api) = {
	.configure = audio_dac_qcc74x_configure,
	.start = audio_dac_qcc74x_start,
	.stop = audio_dac_qcc74x_stop,
};

#define AUDIO_DAC_QCC74X_DMA_CONF(inst)                                                              \
	{                                                                                          \
		.dma_dev = (DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_IDX(inst, 0))),                     \
		.dma_channel = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, channel)),                       \
		.dma_slot = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, slot)),                             \
		.per_fifo_addr = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, per_fifo_addr)),               \
	}

#define AUDIO_DAC_QCC74X_DEVICE(inst)                                                                \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static const struct audio_dac_qcc74x_config audio_dac_qcc74x_config_##inst = {                 \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                    \
		.dma_config = AUDIO_DAC_QCC74X_DMA_CONF(inst),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clk_div = DT_PROP(DT_DRV_INST(inst), clk_div),                                    \
	};                                                                                         \
                                                                                                   \
	static struct audio_dac_qcc74x_data audio_dac_qcc74x_data_##inst = {};                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &audio_dac_qcc74x_init, NULL, &audio_dac_qcc74x_data_##inst,       \
			      &audio_dac_qcc74x_config_##inst, POST_KERNEL,                          \
			      CONFIG_AUDIO_DAC_INIT_PRIORITY, &audio_dac_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(AUDIO_DAC_QCC74X_DEVICE)
