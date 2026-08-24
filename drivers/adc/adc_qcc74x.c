/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_adc

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#if defined(CONFIG_ADC_QCC74X_DMA)
#include <zephyr/drivers/dma.h>
#endif

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <qc7xx_adc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adc_qcc74x, CONFIG_ADC_LOG_LEVEL);

/**
 * @brief Opal ADC driver instance config.
 */
struct adc_qcc74x_conf {
	uint32_t base;                         /**< Register base address. */
	const struct pinctrl_dev_config *pcfg; /**< Pin control device config. */
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const uint8_t clock_source;
#if defined(CONFIG_ADC_QCC74X_DMA)
	const struct device *dma_dev; /**< DMA device. */
	uint8_t dma_channel;          /**< DMA channel. */
	uint32_t dma_slot;            /**< DMA slot. */
	uint32_t per_fifo_addr;       /**< DMA channel source address. */
#endif
};

/**
 * @brief Opal ADC driver instance data.
 */
struct adc_qcc74x_data {
	/** ADC configuration in Opal HAL format. */
	struct qc7xx_adc_config_s adc_conf;
	/** ADC channel configs. */
	struct adc_channel_cfg adc_channel_cfgs[CONFIG_ADC_QCC74X_CHANNEL_COUNT_SETUP];
	/** Bit-mask of ADC channels for which setup has been called. */
	uint32_t setup_called_channels;
#if defined(CONFIG_ADC_QCC74X_DMA)
	struct k_sem dma_lock; /**< DMA lock semaphore. */
#endif
};

/**
 * @brief Get ADC conversion result from Opal ADC raw format.
 *
 * @param[in] adc Pointer to HAL ADC device.
 * @param[in] raw Conversion result in Opal raw format.
 * @return int16_t Conversion result.
 */
static int32_t adc_qcc74x_get_result(struct qc7xx_device_s *adc, uint32_t raw)
{
	struct qc7xx_adc_result_s result;

	qc7xx_adc_parse_result(adc, &raw, &result, 1);
	LOG_DBG("pos chan %02d, neg chan %02d, %05d mV (raw data: 0x%08x, value: %05d)",
		result.pos_chan, result.neg_chan, result.millivolt, raw, result.value);

	return result.value;
}

/**
 * @brief Set ADC differential mode.
 *
 * QCC74x only supports differential mode off or on for all channels in an ADC sequence. All
 * channels in an ADC sequence must have either differential mode on or off.
 *
 * @param[in] channels Bit-mask of ADC channels.
 * @param[in] channel_cfgs ADC channel configuration.
 * @param[out] differential Differential mode.
 * @retval 0 if successful.
 */
static int adc_qcc74x_set_differential_mode(uint32_t channels,
					  const struct adc_channel_cfg *channel_cfgs,
					  uint8_t *differential)
{
	bool first_round = true;
	while (channels) {
		int bit_index = __builtin_ctz(channels);
		if (first_round) {
			*differential = channel_cfgs[bit_index].differential;
			first_round = false;
		} else {
			if (channel_cfgs[bit_index].differential != *differential) {
				LOG_ERR("Different differential modes in sequence channels are "
					"unsupported");
				return -EINVAL;
			}
		}
		channels &= (channels - 1);
	}

	return 0;
}

/**
 * @brief Create ADC channels used by HAL from Zephyr ADC channels.
 *
 * @param[in] channels Bit-mask of ADC channels.
 * @param[in] adc_channel_cfg ADC channel configuration.
 * @param[out] qcc74x_adc_channels Channel array in QC7xx HAL format.
 */
static void adc_qcc74x_create_channels(uint32_t channels,
				     const struct adc_channel_cfg *adc_channel_cfg,
				     struct qc7xx_adc_channel_s *qcc74x_adc_channels)
{
	int qcc74x_index = 0;
	while (channels) {
		int bit_index = __builtin_ctz(channels);
		qcc74x_adc_channels[qcc74x_index].pos_chan = adc_channel_cfg[bit_index].input_positive;
		qcc74x_adc_channels[qcc74x_index].neg_chan =
			adc_channel_cfg[bit_index].differential
				? adc_channel_cfg[bit_index].input_negative
				: ADC_CHANNEL_GND;
		channels &= (channels - 1);
		qcc74x_index++;
	}
}

/**
 * @brief Set VBAT/2 input state.
 *
 * Enable VBAT/2 input if VBAT/2 is selected as a positive channel in any of the ADC channels.
 * Disable VBAT/2 input otherwise.
 *
 * @param[in] adc Pointer to HAL ADC device.
 * @param[in] qcc74x_adc_channels ADC channels in sequence.
 * @param[in] seq_channel_count Channel count in ADC sequence.
 */
