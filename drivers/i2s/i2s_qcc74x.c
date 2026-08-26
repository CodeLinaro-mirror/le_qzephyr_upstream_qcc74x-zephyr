/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_i2s

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <qc7xx_i2s.h>
#include <hardware/i2s_reg.h>
#include <qc7xx_dma.h>
#include <qc7xx_common.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(i2s_qcc74x, CONFIG_I2S_LOG_LEVEL);

struct i2s_qcc74x_queue_item {
	void *mem_block;
	size_t size;
};

struct i2s_qcc74x_stream {
	int32_t state;
	struct i2s_config i2s_cfg;
	const struct device *dma_dev;
	uint32_t dma_channel;
	uint32_t dma_slot;
	uint32_t per_fifo_addr;
	struct k_msgq queue;
	struct i2s_qcc74x_queue_item current_item;
	bool drain_flag;
	bool tx_waiting_refill;
	int64_t tx_refill_deadline;
};

struct i2s_qcc74x_cfg {
	uint32_t base;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_cfg_func)(void);
	const struct reset_dt_spec reset;
	const uint8_t diclk;
	const uint8_t doclk;
	const uint8_t div;
};

struct i2s_qcc74x_data {
	struct qc7xx_device_s i2s_dev;
	struct i2s_qcc74x_stream tx;
	struct i2s_qcc74x_stream rx;
};

#if defined(CONFIG_DCACHE_LINE_SIZE) && (CONFIG_DCACHE_LINE_SIZE > 0)
#define I2S_QCC74X_DMA_BUF_ALIGN CONFIG_DCACHE_LINE_SIZE
#else
#define I2S_QCC74X_DMA_BUF_ALIGN 32
#endif

#define I2S_QCC74X_TX_REFILL_GRACE_BLOCKS 2U

static int i2s_qcc74x_get_dma_params(const struct i2s_config *cfg, size_t block_size,
				   uint32_t *sample_size, uint32_t *burst_length)
{
	uint32_t ss;

	if (cfg == NULL || sample_size == NULL || burst_length == NULL) {
		return -EINVAL;
	}

	ss = cfg->word_size / 8U;
	if (ss != 1U && ss != 2U && ss != 4U) {
		return -EINVAL;
	}

	if ((block_size % ss) != 0U) {
		return -EINVAL;
	}

	*sample_size = ss;
	*burst_length = 4U;

	return 0;
}

static inline uint32_t i2s_qcc74x_get_dma_addr(const void *addr)
{
	void *no_cache_addr = qc7xx_get_no_cache_addr(addr);

	if (no_cache_addr != NULL) {
		return (uint32_t)(uintptr_t)no_cache_addr;
	}

	return (uint32_t)(uintptr_t)addr;
}

static inline void i2s_qcc74x_prepare_tx_dma_buffer(const void *addr, size_t size)
{
	if (qc7xx_check_cache_addr(addr)) {
		(void)cache_data_flush_range((void *)addr, size);
	}
}

static inline void i2s_qcc74x_prepare_rx_dma_buffer(const void *addr, size_t size)
{
	if (qc7xx_check_cache_addr(addr)) {
		(void)cache_data_invd_range((void *)addr, size);
	}
}

static int64_t i2s_qcc74x_stream_tx_refill_grace_ticks(const struct i2s_qcc74x_stream *stream_tx)
{
	uint64_t frame_size;
	uint64_t frames_per_block;
	uint64_t block_ms;
	uint64_t grace_ms;

	if (stream_tx->i2s_cfg.channels == 0U || stream_tx->i2s_cfg.word_size == 0U ||
	    stream_tx->i2s_cfg.frame_clk_freq == 0U) {
		return k_ms_to_ticks_ceil64(1);
	}

	frame_size =
		(uint64_t)stream_tx->i2s_cfg.channels * (uint64_t)stream_tx->i2s_cfg.word_size / 8U;
	if (frame_size == 0U) {
		return k_ms_to_ticks_ceil64(1);
	}

	frames_per_block = ((uint64_t)stream_tx->i2s_cfg.block_size + frame_size - 1U) / frame_size;
	block_ms = (frames_per_block * 1000ULL + stream_tx->i2s_cfg.frame_clk_freq - 1U) /
		   stream_tx->i2s_cfg.frame_clk_freq;
	if (block_ms == 0U) {
		block_ms = 1U;
	}

	grace_ms = block_ms * I2S_QCC74X_TX_REFILL_GRACE_BLOCKS;
	return k_ms_to_ticks_ceil64(grace_ms);
}

static bool i2s_qcc74x_stream_tx_refill_deadline_expired(const struct i2s_qcc74x_stream *stream_tx)
{
	return stream_tx->tx_waiting_refill && k_uptime_ticks() >= stream_tx->tx_refill_deadline;
}

static int i2s_qcc74x_stream_tx_dma_kickoff(struct i2s_qcc74x_data *dev_data,
					  struct i2s_qcc74x_stream *stream_tx)
{
	int ret;

	qc7xx_i2s_link_txdma(&dev_data->i2s_dev, false);
	qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_CLEAR_TX_FIFO, 0);
	qc7xx_i2s_link_txdma(&dev_data->i2s_dev, true);

	ret = dma_start(stream_tx->dma_dev, stream_tx->dma_channel);
	if (ret < 0) {
		LOG_ERR("Error starting DMA channel[%d]: %d", (int)stream_tx->dma_channel, ret);
		return -EIO;
	}

	qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_DATA_ENABLE,
				  I2S_CMD_DATA_ENABLE_TX | I2S_CMD_DATA_ENABLE_RX);

	return 0;
}

static int i2s_qcc74x_stream_tx_start_hw_sequence(struct i2s_qcc74x_data *dev_data,
						struct i2s_qcc74x_stream *stream_tx)
{
	uint32_t ws_regval = getreg32(dev_data->i2s_dev.reg_base + I2S_CONFIG_OFFSET);

