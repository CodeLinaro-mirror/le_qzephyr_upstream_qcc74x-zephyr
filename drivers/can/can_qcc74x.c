/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_can

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/clock_control.h>

#include "qcc74x_iso11898.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(can_qcc74x, CONFIG_CAN_LOG_LEVEL);

/** Maximum achievable CAN timing parameters */
#define CAN_QCC74X_TIMING_MAX                                                                        \
	{                                                                                          \
		.sjw = ISO11898_SJW_4TQ, .prop_seg = 0, .phase_seg1 = ISO11898_BS1_16TQ,           \
		.phase_seg2 = ISO11898_BS2_8TQ, .prescaler = 63,                                   \
	}

/** Minimum achievable CAN timing parameters */
#define CAN_QCC74X_TIMING_MIN                                                                        \
	{                                                                                          \
		.sjw = ISO11898_SJW_1TQ, .prop_seg = 0, .phase_seg1 = ISO11898_BS1_1TQ,            \
		.phase_seg2 = ISO11898_BS2_1TQ, .prescaler = 1,                                    \
	}

/**
 * @brief Per-filter state: configuration and callback linkage
 */
struct can_qcc74x_filter {
	bool set;                 /**< Filter slot is occupied */
	struct can_filter filter; /**< Zephyr filter (ID + mask) */
	can_rx_callback_t rx_cb;  /**< RX callback (invoked from ISR) */
	void *rx_usr_data;        /**< User context for rx_cb */
};

/**
 * @brief Static (devicetree-provided) configuration for one instance
 */
struct can_qcc74x_config {
	struct can_driver_config common;                   /* MUST be first */
	const uint32_t base_reg;                           /**< Register base address */
	const struct pinctrl_dev_config *const pincfg;     /**< Pin control configuration */
	const struct device *clock_dev;                    /**< Clock controller */
	const clock_control_subsys_t clock_id;             /**< Clock subsystem ID */
	uint8_t clock_source;                              /**< Clock source selector */
	const uint32_t rx_filter_num;                      /**< Number of RX filter slots */
	void (*irq_config_func)(const struct device *dev); /**< IRQ setup function */
	uint32_t irq_num;                                  /**< IRQ number for low-level driver */
};

/**
 * @brief Per-instance runtime data
 */
struct can_qcc74x_data {
	struct can_driver_data common; /**< Common CAN state */
	struct k_mutex inst_mutex;     /**< Instance lock */
	can_tx_callback_t tx_cb;       /**< Current TX completion callback */
	struct k_sem tx_sem;           /**< TX slot availability */
	void *tx_usr_data;             /**< User data for tx_cb */
	struct can_qcc74x_filter rx_filter[CONFIG_CAN_MAX_FILTER]; /**< SW RX filters + callbacks */
	struct can_timing timing;                                /**< Active timing settings */
	struct qc7xx_device_s can_dev;                           /**< Low-level ISO11898 context */
};

/** Default timing at probe time (loopback mode) */
static struct qcc74x_iso11898_config_s can_timing_default_config = {
	.prescaler = 19,
	.mode = ISO11898_MODE_LOOPBACK,
	.sample = ISO11898_SAMPLE_TRIPLE,
	.sjw = ISO11898_SJW_2TQ,
	.bs1 = ISO11898_BS1_5TQ,
	.bs2 = ISO11898_BS2_4TQ,
};

/** Accept-all filter used when no user filters are active */
static struct qcc74x_iso11898_filter_s can_default_filter = {
	.filter_type = ISO11898_FILTER_TYPE_SINGLE,
	.code0 = 0,
	.code1 = 0,
	.code2 = 0,
	.code3 = 0,
	.mask0 = 0xFF,
	.mask1 = 0xFF,
	.mask2 = 0xFF,
	.mask3 = 0xFF,
};

/**
 * @brief Apply timing parameters to the hardware
 *
 * Converts Zephyr can_timing to low-level config and programs the IP.
 *
 * @param dev Device instance
 * @param[out] f_timing Updated driver copy of timing
 * @param z_timing Requested Zephyr timing
 *
 * @return 0 on success, negative errno otherwise
 */