static void adc_qcc74x_set_vbat_state(struct qc7xx_device_s *adc,
				    const struct qc7xx_adc_channel_s *qcc74x_adc_channels,
				    uint8_t seq_channel_count)
{
	for (uint8_t i = 0; i < seq_channel_count; i++) {
		if (qcc74x_adc_channels[i].pos_chan == ADC_CHANNEL_VABT_HALF) {
			qc7xx_adc_vbat_enable(adc);
			return;
		}
	}

	qc7xx_adc_vbat_disable(adc);
}

#if defined(CONFIG_ADC_QCC74X_DMA)

/**
 * @brief Implements (*dma_callback_t)() that is declared in zephyr\include\zephyr\drivers\dma.h.
 *
 * @param[out] dma_lock Pointer to DMA lock semaphore.
 */
static void adc_qcc74x_dma_done_callback(const struct device *dma_dev, void *dma_lock,
				       uint32_t channel, int status)
{
	ARG_UNUSED(dma_dev);

	k_sem_give((struct k_sem *)dma_lock);
	if (status != DMA_STATUS_COMPLETE) {
		LOG_ERR("DMA transfer callback status %d != DMA_STATUS_COMPLETE", status);
	}
}

/**
 * @brief Enable DMA channel and start DMA transfer.
 *
 * @param[in] dev Pointer to device structure.
 * @param[out] buf DMA transfer destination buffer.
 * @param[in] len Buffer length in bytes.
 * @retval 0 if successful.
 */
static int adc_qcc74x_dma_start(const struct device *dev, uint32_t *buf, uint32_t len)
{
	const struct adc_qcc74x_conf *conf = dev->config;
	const struct adc_qcc74x_data *data = dev->data;
	struct dma_block_config dma_blk = {0};
	struct dma_config dma_cfg = {0};
	int ret = 0;

	dma_blk.source_address = conf->per_fifo_addr;
	dma_blk.dest_address = (uint32_t)buf;
	dma_blk.block_size = len;

	dma_cfg.dma_slot = conf->dma_slot;
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.source_data_size = 4;
	dma_cfg.dest_data_size = 4;
	dma_cfg.source_burst_length = 1;
	dma_cfg.dest_burst_length = 1;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;
	dma_cfg.user_data = (void *)&data->dma_lock;
	dma_cfg.dma_callback = adc_qcc74x_dma_done_callback;

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

	return 0;
}

/**
 * @brief Stop DMA transfer and disable DMA channel.
 *
 * @param[in] dev Pointer to device structure.
 * @retval 0 if successful.
 */
static int adc_qcc74x_dma_stop(const struct device *dev)
{
	const struct adc_qcc74x_conf *conf = dev->config;
	int ret = 0;

	ret = dma_stop(conf->dma_dev, conf->dma_channel);
	if (ret) {
		LOG_ERR("Error stopping DMA: %d", ret);
		return ret;
	}

	return 0;
}

#else /* defined(CONFIG_ADC_QCC74X_DMA) */

/**
 * @brief Read channel results of one sequence into buffer.
 *
 * @param[in] adc Pointer to HAL ADC device.
 * @param[in] seq_channel_count Count of channels in sequence.
 * @param[out] buffer Buffer to store readings.
 */
static void adc_qcc74x_read_sequence_to_buffer(struct qc7xx_device_s *adc, uint8_t seq_channel_count,
					     int32_t **buffer)
{
	for (uint16_t i = 0; i < seq_channel_count; i++) {
		while (qc7xx_adc_get_count(adc) == 0) {
			__asm__ volatile("nop");
		}
		**buffer = adc_qcc74x_get_result(adc, qc7xx_adc_read_raw(adc));
		(*buffer)++;
	}
}

#endif /* defined(CONFIG_ADC_QCC74X_DMA) */

/**
 * @brief Do conversion in ADC hardware and store result in seq->buffer.
 *
 * @param[in] dev Pointer to device structure.
 * @param[in,out] seq Structure specifying requested sequence of samplings.
 * @retval 0 if successful.
 */
