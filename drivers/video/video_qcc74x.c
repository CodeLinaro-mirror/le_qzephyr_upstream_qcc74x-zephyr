/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_cam

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/drivers/clock_control.h>

#include "qc7xx_cam.h"
#include "hardware/cam_reg.h"
#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>

#define LOG_LEVEL CONFIG_VIDEO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(qcom_qcc74x_cam);

/**
 * @brief Video configuration structure.
 */
struct video_qcc74x_config {
	/** Pin control configuration */
	const struct pinctrl_dev_config *pcfg;
	/** Pointer for IRQ initialization func */
	void (*irq_init)(void);
	/** Base register address */
	const uint32_t reg_base;
	/** Source device (image sensor) */
	const struct device *source_dev;
	/** Clock device */
	const struct device *clock_dev;
	/** Clock id (Peripheral ID) */
	const clock_control_subsys_t clock_id;
	/** Clock source (source option ID) */
	uint8_t clock_source;
	/** Clock divider value */
	uint8_t clock_div;
};

/**
 * @brief Video runtime data structure.
 */
struct video_qcc74x_data {
	/** Buffer which is currently used to capture image */
	struct video_buffer *active_buffer;
	/** Struct to device in HAL layer */
	struct qc7xx_device_s cam_hal_device;
	/** Checks if capture is currently running */
	bool stream_started;
	/** Current video format */
	struct video_format video_format;
	/** Input buffers FIFO */
	struct k_fifo fifo_in;
	/** Output buffers FIFO */
	struct k_fifo fifo_out;
	/** Frame completion signal */
	struct k_poll_signal *signal;
};

/**
 * @brief Helper function to transform video pixel format to internal pixel format
 *
 * @param pixel_format video pixel format
 * @return pixel format used in HAL internaly.
 */
static uint32_t pixel_format_to_qcc_cam_pixel_format(uint32_t pixel_format)
{
	switch (pixel_format) {
	case VIDEO_PIX_FMT_RGB565:
		return CAM_INPUT_FORMAT_RGB565;
	case VIDEO_PIX_FMT_YUYV:
		return CAM_INPUT_FORMAT_YUV422_YUYV;
	default:
		LOG_INF("Unknown pixel format: %u. Default RGB565 will be returned", pixel_format);
		return CAM_INPUT_FORMAT_RGB565;
	}
}

/**
 * @brief Helper function to initialize internal cam config from HAL
 *
 * @param config internal cam config
 * @param fmt video format
 * @param active_buffer buffer which is intended to save frame into
 */
static void video_qcc74x_fill_hal_cam_config(struct qc7xx_cam_config_s *config,
					   const struct video_format *fmt,
					   const struct video_buffer *active_buffer)
{
	config->input_source = CAM_INPUT_SOURCE_DVP;
	config->output_format = CAM_OUTPUT_FORMAT_AUTO;

	config->input_format = pixel_format_to_qcc_cam_pixel_format(fmt->pixelformat);
	if (fmt->pixelformat == VIDEO_PIX_FMT_JPEG) {
		config->with_mjpeg = true;
	} else {
		config->with_mjpeg = false;
	}

	config->resolution_x = fmt->width;
	config->resolution_y = fmt->height;

	config->output_bufsize = active_buffer->size;
	config->output_bufaddr = (uint32_t)(active_buffer->buffer);
}

/**
 * @brief Helper function to trigger capturing video data. Called, when the new video buffer is
 * used.
 *
 * @param data video driver runtime data
 */
static void video_qcc74x_start_capture(struct video_qcc74x_data *data)
{
	struct qc7xx_cam_config_s cam_hal_config;

	qc7xx_cam_stop(&data->cam_hal_device);
	video_qcc74x_fill_hal_cam_config(&cam_hal_config, &data->video_format, data->active_buffer);
	qc7xx_cam_init(&data->cam_hal_device, &cam_hal_config);
	qc7xx_cam_start(&data->cam_hal_device);
}

/**
 * @brief Initialize Video device.
 *
 * @param dev     Pointer to device structure.
 * @retval 0      On success.
 * @retval -errno Negative errno on failure.
 */