	ws_regval &= ~I2S_CR_I2S_M_EN;
	putreg32(ws_regval, dev_data->i2s_dev.reg_base + I2S_CONFIG_OFFSET);
	ws_regval |= I2S_CR_I2S_M_EN;
	putreg32(ws_regval, dev_data->i2s_dev.reg_base + I2S_CONFIG_OFFSET);

	qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_CLEAR_TX_FIFO, 0);
	qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_CLEAR_RX_FIFO, 0);

	return i2s_qcc74x_stream_tx_dma_kickoff(dev_data, stream_tx);
}

static int i2s_qcc74x_stream_tx_restart_hw_sequence(struct i2s_qcc74x_data *dev_data,
						  struct i2s_qcc74x_stream *stream_tx)
{
	/* Re-enable TX DMA path without resetting WS or clearing RX FIFO. */
	return i2s_qcc74x_stream_tx_dma_kickoff(dev_data, stream_tx);
}

static int i2s_qcc74x_stream_tx_restart_from_item(const struct device *dev,
						struct i2s_qcc74x_queue_item *item)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_stream *stream_tx = &dev_data->tx;
	int ret;

	i2s_qcc74x_prepare_tx_dma_buffer(item->mem_block, item->size);

	ret = dma_reload(stream_tx->dma_dev, stream_tx->dma_channel,
			 i2s_qcc74x_get_dma_addr(item->mem_block), stream_tx->per_fifo_addr,
			 (uint32_t)item->size);
	if (ret < 0) {
		LOG_ERR("Error reload DMA channel[%d]: %d", (int)stream_tx->dma_channel, ret);
		return ret;
	}

	stream_tx->current_item = *item;

	return i2s_qcc74x_stream_tx_restart_hw_sequence(dev_data, stream_tx);
}

/**
 * @brief Implements (*dma_callback_t)() that is declared in zephyr\include\zephyr\drivers\dma.h.
 */
static void i2s_qcc74x_stream_tx_cb(const struct device *dma_dev, void *user_data, uint32_t channel,
				  int status)
{
	const struct device *i2s_dev = (const struct device *)user_data;
	struct i2s_qcc74x_data *const dev_data = i2s_dev->data;
	struct i2s_qcc74x_stream *stream_tx = &dev_data->tx;
	struct i2s_qcc74x_queue_item item;
	int ret = 0;

	if (stream_tx->current_item.mem_block == NULL) {
		stream_tx->state = I2S_STATE_ERROR;
		LOG_ERR("TX mem_block NULL");
		return;
	}

	/* TX uses a single DMA block.
	 * Treat both BLOCK and COMPLETE as completion callbacks. */
	if (status != DMA_STATUS_COMPLETE && status != DMA_STATUS_BLOCK) {
		stream_tx->state = I2S_STATE_ERROR;
		LOG_ERR("TX DMA bad status: %d", status);
		return;
	}

	/* Free TX mem_block after transfer completion */
	k_mem_slab_free(stream_tx->i2s_cfg.mem_slab, stream_tx->current_item.mem_block);
	stream_tx->current_item.mem_block = NULL;
	stream_tx->current_item.size = 0;

	/* I2S TX stopping, exit */
	if (stream_tx->state == I2S_STATE_STOPPING && stream_tx->drain_flag == false) {
		stream_tx->state = I2S_STATE_READY;
		LOG_DBG("TX stop done");
		return;
	}

	/* Get next block */
	ret = k_msgq_get(&stream_tx->queue, &item, K_NO_WAIT);
	if (ret < 0) {
		if (stream_tx->drain_flag == true && stream_tx->state == I2S_STATE_STOPPING) {
			stream_tx->state = I2S_STATE_READY;
			stream_tx->drain_flag = false;
			stream_tx->tx_waiting_refill = false;
			stream_tx->tx_refill_deadline = 0;
			LOG_DBG("TX drain done");
			return;
		}

		/* Enter refill wait on every queue-empty event during RUNNING. */
		stream_tx->tx_waiting_refill = true;
		stream_tx->tx_refill_deadline =
			k_uptime_ticks() + i2s_qcc74x_stream_tx_refill_grace_ticks(stream_tx);
		LOG_DBG("TX queue empty, entering refill grace window");
		return;
	} else {
		stream_tx->tx_waiting_refill = false;
		stream_tx->tx_refill_deadline = 0;
	}
	stream_tx->current_item = item;
	i2s_qcc74x_prepare_tx_dma_buffer(item.mem_block, item.size);

	LOG_DBG("TX DMA reload: %p, size:%d", item.mem_block, item.size);

	ret = dma_reload(stream_tx->dma_dev, stream_tx->dma_channel,
			 i2s_qcc74x_get_dma_addr(item.mem_block), stream_tx->per_fifo_addr,
			 (uint32_t)item.size);
	if (ret < 0) {
		LOG_ERR("Error reload DMA channel[%d]: %d", (int)stream_tx->dma_channel, ret);
		stream_tx->state = I2S_STATE_ERROR;
		return;
	}

	ret = dma_start(stream_tx->dma_dev, stream_tx->dma_channel);
	if (ret < 0) {
		LOG_ERR("Error starting DMA channel[%d]: %d", (int)stream_tx->dma_channel, ret);
		stream_tx->state = I2S_STATE_ERROR;
		return;
	}
}

