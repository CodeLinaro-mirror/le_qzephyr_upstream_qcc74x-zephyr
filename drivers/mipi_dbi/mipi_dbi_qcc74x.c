/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_mipi_dbi

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/drivers/dma.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <qc7xx_dbi.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mipi_dbi_qcc74x, CONFIG_MIPI_DBI_LOG_LEVEL);

#if defined(CONFIG_SOC_SERIES_QCC75X) || defined(CONFIG_SOC_SERIES_QCR3XX)
#define QCC74X_DBI_B_CLK_FREQ_MAX (MHZ(40))
#else
#define QCC74X_DBI_B_CLK_FREQ_MAX (MHZ(27))
#endif
#define QCC74X_DBI_C_CLK_FREQ_MAX (MHZ(40))
#define QC7XX_DMA_LLI_MAX_CNT   (4064)


#ifdef CONFIG_MIPI_DBI_QCC74X_USE_DMA

#define DBI_EVENT_WAIT_TIMEOUT (K_FOREVER)

#define DBI_EVENT_DMA_ERR BIT(0)
#define DBI_EVENT_DBI_ERR BIT(1)
#define DBI_EVENT_DBI_CPT BIT(2)

/**
 * @brief DMA device configuration structure
 * @details Contains configuration data for the DMA stream
 */
struct mipi_dbi_qcc74x_dma_stream {
	const struct device *dma_dev;
	uint32_t dma_channel;
	uint32_t slot;
	uint32_t per_fifo_addr;
	struct k_event xfer_event;
	uint32_t buff_addr;
	uint32_t remain_size;
};
#endif /* CONFIG_MIPI_DBI_QCC74X_USE_DMA */

/**
 * @brief DBI device configuration structure
 * @details Contains configuration data for the DBI driver
 */
struct mipi_dbi_qcc74x_config {
	uint8_t mipi_mode;
	uint32_t clk_freq_hz;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const uint8_t clock_source;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_configure)(void);
	const struct gpio_dt_spec gpio_reset;
	const struct reset_dt_spec reset;
	uint32_t base_addr;
	uint32_t irq_num;
	uint32_t irq_priority;
	uint32_t irq_flags;
};

/**
 * @brief DBI device data structure
 * @details Contains runtime data for the DBI driver instance
 */
struct mipi_dbi_qcc74x_data {
	struct qc7xx_device_s dbi_dev;
	struct qc7xx_dbi_config_s *dbi_cfg;

#ifdef CONFIG_MIPI_DBI_QCC74X_USE_DMA
	struct mipi_dbi_qcc74x_dma_stream dma_stream;
#endif /* CONFIG_MIPI_DBI_QCC74X_USE_DMA */
};

#ifdef CONFIG_MIPI_DBI_QCC74X_USE_DMA
/**
 * @brief Restart DMA transfer for MIPI DBI
 *
 * Reloads and restarts the DMA channel for the next data transfer segment.
 *
 * @param dev Pointer to the MIPI DBI device structure
 * @param dma_stream Pointer to the DMA stream configuration
 * @return 0 on success, negative error code on failure
 */
static int dbi_qcc74x_dma_restart(const struct device *dev,
				struct mipi_dbi_qcc74x_dma_stream *dma_stream)
{
	uint32_t tx_size, tx_max_size;
	int ret = 0;

	tx_max_size = (CONFIG_DMA_CH_TRANSFER_LLI_MAX) * (QC7XX_DMA_LLI_MAX_CNT) * 4;

	if (dma_stream->remain_size < tx_max_size) {
		tx_size = dma_stream->remain_size;
	} else {
		tx_size = tx_max_size;
	}

	ret = dma_reload(dma_stream->dma_dev, dma_stream->dma_channel, dma_stream->buff_addr,
			 dma_stream->per_fifo_addr, tx_size);
	if (ret < 0) {
		LOG_ERR("DBI error reload DMA channel[%d]: %d", (int)dma_stream->dma_channel, ret);
		return -EIO;
	}

	ret = dma_start(dma_stream->dma_dev, dma_stream->dma_channel);
	if (ret < 0) {
		LOG_ERR("DBI error starting DMA channel[%d]: %d", (int)dma_stream->dma_channel,
			ret);
		return -EIO;
	}

	dma_stream->remain_size -= tx_size;
	dma_stream->buff_addr += tx_size;

	return 0;
}