static int adc_qcc74x_convert(const struct device *dev, const struct adc_sequence *seq)
{
	const struct adc_qcc74x_conf *conf = dev->config;
	struct qc7xx_device_s adc = {.reg_base = conf->base};
	uint16_t sampling_count = seq->options ? 1 + seq->options->extra_samplings : 1;
	uint8_t seq_channel_count = __builtin_popcount(seq->channels);
	int32_t *buffer = seq->buffer;

#if defined(CONFIG_ADC_QCC74X_DMA)
	struct adc_qcc74x_data *data = dev->data;

	qc7xx_adc_link_rxdma(&adc, true);
	uint32_t len = (sampling_count * seq_channel_count) * sizeof(int32_t);
	int32_t *buf = k_aligned_alloc(32, len);
	int ret = 0;
	/* Modify buffer address to non-cached area. */
	int32_t *dma_buffer = (uint32_t *)((uint32_t)buf & ~0x40000000);

	ret = adc_qcc74x_dma_start(dev, dma_buffer, len);
	if (ret) {
		return ret;
	}

	qc7xx_adc_start_conversion(&adc);
	k_sem_take(&data->dma_lock, K_FOREVER);
	qc7xx_adc_stop_conversion(&adc);

	ret = adc_qcc74x_dma_stop(dev);
	if (ret) {
		return ret;
	}

	for (int i = 0; i < (sampling_count * seq_channel_count); i++) {
		*buffer++ = adc_qcc74x_get_result(&adc, dma_buffer[i]);
	}

	k_free(buf);

#else  /* defined(CONFIG_ADC_QCC74X_DMA) */

	uint16_t sampling_index = 0;

	qc7xx_adc_feature_control(&adc, ADC_CMD_CLR_FIFO, 0);
	qc7xx_adc_start_conversion(&adc);

	while (sampling_index < sampling_count) {
		adc_qcc74x_read_sequence_to_buffer(&adc, seq_channel_count, &buffer);
		if (seq->options && seq->options->callback) {
			switch (seq->options->callback(dev, seq, sampling_index)) {
			case ADC_ACTION_REPEAT:
				buffer = buffer - seq_channel_count;
				break;
			case ADC_ACTION_FINISH:
				sampling_index = sampling_count;
				break;
			case ADC_ACTION_CONTINUE:
			default:
				sampling_index++;
				break;
			}
		} else {
			sampling_index++;
		}
	}

	qc7xx_adc_stop_conversion(&adc);
#endif /* defined(CONFIG_ADC_QCC74X_DMA) */

	return 0;
}

static int adc_qcc74x_read(const struct device *dev, const struct adc_sequence *seq)
{
	struct adc_qcc74x_data *data = dev->data;
	const struct adc_qcc74x_conf *conf = dev->config;

	struct adc_channel_cfg *channel_cfgs = data->adc_channel_cfgs;
	struct qc7xx_adc_channel_s qcc74x_adc_channels[CONFIG_ADC_QCC74X_CHANNEL_COUNT];
	struct qc7xx_adc_config_s *adc_conf = &data->adc_conf;
	struct qc7xx_device_s adc = {.reg_base = conf->base};
	uint16_t sampling_count = seq->options ? 1 + seq->options->extra_samplings : 1;
	uint8_t seq_channel_count = __builtin_popcount(seq->channels);
	int32_t *buffer = seq->buffer;
	int ret = 0;

	LOG_INF("seq.channels: 0x%x", seq->channels);

	if (seq_channel_count < 1 || seq_channel_count > CONFIG_ADC_QCC74X_CHANNEL_COUNT) {
		LOG_ERR("Sequence channel count %d is < 1 or > allowed channel count %u",
			seq_channel_count, CONFIG_ADC_QCC74X_CHANNEL_COUNT);
		return -EINVAL;
	}

	{
		size_t needed_buffer_size = sampling_count * seq_channel_count * sizeof(*buffer);
		if (seq->buffer_size < needed_buffer_size) {
			LOG_ERR("Provided buffer size %u is < needed buffer size %u",
				seq->buffer_size, needed_buffer_size);
			return -ENOMEM;
		}
	}

	if ((data->setup_called_channels & seq->channels) != seq->channels) {
		LOG_ERR("Setup is called for channels %d, but not for all channels %d",
			data->setup_called_channels, seq->channels);
		return -EINVAL;
	}

	if (seq->oversampling != 0U) {
		LOG_ERR("Setting oversampling value is unsupported");
		return -EINVAL;
	}

	if (seq->resolution == 12) {
		adc_conf->resolution = ADC_RESOLUTION_12B;
	} else if (seq->resolution == 14) {
		adc_conf->resolution = ADC_RESOLUTION_14B;
	} else if (seq->resolution == 16) {
		adc_conf->resolution = ADC_RESOLUTION_16B;
	} else {
		LOG_ERR("Unsupported resolution: %d", seq->resolution);
		return -EINVAL;
	}

	ret = adc_qcc74x_set_differential_mode(seq->channels, channel_cfgs,
					     &adc_conf->differential_mode);
	if (ret) {
		return ret;
	}

	adc_qcc74x_create_channels(seq->channels, channel_cfgs, qcc74x_adc_channels);
	qc7xx_adc_init(&adc, adc_conf);
	qc7xx_adc_channel_config(&adc, qcc74x_adc_channels, seq_channel_count);
	adc_qcc74x_set_vbat_state(&adc, qcc74x_adc_channels, seq_channel_count);

	ret = adc_qcc74x_convert(dev, seq);
	if (ret) {
		return ret;
	}

	return 0;
}

