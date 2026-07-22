/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_jpeg

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/video/jpeg_qcc74x.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>

#include "qc7xx_mjpeg.h"
#include "jpeg/jpeg_head.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(qcc74x_jpeg, CONFIG_VIDEO_LOG_LEVEL);

/* Min/max and default jpeg quality */
#define JPEG_QUALITY     50
#define JPEG_QUALITY_MIN 1
#define JPEG_QUALITY_MAX 100

/**
 * @brief Static (devicetree-provided) configuration for one instance
 */
struct jpeg_qcc74x_config {
	const uint32_t base_reg;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	void (*irq_config_func)(const struct device *dev);
	uint32_t irq_num;
	const struct reset_dt_spec reset;
};

/**
 * @brief Per-instance runtime data
 */
struct jpeg_qcc74x_data {
	struct k_mutex lock;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	struct k_fifo fifo_appin;
	struct k_fifo fifo_appout;
	struct video_buffer *current_in;
	struct video_buffer *current_out;
	atomic_t is_streaming;
	atomic_t codec_is_running;
	atomic_t start_pending;
	struct k_work start_work; /* Deferred start work */
	const struct device *dev; /* Back-pointer to device */
	struct qc7xx_device_s mjpeg;
	struct qc7xx_mjpeg_config_s m_config;
	uint8_t jpg_head_buf[800];
};

/**
 * @brief Bytes-per-pixel as a fraction (num/den).
 *
 * Used for formats with non-integer Bpp (e.g. YUV420 = 3/2).
 * bytes_per_pixel = num / den
 */
struct bpp_frac {
	uint32_t num; /**< Numerator */
	uint32_t den; /**< Denominator */
};

/* JPEG Opal input format caps */
static const struct video_format_cap jpeg_qcc74x_in_fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_YUYV,
		.width_min = 16,
		.width_max = 65520,
		.height_min = 16,
		.height_max = 65520,
		.width_step = 16,
		.height_step = 16,
	},
	{0}};

/* JPEG Opal output format caps */
static const struct video_format_cap jpeg_qcc74x_out_fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 16,
		.width_max = 65520,
		.height_min = 16,
		.height_max = 65520,
		.width_step = 16,
		.height_step = 16,
	},
	{0}};

/**
 * @brief Convert MJPEG input format to JPEG YUV mode.
 *
 * Maps MJPEG_FORMAT_* enumeration to JPEG encoder YUV mode:
 *  - YUV_MODE_400  (grayscale)
 *  - YUV_MODE_420  (4:2:0)
 *  - YUV_MODE_422  (4:2:2)
 *
 * @param format MJPEG input format.
 *
 * @retval YUV_MODE_400  Grayscale input.
 * @retval YUV_MODE_420  4:2:0 input.
 * @retval YUV_MODE_422  4:2:2 input.
 * @retval -EINVAL       Unsupported format.
 */
static int mjpeg_to_jpeg_mode(int format)
{
	switch (format) {

	/* Grayscale */
	case MJPEG_FORMAT_GRAY:
		return YUV_MODE_400;

	/* 4:2:0 formats */
	case MJPEG_FORMAT_YUV420SP_NV12:
	case MJPEG_FORMAT_YUV420SP_NV21:
		return YUV_MODE_420;

	/* 4:2:2 packed */
	case MJPEG_FORMAT_YUV422_YUYV:
	case MJPEG_FORMAT_YUV422_YVYU:
	case MJPEG_FORMAT_YUV422_UYVY:
	case MJPEG_FORMAT_YUV422_VYUY:

	/* 4:2:2 semi-planar */
	case MJPEG_FORMAT_YUV422SP_NV16:
	case MJPEG_FORMAT_YUV422SP_NV61:
		return YUV_MODE_422;

	default:
		return -EINVAL;
	}
}

