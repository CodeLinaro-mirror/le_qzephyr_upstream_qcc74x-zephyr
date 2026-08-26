/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_dma

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_qcc74x.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/cache.h>

#include <qc7xx_dma.h>
#include <hardware/dma_reg.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dma_qcc74x, CONFIG_DMA_LOG_LEVEL);

#define DMA_CHANNEL_OFFSET (0x100U)

struct dma_qcc74x_channel {
	struct qc7xx_device_s dma_ch_dev;
	struct qc7xx_dma_channel_config_s dma_ch_cfg;
	struct qc7xx_dma_channel_lli_transfer_s transfers[CONFIG_DMA_CH_TRANSFER_BLOCK_MAX];
	struct qc7xx_dma_channel_lli_pool_s lli_pools[CONFIG_DMA_CH_TRANSFER_LLI_MAX];
	uint32_t transfers_used_count;
	uint32_t lli_used_count;
	dma_callback_t callback;
	void *user_data;
	uint32_t direction;
	uint32_t channel_id;
	uint32_t periph_id;
	uint32_t host_id;
	bool busy;
};

struct dma_qcc74x_data {
	struct dma_context ctx;
	struct dma_qcc74x_channel *channels;
	uint8_t instance_id;
};

struct dma_qcc74x_config {
	uint32_t base;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	void (*irq_configure)(void);
};

/**
 * @brief Clean DMA source address cache.
 *
 * @param channel DMA channel.
 */
static void dma_qcc74x_src_addr_cache_clean(struct dma_qcc74x_channel *channel)
{
	for (int i = 0; i < channel->transfers_used_count; i++) {
		if (qc7xx_check_cache_addr((void *)channel->transfers[i].src_addr) == false) {
			continue;
		}

		/* XIP flash (0xa0xxxxxx) is read-only; its cache lines can
		 * never be dirty.  Calling sys_cache_data_flush_range on an
		 * XIP address triggers a global D-cache clean on this
		 * hardware (~52 ms), with no benefit.  Skip it entirely. */
		if ((channel->transfers[i].src_addr >> 24) == 0xa0U) {
			continue;
		}

		if (channel->transfers[i].src_addr % sys_cache_data_line_size_get() != 0) {
			LOG_WRN("DMA channel %d src_addr 0x%08x is not cache line aligned",
				channel->channel_id, channel->transfers[i].src_addr);
		}

		if (channel->dma_ch_cfg.src_addr_inc == DMA_ADDR_INCREMENT_ENABLE) {
			uint32_t line  = sys_cache_data_line_size_get();
			uint32_t start = ROUND_DOWN(channel->transfers[i].src_addr, line);
			uint32_t end   = ROUND_UP(channel->transfers[i].src_addr +
						  channel->transfers[i].nbytes, line);
			sys_cache_data_flush_range((void *)(uintptr_t)start, end - start);
		} else {
			uint32_t line  = sys_cache_data_line_size_get();
			uint32_t start = ROUND_DOWN(channel->transfers[i].src_addr, line);
			uint32_t end   = ROUND_UP(channel->transfers[i].src_addr +
						  channel->dma_ch_cfg.src_width, line);
			sys_cache_data_flush_range((void *)(uintptr_t)start, end - start);
		}
	}
}

/**
 * @brief Invalidate DMA destination address cache.
 *
 * @param dev     Pointer to dma device structure.
 * @param channel DMA channel.
 */
static void dma_qcc74x_dest_addr_cache_invalidate_priv(struct dma_qcc74x_channel *channel)
{
	for (int i = 0; i < channel->transfers_used_count; i++) {
		if (qc7xx_check_cache_addr((void *)channel->transfers[i].dst_addr) == false) {
			continue;
		}

		if (channel->transfers[i].dst_addr % sys_cache_data_line_size_get() != 0) {
			LOG_WRN("DMA channel %d dst_addr 0x%08x is not cache line aligned",
				channel->channel_id, channel->transfers[i].dst_addr);
		}

		if (channel->dma_ch_cfg.dst_addr_inc == DMA_ADDR_INCREMENT_ENABLE) {
			uint32_t line  = sys_cache_data_line_size_get();
			uint32_t start = ROUND_DOWN(channel->transfers[i].dst_addr, line);
			uint32_t end   = ROUND_UP(channel->transfers[i].dst_addr +
						  channel->transfers[i].nbytes, line);
			sys_cache_data_flush_and_invd_range((void *)(uintptr_t)start, end - start);
		} else {
			uint32_t line  = sys_cache_data_line_size_get();
			uint32_t start = ROUND_DOWN(channel->transfers[i].dst_addr, line);
			uint32_t end   = ROUND_UP(channel->transfers[i].dst_addr +
						  channel->dma_ch_cfg.dst_width, line);
			sys_cache_data_flush_and_invd_range((void *)(uintptr_t)start, end - start);
		}
	}
}