static int video_qcc74x_init(const struct device *dev)
{
	const struct video_qcc74x_config *config = dev->config;
	struct video_qcc74x_data *data = dev->data;
	int regval = 0;
	int ret = 0;
	struct qcc74x_clk_settings clk_settings = {
		.enable = CLOCK_ENABLE,
		.clk_src = config->clock_source,
		.clk_div = config->clock_div,
	};

	ret = clock_control_configure(config->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)config->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure CAM clock: %d", ret);
	}

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		LOG_ERR("Pin control setup failed: %d", ret);
		return ret;
	}

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);

	config->irq_init();
	regval = sys_read32(config->reg_base + CAM_DVP_STATUS_AND_ERROR_OFFSET);
	/* Trigger interrupt when frame is captured. */
	regval |= (1 << CAM_REG_FRAME_CNT_TRGR_INT_SHIFT);
	regval |= CAM_REG_INT_NORMAL_EN;
	sys_write32(regval, config->reg_base + CAM_DVP_STATUS_AND_ERROR_OFFSET);

	/* Save default format */
	ret = video_get_format(config->source_dev, &data->video_format);
	if (ret != 0) {
		LOG_ERR("Cannot get video format.");
		return ret;
	}

	return ret;
}

/**
 * @brief Interrupt handler. Interrupt is triggered when frame is completed and present in buffer.
 *
 * @param dev     Pointer to device structure.
 */
static void video_qcc74x_isr(const struct device *dev)
{
	struct video_qcc74x_data *data = dev->data;
	struct video_buffer *next_buffer = NULL;

	qc7xx_cam_int_clear(&data->cam_hal_device, CAM_REG_INT_NORMAL_CLR);
	qc7xx_cam_pop_one_frame(&data->cam_hal_device);

	if (data->active_buffer == NULL) {
		LOG_INF("No video buffers available. Call video_enqueue first \n");
		return;
	}

	data->active_buffer->timestamp = k_uptime_get_32();
	k_fifo_put(&data->fifo_out, data->active_buffer);

#ifdef CONFIG_POLL
	if (data->signal) {
		k_poll_signal_raise(data->signal, VIDEO_BUF_DONE);
	}
#endif

	next_buffer = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	if (next_buffer == NULL) {
		/* Current buffer will be re-used. It could then being overwritten before
		 * fully consumed by client. */
		LOG_WRN("Buffer for the next frame is not available.");
		return;
	}

	data->active_buffer = next_buffer;
	/* Address to save image data changed, so re-do cam init and start*/
	video_qcc74x_start_capture(data);
}

/* Video  API Implementation */

static int video_qcc74x_set_fmt(const struct device *dev, struct video_format *fmt)
{
	const struct video_qcc74x_config *config = dev->config;
	struct video_qcc74x_data *data = dev->data;

	if (fmt == NULL) {
		return -EINVAL;
	}

	data->video_format = *fmt;

	return video_set_format(config->source_dev, fmt);
}

static int video_qcc74x_get_fmt(const struct device *dev, struct video_format *fmt)
{
	const struct video_qcc74x_config *config = dev->config;

	if (fmt == NULL) {
		return -EINVAL;
	}

	return video_get_format(config->source_dev, fmt);
}

static int video_qcc74x_set_frmival(const struct device *dev, struct video_frmival *frmival)
{
	const struct video_qcc74x_config *config = dev->config;

	if (frmival == NULL) {
		return -EINVAL;
	}

	return video_set_frmival(config->source_dev, frmival);
}

static int video_qcc74x_get_frmival(const struct device *dev, struct video_frmival *frmival)
{
	const struct video_qcc74x_config *config = dev->config;

	if (frmival == NULL) {
		return -EINVAL;
	}

	return video_get_frmival(config->source_dev, frmival);
}

static int video_qcc74x_get_caps(const struct device *dev, struct video_caps *caps)
{
	const struct video_qcc74x_config *config = dev->config;

	if (caps == NULL) {
		return -EINVAL;
	}

	return video_get_caps(config->source_dev, caps);
}

static int video_qcc74x_set_stream(const struct device *dev, bool enable,
				   enum video_buf_type type)
{
	const struct video_qcc74x_config *config = dev->config;
	struct video_qcc74x_data *data = dev->data;

	if (!enable) {
		qc7xx_cam_stop(&data->cam_hal_device);

		if (video_stream_stop(config->source_dev, type)) {
			return -EIO;
		}

		data->stream_started = false;
	}

	if (data->stream_started) {
		LOG_WRN("Video stream already started \n");
		return -EBUSY;
	}

	data->active_buffer = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	if (data->active_buffer == NULL) {
		LOG_ERR("No enqueued video buffers available.");
		return -EAGAIN;
	}

	video_qcc74x_start_capture(data);

	if (video_stream_start(config->source_dev, type)) {
		return -EIO;
	}

	data->stream_started = true;
	return 0;
}