/**
 * @brief Start one JPEG encode job using one queued input and one queued output buffer.
 *
 * Dequeues exactly one buffer from the input FIFO (raw frame) and one buffer from the
 * output FIFO (JPEG bitstream), programs the hardware encoder, enables interrupts,
 * and starts the encode operation.
 *
 * If either FIFO does not contain a buffer, no job is started and the function
 * returns -EAGAIN. Buffer pairing is handled safely: if an input buffer is dequeued
 * but no output buffer is available, the input buffer is returned back to the input
 * FIFO before returning.
 *
 * @note This function must be called from thread context (never from ISR).
 * @note The caller must hold @ref jpeg_qcc74x_data::lock to serialize access with
 *       enqueue/dequeue and with the deferred start work handler.
 *
 * @param[in] dev JPEG Opal device instance.
 *
 * @retval 0        Encoder started successfully.
 * @retval -EAGAIN  Not enough buffers queued (need one input and one output).
 * @retval -EINVAL  Unsupported/invalid configuration (e.g. format).
 * @retval -EIO     Hardware/programming failure (if supported by qc7xx API).
 */
static int jpeg_qcc74x_start_codec(const struct device *dev)
{
	struct jpeg_qcc74x_data *data = dev->data;
	struct k_fifo *in_fifo = &data->fifo_in;
	struct k_fifo *out_fifo = &data->fifo_out;

	LOG_DBG("Start encoder...");

	if (k_fifo_is_empty(in_fifo) || k_fifo_is_empty(out_fifo)) {
		LOG_INF("Encoder queue is empty!");
		return -EAGAIN;
	}

	/* Get one in/out buffer pair */
	data->current_in = k_fifo_get(in_fifo, K_NO_WAIT);
	data->current_out = k_fifo_get(out_fifo, K_NO_WAIT);

	/* Configure mjpeg encoder */
	if (!IN_RANGE(data->m_config.quality, JPEG_QUALITY_MIN, JPEG_QUALITY_MAX)) {
		data->m_config.quality = JPEG_QUALITY;
	}

	data->m_config.input_bufaddr0 = 0;
	data->m_config.input_bufaddr1 = 0;
	data->m_config.output_bufaddr = 0;
	data->m_config.output_bufsize = 0;

	/* Use default table */
	data->m_config.input_yy_table = NULL; /* use default table */
	data->m_config.input_uv_table = NULL; /* use default table */

	LOG_DBG("Format %d", data->m_config.format);
	LOG_DBG("Quality %d", data->m_config.quality);
	LOG_DBG("In  buf %p, size %d", (void *)&data->current_in->buffer[0],
		data->current_in->size);
	LOG_DBG("Out buf %p, size %d", (void *)&data->current_out->buffer[0],
		data->current_out->size);
	LOG_DBG("Out resolution %dx%d", data->m_config.resolution_x, data->m_config.resolution_y);

	/* Convert MJPEG input format to JPEG YUV mode */
	int jpeg_mode = mjpeg_to_jpeg_mode(data->m_config.format);
	if (jpeg_mode < 0) {
		return -EINVAL; /* Unsupported format */
	}

	LOG_DBG("JPEG mode %d", jpeg_mode);

	/* Init mjpeg encoder */
	qc7xx_mjpeg_init(&data->mjpeg, &data->m_config);
	uint32_t jpg_head_len =
		JpegHeadCreate(jpeg_mode, data->m_config.quality, data->m_config.resolution_x,
			       data->m_config.resolution_y, data->jpg_head_buf);
	qc7xx_mjpeg_fill_jpeg_header_tail(&data->mjpeg, data->jpg_head_buf, jpg_head_len);
	qc7xx_mjpeg_tcint_mask(&data->mjpeg, false);
	data->m_config.rows = data->m_config.resolution_y;
	qc7xx_mjpeg_update_input_output_buff(&data->mjpeg, data->current_in->buffer, NULL,
					     data->current_out->buffer, data->current_out->size);
	irq_enable(data->mjpeg.irq_num);

	/* Start mjpeg encoder */
	data->current_in->timestamp = k_uptime_get_32();
	qc7xx_mjpeg_sw_run(&data->mjpeg, 1);

	atomic_set(&data->codec_is_running, 1);

	return 0;
}