/**
 * @brief DMA interrupt service routine.
 *
 * @param dev Pointer to device structure.
 */
static void dma_qcc74x_isr(const struct device *dev)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	struct dma_qcc74x_channel *dma_channel;

	for (int i = 0; i < data->ctx.dma_channels; i++) {
		dma_channel = &data->channels[i];
		if (qc7xx_dma_channel_get_tcint_status(&dma_channel->dma_ch_dev) == false) {
			continue;
		}
		qc7xx_dma_channel_tcint_clear(&dma_channel->dma_ch_dev);

		if (dma_channel->transfers_used_count == 0) {
			LOG_WRN("DMA channel %d transfer count is 0", dma_channel->channel_id);
			continue;
		}

		bool busy = qc7xx_dma_channel_isbusy(&dma_channel->dma_ch_dev);
		uint32_t pending = qc7xx_dma_feature_control(&dma_channel->dma_ch_dev,
							     DMA_CMD_GET_TRANSFER_PENDING, 0);

		/*
		 * Treat an IRQ with zero pending length as transfer complete even
		 * if the BUSY bit has not deasserted yet. This avoids missing the
		 * final completion when hardware clears BUSY slightly later.
		 */
		if (!busy || pending == 0U) {
			dma_qcc74x_dest_addr_cache_invalidate_priv(dma_channel);
			dma_channel->busy = false;
			dma_channel->transfers_used_count = 0;
			dma_channel->lli_used_count = 0;
			if (dma_channel->callback) {
				dma_channel->callback(dev, dma_channel->user_data, i,
						      DMA_STATUS_COMPLETE);
			}
		} else {
			if (dma_channel->callback) {
				dma_channel->callback(dev, dma_channel->user_data, i,
						      DMA_STATUS_BLOCK);
			}
		}
	}
}