/**
 * @brief Start TX stream.
 *
 * @param dev Pointer to device structure.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int i2s_qcc74x_stream_tx_start(const struct device *dev)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_stream *stream_tx = &dev_data->tx;
	struct i2s_qcc74x_queue_item item = {0};
	uint32_t sample_size;
	uint32_t burst_length;
	int ret = 0;

	ret = k_msgq_get(&stream_tx->queue, &item, K_NO_WAIT);
	if (ret < 0) {
		LOG_ERR("TX start but queue empty");
		return -ENOMEM;
	}
	stream_tx->current_item = item;
	stream_tx->tx_waiting_refill = false;
	stream_tx->tx_refill_deadline = 0;

	struct dma_config dma_cfg = {0};
	struct dma_block_config dma_blk = {0};

	i2s_qcc74x_prepare_tx_dma_buffer(item.mem_block, item.size);
	dma_blk.source_address = i2s_qcc74x_get_dma_addr(item.mem_block);
	dma_blk.dest_address = stream_tx->per_fifo_addr; /* I2S TX fifo */
	dma_blk.block_size = item.size;

	dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	dma_cfg.dma_slot = stream_tx->dma_slot; /* I2S TX request num */
	dma_cfg.complete_callback_en = 1;
	dma_cfg.error_callback_dis = 0;
	ret = i2s_qcc74x_get_dma_params(&stream_tx->i2s_cfg, item.size, &sample_size, &burst_length);
	if (ret < 0) {
		LOG_ERR("Unsupported TX DMA params: channels=%u, word_size=%u, block_size=%u",
			stream_tx->i2s_cfg.channels, stream_tx->i2s_cfg.word_size,
			(unsigned int)item.size);
		return -EINVAL;
	}
	dma_cfg.source_data_size = sample_size;
	dma_cfg.dest_data_size = sample_size;
	dma_cfg.source_burst_length = burst_length;
	dma_cfg.dest_burst_length = burst_length;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;
	dma_cfg.dma_callback = i2s_qcc74x_stream_tx_cb;
	dma_cfg.user_data = (void *)dev;

	LOG_DBG("TX DMA start: %p, block_size:%u, sample_size:%u, burst:%u", item.mem_block,
		(unsigned int)item.size, sample_size, burst_length);

	ret = dma_config(stream_tx->dma_dev, stream_tx->dma_channel, &dma_cfg);
	if (ret < 0) {
		LOG_ERR("Error configuring DMA channel[%d]: %d", (int)stream_tx->dma_channel, ret);
		return -EINVAL;
	}

	ret = i2s_qcc74x_stream_tx_start_hw_sequence(dev_data, stream_tx);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/**
 * @brief Implements (*dma_callback_t)() that is declared in zephyr\include\zephyr\drivers\dma.h.
 */
static void i2s_qcc74x_stream_rx_cb(const struct device *dma_dev, void *user_data, uint32_t channel,
				  int status)
{
	const struct device *i2s_dev = (const struct device *)user_data;
	struct i2s_qcc74x_data *const dev_data = i2s_dev->data;
	struct i2s_qcc74x_stream *stream_rx = &dev_data->rx;
	struct i2s_qcc74x_queue_item item = {0};
	int ret = 0;

	if (stream_rx->current_item.mem_block == NULL) {
		stream_rx->state = I2S_STATE_ERROR;
		LOG_ERR("RX mem_block NULL");
		return;
	}

	if (status != DMA_STATUS_COMPLETE) {
		stream_rx->state = I2S_STATE_ERROR;
		LOG_ERR("RX DMA bad status: %d", status);
		goto block_free;
	}

	i2s_qcc74x_prepare_rx_dma_buffer(stream_rx->current_item.mem_block,
				       stream_rx->current_item.size);

	if (stream_rx->state == I2S_STATE_STOPPING) {
		ret = k_msgq_put(&stream_rx->queue, &stream_rx->current_item, K_NO_WAIT);
		if (ret < 0) {
			stream_rx->state = I2S_STATE_ERROR;
			LOG_ERR("RX queue full");
			goto block_free;
		}
		stream_rx->current_item.mem_block = NULL;
		stream_rx->current_item.size = 0;
		stream_rx->state = I2S_STATE_READY;
		LOG_DBG("RX stop done");
		return;
	}

	ret = k_msgq_put(&stream_rx->queue, &stream_rx->current_item, K_NO_WAIT);
	if (ret < 0) {
		stream_rx->state = I2S_STATE_ERROR;
		LOG_ERR("RX queue full");
		goto block_free;
	}
	stream_rx->current_item.mem_block = NULL;
	stream_rx->current_item.size = 0;

	/* Get next block */
	ret = k_mem_slab_alloc(stream_rx->i2s_cfg.mem_slab, &item.mem_block, K_NO_WAIT);
	if (ret < 0) {
		stream_rx->state = I2S_STATE_ERROR;
		LOG_WRN("RX mem_slab exhausted, RX overrun detected");
		return;
	}
	item.size = stream_rx->i2s_cfg.block_size;
	stream_rx->current_item = item;
	i2s_qcc74x_prepare_rx_dma_buffer(item.mem_block, item.size);

	LOG_DBG("RX DMA reload: %p, size:%d", item.mem_block, item.size);

	ret = dma_reload(stream_rx->dma_dev, stream_rx->dma_channel, stream_rx->per_fifo_addr,
			 i2s_qcc74x_get_dma_addr(item.mem_block), (uint32_t)item.size);
	if (ret < 0) {
		LOG_ERR("Error in reloading DMA channel[%d]: %d", (int)stream_rx->dma_channel, ret);
		stream_rx->state = I2S_STATE_ERROR;
		goto block_free;
	}

	ret = dma_start(stream_rx->dma_dev, stream_rx->dma_channel);
	if (ret < 0) {
		LOG_ERR("Error in starting DMA channel[%d]: %d", (int)stream_rx->dma_channel, ret);
		stream_rx->state = I2S_STATE_ERROR;
		goto block_free;
	}

	return;

block_free:
	if (stream_rx->current_item.mem_block != NULL) {
		k_mem_slab_free(stream_rx->i2s_cfg.mem_slab, stream_rx->current_item.mem_block);
		stream_rx->current_item.mem_block = NULL;
		stream_rx->current_item.size = 0;
	}
	return;
}