/**
 * @brief Deferred work handler that starts the JPEG encoder.
 *
 * This function is executed in thread context by the Zephyr system
 * workqueue. It is typically submitted from interrupt context (ISR)
 * after a frame has completed, in order to start processing the next
 * queued frame.
 *
 * The handler performs the following steps:
 *   1. Recovers the driver instance data from the embedded work item
 *      using CONTAINER_OF().
 *   2. Atomically checks and clears the @ref start_pending flag.
 *      If no start request is pending, the function exits.
 *   3. Acquires the driver mutex to protect shared state.
 *   4. If streaming is enabled and the codec is not currently running,
 *      attempts to start the hardware encoder by calling
 *      jpeg_qcc74x_start_codec().
 *
 * This function must not be called directly. It is invoked automatically
 * by the Zephyr workqueue subsystem after k_work_submit() is called.
 *
 * @param[in] work Pointer to the embedded k_work structure that is part
 *                 of struct jpeg_qcc74x_data. The pointer is provided by
 *                 the workqueue subsystem when the work item executes.
 *
 * @note The driver instance is retrieved using:
 *       CONTAINER_OF(work, struct jpeg_qcc74x_data, start_work)
 *       because Zephyr v4.1 does not provide per-work user data support.
 *
 * @note This function runs in thread context (not ISR context) and
 *       therefore may use mutexes and call non-ISR-safe APIs.
 */
static void jpeg_qcc74x_start_work_handler(struct k_work *work)
{
	struct jpeg_qcc74x_data *data = CONTAINER_OF(work, struct jpeg_qcc74x_data, start_work);

	/* Ignore duplicate submissions */
	if (!atomic_cas(&data->start_pending, 1, 0)) {
		return;
	}

	/* Protect shared state */
	k_mutex_lock(&data->lock, K_FOREVER);

	if (!atomic_get(&data->codec_is_running) && atomic_get(&data->is_streaming)) {

		int ret = jpeg_qcc74x_start_codec(data->dev);
		if (ret && ret != -EAGAIN) {
			LOG_ERR("Failed to start JPEG codec: %d", ret);
		}
	}

	k_mutex_unlock(&data->lock);
}

static int jpeg_qcc74x_set_ctrl(const struct device *dev, unsigned int cid, void *value)
{
	struct jpeg_qcc74x_data *data = dev->data;
	if (value == NULL) {
		return -EINVAL;
	}

	switch (cid) {
	case VIDEO_CID_JPEG_COMPRESSION_QUALITY: {
		uint32_t quality = *(uint32_t *)value;

		/* Validate range */
		if (!IN_RANGE(quality, JPEG_QUALITY_MIN, JPEG_QUALITY_MAX)) {
			return -EINVAL;
		}

		data->m_config.quality = (uint8_t)quality;
		return 0;
	}

	default:
		return -ENOTSUP;
	}
}

/**
 * Convert Zephyr pixel FOURCC -> MJPEG_FORMAT_*
 *
 * @param pixfmt  video pixelformat (VIDEO_PIX_FMT_* FOURCC)
 * @return >=0 MJPEG_FORMAT_* on success, negative errno on unsupported
 */
static int pixfmt_to_mjpeg(uint32_t pixfmt)
{
	switch (pixfmt) {
	case VIDEO_PIX_FMT_YUYV:
		return MJPEG_FORMAT_YUV422_YUYV;
	case VIDEO_PIX_FMT_YVYU:
		return MJPEG_FORMAT_YUV422_YVYU;
	case VIDEO_PIX_FMT_UYVY:
		return MJPEG_FORMAT_YUV422_UYVY;
	case VIDEO_PIX_FMT_VYUY:
		return MJPEG_FORMAT_YUV422_VYUY;
	case VIDEO_PIX_FMT_NV16:
		return MJPEG_FORMAT_YUV422SP_NV16;
	case VIDEO_PIX_FMT_NV61:
		return MJPEG_FORMAT_YUV422SP_NV61;
	case VIDEO_PIX_FMT_NV12:
		return MJPEG_FORMAT_YUV420SP_NV12;
	case VIDEO_PIX_FMT_NV21:
		return MJPEG_FORMAT_YUV420SP_NV21;
	case VIDEO_PIX_FMT_GREY:
		return MJPEG_FORMAT_GRAY;
	case VIDEO_PIX_FMT_JPEG:
		/* If you want to treat JPEG as an output container format rather than input:
		 * return some sentinel or -ENOTSUP. For now treat as unsupported input.
		 */
		return -ENOTSUP;
	default:
		return -ENOTSUP;
	}
}