/**
 * @brief Initialize DMA channel.
 *
 * @param dev     DMA device.
 * @param channel DMA channel.
 * @param config  DMA configuration.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int dma_qcc74x_channel_init(const struct device *dev,
				 struct dma_qcc74x_channel *channel,
				 struct dma_config *config)
{
	uint32_t src_req = 0U;
	uint32_t dst_req = 0U;
	uint8_t direction = 0U;
	uint8_t src_addr_inc = 0U;
	uint8_t dst_addr_inc = 0U;
	uint8_t src_burst_count = 0U;
	uint8_t dst_burst_count = 0U;
	uint8_t src_width = 0U;
	uint8_t dst_width = 0U;
	int ret = 0;
	const struct dma_qcc74x_config *const dev_cfg = dev->config;

	/* Enable clock for this channel */
	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&channel->channel_id);
	if (ret != 0) {
		LOG_ERR("Failed to configure DMA channel: %u err: %d",
			channel->channel_id,
			ret);
		return ret;
	}

	switch (config->channel_direction) {
	case MEMORY_TO_MEMORY:
		direction = DMA_MEMORY_TO_MEMORY;
		src_req = DMA_REQUEST_NONE;
		dst_req = DMA_REQUEST_NONE;
		src_addr_inc = DMA_ADDR_INCREMENT_ENABLE;
		dst_addr_inc = DMA_ADDR_INCREMENT_ENABLE;
		break;
	case MEMORY_TO_PERIPHERAL:
		direction = DMA_MEMORY_TO_PERIPH;
		src_req = DMA_REQUEST_NONE;
		dst_req = config->dma_slot;
		src_addr_inc = DMA_ADDR_INCREMENT_ENABLE;
		dst_addr_inc = DMA_ADDR_INCREMENT_DISABLE;
		break;
	case PERIPHERAL_TO_MEMORY:
		direction = DMA_PERIPH_TO_MEMORY;
		src_req = config->dma_slot;
		dst_req = DMA_REQUEST_NONE;
		src_addr_inc = DMA_ADDR_INCREMENT_DISABLE;
		dst_addr_inc = DMA_ADDR_INCREMENT_ENABLE;
		break;
	default:
		LOG_ERR("Unsupported channel_direction %d", config->channel_direction);
		return -EINVAL;
	}

	switch (config->source_burst_length) {
	case 1:
		src_burst_count = DMA_BURST_INCR1;
		break;
	case 4:
		src_burst_count = DMA_BURST_INCR4;
		break;
	case 8:
		src_burst_count = DMA_BURST_INCR8;
		break;
	case 16:
		src_burst_count = DMA_BURST_INCR16;
		break;
	default:
		LOG_ERR("Unsupported source_burst_length %d", config->source_burst_length);
		return -EINVAL;
	}

	switch (config->dest_burst_length) {
	case 1:
		dst_burst_count = DMA_BURST_INCR1;
		break;
	case 4:
		dst_burst_count = DMA_BURST_INCR4;
		break;
	case 8:
		dst_burst_count = DMA_BURST_INCR8;
		break;
	case 16:
		dst_burst_count = DMA_BURST_INCR16;
		break;
	default:
		LOG_ERR("Unsupported dest_burst_length %d", config->dest_burst_length);
		return -EINVAL;
	}

	switch (config->source_data_size) {
	case 1:
		src_width = DMA_DATA_WIDTH_8BIT;
		break;
	case 2:
		src_width = DMA_DATA_WIDTH_16BIT;
		break;
	case 4:
		src_width = DMA_DATA_WIDTH_32BIT;
		break;
	default:
		LOG_ERR("Unsupported source_data_size %d", config->source_data_size);
		return -EINVAL;
	}

	switch (config->dest_data_size) {
	case 1:
		dst_width = DMA_DATA_WIDTH_8BIT;
		break;
	case 2:
		dst_width = DMA_DATA_WIDTH_16BIT;
		break;
	case 4:
		dst_width = DMA_DATA_WIDTH_32BIT;
		break;
	default:
		LOG_ERR("Unsupported dest_data_size %d", config->dest_data_size);
		return -EINVAL;
	}

	struct qc7xx_dma_channel_config_s *dma_ch_cfg = &channel->dma_ch_cfg;
	dma_ch_cfg->direction = direction;
	dma_ch_cfg->src_req = src_req;
	dma_ch_cfg->dst_req = dst_req;
	dma_ch_cfg->src_addr_inc = src_addr_inc;
	dma_ch_cfg->dst_addr_inc = dst_addr_inc;
	dma_ch_cfg->src_burst_count = src_burst_count;
	dma_ch_cfg->dst_burst_count = dst_burst_count;
	dma_ch_cfg->src_width = src_width;
	dma_ch_cfg->dst_width = dst_width;

	qc7xx_dma_channel_init(&channel->dma_ch_dev, dma_ch_cfg);

	return 0;
}

void dma_qcc74x_dest_addr_cache_invalidate(const struct device *dev, const uint32_t channel)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	dma_qcc74x_dest_addr_cache_invalidate_priv(&data->channels[channel]);
}