static inline int set_hw_timing_configuration(const struct device *dev, struct can_timing *f_timing,
					      const struct can_timing *z_timing)
{
	struct can_qcc74x_data *data = dev->data;

	if (f_timing == NULL || z_timing == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	struct qcc74x_iso11898_config_s can_timing_config = {
		.prescaler = z_timing->prescaler,
		.mode = data->common.mode,
		.sample = ISO11898_SAMPLE_TRIPLE,
		.sjw = z_timing->sjw,
		.bs1 = z_timing->prop_seg + z_timing->phase_seg1,
		.bs2 = z_timing->phase_seg2,
	};

	qcc74x_iso11898_init(&data->can_dev, &can_timing_config);
	memcpy(f_timing, z_timing, sizeof(struct can_timing));

	k_mutex_unlock(&data->inst_mutex);

	return 0;
}

/**
 * @brief Find the first unused RX filter slot
 *
 * @param dev Device instance
 *
 * @return Filter index (>=0) or -ENOSPC if full
 */
static inline int get_free_filter_id(const struct device *dev)
{
	struct can_qcc74x_data *data = dev->data;
	const struct can_qcc74x_config *cfg = dev->config;

	for (uint32_t filter_id = 0; filter_id < cfg->rx_filter_num; filter_id++) {
		if (!data->rx_filter[filter_id].set) {
			return filter_id;
		}
	}

	return -ENOSPC;
}

/**
 * @brief Program a single filter rule into hardware
 *
 * @param dev Device instance
 * @param filter Zephyr filter to apply
 */
static void set_acf_rule(const struct device *dev, const struct can_filter *filter)
{
	struct can_qcc74x_data *data = dev->data;

	LOG_DBG("Set acf rule.Id - 0x%x, mask 0x%x", filter->id, filter->mask);

	struct qcc74x_iso11898_filter_s acf_filter = {
		.filter_type = ISO11898_FILTER_TYPE_SINGLE,
		.code0 = 0,
		.code1 = 0,
		.code2 = 0,
		.code3 = 0,
		.mask0 = 0xFF,
		.mask1 = 0xFF,
		.mask2 = 0xFF,
		.mask3 = 0xFF,
	};

	/* Store filter id in big-endian format safely */
	uint8_t be_id[4] = {0};

	sys_put_be32(filter->id, be_id);

	acf_filter.code0 = be_id[0];
	acf_filter.code1 = be_id[1];
	acf_filter.code2 = be_id[2];
	acf_filter.code3 = be_id[3];

	LOG_DBG("Filter code[0x%x 0x%x 0x%x 0x%x]", acf_filter.code0, acf_filter.code1,
		acf_filter.code2, acf_filter.code3);
	LOG_DBG("Filter mask[0x%x 0x%x 0x%x 0x%x]", acf_filter.mask0, acf_filter.mask1,
		acf_filter.mask2, acf_filter.mask3);

	qcc74x_iso11898_set_filter(&data->can_dev, &acf_filter);
}

/**
 * @brief Reset hardware filter to accept-all state
 *
 * @param dev Device instance
 */
static void remove_acf_rule(const struct device *dev)
{
	struct can_qcc74x_data *data = dev->data;

	qcc74x_iso11898_set_filter(&data->can_dev, &can_default_filter);
}

static int can_qcc74x_get_capabilities(const struct device *dev, can_mode_t *cap)
{
	*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY;

	return 0;
}

static int can_qcc74x_set_mode(const struct device *dev, can_mode_t mode)
{
	struct can_qcc74x_data *data = dev->data;
	int ret = 0;

	/* CAN controller should be in stopped state */
	if (data->common.started) {
		return -EBUSY;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	can_mode_t caps = 0;

	ret = can_qcc74x_get_capabilities(dev, &caps);
	if ((mode & ~caps) == 0) {
		data->common.mode = mode;
	} else {
		ret = -ENOTSUP;
	}

	k_mutex_unlock(&data->inst_mutex);

	return ret;
}

static int can_qcc74x_start(const struct device *dev)
{
	struct can_qcc74x_data *data = dev->data;

	if (!device_is_ready(dev)) {
		return -EIO;
	}

	if (data->common.started) {
		return -EALREADY;
	}

	int key = irq_lock();

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	data->common.started = true;

	qcc74x_iso11898_txint_mask(&data->can_dev, false);
	qcc74x_iso11898_rxint_mask(&data->can_dev, false);
	qcc74x_iso11898_errint_mask(&data->can_dev, false);

	k_mutex_unlock(&data->inst_mutex);
	irq_unlock(key);

	return 0;
}

static int can_qcc74x_stop(const struct device *dev)
{
	struct can_qcc74x_data *data = dev->data;

	if (!data->common.started) {
		return -EALREADY;
	}

	int key = irq_lock();

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	qcc74x_iso11898_txint_mask(&data->can_dev, true);
	qcc74x_iso11898_rxint_mask(&data->can_dev, true);
	qcc74x_iso11898_errint_mask(&data->can_dev, true);
	qcc74x_iso11898_abort_send(&data->can_dev);

	k_sem_give(&data->tx_sem);
	if (data->tx_cb != NULL) {
		data->tx_cb = NULL;
	}

	data->common.started = false;

	k_mutex_unlock(&data->inst_mutex);
	irq_unlock(key);

	return 0;
}

static int can_qcc74x_set_timing(const struct device *dev, const struct can_timing *timing)
{
	struct can_qcc74x_data *data = dev->data;

	LOG_DBG("Set timing");

	/* Device is not in stopped state */
	if (data->common.started) {
		return -EBUSY;
	}

	if (!timing) {
		return -EINVAL;
	}

	LOG_DBG("prescaller - %d", timing->prescaler);
	LOG_DBG("sjw - %d", timing->sjw);
	LOG_DBG("prop_seg - %d", timing->prop_seg);
	LOG_DBG("phase_seg1 - %d", timing->phase_seg1);
	LOG_DBG("phase_seg2 - %d", timing->phase_seg2);

	/* Program BRP, TSEG1, TSEG2, SJW into registers */
	return set_hw_timing_configuration(dev, &data->timing, timing);
}

static int can_qcc74x_send(const struct device *dev, const struct can_frame *frame,
			 k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	struct can_qcc74x_data *data = dev->data;
	int ret = 0;

	LOG_DBG("Sending %d bytes. Id: 0x%x, ID type: %s %s", frame->dlc, frame->id,
		(frame->flags & CAN_FRAME_IDE) != 0 ? "extended" : "standard",
		(frame->flags & CAN_FRAME_RTR) != 0 ? ", RTR frame" : "");

	if (!data->common.started) {
		LOG_WRN("can send %d, net is down !", data->common.started);
		return -ENETDOWN;
	}

	/* Explicitly reject CAN FD (Classic CAN only driver) */
	if ((frame->flags & CAN_FRAME_FDF) != 0U) {
		LOG_ERR("CAN FD frames are not supported (flags=0x%02x)", frame->flags);
		return -ENOTSUP;
	}

	/* Reject any other unsupported flags (keep only IDE and RTR) */
	if ((frame->flags & ~(CAN_FRAME_IDE | CAN_FRAME_RTR)) != 0U) {
		LOG_ERR("unsupported CAN frame flags 0x%02x", frame->flags);
		return -ENOTSUP;
	}

	/* Classic CAN DLC check */
	if (frame->dlc > CAN_MAX_DLC) {
		LOG_ERR("DLC of %d for classic CAN frame", frame->dlc);
		return -EINVAL;
	}

	if (k_sem_take(&data->tx_sem, timeout) != 0) {
		return -EAGAIN;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	data->tx_cb = callback;
	data->tx_usr_data = user_data;

	struct qcc74x_iso11898_msg_s msg_tx = {0};

	msg_tx.id_type =
		((frame->flags & CAN_FRAME_IDE) != 0) ? ISO11898_ID_EXTENDED : ISO11898_ID_STANDARD;
	msg_tx.rtr =
		((frame->flags & CAN_FRAME_RTR) != 0) ? ISO11898_RTR_REMOTE : ISO11898_RTR_DATA;
	msg_tx.dlc = frame->dlc;

	if ((frame->flags & CAN_FRAME_IDE) != 0) {
		/* Extended identifier */
		msg_tx.ext_id = frame->id;
	} else {
		/* Standard identifier */
		msg_tx.std_id = frame->id;
	}

	uint8_t data_length = can_dlc_to_bytes(frame->dlc);

	if (data_length > sizeof(msg_tx.data)) {
		LOG_ERR("CAN TX overflow (%u)", data_length);
		ret = -EINVAL;
	} else {
		memcpy(msg_tx.data, frame->data, data_length);
		ret = qcc74x_iso11898_send(&data->can_dev, &msg_tx, 0);
		if (ret != 0) {
			LOG_ERR("CAN transmit failed(%d)", ret);
			data->tx_cb = NULL;
			data->tx_usr_data = NULL;
			ret = -EIO;
		}
	}

	k_mutex_unlock(&data->inst_mutex);

	/* Release semaphore only if TX did NOT start */
	if (ret != 0) {
		k_sem_give(&data->tx_sem);
	}

	return ret;
}

static int can_qcc74x_add_rx_filter(const struct device *dev, can_rx_callback_t callback,
				  void *user_data, const struct can_filter *filter)
{
	const struct can_qcc74x_config *cfg = dev->config;
	struct can_qcc74x_data *data = dev->data;
	int filter_id = -ENOSPC;

	if (filter == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	filter_id = get_free_filter_id(dev);
	if (filter_id == -ENOSPC) {
		LOG_WRN("can add_rx_filter no free slots !");
	} else {

		/* Program hardware filter */
		if (cfg->rx_filter_num == 1U) {
			set_acf_rule(dev, filter);
		}

		memcpy(&data->rx_filter[filter_id].filter, filter, sizeof(struct can_filter));
		data->rx_filter[filter_id].rx_cb = callback;
		data->rx_filter[filter_id].rx_usr_data = user_data;
		data->rx_filter[filter_id].set = true;
	}

	k_mutex_unlock(&data->inst_mutex);

	return filter_id;
}

static void can_qcc74x_remove_rx_filter(const struct device *dev, int filter_id)
{
	const struct can_qcc74x_config *cfg = dev->config;
	struct can_qcc74x_data *data = dev->data;

	if ((filter_id < 0) || (filter_id >= cfg->rx_filter_num)) {
		return;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	/* Remove hardware filter */
	if (cfg->rx_filter_num == 1U) {
		remove_acf_rule(dev);
	}

	data->rx_filter[filter_id].rx_cb = NULL;
	data->rx_filter[filter_id].rx_usr_data = NULL;
	data->rx_filter[filter_id].set = false;

	k_mutex_unlock(&data->inst_mutex);
}

static int can_qcc74x_get_state(const struct device *dev, enum can_state *state,
			      struct can_bus_err_cnt *err_cnt)
{
	struct can_qcc74x_data *data = dev->data;

	if (state != NULL) {
		*state = data->common.started ? CAN_STATE_ERROR_ACTIVE : CAN_STATE_STOPPED;
	}

	if (err_cnt != NULL) {
		err_cnt->tx_err_cnt = 0;
		err_cnt->rx_err_cnt = 0;
	}

	return 0;
}

static void can_qcc74x_set_state_change_callback(const struct device *dev,
					       can_state_change_callback_t callback,
					       void *user_data)
{
	struct can_qcc74x_data *data = dev->data;

	int key = irq_lock();

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	data->common.state_change_cb = callback;
	data->common.state_change_cb_user_data = user_data;

	k_mutex_unlock(&data->inst_mutex);
	irq_unlock(key);
}

static int can_qcc74x_get_core_clock(const struct device *dev, uint32_t *rate)
{
	const struct can_qcc74x_config *cfg = dev->config;

	return clock_control_get_rate(cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)cfg->clock_id, rate);
}

static int can_qcc74x_get_max_filters(const struct device *dev, bool ide)
{
	ARG_UNUSED(ide);
	const struct can_qcc74x_config *cfg = dev->config;

	return cfg->rx_filter_num;
}

/**
 * @brief Invoke CAN state change callback (ISR context).
 *
 * Error statistics are not implemented and are reported as zero.
 *
 * @param dev   Device instance
 * @param state New CAN controller state
 */
static inline void can_qcc74x_call_state_change_cb(const struct device *dev, enum can_state state)
{
	struct can_qcc74x_data *data = dev->data;

	if (data->common.state_change_cb == NULL) {
		return;
	}
	struct can_bus_err_cnt err_cnt = {
		.rx_err_cnt = 0,
		.tx_err_cnt = 0,
	};

	data->common.state_change_cb(dev, state, err_cnt, data->common.state_change_cb_user_data);
}

/**
 * @brief Handle RX interrupt and dispatch received frames.
 *
 * Drains the RX FIFO and forwards each received CAN frame
 * to all matching software filters.
 *
 * @param dev    Device instance
 */
static inline void can_qcc74x_call_rx_cb(const struct device *dev)
{
	const struct can_qcc74x_config *cfg = dev->config;
	struct can_qcc74x_data *data = dev->data;

	/* Drain RX FIFO while not empty.
	 * We cap iterations to avoid an infinite loop if the low-level recv()
	 * does not advance/release the FIFO entry.
	 *
	 * FIFO is 64 bytes; frame window is 13 bytes => ~4 frames typical max.
	 * Cap at 8 is conservative.
	 */
	for (int iter = 0; iter < 8; iter++) {
		uint8_t sts = qcc74x_iso11898_get_status(&data->can_dev);

		if ((sts & ISO11898_STATUS_RX_NOT_EMPTY) == 0U) {
			break; /* FIFO empty */
		}

		struct qcc74x_iso11898_msg_s msg = {0};

		int8_t ret = qcc74x_iso11898_recv(&data->can_dev, &msg, 0);

		if (ret != 0) {
			break;
		}

		/* Defensive: classic CAN only (FD not supported).
		 * In your header, msg.dlc is documented as 0..8 and data[8],
		 * so this should never trigger unless corruption occurs.
		 */
		if (msg.dlc > CAN_MAX_DLC) {
			LOG_ERR("RX: DLC %u > %u (CAN FD not supported) - drop", msg.dlc,
				CAN_MAX_DLC);
			continue;
		}
		struct can_frame frame = {0};

		/* msg.dlc is bytes (0..8). Convert to Zephyr DLC code. */
		frame.dlc = can_bytes_to_dlc(msg.dlc);

		/* Copy correct number of bytes */
		uint8_t len = can_dlc_to_bytes(frame.dlc);

		if (len > sizeof(frame.data) || len > sizeof(msg.data)) {
			LOG_ERR("RX: length overflow (%u) - drop", len);
			continue;
		}
		memcpy(frame.data, msg.data, len);

		frame.flags = 0U;
		if (msg.id_type == ISO11898_ID_STANDARD) {
			frame.id = msg.std_id;
		} else {
			frame.id = msg.ext_id;
			frame.flags |= CAN_FRAME_IDE;
		}

		if (msg.rtr == ISO11898_RTR_REMOTE) {
#ifdef CONFIG_CAN_ACCEPT_RTR
			frame.flags |= CAN_FRAME_RTR;
#else
			continue;
#endif
		}

		LOG_DBG("Receive data frame [%u bytes]", msg.dlc);

		/* Dispatch to ALL matching SW filters */
		for (uint32_t i = 0; i < cfg->rx_filter_num; i++) {
			struct can_qcc74x_filter *f = &data->rx_filter[i];

			if (f->rx_cb == NULL) {
				continue;
			}

			if (!can_frame_matches_filter(&frame, &f->filter)) {
				continue;
			}

			f->rx_cb(dev, &frame, f->rx_usr_data);
		}
	}
}

/**
 * @brief Handle TX completion (ISR context)
 *
 * Releases the transmit semaphore to free the TX slot and invokes the
 * user-provided TX callback if one was registered. The callback is
 * executed in interrupt context.
 *
 * @param dev CAN device instance
 * @param err Transmission result (0 on success, negative errno on failure)
 */
static inline void can_qcc74x_call_tx_cb(const struct device *dev, int err)
{
	struct can_qcc74x_data *data = dev->data;
	can_tx_callback_t cb = data->tx_cb;

	/* Release TX slot unconditionally */
	k_sem_give(&data->tx_sem);

	/* Callback is optional */
	if (cb != NULL) {
		data->tx_cb = NULL;
		cb(dev, err, data->tx_usr_data);
	}
}

/**
 * @brief CAN controller ISR
 *
 * Handles RX, TX, error and state-change events.
 * Dispatches to user callbacks in interrupt context.
 *
 * @param dev Device pointer
 */
static void can_qcc74x_isr(const struct device *dev)
{
	struct can_qcc74x_data *data = dev->data;
	uint32_t int_status = qcc74x_iso11898_get_int_status(&data->can_dev);

	if (int_status & ISO11898_INTSTS_RX) {
		can_qcc74x_call_rx_cb(dev);
	}
	if (int_status & ISO11898_INTSTS_TX) {
		LOG_DBG("TX succeed!");
		can_qcc74x_call_tx_cb(dev, 0);
	}
	if (int_status & ISO11898_INTSTS_ARBITRATION_LOST) {
		can_qcc74x_call_tx_cb(dev, -EBUSY);
		LOG_DBG("Arbitration failed!");
	}
	if (int_status & ISO11898_INTSTS_BUS_ERROR) {
		can_qcc74x_call_tx_cb(dev, -EIO);
		LOG_DBG("Bus error!");
	}
	if (int_status & ISO11898_INTSTS_ERROR_PASSIVE) {
		can_qcc74x_call_state_change_cb(dev, CAN_STATE_ERROR_PASSIVE);
		LOG_DBG("Passive error!");
	}
	if (int_status & ISO11898_INTSTS_DATA_OVERRUN) {
		can_qcc74x_call_state_change_cb(dev, CAN_STATE_ERROR_ACTIVE);
		LOG_DBG("Data overrun!");
	}
	if (int_status & ISO11898_INTSTS_ERROR_WARNING) {
		can_qcc74x_call_state_change_cb(dev, CAN_STATE_ERROR_WARNING);
		LOG_DBG("Warning error!");
	}
}

/** Helper function for iso11898 library */
uint64_t qcc74x_mtimer_get_time_ms(void)
{
	return k_uptime_get();
}

/**
 * @brief Initialize the CAN Opal device
 *
 * This function performs the complete initialization of the CAN Opal driver instance.
 * It sets up the instance mutex and semaphore, applies pin multiplexing configuration,
 * enables the peripheral clock, initializes the underlying ISO11898 controller with
 * default timing and filter settings, clears receive filters, and hooks up the interrupt
 * handler.
 *
 * @param dev Pointer to the Zephyr device structure for this CAN Opal instance
 *
 * @return 0 on successful initialization
 * @return negative errno code on failure (e.g. -EINVAL, -EIO, clock/pinctrl errors)
 */
static int can_qcc74x_init(const struct device *dev)
{
	const struct can_qcc74x_config *cfg = dev->config;
	struct can_qcc74x_data *data = dev->data;
	int ret = 0;

	LOG_DBG("Init CAN device at base address 0x%08x", cfg->base_reg);

	k_mutex_init(&data->inst_mutex);
	k_sem_init(&data->tx_sem, 1, 1);
	data->common.started = false;

	ret = pinctrl_apply_state(cfg->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Failed to apply pin configuration: %d", ret);
		return ret;
	}

	/* Enable clock */
	ret = clock_control_on(cfg->clock_dev, (clock_control_subsys_t)(uintptr_t)cfg->clock_id);
	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to enable CAN clock: %d", ret);
		return ret;
	}

	/* Init CAN instance */
	data->can_dev.reg_base = cfg->base_reg;
	data->can_dev.irq_num = cfg->irq_num;
	LOG_DBG("iso11898 irq_num %d", data->can_dev.irq_num);

	qcc74x_iso11898_init(&data->can_dev, &can_timing_default_config);

	/* Configure hardware acceptance filter to "accept all".
	 * The ISO11898 controller supports only a single hardware filter.
	 * To allow multiple Zephyr RX filters to coexist, we keep the HW
	 * filter fully permissive and perform all filtering in software.
	 */
	qcc74x_iso11898_set_filter(&data->can_dev, &can_default_filter);
	memset(data->rx_filter, 0x00, sizeof(data->rx_filter));

	/* IRQ hookup */
	cfg->irq_config_func(dev);

	return 0;
}

static const struct can_driver_api can_qcc74x_api = {
	.start = can_qcc74x_start,
	.stop = can_qcc74x_stop,
	.set_mode = can_qcc74x_set_mode,
	.set_timing = can_qcc74x_set_timing,
	.send = can_qcc74x_send,
	.add_rx_filter = can_qcc74x_add_rx_filter,
	.remove_rx_filter = can_qcc74x_remove_rx_filter,
	.get_capabilities = can_qcc74x_get_capabilities,
	.get_state = can_qcc74x_get_state,
	.set_state_change_callback = can_qcc74x_set_state_change_callback,
	.get_core_clock = can_qcc74x_get_core_clock,
	.get_max_filters = can_qcc74x_get_max_filters,
	.timing_min = CAN_QCC74X_TIMING_MIN,
	.timing_max = CAN_QCC74X_TIMING_MAX,
};

#define CAN_QCC74X_IRQ_HANDLER_DECL(n) static void can_qcc74x_config_func_##n(const struct device *dev);

#define CAN_QCC74X_IRQ_HANDLER_FUNC(n) .irq_config_func = can_qcc74x_config_func_##n

#define CAN_QCC74X_IRQ_HANDLER(n)                                                                    \
	static void can_qcc74x_config_func_##n(const struct device *dev)                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), can_qcc74x_isr,               \
			    DEVICE_DT_INST_GET(n), 0U);                                            \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define CAN_QCC74X_DEVICE(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	CAN_QCC74X_IRQ_HANDLER_DECL(n)                                                               \
                                                                                                   \
	static struct can_qcc74x_data can_qcc74x_data_##n;                                             \
                                                                                                   \
	static const struct can_qcc74x_config can_qcc74x_cfg_##n = {                                   \
		.common = {.phy = NULL,            /* no transceiver */                            \
			   .max_bitrate = 1000000, /* 1 Mbit/s */                                  \
			   .min_bitrate = 20000,   /* 20 kbit/s */                                 \
			   .bitrate = DT_INST_PROP_OR(n, bitrate, CONFIG_CAN_DEFAULT_BITRATE)},    \
		.base_reg = DT_INST_REG_ADDR(n),                                                   \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(n, source),                           \
		.rx_filter_num = CONFIG_CAN_MAX_FILTER,                                            \
		.irq_num = DT_INST_IRQ_BY_IDX(n, 0, irq),                                          \
                                                                                                   \
		CAN_QCC74X_IRQ_HANDLER_FUNC(n),                                                      \
	};                                                                                         \
                                                                                                   \
	CAN_DEVICE_DT_INST_DEFINE(n, can_qcc74x_init, NULL, &can_qcc74x_data_##n, &can_qcc74x_cfg_##n,   \
				  POST_KERNEL, CONFIG_CAN_INIT_PRIORITY, &can_qcc74x_api);           \
                                                                                                   \
	CAN_QCC74X_IRQ_HANDLER(n)

DT_INST_FOREACH_STATUS_OKAY(CAN_QCC74X_DEVICE)