static int jpeg_qcc74x_set_fmt(const struct device *dev, enum video_endpoint_id ep,
			     struct video_format *fmt)
{
	struct jpeg_qcc74x_data *data = dev->data;
	int ret = 0;

	if (fmt == NULL) {
		return -EINVAL;
	}

	if (atomic_get(&data->is_streaming)) {
		return -EBUSY;
	}

	switch (ep) {
	case VIDEO_EP_IN: {

		int format = pixfmt_to_mjpeg(fmt->pixelformat);
		if (format < 0) {
			return -ENOTSUP;
		}
		data->m_config.format = format;
		break;
	}
	case VIDEO_EP_OUT: {
		uint32_t width = fmt->width;
		uint32_t height = fmt->height;

		/* Range validation */
		if (!IN_RANGE(width, jpeg_qcc74x_out_fmts[0].width_min,
			      jpeg_qcc74x_out_fmts[0].width_max) ||
		    !IN_RANGE(height, jpeg_qcc74x_out_fmts[0].height_min,
			      jpeg_qcc74x_out_fmts[0].height_max)) {

			return -EINVAL;
		}

		if (!IS_MJPEG_RESOLUTION(width) || !IS_MJPEG_RESOLUTION(height)) {
			return -EINVAL;
		}

		data->m_config.resolution_x = fmt->width;
		data->m_config.resolution_y = fmt->height;

		if (fmt->pixelformat != VIDEO_PIX_FMT_JPEG) {
			return -ENOTSUP;
		}
		break;
	}

	default:
		return -EINVAL;
	}

	return ret;
}