/**
 * @brief DMA transfer callback
 *
 * Handles DMA transfer completion or errors for the MIPI DBI device.
 *
 * @param dma_dev Pointer to the DMA device
 * @param arg Pointer to the MIPI DBI device structure
 * @param channel DMA channel number
 * @param status DMA transfer status
 */
static void dbi_qcc74x_dma_tx_cb(const struct device *dma_dev, void *arg, uint32_t channel,
			       int status)
{
	const struct device *dbi_dev = (const struct device *)arg;
	struct mipi_dbi_qcc74x_data *const dev_data = dbi_dev->data;
	struct mipi_dbi_qcc74x_dma_stream *dma_stream = &dev_data->dma_stream;
	int ret = 0;

	if (status != DMA_STATUS_COMPLETE) {
		LOG_ERR("DBI dma bad status: %d", status);
		goto error_exit;
	}

	if (dma_stream->remain_size == 0) {
		/* Dma all done */
		return;
	}

	ret = dbi_qcc74x_dma_restart(dbi_dev, dma_stream);
	if (ret < 0) {
		goto error_exit;
	}

	return;

error_exit:
	/* Disable DMA request for dbi */
	qc7xx_dbi_link_txdma(&dev_data->dbi_dev, false);

	/* Clear flag and mask int */
	qc7xx_dbi_tcint_mask(&dev_data->dbi_dev, true);
	qc7xx_dbi_int_clear(&dev_data->dbi_dev, DBI_INTCLR_TC);

	/* Dma error */
	k_event_set(&dma_stream->xfer_event, DBI_EVENT_DMA_ERR);
	return;
}

/**
 * @brief MIPI DBI interrupt service routine (DMA mode)
 *
 * Handles interrupts for the MIPI DBI device when using DMA, processing
 * transfer completion or errors.
 *
 * @param dev Pointer to the MIPI DBI device structure
 */
static void mipi_dbi_qcc74x_isr(const struct device *dev)
{
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;
	struct mipi_dbi_qcc74x_dma_stream *dma_stream = &dev_data->dma_stream;

	dma_stop(dma_stream->dma_dev, dma_stream->dma_channel);

	/* Disable DMA request for dbi */
	qc7xx_dbi_link_txdma(&dev_data->dbi_dev, false);

	/* Clear flag and mask int */
	qc7xx_dbi_tcint_mask(&dev_data->dbi_dev, true);
	qc7xx_dbi_int_clear(&dev_data->dbi_dev, DBI_INTCLR_TC);

	if (dma_stream->remain_size > 0) {
		k_event_set(&dma_stream->xfer_event, DBI_EVENT_DBI_ERR);
		LOG_ERR("DBI data size error");
		return;
	}

	k_event_set(&dma_stream->xfer_event, DBI_EVENT_DBI_CPT);

	return;
}

/**
 * @brief Initialize DMA for MIPI DBI
 *
 * Configures the DMA channel for MIPI DBI data transfers.
 *
 * @param dev Pointer to the MIPI DBI device structure
 * @return 0 on success, negative error code on failure
 */
static int dbi_qcc74x_dma_init(const struct device *dev)
{
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;
	struct mipi_dbi_qcc74x_dma_stream *dma_stream = &dev_data->dma_stream;
	int ret = 0;

	if (dev_data->dma_stream.dma_dev && !device_is_ready(dev_data->dma_stream.dma_dev)) {
		LOG_ERR("%s device not ready", dev_data->dma_stream.dma_dev->name);
		return -ENODEV;
	}

	k_event_init(&dev_data->dma_stream.xfer_event);

	struct dma_config dma_cfg = {0};

	dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	dma_cfg.dma_slot = dma_stream->slot; /* dbi tx request num */
	dma_cfg.complete_callback_en = 1;
	dma_cfg.error_callback_dis = 0;
	dma_cfg.source_data_size = sizeof(uint32_t);
	dma_cfg.dest_data_size = sizeof(uint32_t);
	dma_cfg.source_burst_length = sizeof(uint32_t);
	dma_cfg.dest_burst_length = sizeof(uint32_t);
	dma_cfg.block_count = 0;
	dma_cfg.head_block = NULL;
	dma_cfg.dma_callback = dbi_qcc74x_dma_tx_cb;
	dma_cfg.user_data = (void *)dev;

	ret = dma_config(dma_stream->dma_dev, dma_stream->dma_channel, &dma_cfg);
	if (ret < 0) {
		LOG_ERR("Error configuring DMA channel[%d]: %d", (int)dma_stream->dma_channel, ret);
		return ret;
	}

	LOG_DBG("DBI %s used dma: %s ch%d", dev->name, dev_data->dma_stream.dma_dev->name,
		dev_data->dma_stream.dma_channel);

	return 0;
}