/**
 * @brief Start RX stream.
 *
 * @param dev Pointer to device structure.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int i2s_qcc74x_stream_rx_start(const struct device *dev)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_stream *stream_rx = &dev_data->rx;
	struct i2s_qcc74x_queue_item item = {0};
	uint32_t sample_size;
	uint32_t burst_length;
	int ret = 0;

	ret = k_mem_slab_alloc(stream_rx->i2s_cfg.mem_slab, &item.mem_block, K_NO_WAIT);
	if (ret < 0) {
		LOG_ERR("RX start, mem_slab empty");
		return -ENOMEM;
	}
	item.size = stream_rx->i2s_cfg.block_size;
	stream_rx->current_item = item;

	struct dma_config dma_cfg = {0};
	struct dma_block_config dma_blk = {0};

	dma_blk.source_address = stream_rx->per_fifo_addr; /* I2S RX fifo */
	i2s_qcc74x_prepare_rx_dma_buffer(item.mem_block, item.size);
	dma_blk.dest_address = i2s_qcc74x_get_dma_addr(item.mem_block);
	dma_blk.block_size = item.size;

	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_slot = stream_rx->dma_slot; /* I2S RX request num */
	dma_cfg.complete_callback_en = 1;
	dma_cfg.error_callback_dis = 0;
	ret = i2s_qcc74x_get_dma_params(&stream_rx->i2s_cfg, item.size, &sample_size, &burst_length);
	if (ret < 0) {
		LOG_ERR("Unsupported RX DMA params: channels=%u, word_size=%u, block_size=%u",
			stream_rx->i2s_cfg.channels, stream_rx->i2s_cfg.word_size,
			(unsigned int)item.size);
		return -EINVAL;
	}
	dma_cfg.source_data_size = sample_size;
	dma_cfg.dest_data_size = sample_size;
	dma_cfg.source_burst_length = burst_length;
	dma_cfg.dest_burst_length = burst_length;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;
	dma_cfg.dma_callback = i2s_qcc74x_stream_rx_cb;
	dma_cfg.user_data = (void *)dev;

	LOG_DBG("RX DMA start: %p, block_size:%u, sample_size:%u, burst:%u", item.mem_block,
		(unsigned int)item.size, sample_size, burst_length);

	ret = dma_config(stream_rx->dma_dev, stream_rx->dma_channel, &dma_cfg);
	if (ret < 0) {
		LOG_ERR("Error in configuring DMA channel[%d]: %d", (int)stream_rx->dma_channel,
			ret);
		return -EINVAL;
	}

	qc7xx_i2s_link_rxdma(&dev_data->i2s_dev, false);
	qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_CLEAR_RX_FIFO, 0);
	qc7xx_i2s_link_rxdma(&dev_data->i2s_dev, true);

	ret = dma_start(stream_rx->dma_dev, stream_rx->dma_channel);
	if (ret < 0) {
		LOG_ERR("Error in starting DMA channel[%d]: %d", (int)stream_rx->dma_channel, ret);
		return -EIO;
	}

	return 0;
}