static int video_qcc74x_enqueue(const struct device *dev, struct video_buffer *buf)
{
	struct video_qcc74x_data *data = dev->data;
	const uint32_t frame_size = data->video_format.pitch * data->video_format.height;

	if (frame_size > buf->size) {
		LOG_ERR("Buffer size (%u) is not enough to fit the frame (size=%u)", buf->size,
			frame_size);
		return -EINVAL;
	}

	buf->bytesused = frame_size;
	buf->line_offset = 0;
	k_fifo_put(&data->fifo_in, buf);
	return 0;
}

static int video_qcc74x_dequeue(const struct device *dev,
			      struct video_buffer **buf, k_timeout_t timeout)
{
	struct video_qcc74x_data *data = dev->data;

	*buf = k_fifo_get(&data->fifo_out, timeout);
	if (*buf == NULL) {
		LOG_ERR("No buffer to dequeue \n");
		return -EAGAIN;
	}

	return 0;
}

static int video_qcc74x_flush(const struct device *dev, bool cancel)
{
	struct video_qcc74x_data *data = dev->data;
	struct video_buffer *vbuf = data->active_buffer;

	if (cancel) {
		data->active_buffer = NULL;
		qc7xx_cam_stop(&data->cam_hal_device);
		do {
			if (vbuf != NULL) {
				k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
				if (data->signal) {
					k_poll_signal_raise(data->signal, VIDEO_BUF_ABORTED);
				}
#endif
			}
		} while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT)));

	} else {
		/* wait for all buffers to be processed */
		do {
			k_sleep(K_MSEC(1));
		} while (!k_fifo_is_empty(&data->fifo_in));
	}

	return 0;
}

static inline int video_qcc74x_set_ctrl(const struct device *dev, uint32_t cid)
{
	const struct video_qcc74x_config *config = dev->config;
	struct video_control ctrl = { .id = cid };

	return video_set_ctrl(config->source_dev, &ctrl);
}

static inline int video_qcc74x_get_ctrl(const struct device *dev, uint32_t cid)
{
	const struct video_qcc74x_config *config = dev->config;
	struct video_control ctrl = { .id = cid };

	return video_get_ctrl(config->source_dev, &ctrl);
}

#ifdef CONFIG_POLL
static int video_qcc74x_set_signal(const struct device *dev,
				 struct k_poll_signal *signal)
{
	struct video_qcc74x_data *data = dev->data;

	if (data->signal && signal != NULL) {
		LOG_WRN("Overwriting existing signal");
	}

	data->signal = signal;
	return 0;
}
#endif

static DEVICE_API(video, video_qcc74x_driver_api) = {
	.set_format = video_qcc74x_set_fmt,
	.get_format = video_qcc74x_get_fmt,
	.set_frmival = video_qcc74x_set_frmival,
	.get_frmival = video_qcc74x_get_frmival,
	.set_stream = video_qcc74x_set_stream,
	.get_caps = video_qcc74x_get_caps,
	.enqueue = video_qcc74x_enqueue,
	.dequeue = video_qcc74x_dequeue,
	.flush = video_qcc74x_flush,
	.set_ctrl = video_qcc74x_set_ctrl,
	.get_volatile_ctrl = video_qcc74x_get_ctrl,
#ifdef CONFIG_POLL
	.set_signal = video_qcc74x_set_signal,
#endif
};

#define VIDEO_qcc74x_DEVICE(n)                                                                       \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static void video_qcc74x_irq_init##n(void)                                                   \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), video_qcc74x_isr,             \
			    DEVICE_DT_INST_GET(n), 0);                                             \
                                                                                                   \
		irq_enable(DT_INST_IRQN(n));                                                       \
	};                                                                                         \
	static struct video_qcc74x_data video_qcc74x_data_##n = {                                      \
		.active_buffer = NULL,                                                             \
		.cam_hal_device = {.name = "",                                                     \
				   .reg_base = DT_INST_REG_ADDR(n),                                \
				   .irq_num = DT_INST_IRQN(n),                                     \
				   .idx = n},                                                      \
		.stream_started = false,                                                           \
		.signal = NULL,                                                                    \
	};                                                                                         \
	static const struct video_qcc74x_config video_qcc74x_config_##n = {                            \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.irq_init = video_qcc74x_irq_init##n,                                                \
		.reg_base = DT_INST_REG_ADDR(n),                                                   \
		.source_dev = DEVICE_DT_GET(DT_INST_PHANDLE(0, source)),                           \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(n, source),                           \
		.clock_div = (uint8_t)DT_INST_PROP(n, div),                                        \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, video_qcc74x_init, NULL, &video_qcc74x_data_##n,                      \
			      &video_qcc74x_config_##n, POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY,     \
			      &video_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VIDEO_qcc74x_DEVICE)