#else

/**
 * @brief MIPI DBI interrupt service routine (non-DMA mode)
 *
 * Handles interrupts for the MIPI DBI device when DMA is not used, logging a
 * warning as interrupts are unexpected in this mode.
 *
 * @param dev Pointer to the MIPI DBI device structure
 */
static void mipi_dbi_qcc74x_isr(const struct device *dev)
{
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;

	LOG_WRN("DBI should not trigger an interrupt");

	/* Clear flag and mask int */
	qc7xx_dbi_int_clear(&dev_data->dbi_dev, DBI_INTCLR_TC);
	qc7xx_dbi_tcint_mask(&dev_data->dbi_dev, true);

	return;
}

#endif /* CONFIG_MIPI_DBI_QCC74X_USE_DMA */

/**
 * @brief Reconfigure the MIPI DBI device
 *
 * Updates the MIPI DBI configuration, including pixel format, if necessary.
 *
 * @param dev Pointer to the MIPI DBI device structure
 * @param dbi_config Pointer to the MIPI DBI configuration structure
 * @param pixfmt Pixel format to configure
 * @return 0 on success, negative error code on failure
 */
static int mipi_dbi_qcc74x_reconfig(const struct device *dev,
				  const struct mipi_dbi_config *dbi_config,
				  enum display_pixel_format pixfmt)
{
	const struct mipi_dbi_qcc74x_config *dev_cfg = dev->config;
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;
	bool re_cfg_flag = false;
	uint8_t pixel_input_format = 0U;
	uint8_t pixel_output_format = 0U;
	struct qcc74x_clk_settings clk_settings = {
		.clk_src = dev_cfg->clock_source,
		.clk_div = 0U,
	};
	int ret = 0;

	if (dbi_config->mode != dev_cfg->mipi_mode) {
		LOG_ERR("Unsupported cfg dbi mode: %d", dbi_config->mode);
		return -ENOTSUP;
	}

	if (pixfmt == PIXEL_FORMAT_RGB_888) {
		pixel_input_format = DBI_PIXEL_INPUT_FORMAT_RGB_888;
		pixel_output_format = DBI_PIXEL_OUTPUT_FORMAT_RGB_888;
	} else if (pixfmt == PIXEL_FORMAT_RGB_565) {
		pixel_input_format = DBI_PIXEL_INPUT_FORMAT_RGB_565;
		pixel_output_format = DBI_PIXEL_OUTPUT_FORMAT_RGB_565;
	} else {
		LOG_ERR("Unsupported pixel format: %d", pixfmt);
		return -ENOTSUP;
	}

	if (dev_data->dbi_cfg->pixel_input_format != pixel_input_format ||
	    dev_data->dbi_cfg->pixel_output_format != pixel_output_format) {
		LOG_DBG("Reconfig DBI pixel format: %d", pixfmt);
		re_cfg_flag = true;
	}

	if (re_cfg_flag == false) {
		return 0;
	}

	if (reset_line_toggle_dt(&dev_cfg->reset) < 0) {
		LOG_ERR("DBI reset line toggle failed");
		return -EIO;
	}

	dev_data->dbi_cfg->pixel_input_format = pixel_input_format;
	dev_data->dbi_cfg->pixel_output_format = pixel_output_format;

	/* Enable clock */
	ret = clock_control_on(dev_cfg->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for DBI driver is already enabled");
		} else {
			LOG_ERR("Failed to enable DBI clock: %d", ret);
			return ret;
		}
	}

	/* Configure clock source. clk_div set in qc7xx_dbi_init() */
	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure DBI clock: %d", ret);
		return ret;
	}

	qc7xx_dbi_init(&dev_data->dbi_dev, dev_data->dbi_cfg);
	qc7xx_dbi_feature_control(&dev_data->dbi_dev, DBI_CMD_CS_CONTINUE, true);
	qc7xx_dbi_tcint_mask(&dev_data->dbi_dev, true);

	LOG_DBG("DBI set pixel format: %d", pixfmt);

	return 0;
}