static int adc_qcc74x_channel_setup(const struct device *dev,
				  const struct adc_channel_cfg *channel_cfg)
{
	struct adc_qcc74x_data *data = dev->data;

	uint8_t channel_id = channel_cfg->channel_id;

	LOG_INF("channel_id: %u, input_positive: %u input_negative: %u", (unsigned)channel_id,
		(unsigned)channel_cfg->input_positive, (unsigned)channel_cfg->input_negative);

	if (channel_id >= CONFIG_ADC_QCC74X_CHANNEL_COUNT_SETUP) {
		LOG_ERR("Channel id %u is >= maximum channel count %u", channel_id,
			CONFIG_ADC_QCC74X_CHANNEL_COUNT_SETUP);
		return -EINVAL;
	}

	if (channel_cfg->acquisition_time != ADC_ACQ_TIME_DEFAULT) {
		LOG_ERR("Unsupported acquisition_time: %u", channel_cfg->acquisition_time);
		return -EINVAL;
	}

	if (channel_cfg->gain != ADC_GAIN_1) {
		LOG_ERR("Unsupported gain: %u", channel_cfg->gain);
		return -EINVAL;
	}

	if (channel_cfg->reference != ADC_REF_INTERNAL) {
		LOG_ERR("Unsupported reference: %u", channel_cfg->reference);
		return -EINVAL;
	}

	data->adc_channel_cfgs[channel_id] = *channel_cfg;
	data->setup_called_channels |= (1U << channel_id);

	return 0;
}

/**
 * @brief Initialize Opal ADC
 *
 * @param dev Pointer to device structure.
 * @retval 0 if successful.
 */
static int adc_qcc74x_init(const struct device *dev)
{
	const struct adc_qcc74x_conf *conf = dev->config;
	struct adc_qcc74x_data *data = dev->data;
	struct qcc74x_clk_settings clk_settings = {
			.enable = CLOCK_ENABLE,
			.clk_src = conf->clock_source,
			.clk_div = data->adc_conf.clk_div,
	};
	int ret = 0;

	if (conf->pcfg && conf->pcfg->state_cnt > 0) {
		ret = pinctrl_apply_state(conf->pcfg, PINCTRL_STATE_DEFAULT);
		if (ret) {
			LOG_WRN("ADC pinctrl setup failed: %d", ret);
		}
	} else {
		LOG_INF("ADC pinctrl not required or not configured");
	}

	ret = clock_control_on(conf->clock_dev,
			  (clock_control_subsys_t)(uintptr_t)conf->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for ADC driver is already enabled");
		} else {
			LOG_ERR("Failed to enable ADC clock: %d", ret);
			return ret;
		}
	}

	ret = clock_control_configure(conf->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)conf->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure ADC clock: %d", ret);
		return ret;
	}

#if defined(CONFIG_ADC_QCC74X_DMA)
	k_sem_init(&data->dma_lock, 0, 1);
#endif /* defined(CONFIG_ADC_QCC74X_DMA) */

	return 0;
}

static DEVICE_API(adc, adc_qcc74x_api) = {
	.channel_setup = adc_qcc74x_channel_setup,
	.read = adc_qcc74x_read,
	.ref_internal = 3200,
};

#if defined(CONFIG_ADC_QCC74X_DMA)
#define ADC_QCC74X_CONF_DMA_INIT(inst)                                                               \
	.dma_dev = (DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_IDX(inst, 0))),                             \
	.dma_channel = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, channel)),                               \
	.dma_slot = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, slot)),                                     \
	.per_fifo_addr = (DT_INST_DMAS_CELL_BY_IDX(inst, 0, per_fifo_addr)),

#else
#define ADC_QCC74X_CONF_DMA_INIT(n)
#endif /* defined(CONFIG_ADC_QCC74X_DMA) */

#define ADC_QCC74X_INIT(inst)                                                                        \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static const struct adc_qcc74x_conf adc_qcc74x_conf_##inst = {                                 \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(inst, source),                        \
		ADC_QCC74X_CONF_DMA_INIT(inst)};                                                     \
                                                                                                   \
	static struct adc_qcc74x_data adc_qcc74x_data_##inst = {                                       \
		.adc_conf =                                                                        \
			{                                                                          \
				.clk_div = DT_PROP(DT_DRV_INST(inst), clk_div),                    \
				.scan_conv_mode = 1,                                               \
				.continuous_conv_mode = 1,                                         \
				.vref = ADC_VREF_3P2V,                                             \
			},                                                                         \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &adc_qcc74x_init, NULL, &adc_qcc74x_data_##inst,                   \
			      &adc_qcc74x_conf_##inst, POST_KERNEL, CONFIG_ADC_INIT_PRIORITY,        \
			      &adc_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_QCC74X_INIT)