/**
 * @brief Drop stream.
 *
 * @param stream Stream to drop.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int i2s_qcc74x_stream_drop(struct i2s_qcc74x_stream *stream)
{
	struct i2s_qcc74x_queue_item item = {0};
	int ret = 0;

	while (1) {
		ret = k_msgq_get(&stream->queue, &item, K_NO_WAIT);
		if (ret < 0) {
			if (ret != -ENOMSG) {
				return ret;
			}
			break;
		}
		k_mem_slab_free(stream->i2s_cfg.mem_slab, item.mem_block);
	}

	return 0;
}

/**
 * @brief Initialize Opal I2S driver instance.
 *
 * @param dev Pointer to device structure.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int i2s_qcc74x_init(const struct device *dev)
{
	int ret = 0;
	const struct i2s_qcc74x_cfg *dev_cfg = dev->config;
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct qcc74x_clk_settings cfg_settings = {
		.enable = CLOCK_ENABLE,
		.clk_src = dev_cfg->diclk,
		.clk_div = dev_cfg->div,
		.clk_src2 = dev_cfg->doclk,
	};

	dev_data->i2s_dev.reg_base = dev_cfg->base;

	if (dev_data->tx.dma_dev && !device_is_ready(dev_data->tx.dma_dev)) {
		LOG_ERR("%s device not ready", dev_data->tx.dma_dev->name);
		return -ENODEV;
	}

	if (dev_data->rx.dma_dev && !device_is_ready(dev_data->rx.dma_dev)) {
		LOG_ERR("%s device not ready", dev_data->rx.dma_dev->name);
		return -ENODEV;
	}

	ret = clock_control_on(dev_cfg->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for I2S driver is already enabled");
		} else {
			LOG_ERR("Failed to enable I2S clock: %d", ret);
			return ret;
		}
	}

	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&cfg_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure I2S clock: %d", ret);
	}

	ret = reset_line_toggle_dt(&dev_cfg->reset);
	if (ret < 0) {
		LOG_ERR("I2S reset line toggle failed: %d", ret);
		return -EIO;
	}

	LOG_DBG("Allocate TX queue");
	ret = k_msgq_alloc_init(&dev_data->tx.queue, sizeof(struct i2s_qcc74x_queue_item),
				CONFIG_I2S_QCC74X_TX_BLOCK_COUNT);
	if (ret < 0) {
		LOG_ERR("TX queue alloc failed, ret: %d", ret);
		return ret;
	}

	LOG_DBG("Allocate RX queue");
	ret = k_msgq_alloc_init(&dev_data->rx.queue, sizeof(struct i2s_qcc74x_queue_item),
				CONFIG_I2S_QCC74X_RX_BLOCK_COUNT);
	if (ret < 0) {
		LOG_ERR("TX queue alloc failed, ret: %d", ret);
		return ret;
	}

	ret = pinctrl_apply_state(dev_cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int i2s_qcc74x_configure(const struct device *dev, enum i2s_dir dir,
			      const struct i2s_config *i2s_cfg)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_stream *stream_tx, *stream_rx;
	int ret = 0;

	stream_tx = &dev_data->tx;
	stream_rx = &dev_data->rx;

	/* TX */
	if (dir == I2S_DIR_TX || dir == I2S_DIR_BOTH) {
		/* Implicitly recover from ERROR state so configure can proceed */
		if (stream_tx->state == I2S_STATE_ERROR) {
			dma_stop(stream_tx->dma_dev, stream_tx->dma_channel);
			if (stream_tx->current_item.mem_block != NULL) {
				k_mem_slab_free(stream_tx->i2s_cfg.mem_slab,
						stream_tx->current_item.mem_block);
				stream_tx->current_item.mem_block = NULL;
				stream_tx->current_item.size = 0;
			}
			i2s_qcc74x_stream_drop(stream_tx);
			stream_tx->state = I2S_STATE_READY;
		}
		if (stream_tx->state != I2S_STATE_NOT_READY &&
		    stream_tx->state != I2S_STATE_READY) {
			LOG_ERR("Invalid TX state: %d", (int)stream_tx->state);
			return -EINVAL;
		}
		if (stream_tx->dma_dev == NULL) {
			LOG_ERR("TX DMA controller not available");
			return -EINVAL;
		}
		stream_tx->i2s_cfg = *i2s_cfg;
		stream_tx->tx_waiting_refill = false;
		stream_tx->tx_refill_deadline = 0;
	}

	/* RX */
	if (dir == I2S_DIR_RX || dir == I2S_DIR_BOTH) {
		/* Implicitly recover from ERROR state so configure can proceed */
		if (stream_rx->state == I2S_STATE_ERROR) {
			dma_stop(stream_rx->dma_dev, stream_rx->dma_channel);
			if (stream_rx->current_item.mem_block != NULL) {
				k_mem_slab_free(stream_rx->i2s_cfg.mem_slab,
						stream_rx->current_item.mem_block);
				stream_rx->current_item.mem_block = NULL;
				stream_rx->current_item.size = 0;
			}
			i2s_qcc74x_stream_drop(stream_rx);
			stream_rx->state = I2S_STATE_READY;
		}
		if (stream_rx->state != I2S_STATE_NOT_READY &&
		    stream_rx->state != I2S_STATE_READY) {
			LOG_ERR("Invalid RX state: %d", (int)stream_rx->state);
			return -EINVAL;
		}
		if (stream_rx->dma_dev == NULL) {
			LOG_ERR("RX DMA controller not available");
		}
		stream_rx->i2s_cfg = *i2s_cfg;
	}

	/* Deinit */
	if (i2s_cfg->frame_clk_freq == 0) {
		if (dir == I2S_DIR_TX || dir == I2S_DIR_BOTH) {
			ret = i2s_qcc74x_stream_drop(stream_tx);
			if (ret < 0) {
				LOG_ERR("Configure TX k_msgq_get error: %d", ret);
				return -EIO;
			}
			memset(&stream_tx->i2s_cfg, 0, sizeof(struct i2s_config));
			stream_tx->state = I2S_STATE_NOT_READY;
			LOG_DBG("TX disable done");
		}
		if (dir == I2S_DIR_RX || dir == I2S_DIR_BOTH) {
			ret = i2s_qcc74x_stream_drop(stream_rx);
			if (ret < 0) {
				LOG_ERR("Configure RX k_msgq_get error: %d", ret);
				return -EIO;
			}
			memset(&stream_rx->i2s_cfg, 0, sizeof(struct i2s_config));
			stream_rx->state = I2S_STATE_NOT_READY;
			LOG_DBG("RX disable done");
		}
		if (stream_tx->state == I2S_STATE_NOT_READY &&
		    stream_rx->state == I2S_STATE_NOT_READY) {
			/* TODO: I2S hw reset */
		}
		return 0;
	}

	struct qc7xx_i2s_config_s i2s_qcc74x_cfg = {0};

	/* I2S role */
	if ((i2s_cfg->options & I2S_OPT_FRAME_CLK_SLAVE) &&
	    (i2s_cfg->options & I2S_OPT_BIT_CLK_SLAVE)) {
		i2s_qcc74x_cfg.role = I2S_ROLE_SLAVE;
	} else if (!(i2s_cfg->options & I2S_OPT_FRAME_CLK_SLAVE) &&
		   !(i2s_cfg->options & I2S_OPT_BIT_CLK_SLAVE)) {
		i2s_qcc74x_cfg.role = I2S_ROLE_MASTER;
	} else {
		LOG_ERR("Unsupported I2S clock mode");
		return -EINVAL;
	}

	uint32_t format = i2s_cfg->format & I2S_FMT_DATA_FORMAT_MASK;
	bool pcm_mode = false;
	if (format == I2S_FMT_DATA_FORMAT_I2S) {
		/* Standard I2S Data Format. */
		i2s_qcc74x_cfg.format_mode = I2S_MODE_LEFT_JUSTIFIED;
		i2s_qcc74x_cfg.fs_offset_cycle = 1;
	} else if (format == I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED) {
		/* Left Justified Data Format. */
		i2s_qcc74x_cfg.format_mode = I2S_MODE_LEFT_JUSTIFIED;
		i2s_qcc74x_cfg.fs_offset_cycle = 0;
	} else if (format == I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED) {
		/* Right Justified Data Format. */
		i2s_qcc74x_cfg.format_mode = I2S_MODE_RIGHT_JUSTIFIED;
		i2s_qcc74x_cfg.fs_offset_cycle = 0;
	} else if (format == I2S_FMT_DATA_FORMAT_PCM_SHORT) {
		/* PCM Short Frame Sync Data Format. (dsp modeA short frame sync) */
		i2s_qcc74x_cfg.format_mode = I2S_MODE_DSP_SHORT_FRAME_SYNC;
		i2s_qcc74x_cfg.fs_offset_cycle = 1;
		pcm_mode = true;
	} else if (format == I2S_FMT_DATA_FORMAT_PCM_LONG) {
		/* PCM Long Frame Sync Data Format. (dsp modeB long frame sync) */
		i2s_qcc74x_cfg.format_mode = I2S_MODE_DSP_LONG_FRAME_SYNC;
		i2s_qcc74x_cfg.fs_offset_cycle = 1;
		pcm_mode = true;
	} else {
		LOG_ERR("Unsupported I2S data format");
		return -EINVAL;
	}
	if (i2s_cfg->format & I2S_FMT_DATA_ORDER_INV) {
		LOG_ERR("Unsupported LSB data order");
		return -EINVAL;
	}
	if (i2s_cfg->format & I2S_FMT_CLK_FORMAT_MASK) {
		LOG_ERR("Unsupported I2S clock format");
		return -EINVAL;
	}
	if (i2s_cfg->format & I2S_OPT_BIT_CLK_GATED) {
		LOG_ERR("Unsupported I2S bit clock gated");
		return -EINVAL;
	}

	if (pcm_mode == false) {
		if (i2s_cfg->channels == 1) {
			i2s_qcc74x_cfg.channel_mode = I2S_CHANNEL_MODE_NUM_1;
		} else if (i2s_cfg->channels == 2) {
			i2s_qcc74x_cfg.channel_mode = I2S_CHANNEL_MODE_NUM_2;
		} else {
			LOG_ERR("Unsupported I2S channels: %d", i2s_cfg->channels);
			return -EINVAL;
		}
	} else {
		if (i2s_cfg->channels == 3) {
			i2s_qcc74x_cfg.channel_mode = I2S_CHANNEL_MODE_NUM_3;
		} else if (i2s_cfg->channels == 4) {
			i2s_qcc74x_cfg.channel_mode = I2S_CHANNEL_MODE_NUM_4;
		} else if (i2s_cfg->channels == 6) {
			i2s_qcc74x_cfg.channel_mode = I2S_CHANNEL_MODE_NUM_6;
		} else {
			LOG_ERR("Unsupported PCM channels: %d", i2s_cfg->channels);
			return -EINVAL;
		}
	}

	if (i2s_cfg->word_size == 8) {
		i2s_qcc74x_cfg.data_width = I2S_SLOT_WIDTH_8;
		i2s_qcc74x_cfg.frame_width = I2S_SLOT_WIDTH_8;
	} else if (i2s_cfg->word_size == 16) {
		i2s_qcc74x_cfg.data_width = I2S_SLOT_WIDTH_16;
		i2s_qcc74x_cfg.frame_width = I2S_SLOT_WIDTH_16;
	} else if (i2s_cfg->word_size == 24) {
		i2s_qcc74x_cfg.data_width = I2S_SLOT_WIDTH_24;
		i2s_qcc74x_cfg.frame_width = I2S_SLOT_WIDTH_24;
	} else if (i2s_cfg->word_size == 32) {
		i2s_qcc74x_cfg.data_width = I2S_SLOT_WIDTH_32;
		i2s_qcc74x_cfg.frame_width = I2S_SLOT_WIDTH_32;
	} else {
		LOG_ERR("Unsupported word size: %d", i2s_cfg->word_size);
		return -EINVAL;
	}

	if (pcm_mode == false) {
		i2s_qcc74x_cfg.bclk_freq_hz = i2s_cfg->frame_clk_freq * i2s_cfg->word_size * 2;
	} else {
		i2s_qcc74x_cfg.bclk_freq_hz =
			i2s_cfg->frame_clk_freq * i2s_cfg->word_size * i2s_cfg->channels;
	}

	i2s_qcc74x_cfg.tx_fifo_threshold = (4);
	i2s_qcc74x_cfg.rx_fifo_threshold = (4);

	qc7xx_i2s_init(&dev_data->i2s_dev, &i2s_qcc74x_cfg);

	/* Enable DMA */
	qc7xx_i2s_link_txdma(&dev_data->i2s_dev, true);
	qc7xx_i2s_link_rxdma(&dev_data->i2s_dev, true);

	if (dir == I2S_DIR_TX || dir == I2S_DIR_BOTH) {
		stream_tx->state = I2S_STATE_READY;
	}
	if (dir == I2S_DIR_RX || dir == I2S_DIR_BOTH) {
		stream_rx->state = I2S_STATE_READY;
	}

	/* Keep data signals disabled after configure; DATA_ENABLE is called
	 * at TX start time after WS frame counter is reset, so that DMA
	 * always begins on a LEFT-channel (WS=0) boundary.
	 */
	qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_DATA_ENABLE, 0);

	LOG_DBG("%s configure done, channels: %d",
		(dir == I2S_DIR_BOTH) ? "TX/RX" : ((dir == I2S_DIR_TX) ? "TX" : "RX"),
		i2s_cfg->channels);
	return 0;
}