/**
 * @brief Initialize the MIPI DBI device
 *
 * Initializes the MIPI DBI peripheral, including clock, pin, and DMA setup,
 * as well as mode and pixel format configuration.
 *
 * @param dev Pointer to the MIPI DBI device structure
 * @return 0 on success, negative error code on failure
 */
static int mipi_dbi_qcc74x_init(const struct device *dev)
{
	const struct mipi_dbi_qcc74x_config *dev_cfg = dev->config;
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;
	struct qcc74x_clk_settings clk_settings = {
		.clk_src = dev_cfg->clock_source,
		.clk_div = 0U,
	};
	int ret = 0;

	LOG_DBG("Init mipi_dbi mode:%d, clk_freq:%d", dev_cfg->mipi_mode, dev_cfg->clk_freq_hz);

	dev_data->dbi_dev.reg_base = dev_cfg->base_addr;
	dev_data->dbi_dev.irq_num = dev_cfg->irq_num;

	if (!device_is_ready(dev_cfg->clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	ret = pinctrl_apply_state(dev_cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	if (dev_cfg->gpio_reset.port) {
		if (!gpio_is_ready_dt(&dev_cfg->gpio_reset)) {
			LOG_ERR("Reset GPIO device not ready");
			return -ENODEV;
		}

		LOG_DBG("lcd reset io: IO%d", dev_cfg->gpio_reset.pin);

		if (gpio_pin_configure_dt(&dev_cfg->gpio_reset, GPIO_OUTPUT_INACTIVE)) {
			LOG_ERR("Couldn't configure reset pin");
			return -EIO;
		}
	}

#ifdef CONFIG_MIPI_DBI_QCC74X_USE_DMA
	ret = dbi_qcc74x_dma_init(dev);
	if (ret < 0) {
		return ret;
	}
#endif /* CONFIG_MIPI_DBI_QCC74X_USE_DMA */

	if (dev_cfg->mipi_mode == MIPI_DBI_MODE_8080_BUS_8_BIT) {
		if (dev_cfg->clk_freq_hz > QCC74X_DBI_B_CLK_FREQ_MAX) {
			LOG_ERR("Over frequency: %d, typeB_max:%d", dev_cfg->clk_freq_hz,
				QCC74X_DBI_B_CLK_FREQ_MAX);
			return -ENOTSUP;
		}
		dev_data->dbi_cfg->dbi_mode = DBI_MODE_TYPE_B;
	} else if (dev_cfg->mipi_mode == MIPI_DBI_MODE_SPI_4WIRE) {
		if (dev_cfg->clk_freq_hz > QCC74X_DBI_C_CLK_FREQ_MAX) {
			LOG_ERR("Over frequency: %d, typeC_max:%d", dev_cfg->clk_freq_hz,
				QCC74X_DBI_C_CLK_FREQ_MAX);
			return -ENOTSUP;
		}
		dev_data->dbi_cfg->dbi_mode = DBI_MODE_TYPE_C_4_WIRE;
	} else if (dev_cfg->mipi_mode == MIPI_DBI_MODE_SPI_3WIRE) {
		if (dev_cfg->clk_freq_hz > QCC74X_DBI_C_CLK_FREQ_MAX) {
			LOG_ERR("Over frequency: %d, typeC_max:%d", dev_cfg->clk_freq_hz,
				QCC74X_DBI_C_CLK_FREQ_MAX);
			return -ENOTSUP;
		}
		dev_data->dbi_cfg->dbi_mode = DBI_MODE_TYPE_C_3_WIRE;
	} else {
		LOG_ERR("Unsupport dbi mode: %d", dev_cfg->mipi_mode);
		return -ENOTSUP;
	}

	dev_data->dbi_cfg->clk_freq_hz = dev_cfg->clk_freq_hz;
	dev_data->dbi_cfg->pixel_input_format = DBI_PIXEL_INPUT_FORMAT_NRGB_8888;
	dev_data->dbi_cfg->pixel_output_format = DBI_PIXEL_OUTPUT_FORMAT_RGB_888;

	ret = reset_line_toggle_dt(&dev_cfg->reset);
	if (ret < 0) {
		LOG_ERR("DBI reset line toggle failed: %d", ret);
		return -EIO;
	}

	/* Enable clock */
	ret = clock_control_on(dev_cfg->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for DBI driver is already enabled");
		} else {
			LOG_ERR("Failed to enable DBI clock: %d", ret);
			return ret;
		}
	}

	/* Configure clock source. clk_div set in qc7xx_dbi_init() */
	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure DBI clock: %d", ret);
		return ret;
	}

	qc7xx_dbi_init(&dev_data->dbi_dev, dev_data->dbi_cfg);
	qc7xx_dbi_feature_control(&dev_data->dbi_dev, DBI_CMD_CS_CONTINUE, true);
	qc7xx_dbi_tcint_mask(&dev_data->dbi_dev, true);

	dev_cfg->irq_configure();

	return 0;
}

static int mipi_dbi_qcc74x_reset(const struct device *dev, k_timeout_t delay)
{
	const struct mipi_dbi_qcc74x_config *dev_cfg = dev->config;
	int ret = 0;

	if (dev_cfg->gpio_reset.port == NULL) {
		LOG_ERR("The reset pin is not configured");
		return -ENOTSUP;
	}

	LOG_DBG("Reset lcd");
	ret = gpio_pin_set_dt(&dev_cfg->gpio_reset, 1);
	if (ret < 0) {
		return ret;
	}

	k_sleep(delay);

	ret = gpio_pin_set_dt(&dev_cfg->gpio_reset, 0);
	if (ret < 0) {
		return ret;
	}
	LOG_DBG("reset done");

	return 0;
}

int mipi_dbi_qcc74x_command_write(const struct device *dev, const struct mipi_dbi_config *dbi_config,
				uint8_t cmd, const uint8_t *data_buf, size_t len)
{
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;

	LOG_DBG("write cmd: 0x%02X, len: %d", cmd, len);

	/* Enable cmd phase */
	qc7xx_dbi_feature_control(&dev_data->dbi_dev, DBI_CMD_MASK_CMD_PHASE, false);
	qc7xx_dbi_send_cmd_data(&dev_data->dbi_dev, cmd, len, (void *)data_buf);

	LOG_DBG("write cmd done");

	return 0;
}

static int mipi_dbi_qcc74x_write_display(const struct device *dev,
				       const struct mipi_dbi_config *dbi_config,
				       const uint8_t *framebuf,
				       struct display_buffer_descriptor *desc,
				       enum display_pixel_format pixfmt)
{
	struct mipi_dbi_qcc74x_data *const dev_data = dev->data;
	uint32_t pixel_num = 0U;
	int ret = 0;

	ret = mipi_dbi_qcc74x_reconfig(dev, dbi_config, pixfmt);
	if (ret < 0) {
		return ret;
	}

	pixel_num = desc->width * desc->height;
	if (pixel_num == 0) {
		if (pixfmt == PIXEL_FORMAT_RGB_888) {
			pixel_num = desc->buf_size / 3;
		} else if (pixfmt == PIXEL_FORMAT_RGB_565) {
			pixel_num = desc->buf_size / 2;
		} else if (pixfmt == PIXEL_FORMAT_ARGB_8888) {
			pixel_num = desc->buf_size / 4;
		}
	}
	LOG_DBG("write pixel: %d*%d, pixel_num: %d", desc->width, desc->height, pixel_num);

	/* Disable cmd phase */
	qc7xx_dbi_feature_control(&dev_data->dbi_dev, DBI_CMD_MASK_CMD_PHASE, true);

#ifdef CONFIG_MIPI_DBI_QCC74X_USE_DMA
	struct mipi_dbi_qcc74x_dma_stream *dma_stream = &dev_data->dma_stream;

	/* Trigger dbi data transfer */
	qc7xx_dbi_send_cmd_pixel(&dev_data->dbi_dev, 0xff, pixel_num, NULL);

	/* Unmask int */
	qc7xx_dbi_tcint_mask(&dev_data->dbi_dev, false);

	/* Enabled DMA request for dbi */
	qc7xx_dbi_link_txdma(&dev_data->dbi_dev, true);

	/* Clear event */
	k_event_clear(&dma_stream->xfer_event, 0xFFFFFFFF);

	dma_stream->buff_addr = (uint32_t)framebuf;
	dma_stream->remain_size = (desc->buf_size + 3) & 0xFFFFFFFC;
	/* Start dma */
	ret = dbi_qcc74x_dma_restart(dev, dma_stream);
	if (ret < 0) {
		return ret;
	}

	uint32_t event =
		k_event_wait(&dma_stream->xfer_event, 0xFFFFFFFF, false, DBI_EVENT_WAIT_TIMEOUT);
	if (event != DBI_EVENT_DBI_CPT) {
		LOG_ERR("DBI DMA write event error: %d", event);
		return -EIO;
	}

#else

	qc7xx_dbi_send_cmd_pixel(&dev_data->dbi_dev, 0xff, pixel_num, (void *)framebuf);

#endif /* CONFIG_MIPI_DBI_QCC74X_USE_DMA */

	LOG_DBG("write pixel done");

	return 0;
}

static DEVICE_API(mipi_dbi, mipi_dbi_qcc74x_driver_api) = {
	.reset = mipi_dbi_qcc74x_reset,
	.command_write = mipi_dbi_qcc74x_command_write,
	.write_display = mipi_dbi_qcc74x_write_display,
};

#ifdef CONFIG_MIPI_DBI_QCC74X_USE_DMA
#define MIPI_DBI_DMA_INIT(idx)                                                                     \
	.dma_stream = {                                                                            \
		.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR(idx)),                                  \
		.dma_channel = DT_INST_DMAS_CELL_BY_IDX(idx, 0, channel),                          \
		.slot = DT_INST_DMAS_CELL_BY_IDX(idx, 0, slot),                                    \
		.per_fifo_addr = DT_INST_DMAS_CELL_BY_IDX(idx, 0, per_fifo_addr),                  \
	},