static int jpeg_qcc74x_set_stream(const struct device *dev, bool enable)
{
	struct jpeg_qcc74x_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (enable && atomic_get(&data->is_streaming)) {
		ret = -EALREADY;
	} else {

		atomic_set(&data->is_streaming, enable ? 1 : 0);

		/* Stop encoder */
		if (!enable && data->codec_is_running) {
			irq_disable(data->mjpeg.irq_num);
			qc7xx_mjpeg_stop(&data->mjpeg);
			qc7xx_mjpeg_int_clear(&data->mjpeg, 0xFFFFFFFF);
			k_work_cancel(&data->start_work);
		}

		if (enable) {
			/* Start encoder */
			ret = jpeg_qcc74x_start_codec(dev);
		} else {
			atomic_clear(&data->codec_is_running);
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

/**
 * @brief Get bytes-per-pixel ratio for a given MJPEG input format.
 *
 * This function returns the number of bytes per pixel expressed as a
 * rational fraction (num/den). This is required because some pixel
 * formats (e.g. YUV420) use 1.5 bytes per pixel and cannot be
 * represented as an integer.
 *
 * Supported formats:
 * - MJPEG_FORMAT_YUV422_*      -> 2/1 bytes per pixel
 * - MJPEG_FORMAT_YUV422SP_*    -> 2/1 bytes per pixel
 * - MJPEG_FORMAT_YUV420SP_*    -> 3/2 bytes per pixel (1.5 Bpp)
 * - MJPEG_FORMAT_GRAY          -> 1/1 bytes per pixel
 *
 * @param[in]  format  MJPEG input format enumeration.
 * @param[out] out     Pointer to structure that receives the
 *                      bytes-per-pixel fraction (num/den).
 *
 * @retval 0        Success.
 * @retval -EINVAL  Invalid format or NULL output pointer.
 *
 * @note The returned fraction represents:
 *       bytes_per_pixel = num / den
 *
 * @note The caller is responsible for validating width/height
 *       constraints (e.g. even width for 4:2:2, even width/height
 *       for 4:2:0).
 */
static int mjpeg_bpp_frac(int format, struct bpp_frac *out)
{
	if (!out) {
		return -EINVAL;
	}

	switch (format) {
	case MJPEG_FORMAT_YUV422_YUYV:
	case MJPEG_FORMAT_YUV422_YVYU:
	case MJPEG_FORMAT_YUV422_UYVY:
	case MJPEG_FORMAT_YUV422_VYUY:
	case MJPEG_FORMAT_YUV422SP_NV16:
	case MJPEG_FORMAT_YUV422SP_NV61:
		out->num = 2;
		out->den = 1; // 2 Bpp
		return 0;

	case MJPEG_FORMAT_YUV420SP_NV12:
	case MJPEG_FORMAT_YUV420SP_NV21:
		out->num = 3;
		out->den = 2; // 1.5 Bpp
		return 0;

	case MJPEG_FORMAT_GRAY:
		out->num = 1;
		out->den = 1; // 1 Bpp
		return 0;

	default:
		return -EINVAL;
	}
}

/**
 * @brief Calculate minimum required input buffer size for MJPEG format.
 *
 * Computes the minimum number of bytes required to store a frame of the
 * specified MJPEG input format and resolution. The size is based on
 * tightly packed image data (no stride/padding).
 *
 * @param[in]  format     MJPEG input format enumeration.
 * @param[in]  width      Frame width in pixels.
 * @param[in]  height     Frame height in pixels.
 * @param[out] out_size   Pointer to receive calculated buffer size in bytes.
 *
 * @retval 0        Success.
 * @retval -EINVAL  Invalid format, dimensions, or NULL pointer.
 * @retval -EOVERFLOW Calculated size exceeds 32-bit range.
 *
 * @note This function assumes tightly packed buffers.
 *       If stride or alignment padding is used, the actual buffer
 *       size may be larger than the returned minimum size.
 */
static int mjpeg_min_input_size(int format, int width, int height, uint32_t *out_size)
{
	struct bpp_frac bpp;
	uint64_t pixels, bytes;

	if (!out_size || width <= 0 || height <= 0) {
		return -EINVAL;
	}

	if (mjpeg_bpp_frac(format, &bpp) != 0) {
		return -EINVAL;
	}

	pixels = (uint64_t)width * (uint64_t)height;
	bytes = (pixels * bpp.num) / bpp.den; // exact for valid dims

	if (bytes > UINT32_MAX) {
		return -EOVERFLOW;
	}

	*out_size = (uint32_t)bytes;

	return 0;
}

static int jpeg_qcc74x_enqueue(const struct device *dev, enum video_endpoint_id ep,
			     struct video_buffer *vbuf)
{
	struct jpeg_qcc74x_data *data = dev->data;
	int ret = 0;

	/* Basic pointers and size validation */
	if (vbuf == NULL) {
		return -EINVAL;
	}

	if (vbuf->buffer == NULL || vbuf->size == 0U) {
		return -EINVAL;
	}

	/* Check byte alignment of buffer address */
	if (!IS_MJPEG_ADDR((uintptr_t)vbuf->buffer)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (ep == VIDEO_EP_IN) {
		uint32_t min_sz = 0;
		ret = mjpeg_min_input_size(data->m_config.format, data->m_config.resolution_x,
					   data->m_config.resolution_y, &min_sz);
		if (ret) {
			/* bad format/geometry */
			goto out;
		}
		/* Must have at least minimum required bytes
		 * (buffers may be larger due to alignment)
		 */
		if (vbuf->size < min_sz) {
			ret = -EINVAL;
			goto out;
		}
		k_fifo_put(&data->fifo_in, vbuf);
	} else if (ep == VIDEO_EP_OUT) {
		k_fifo_put(&data->fifo_out, vbuf);
	} else {
		ret = -EINVAL;
	}
out:
	/* Try to start codec if necessary */
	if (ret == 0 && atomic_get(&data->is_streaming) && !atomic_get(&data->codec_is_running)) {
		int sret = jpeg_qcc74x_start_codec(dev);
		if (sret == 0 || sret == -EAGAIN) {
			/* -EAGAIN means one FIFO still missing a buffer; wait for next enqueue */
		} else {
			LOG_ERR("Failed to start codec, err: %d", sret);
			ret = sret;
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int jpeg_qcc74x_dequeue(const struct device *dev, enum video_endpoint_id ep,
			     struct video_buffer **vbuf, k_timeout_t timeout)
{
	struct jpeg_qcc74x_data *data = dev->data;
	int ret = -EINVAL;

	if (vbuf == NULL) {
		return ret;
	}

	/* Retrieve the completed buffer (prepared by ISR) */
	if (ep == VIDEO_EP_OUT) {
		*vbuf = (struct video_buffer *)k_fifo_get(&data->fifo_appout, timeout);
	} else if (ep == VIDEO_EP_IN) {
		*vbuf = (struct video_buffer *)k_fifo_get(&data->fifo_appin, timeout);
	} else {
		return ret;
	}

	if (*vbuf == NULL) {
		return -EIO;
	}

	return 0;
}

/**
 * @brief JPEG Opal MJPEG encoder interrupt service routine.
 *
 * Handles MJPEG encoder interrupts, primarily the "one frame completed"
 * condition. On frame completion, the ISR:
 *  - Acknowledges/clears the completion interrupt source.
 *  - Reads the encoded frame length from hardware.
 *  - Pops the completed frame from the hardware FIFO and stops the encoder.
 *  - Updates the current output buffer metadata (bytesused, timestamp).
 *  - Returns the current input and output buffers to the application-facing
 *    FIFOs (@ref fifo_appin and @ref fifo_appout).
 *
 * After servicing the interrupt, the ISR disables further MJPEG interrupts,
 * clears any latched interrupt sources, marks the codec as idle, and—if
 * streaming is enabled—submits deferred work to start encoding the next
 * queued buffer pair in thread context.
 *
 * @note This ISR must not perform operations that may sleep or block.
 *       The next encode start is deferred to @ref jpeg_qcc74x_start_work_handler()
 *       via k_work_submit().
 *
 * @param[in] dev JPEG Opal device instance.
 */
static void jpeg_qcc74x_isr(const struct device *dev)
{
	struct jpeg_qcc74x_data *data = dev->data;
	uint8_t *pic_addr = NULL;
	uint32_t pic_len = 0;

	uint32_t intstatus = qc7xx_mjpeg_get_intstatus(&data->mjpeg);
	if (intstatus & MJPEG_INTSTS_ONE_FRAME) {
		qc7xx_mjpeg_int_clear(&data->mjpeg, MJPEG_INTCLR_ONE_FRAME);
		data->current_out->timestamp = k_uptime_get_32();
		pic_len = qc7xx_mjpeg_get_frame_info(&data->mjpeg, &pic_addr);
		qc7xx_mjpeg_pop_one_frame(&data->mjpeg);
		qc7xx_mjpeg_stop(&data->mjpeg);
		data->current_out->bytesused = pic_len;

		/* Prepare fifos for application */
		k_fifo_put(&data->fifo_appin, data->current_in);
		k_fifo_put(&data->fifo_appout, data->current_out);
	}

	irq_disable(data->mjpeg.irq_num);
	qc7xx_mjpeg_int_clear(&data->mjpeg, 0xFFFFFFFF);
	atomic_clear(&data->codec_is_running);

	/* Encode next image...*/
	if (atomic_get(&data->is_streaming)) {
		atomic_set(&data->start_pending, 1);
		(void)k_work_submit(&data->start_work);
	}
}

static int jpeg_qcc74x_get_caps(const struct device *dev, enum video_endpoint_id ep,
			      struct video_caps *caps)
{
	ARG_UNUSED(dev);

	if (caps == NULL) {
		return -EINVAL;
	}

	if (ep == VIDEO_EP_IN) {
		caps->format_caps = jpeg_qcc74x_in_fmts;
	} else if (ep == VIDEO_EP_OUT) {
		caps->format_caps = jpeg_qcc74x_out_fmts;
	} else {
		return -EINVAL;
	}

	caps->min_vbuf_count = 1;

	return 0;
}

/**
 * @brief Enable JPEG Opal module clocks
 *
 * Turns on all clocks required for JPEG encoder operation.
 *
 * @param dev JPEG Opal device instance
 * @return 0 on success, negative errno on failure
 */
static int jpeg_qcc74x_enable_clock(const struct device *dev)
{
	const struct jpeg_qcc74x_config *cfg = dev->config;

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	/* Turn on JPEG peripheral clock */
	return clock_control_on(cfg->clock_dev, (clock_control_subsys_t)cfg->clock_id);
}

/**
 * @brief JPEG Opal device initialization function
 *
 * Configures hardware, resets IP block, initializes software state
 * and prepares the driver for use.
 *
 * @param dev Device instance
 * @retval 0 Success
 * @retval -errno Error code on failure
 */
static int jpeg_qcc74x_init(const struct device *dev)
{
	const struct jpeg_qcc74x_config *cfg = dev->config;
	struct jpeg_qcc74x_data *data = dev->data;
	int ret = 0;

	ret = jpeg_qcc74x_enable_clock(dev);
	if (ret < 0) {
		LOG_ERR("Clock enabling failed %d", ret);
		return ret;
	}

	k_usleep(5);

	ret = reset_line_toggle_dt(&cfg->reset);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to reset the device.");
		return ret;
	}

	/* Init MJPEG instance */
	data->mjpeg.reg_base = cfg->base_reg;
	data->mjpeg.irq_num = cfg->irq_num;

	/* IRQ hookup */
	cfg->irq_config_func(dev);

	/* Initialize driver runtime synchronization objects */
	k_mutex_init(&data->lock);
	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
	k_fifo_init(&data->fifo_appin);
	k_fifo_init(&data->fifo_appout);

	atomic_clear(&data->is_streaming);
	atomic_clear(&data->codec_is_running);
	atomic_clear(&data->start_pending);
	data->dev = dev;
	/* Initialize deferred start work item.
	 *
	 * The ISR submits this work after an encode completes to restart the
	 * next encode job in thread context (workqueue), since codec start is
	 * not ISR-safe. The handler recovers the driver instance via
	 * CONTAINER_OF(work, struct jpeg_qcc74x_data, start_work).
	 */
	k_work_init(&data->start_work, jpeg_qcc74x_start_work_handler);

	return 0;
}

/* JPEG QCC74X video driver API implementation */
static DEVICE_API(video, jpeg_qcc74x_driver_api) = {
	.set_format = jpeg_qcc74x_set_fmt,
	.set_stream = jpeg_qcc74x_set_stream,
	.enqueue = jpeg_qcc74x_enqueue,
	.dequeue = jpeg_qcc74x_dequeue,
	.get_caps = jpeg_qcc74x_get_caps,
	.set_ctrl = jpeg_qcc74x_set_ctrl,
};

/* Devicetree-driven instantiation of JPEG QCC74X devices and IRQ handlers */
#define JPEG_QCC74X_IRQ_HANDLER_DECL(n)                                                              \
	static void jpeg_qcc74x_config_func_##n(const struct device *dev);

#define JPEG_QCC74X_IRQ_HANDLER_FUNC(n) .irq_config_func = jpeg_qcc74x_config_func_##n

#define JPEG_QCC74X_IRQ_HANDLER(n)                                                                   \
	static void jpeg_qcc74x_config_func_##n(const struct device *dev)                            \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), jpeg_qcc74x_isr,              \
			    DEVICE_DT_INST_GET(n), 0U);                                            \
		irq_disable(DT_INST_IRQN(n));                                                      \
	}

#define JPEG_QCC74X_DEVICE(n)                                                                        \
	JPEG_QCC74X_IRQ_HANDLER_DECL(n)                                                              \
                                                                                                   \
	static struct jpeg_qcc74x_data jpeg_qcc74x_data_##n;                                           \
                                                                                                   \
	static const struct jpeg_qcc74x_config jpeg_qcc74x_cfg_##n = {                                 \
		.base_reg = DT_INST_REG_ADDR(n),                                                   \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.irq_num = DT_INST_IRQ_BY_IDX(n, 0, irq),                                          \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
                                                                                                   \
		JPEG_QCC74X_IRQ_HANDLER_FUNC(n),                                                     \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, jpeg_qcc74x_init, NULL, &jpeg_qcc74x_data_##n, &jpeg_qcc74x_cfg_##n,    \
			      POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY, &jpeg_qcc74x_driver_api);     \
                                                                                                   \
	JPEG_QCC74X_IRQ_HANDLER(n)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "JPEG QCC74X hardware supports exactly one enabled instance");

JPEG_QCC74X_DEVICE(0)