static const struct i2s_config *i2s_qcc74x_config_get(const struct device *dev, enum i2s_dir dir)
{
	struct i2s_qcc74x_data *dev_data = dev->data;
	struct i2s_qcc74x_stream *stream;

	stream = (dir == I2S_DIR_RX) ? &dev_data->rx : &dev_data->tx;

	if (stream->state == I2S_STATE_NOT_READY) {
		return NULL;
	}

	return &stream->i2s_cfg;
}

/**
 * @brief Send a trigger command on either RX or TX stream.
 *
 * @param dev Pointer to device structure.
 * @param dir Stream direction: RX, TX, as defined by I2S_DIR_*.
 * @param cmd Trigger command.
 *
 * @retval 0 On success.
 * @retval -errno On failure.
 */
static int i2s_qcc74x_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_stream *stream;
	unsigned int key = 0U;
	int ret = 0;

	switch (dir) {
	case I2S_DIR_RX:
		stream = &dev_data->rx;
		break;
	case I2S_DIR_TX:
		stream = &dev_data->tx;
		break;
	case I2S_DIR_BOTH:
		LOG_ERR("Unsupported direction: %d", (int)dir);
		return -ENOSYS;
	default:
		LOG_ERR("Invalid direction: %d", (int)dir);
		return -EINVAL;
	}

	switch (cmd) {
	case I2S_TRIGGER_START:
		LOG_DBG("%s START", (dir == I2S_DIR_RX) ? "RX" : "TX");

		key = irq_lock();
		if (stream->state != I2S_STATE_READY) {
			irq_unlock(key);
			LOG_ERR("START - Invalid state: %d", (int)stream->state);
			return -EIO;
		}
		if (dir == I2S_DIR_TX) {
			ret = i2s_qcc74x_stream_tx_start(dev);
		} else {
			ret = i2s_qcc74x_stream_rx_start(dev);
		}
		if (ret < 0) {
			irq_unlock(key);
			LOG_ERR("START - Transfer start failed: %d", ret);
			return -EIO;
		}
		stream->state = I2S_STATE_RUNNING;

		irq_unlock(key);
		break;

	case I2S_TRIGGER_STOP:
		LOG_DBG("%s STOP", (dir == I2S_DIR_RX) ? "RX" : "TX");

		key = irq_lock();
		if (stream->state != I2S_STATE_RUNNING) {
			irq_unlock(key);
			LOG_ERR("STOP - Invalid state: %d", (int)stream->state);
			return -EIO;
		}

		if (dir == I2S_DIR_TX && stream->tx_waiting_refill) {
			ret = i2s_qcc74x_stream_drop(stream);
			if (ret < 0) {
				irq_unlock(key);
				LOG_ERR("DRAIN - k_msgq_get error: %d", ret);
				return -EIO;
			}
			stream->tx_waiting_refill = false;
			stream->tx_refill_deadline = 0;
			stream->state = I2S_STATE_READY;
			stream->drain_flag = false;
			qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_DATA_ENABLE, 0);
			irq_unlock(key);
			LOG_DBG("TX stop done (refill wait)");
			break;
		}
		stream->state = I2S_STATE_STOPPING;
		stream->drain_flag = false;

		irq_unlock(key);
		break;

	case I2S_TRIGGER_DRAIN:
		LOG_DBG("%s DRAIN", (dir == I2S_DIR_RX) ? "RX" : "TX");

		key = irq_lock();
		if (stream->state != I2S_STATE_RUNNING) {
			irq_unlock(key);
			LOG_ERR("DRAIN - Invalid state: %d", (int)stream->state);
			return -EIO;
		}

		if (dir == I2S_DIR_TX && stream->tx_waiting_refill) {
			stream->tx_waiting_refill = false;
			stream->tx_refill_deadline = 0;
			stream->state = I2S_STATE_READY;
			stream->drain_flag = false;
			qc7xx_i2s_feature_control(&dev_data->i2s_dev, I2S_CMD_DATA_ENABLE, 0);
			irq_unlock(key);
			LOG_DBG("TX drain done (refill wait)");
			break;
		}
		stream->state = I2S_STATE_STOPPING;
		stream->drain_flag = true; /* Only TX used */

		irq_unlock(key);
		break;

	case I2S_TRIGGER_DROP:
		LOG_DBG("%s DROP", (dir == I2S_DIR_RX) ? "RX" : "TX");

		key = irq_lock();
		if (stream->state == I2S_STATE_NOT_READY) {
			irq_unlock(key);
			LOG_ERR("DROP - Invalid state: %d", (int)stream->state);
			return -EIO;
		}

		/* Stop DMA and free current mem_block */
		if (stream->state == I2S_STATE_RUNNING) {
			dma_stop(stream->dma_dev, stream->dma_channel);
			if (stream->current_item.mem_block != NULL) {
				k_mem_slab_free(stream->i2s_cfg.mem_slab,
						stream->current_item.mem_block);
				stream->current_item.mem_block = NULL;
				stream->current_item.size = 0;
			}
		}

		/* Clean queue, and free all mem_blocks */
		ret = i2s_qcc74x_stream_drop(stream);
		if (ret < 0) {
			irq_unlock(key);
			LOG_ERR("DRAIN - k_msgq_get error: %d", ret);
			return -EIO;
		}
		stream->state = I2S_STATE_READY;
		stream->tx_waiting_refill = false;
		stream->tx_refill_deadline = 0;

		irq_unlock(key);
		break;

	case I2S_TRIGGER_PREPARE:
		LOG_DBG("%s PREPARE", (dir == I2S_DIR_RX) ? "RX" : "TX");

		key = irq_lock();
		if (stream->state != I2S_STATE_ERROR) {
			irq_unlock(key);
			LOG_ERR("PREPARE - Invalid state: %d", (int)stream->state);
			return -EIO;
		}

		/* clean queue, and free all mem_block */
		ret = i2s_qcc74x_stream_drop(stream);
		if (ret < 0) {
			irq_unlock(key);
			LOG_ERR("DRAIN - k_msgq_get error: %d", ret);
			return -EIO;
		}
		stream->state = I2S_STATE_READY;
		stream->tx_waiting_refill = false;
		stream->tx_refill_deadline = 0;

		irq_unlock(key);
		break;

	default:
		LOG_ERR("Unsupported trigger command: %d", (int)cmd);
		return -EINVAL;
	}

	return 0;
}