#else
#define MIPI_DBI_DMA_INIT(idx)
#endif /* CONFIG_MIPI_DBI_QCC74X_USE_DMA */

#define IRQ_CONFIGURE(n, idx)                                                                      \
	IRQ_CONNECT(DT_INST_IRQ_BY_IDX(idx, n, irq), DT_INST_IRQ_BY_IDX(idx, n, priority),         \
		    mipi_dbi_qcc74x_isr, DEVICE_DT_INST_GET(idx), 0);                                \
	irq_enable(DT_INST_IRQ_BY_IDX(idx, n, irq));

#define CONFIGURE_ALL_IRQS(idx, n) LISTIFY(n, IRQ_CONFIGURE, (), idx)

#define MIPI_DBI_QCC74X_INIT(idx)                                                                    \
	PINCTRL_DT_INST_DEFINE(idx);                                                               \
                                                                                                   \
	static void mipi_dbi_qcc74x##idx##_irq_configure(void)                                       \
	{                                                                                          \
		CONFIGURE_ALL_IRQS(idx, DT_NUM_IRQS(DT_DRV_INST(idx)));                            \
	}                                                                                          \
                                                                                                   \
	static const struct mipi_dbi_qcc74x_config mipi_dbi_config_##idx = {                         \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                              \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(idx, id),                  \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(idx, source),                         \
		.irq_configure = mipi_dbi_qcc74x##idx##_irq_configure,                               \
		.mipi_mode = DT_INST_STRING_UPPER_TOKEN(idx, mipi_mode),                           \
		.clk_freq_hz = DT_INST_PROP(idx, clock_frequency),                                 \
		.gpio_reset = GPIO_DT_SPEC_INST_GET_OR(idx, reset_gpios, {}),                      \
		.reset = RESET_DT_SPEC_INST_GET(idx),                                              \
		.base_addr = DT_INST_REG_ADDR(idx),                                                \
		.irq_num = DT_INST_IRQ_BY_IDX(idx, 0, irq),                                        \
		.irq_priority = DT_INST_IRQ_BY_IDX(idx, 0, priority),                              \
		.irq_flags = DT_INST_IRQ_BY_IDX(idx, 0, flags),                                    \
	};                                                                                         \
                                                                                                   \
	static struct qc7xx_dbi_config_s dbi_cfg_##idx = {                                         \
		.clk_freq_hz = 0,                                                                  \
		.clk_mode = DBI_CLOCK_MODE_1,                                                      \
		.tx_fifo_threshold = 3,                                                            \
	};                                                                                         \
                                                                                                   \
	static struct mipi_dbi_qcc74x_data mipi_dbi_data_##idx = {                                   \
		.dbi_dev = {NULL, 0, 0xFF, 0, 0, 0, NULL},                                         \
		.dbi_cfg = &dbi_cfg_##idx,                                                         \
		MIPI_DBI_DMA_INIT(idx)};                                                           \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, mipi_dbi_qcc74x_init, NULL, &mipi_dbi_data_##idx,                 \
			      &mipi_dbi_config_##idx, POST_KERNEL, CONFIG_MIPI_DBI_INIT_PRIORITY,  \
			      &mipi_dbi_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MIPI_DBI_QCC74X_INIT)