static int dma_qcc74x_configure(const struct device *dev, uint32_t channel, struct dma_config *config)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	struct dma_qcc74x_channel *dma_channel;
	int lli_cnt = 0;
	int ret = 0;

	if (channel >= data->ctx.dma_channels) {
		LOG_ERR("Unsupported channel %d", channel);
		return -EINVAL;
	}

	if (config->block_count > CONFIG_DMA_CH_TRANSFER_BLOCK_MAX) {
		LOG_ERR("Unsupported block_count %d", config->block_count);
		return -EINVAL;
	}

	dma_channel = &data->channels[channel];

	ret = dma_qcc74x_channel_init(dev, dma_channel, config);
	if (ret < 0) {
		LOG_ERR("DMA channel %d init failed", channel);
		return ret;
	}

	struct dma_block_config *block_cfg = config->head_block;
	for (int i = 0; i < config->block_count; i++) {
		if (block_cfg == NULL) {
			LOG_ERR("DMA block_cfg is NULL for block %d", i);
			return -EINVAL;
		}
		dma_channel->transfers[i].src_addr = block_cfg->source_address;
		dma_channel->transfers[i].dst_addr = block_cfg->dest_address;
		dma_channel->transfers[i].nbytes = block_cfg->block_size;
		block_cfg = block_cfg->next_block;
	}

	if (config->head_block->source_reload_en != config->head_block->dest_reload_en) {
		LOG_ERR("source_reload_en and dest_reload_en must be the same");
		return -EINVAL;
	}

	if (config->block_count) {
		lli_cnt = qc7xx_dma_channel_lli_reload(&dma_channel->dma_ch_dev,
						       dma_channel->lli_pools,
						       CONFIG_DMA_CH_TRANSFER_LLI_MAX,
						       dma_channel->transfers, config->block_count);
		if (lli_cnt < 0) {
			LOG_ERR("DMA channel %d lli reload failed", channel);
			return -EINVAL;
		}
		if (config->head_block->source_reload_en || config->head_block->dest_reload_en) {
			qc7xx_dma_channel_lli_link_head(&dma_channel->dma_ch_dev,
							dma_channel->lli_pools, lli_cnt);
		}
	}

	dma_channel->transfers_used_count = config->block_count;
	dma_channel->lli_used_count = lli_cnt;
	dma_channel->callback = config->dma_callback;
	dma_channel->user_data = config->user_data;
	dma_channel->direction = config->channel_direction;
	dma_channel->busy = false;

	dma_qcc74x_src_addr_cache_clean(dma_channel);
	dma_qcc74x_dest_addr_cache_invalidate_priv(dma_channel);

	qc7xx_dma_channel_tcint_mask(&dma_channel->dma_ch_dev, false);
	return 0;
}

static int dma_qcc74x_start(const struct device *dev, uint32_t channel)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	struct dma_qcc74x_channel *dma_channel;

	if (channel >= data->ctx.dma_channels) {
		LOG_ERR("Unsupported channel %d", channel);
		return -EINVAL;
	}
	dma_channel = &data->channels[channel];

	dma_channel->busy = true;
	qc7xx_dma_channel_start(&dma_channel->dma_ch_dev);

	return 0;
}

static int dma_qcc74x_stop(const struct device *dev, uint32_t channel)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	struct dma_qcc74x_channel *dma_channel;

	if (channel >= data->ctx.dma_channels) {
		LOG_ERR("Unsupported channel %d", channel);
		return -EINVAL;
	}
	dma_channel = &data->channels[channel];

	qc7xx_dma_channel_stop(&dma_channel->dma_ch_dev);
	qc7xx_dma_channel_tcint_clear(&dma_channel->dma_ch_dev);
	dma_channel->busy = false;

	return 0;
}

static int dma_qcc74x_get_status(const struct device *dev, uint32_t channel, struct dma_status *stat)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	struct dma_qcc74x_channel *dma_channel;

	if (channel >= data->ctx.dma_channels) {
		LOG_ERR("Unsupported channel %d", channel);
		return -EINVAL;
	}
	dma_channel = &data->channels[channel];

	if (!stat) {
		return -EINVAL;
	}

	stat->dir = dma_channel->direction;
	stat->pending_length = qc7xx_dma_feature_control(&dma_channel->dma_ch_dev,
							 DMA_CMD_GET_TRANSFER_PENDING, 0);
	stat->busy = qc7xx_dma_channel_isbusy(&dma_channel->dma_ch_dev);

	return 0;
}