static int i2s_qcc74x_read(const struct device *dev, void **mem_block, size_t *size)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_queue_item item = {0};
	int ret = 0;

	if (dev_data->rx.state == I2S_STATE_NOT_READY) {
		LOG_ERR("RX invalid state: %d", (int)dev_data->rx.state);
		return -EIO;
	} else if (dev_data->rx.state == I2S_STATE_ERROR &&
		   k_msgq_num_used_get(&dev_data->rx.queue) == 0) {
		LOG_ERR("RX queue empty");
		return -EIO;
	}

	ret = k_msgq_get(&dev_data->rx.queue, &item, K_MSEC(dev_data->rx.i2s_cfg.timeout));
	if (ret < 0) {
		LOG_INF("RX queue empty");
		return ret;
	}

	*mem_block = item.mem_block;
	*size = item.size;

	LOG_DBG("Read mem_block: %p, size: %d", *mem_block, *size);

	return 0;
}

static int i2s_qcc74x_write(const struct device *dev, void *mem_block, size_t size)
{
	struct i2s_qcc74x_data *const dev_data = dev->data;
	struct i2s_qcc74x_stream *stream_tx = &dev_data->tx;
	struct i2s_qcc74x_queue_item item = {.mem_block = mem_block, .size = size};
	int ret = 0;

	LOG_DBG("Write mem_block: %p, size: %d", mem_block, size);

	if (stream_tx->state != I2S_STATE_RUNNING && stream_tx->state != I2S_STATE_READY) {
		LOG_ERR("TX invalid state: %d", (int)stream_tx->state);
		return -EIO;
	}

	if (size > stream_tx->i2s_cfg.block_size) {
		LOG_ERR("Max write %u exceeded", (unsigned int)stream_tx->i2s_cfg.block_size);
		return -EINVAL;
	}

	uint32_t frame_size = stream_tx->i2s_cfg.channels * stream_tx->i2s_cfg.word_size / 8;
	if (frame_size == 0U) {
		LOG_ERR("Invalid frame size");
		return -EINVAL;
	}
	if (size % frame_size != 0) {
		LOG_ERR("Write size must be multiple of frame size");
		return -EINVAL;
	}

	if (i2s_qcc74x_stream_tx_refill_deadline_expired(stream_tx)) {
		stream_tx->tx_waiting_refill = false;
		stream_tx->tx_refill_deadline = 0;
		stream_tx->state = I2S_STATE_ERROR;
		LOG_ERR("TX refill grace expired");
		return -EIO;
	}

	ret = k_msgq_put(&stream_tx->queue, &item, K_MSEC(stream_tx->i2s_cfg.timeout));
	if (ret < 0) {
		LOG_ERR("TX queue full");
		return ret;
	}

	if (stream_tx->tx_waiting_refill) {
		struct i2s_qcc74x_queue_item restart_item = {0};

		ret = k_msgq_get(&stream_tx->queue, &restart_item, K_NO_WAIT);
		if (ret < 0) {
			stream_tx->tx_waiting_refill = false;
			stream_tx->tx_refill_deadline = 0;
			stream_tx->state = I2S_STATE_ERROR;
			LOG_ERR("TX refill recovery failed: queue unexpectedly empty");
			return -EIO;
		}

		ret = i2s_qcc74x_stream_tx_restart_from_item(dev, &restart_item);
		if (ret < 0) {
			stream_tx->tx_waiting_refill = false;
			stream_tx->tx_refill_deadline = 0;
			stream_tx->state = I2S_STATE_ERROR;
			if (stream_tx->current_item.mem_block == restart_item.mem_block) {
				stream_tx->current_item.mem_block = NULL;
				stream_tx->current_item.size = 0;
			}
			if (restart_item.mem_block != NULL && stream_tx->i2s_cfg.mem_slab != NULL) {
				k_mem_slab_free(stream_tx->i2s_cfg.mem_slab,
						restart_item.mem_block);
			}
			LOG_ERR("TX refill recovery failed: restart error %d", ret);
			return -EIO;
		}

		stream_tx->tx_waiting_refill = false;
		stream_tx->tx_refill_deadline = 0;
		LOG_DBG("TX refill recovered, DMA restarted");
	}

	return 0;
}

static DEVICE_API(i2s, i2s_qcc74x_driver_api) = {
	.configure = i2s_qcc74x_configure,
	.config_get = i2s_qcc74x_config_get,
	.trigger = i2s_qcc74x_trigger,
	.read = i2s_qcc74x_read,
	.write = i2s_qcc74x_write,
};

#define I2S_QCC74X_DMA_STREAM_INIT(inst, dir)                                                        \
	{                                                                                          \
		.dma_dev = UTIL_AND(DT_INST_DMAS_HAS_NAME(inst, dir),                              \
				    DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, dir))),          \
		.dma_channel = UTIL_AND(DT_INST_DMAS_HAS_NAME(inst, dir),                          \
					DT_INST_DMAS_CELL_BY_NAME(inst, dir, channel)),            \
		.dma_slot = UTIL_AND(DT_INST_DMAS_HAS_NAME(inst, dir),                             \
				     DT_INST_DMAS_CELL_BY_NAME(inst, dir, slot)),                  \
		.per_fifo_addr = UTIL_AND(DT_INST_DMAS_HAS_NAME(inst, dir),                        \
					  DT_INST_DMAS_CELL_BY_NAME(inst, dir, per_fifo_addr)),    \
		.state = I2S_STATE_NOT_READY,                                                      \
		.drain_flag = false,                                                               \
	}

#define I2S_QCC74X_DEVICE(inst)                                                                      \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static const struct i2s_qcc74x_cfg i2s_qcc74x_cfg_##inst = {                                   \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                             \
		.diclk = DT_INST_PROP(inst, diclk),                                                \
		.doclk = DT_INST_PROP(inst, doclk),                                                \
		.div = DT_INST_PROP(inst, div),                                                    \
	};                                                                                         \
                                                                                                   \
	static struct i2s_qcc74x_data i2s_qcc74x_data_##inst = {                                       \
		.tx = I2S_QCC74X_DMA_STREAM_INIT(inst, tx),                                          \
		.rx = I2S_QCC74X_DMA_STREAM_INIT(inst, rx),                                          \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &i2s_qcc74x_init, NULL, &i2s_qcc74x_data_##inst,                   \
			      &i2s_qcc74x_cfg_##inst, POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,         \
			      &i2s_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_QCC74X_DEVICE)