static int dma_qcc74x_reload(const struct device *dev, uint32_t channel, uint32_t src, uint32_t dst,
			   size_t size)
{
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *)dev->data;
	struct dma_qcc74x_channel *dma_channel;

	if (channel >= data->ctx.dma_channels) {
		LOG_ERR("Unsupported channel %d", channel);
		return -EINVAL;
	}
	dma_channel = &data->channels[channel];

	if (qc7xx_dma_channel_isbusy(&dma_channel->dma_ch_dev)) {
		LOG_ERR("DMA channel %d is busy", channel);
		return -EBUSY;
	}

	dma_channel->transfers[0].src_addr = src;
	dma_channel->transfers[0].dst_addr = dst;
	dma_channel->transfers[0].nbytes = size;

	int lli_cnt = qc7xx_dma_channel_lli_reload(&dma_channel->dma_ch_dev, dma_channel->lli_pools,
						   CONFIG_DMA_CH_TRANSFER_LLI_MAX,
						   dma_channel->transfers, 1);
	if (lli_cnt < 0) {
		LOG_ERR("DMA channel %d lli reload failed", channel);
		return -EINVAL;
	}

	dma_channel->transfers_used_count = 1;
	dma_channel->lli_used_count = lli_cnt;
	dma_channel->busy = false;

	dma_qcc74x_src_addr_cache_clean(dma_channel);
	dma_qcc74x_dest_addr_cache_invalidate_priv(dma_channel);

	return 0;
}

/**
 * @brief Initialize Opal DMA driver instance.
 *
 * @param dev Pointer to device structure.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int dma_qcc74x_init(const struct device *dev)
{
	struct dma_qcc74x_config *config = (struct dma_qcc74x_config *)dev->config;
	struct dma_qcc74x_data *data = (struct dma_qcc74x_data *const)(dev)->data;
	int ret = 0;

	config->irq_configure();

	ret = clock_control_on(config->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for DMA driver is already enabled");
		} else {
			LOG_ERR("Failed to enable DMA clock: %d", ret);
			return ret;
		}
	}

	for (int ch = 0; ch < data->ctx.dma_channels; ch++) {
		struct qc7xx_device_s *dma_ch_dev = &data->channels[ch].dma_ch_dev;
		dma_ch_dev->reg_base = config->base + (ch + 1) * DMA_CHANNEL_OFFSET;
		dma_ch_dev->idx = 0;
		dma_ch_dev->sub_idx = ch;
		data->channels[ch].channel_id = ch;
	}

	return 0;
}

static DEVICE_API(dma, dma_qcc74x_api) = {
	.config = dma_qcc74x_configure,
	.start = dma_qcc74x_start,
	.stop = dma_qcc74x_stop,
	.get_status = dma_qcc74x_get_status,
	.reload = dma_qcc74x_reload,
};

#define IRQ_CONFIGURE(n, inst)                                                                     \
	IRQ_CONNECT(DT_INST_IRQ_BY_IDX(inst, n, irq), DT_INST_IRQ_BY_IDX(inst, n, priority),       \
		    dma_qcc74x_isr, DEVICE_DT_INST_GET(inst), 0);                                    \
	irq_enable(DT_INST_IRQ_BY_IDX(inst, n, irq));

#define CONFIGURE_ALL_IRQS(inst, n) LISTIFY(n, IRQ_CONFIGURE, (), inst)

#define DMA_QCC74X_DEVICE(inst)                                                                      \
	static void dma_qcc74x##inst##_irq_configure(void)                                           \
	{                                                                                          \
		CONFIGURE_ALL_IRQS(inst, DT_NUM_IRQS(DT_DRV_INST(inst)));                          \
	}                                                                                          \
                                                                                                   \
	static const struct dma_qcc74x_config dma_config_##inst = {                                  \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.irq_configure = dma_qcc74x##inst##_irq_configure,                                   \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
	};                                                                                         \
                                                                                                   \
	static struct dma_qcc74x_channel                                                             \
		dma_qcc74x##inst##_channels[DT_INST_PROP(inst, dma_channels)];                       \
                                                                                                   \
	ATOMIC_DEFINE(dma_qcc74x_atomic##inst, DT_INST_PROP(inst, dma_channels));                    \
                                                                                                   \
	static struct dma_qcc74x_data dma_data_##inst = {                                            \
		.ctx =                                                                             \
			{                                                                          \
				.magic = DMA_MAGIC,                                                \
				.atomic = dma_qcc74x_atomic##inst,                                   \
				.dma_channels = DT_INST_PROP(inst, dma_channels),                  \
			},                                                                         \
		.channels = dma_qcc74x##inst##_channels,                                             \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &dma_qcc74x_init, NULL, &dma_data_##inst, &dma_config_##inst,    \
			      POST_KERNEL, CONFIG_DMA_INIT_PRIORITY, &dma_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(DMA_QCC74X_DEVICE)
