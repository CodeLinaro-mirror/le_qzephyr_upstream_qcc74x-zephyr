/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_i2c

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/ring_buffer.h>
#ifdef CONFIG_I2C_QCC74X_DMA
#include <zephyr/drivers/dma.h>
#endif
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/pm/device.h>

#include <hardware/glb_reg.h>
#include <qc7xx_clock.h>
#include <qc7xx_i2c.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(i2c_qcc74x, CONFIG_I2C_LOG_LEVEL);

/* Register Offsets */
#include <hardware/i2c_reg.h>
#include "i2c-priv.h"

/* Configuration Constants */
#define I2C_WAIT_TIMEOUT_MS      K_MSEC(100)
#define I2C_MAX_PACKET_LENGTH    0xFFU
#define I2C_STANDARD_SPEED_FREQ  KHZ(100)
#define I2C_FAST_SPEED_FREQ      KHZ(400)
#define I2C_FAST_PLUS_SPEED_FREQ MHZ(1U)
#define I2C_HIGH_SPEED_FREQ      MHZ(3.4)
#define I2C_ULTRA_SPEED_FREQ     MHZ(5)

/* Phase calculation constants */
#define I2C_PHASE_MIN             1U
#define I2C_PHASE_MAX             256U
#define I2C_DUTY_CYCLE_50_PERCENT 2U
#define I2C_DUTY_CYCLE_33_PERCENT 3U
#define I2C_SPEED_THRESHOLD       KHZ(100)

#define WORD_SIZE 4U

/**
 * @brief Transfer state machine states.
 */
typedef enum {
	I2C_QCC74X_STATE_IDLE = 0,
	I2C_QCC74X_STATE_WRITE_DATA,
	I2C_QCC74X_STATE_WRITE_WAIT_END,
	I2C_QCC74X_STATE_READ_WAIT_DATA,
	I2C_QCC74X_STATE_ERROR,
	I2C_QCC74X_STATE_COMPLETE
} i2c_qcc74x_state_t;

/* Masks */
#define I2C_MODE_MASK           4U
#define I2C_TEN_BIT_ADDR_MASK   0x3FFU
#define I2C_SEVEN_BIT_ADDR_MASK 0x7FU

#ifdef CONFIG_I2C_QCC74X_DMA

#define I2C_MAX_DMA_TRANSFER_SIZE (64U)

__aligned(32)
__attribute__((section(".nocache"))) static uint32_t dma_rx_buffer[I2C_MAX_DMA_TRANSFER_SIZE];
__aligned(32)
__attribute__((section(".nocache"))) static uint32_t dma_tx_buffer[I2C_MAX_DMA_TRANSFER_SIZE];

/**
 * @brief Enumeration for RX/TX dma transfer directions.
 */
enum i2c_qcc74x_dma_direction {
	TX = 0,
	RX,
	NUM_OF_DIRECTION
};

/**
 * @brief DMA settings from the DTS.
 */
struct i2c_qcc74x_dma_config {
	const struct device *dev;
	uint32_t channel;
	uint32_t slot;
	uint32_t per_fifo_addr;
};

#endif /* CONFIG_I2C_QCC74X_DMA */

struct i2c_qcc74x_config {
	const struct pinctrl_dev_config *const pincfg;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	uint8_t clock_source;
	const uint32_t base;
	const uint32_t dts_freq;
	void (*irq_config_func)(const struct device *const dev);
	struct reset_dt_spec reset;
	uint32_t timeout;
#ifdef CONFIG_I2C_QCC74X_DMA
	const struct i2c_qcc74x_dma_config dma[NUM_OF_DIRECTION];
#endif
};

struct i2c_qcc74x_data {
	bool is_10_bits_address;
	struct ring_buf buffer;
	uint8_t buffer_data[I2C_MAX_PACKET_LENGTH];
	struct k_sem async_xfer_lock;
	struct k_sem xfer_done_sem;
	volatile i2c_qcc74x_state_t state;
	volatile int32_t xfer_result;
	uint32_t tx_remaining;
	uint32_t rx_total;
	uint32_t rx_received;
#ifdef CONFIG_I2C_QCC74X_DMA
	struct k_sem dma_done_sem; /**< DMA done semaphore. */
	struct k_sem async_dma_work_sem;
#endif
#ifdef CONFIG_I2C_CALLBACK
	uint16_t addr;
	uint32_t msg;
	struct i2c_msg *msgs;
	uint32_t num_msgs;
	i2c_callback_t cb;
	void *userdata;
#endif
};

/**
 * @brief Clamp a phase value to the valid hardware range.
 *
 * @param phase The phase value to clamp.
 *
 * @return The clamped phase value, guaranteed to be within the valid range
 * [I2C_PHASE_MIN, I2C_PHASE_MAX].
 */
static uint32_t i2c_qcc74x_clamp_phase(const uint32_t phase)
{
	return CLAMP(phase, I2C_PHASE_MIN, I2C_PHASE_MAX);
}

/**
 * @brief Clear the I2C RX FIFO.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_clear_rx_fifo(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_FIFO_CONFIG_0_OFFSET);
	reg_val |= I2C_RX_FIFO_CLR;
	sys_write32(reg_val, base_reg + I2C_FIFO_CONFIG_0_OFFSET);
}

/**
 * @brief Clear the I2C TX FIFO.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_clear_tx_fifo(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_FIFO_CONFIG_0_OFFSET);
	reg_val |= I2C_TX_FIFO_CLR;
	sys_write32(reg_val, base_reg + I2C_FIFO_CONFIG_0_OFFSET);
}

/**
 * @brief Enable the I2C master mode.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_enable_master(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_CONFIG_OFFSET);
	reg_val |= I2C_CR_I2C_M_EN;
	sys_write32(reg_val, base_reg + I2C_CONFIG_OFFSET);
}

/**
 * @brief Disable the I2C master mode.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_disable_master(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_CONFIG_OFFSET);
	reg_val &= ~I2C_CR_I2C_M_EN;
	sys_write32(reg_val, base_reg + I2C_CONFIG_OFFSET);
}

/**
 * @brief Disable master mode and clears both RX and TX FIFOs.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_cleanup_fifos(const uint32_t base_reg)
{
	i2c_qcc74x_disable_master(base_reg);
	i2c_qcc74x_clear_rx_fifo(base_reg);
	i2c_qcc74x_clear_tx_fifo(base_reg);
}

/**
 * @brief Mask all I2C transfer-related interrupts.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_mask_transfer_interrupts(const uint32_t base_reg)
{
	struct qc7xx_device_s i2c = {.reg_base = base_reg};

	qc7xx_i2c_int_mask(&i2c,
			   I2C_END_INT | I2C_TXF_INT | I2C_RXF_INT | I2C_NAK_INT | I2C_ARB_INT |
				   I2C_FER_INT,
			   true);
}

/**
 * @brief Clear pending END, NAK, and ARB interrupt flags.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_clear_int_flags(const uint32_t base_reg)
{
	struct qc7xx_device_s i2c = {.reg_base = base_reg};

	qc7xx_i2c_int_clear(&i2c, I2C_END_INT | I2C_NAK_INT | I2C_ARB_INT);
}

/**
 * @brief Drain one word from ring buffer into the TX FIFO.
 *
 * @param data Pointer to driver data.
 * @param base_reg Base register address.
 *
 * @return Number of bytes written (0 if ring buffer was empty).
 */
static uint32_t i2c_qcc74x_drain_ringbuf_to_fifo(struct i2c_qcc74x_data *data, uint32_t base_reg)
{
	uint8_t buf[WORD_SIZE] = {0};
	uint32_t n = ring_buf_get(&data->buffer, buf, WORD_SIZE);

	if (n > 0U) {
		sys_write32(sys_get_le32(buf), base_reg + I2C_FIFO_WDATA_OFFSET);
	}
	return n;
}

/**
 * @brief Prefill the TX FIFO from the ring buffer.
 *
 * @param data Pointer to driver data.
 * @param base_reg Base register address.
 * @param total_len Total transfer length.
 */
static void i2c_qcc74x_prefill_txfifo(struct i2c_qcc74x_data *data, uint32_t base_reg,
				    uint32_t total_len)
{
	/* FIFO size is 2 words */
	uint32_t initial_fill = MIN(total_len, WORD_SIZE * 2);

	while ((total_len - data->tx_remaining) < initial_fill) {
		uint32_t n = i2c_qcc74x_drain_ringbuf_to_fifo(data, base_reg);

		if (n == 0U) {
			break;
		}
		data->tx_remaining -= n;
	}
}

/**
 * @brief Signal an I2C transfer error from ISR context.
 *
 * Sets the error state, masks all transfer interrupts, and signals completion.
 *
 * @param dev Pointer to the I2C device structure.
 * @param err Error code to report.
 */
static void i2c_qcc74x_signal_error(const struct device *dev, int32_t err)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *const data = dev->data;

	data->state = I2C_QCC74X_STATE_ERROR;
	data->xfer_result = err;

	i2c_qcc74x_mask_transfer_interrupts(config->base);

	/* Reset hardware state: disable master and flush both FIFOs so the
	 * controller is ready for the next transfer (fixes TX_UNF leaving
	 * the bus stuck and subsequent transfers failing). */
	i2c_qcc74x_disable_master(config->base);
	i2c_qcc74x_clear_rx_fifo(config->base);
	i2c_qcc74x_clear_tx_fifo(config->base);

	k_sem_give(&data->xfer_done_sem);
}

/**
 * @brief Wait for the I2C bus to become idle.
 *
 * This function polls the bus busy status register until the bus is no longer
 * busy or timeout occurs.
 *
 * @param base_reg The base register address of the I2C controller.
 *
 * @retval 0 if the bus is ready.
 * @retval -ETIMEDOUT if the timeout is reached.
 */
static int32_t i2c_qcc74x_wait_bus_ready(const uint32_t base_reg)
{
	/* It may be possible that I2C busy register does not get updated fast enough into busy
	 * state. In that case bus read or write operation may get overwritten by the next read or
	 * write operation. This busy wait is a fix for it. */
	k_busy_wait(10);

	const k_timepoint_t timeout = sys_timepoint_calc(I2C_WAIT_TIMEOUT_MS);

	while ((sys_read32(base_reg + I2C_BUS_BUSY_OFFSET) & I2C_STS_I2C_BUS_BUSY) != 0U) {
		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("I2C bus busy timeout!");
			return -ETIMEDOUT;
		}
	}
	return 0;
}

/**
 * @brief Retrieve the peripheral clock rate for the I2C controller.
 *
 * @param dev Pointer to the I2C device structure.
 * @param[out] clk_rate Pointer to a variable where the clock rate in Hz will be stored.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the clock controller device is not found or not ready.
 * @retval -errno for other clock control errors.
 */
static int32_t i2c_qcc74x_get_clock_rate(const struct device *const dev, uint32_t *const clk_rate)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	const struct device *clock_dev = config->clock_dev;
	const clock_control_subsys_t clock_id = config->clock_id;

	if (!device_is_ready(clock_dev)) {
		LOG_ERR("Clock controller device not found or not ready");
		return -ENODEV;
	}

	return clock_control_get_rate(clock_dev, clock_id, clk_rate);
}

/**
 * @brief Calculate I2C SCL clock timing phases.
 *
 * This function determines the four phase durations required for I2C
 *
 * @param frequency The target I2C frequency in Hz.
 * @param clk_rate The peripheral source clock rate in Hz.
 * @param[out] phase_0 Pointer to store the calculated phase 0 value.
 * @param[out] phase_1 Pointer to store the calculated phase 1 value.
 * @param[out] phase_2 Pointer to store the calculated phase 2 value.
 * @param[out] phase_3 Pointer to store the calculated phase 3 value.
 */
static void i2c_qcc74x_calculate_phases(const uint32_t frequency, const uint32_t clk_rate,
				      uint32_t *const phase_0, uint32_t *const phase_1,
				      uint32_t *const phase_2, uint32_t *const phase_3)
{
	const uint32_t total_phase = (clk_rate + frequency / 2U) / frequency;

	if (frequency <= I2C_SPEED_THRESHOLD) {
		*phase_0 = (total_phase + I2C_DUTY_CYCLE_50_PERCENT) / 4U;
		*phase_1 = *phase_0;
		*phase_2 = total_phase / 2U - *phase_0;
		*phase_3 = total_phase - *phase_0 - *phase_1 - *phase_2;
	} else {
		*phase_0 = (total_phase + I2C_DUTY_CYCLE_50_PERCENT) / I2C_DUTY_CYCLE_33_PERCENT;
		*phase_1 = (total_phase + I2C_DUTY_CYCLE_33_PERCENT) / 6U;
		*phase_2 = (total_phase + 1U) / I2C_DUTY_CYCLE_33_PERCENT - *phase_1;
		*phase_3 = total_phase - *phase_0 - *phase_1 - *phase_2;
	}

	*phase_0 = i2c_qcc74x_clamp_phase(*phase_0);
	*phase_1 = i2c_qcc74x_clamp_phase(*phase_1);
	*phase_2 = i2c_qcc74x_clamp_phase(*phase_2);
	*phase_3 = i2c_qcc74x_clamp_phase(*phase_3);
}

/**
 * @brief Calculate a timing bias for phase adjustment.
 *
 * @param base_reg The base register address of the I2C controller.
 *
 * @return The calculated bias value.
 */
static uint32_t i2c_qcc74x_calculate_bias(const uint32_t base_reg)
{
	uint32_t bias = 0U;
	const uint32_t reg_val = sys_read32(base_reg + I2C_CONFIG_OFFSET);

	if ((reg_val & I2C_CR_I2C_DEG_EN) && (reg_val & I2C_CR_I2C_SCL_SYNC_EN)) {
		bias = ((reg_val & I2C_CR_I2C_DEG_CNT_MASK) >> I2C_CR_I2C_DEG_CNT_SHIFT);
		bias += 1U;
	}

	if (reg_val & I2C_CR_I2C_SCL_SYNC_EN) {
		bias += 3U;
	}

	return bias;
}

/**
 * @brief Configure the I2C timing registers for SCL.
 *
 * @param dev Pointer to the I2C device structure.
 * @param frequency The target I2C frequency in Hz.
 */
static void i2c_qcc74x_configure_timing(const struct device *const dev, const uint32_t frequency)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	const uint32_t base_reg = config->base;
	uint32_t phase_0 = 0U, phase_1 = 0U, phase_2 = 0U, phase_3 = 0U;
	uint32_t bias = 0U;
	uint32_t clk_rate = 0U;
	uint32_t reg_val = 0U;

	if (i2c_qcc74x_get_clock_rate(dev, &clk_rate) != 0U) {
		LOG_ERR("Failed to get clock rate");
		return;
	}

	i2c_qcc74x_calculate_phases(frequency, clk_rate, &phase_0, &phase_1, &phase_2, &phase_3);
	bias = i2c_qcc74x_calculate_bias(base_reg);

	/* Configure data phase timing */
	reg_val = (phase_0 - 1U) << I2C_CR_I2C_PRD_D_PH_0_SHIFT;
	reg_val |= (((phase_1 - bias - 1U) <= 0U) ? 1U : (phase_1 - bias - 1U))
		   << I2C_CR_I2C_PRD_D_PH_1_SHIFT;
	reg_val |= (phase_2 - 1U) << I2C_CR_I2C_PRD_D_PH_2_SHIFT;
	reg_val |= (phase_3 - 1U) << I2C_CR_I2C_PRD_D_PH_3_SHIFT;
	sys_write32(reg_val, base_reg + I2C_PRD_DATA_OFFSET);

	/* Configure start phase timing */
	reg_val = (phase_0 - 1U) << I2C_CR_I2C_PRD_S_PH_0_SHIFT;
	reg_val |= (((phase_0 + phase_3 - 1U) >= I2C_PHASE_MAX) ? (I2C_PHASE_MAX - 1U)
								: (phase_0 + phase_3 - 1U))
		   << I2C_CR_I2C_PRD_S_PH_1_SHIFT;
	reg_val |= (((phase_1 + phase_2 - 1U) >= I2C_PHASE_MAX) ? (I2C_PHASE_MAX - 1U)
								: (phase_1 + phase_2 - 1U))
		   << I2C_CR_I2C_PRD_S_PH_2_SHIFT;
	reg_val |= (phase_3 - 1U) << I2C_CR_I2C_PRD_S_PH_3_SHIFT;
	sys_write32(reg_val, base_reg + I2C_PRD_START_OFFSET);

	/* Configure stop phase timing */
	reg_val = (phase_0 - 1U) << I2C_CR_I2C_PRD_P_PH_0_SHIFT;
	reg_val |= (((phase_1 + phase_2 - 1U) >= I2C_PHASE_MAX) ? (I2C_PHASE_MAX - 1U)
								: (phase_1 + phase_2 - 1U))
		   << I2C_CR_I2C_PRD_P_PH_1_SHIFT;
	reg_val |= (phase_0 - 1U) << I2C_CR_I2C_PRD_P_PH_2_SHIFT;
	reg_val |= (phase_3 - 1U) << I2C_CR_I2C_PRD_P_PH_3_SHIFT;
	sys_write32(reg_val, base_reg + I2C_PRD_STOP_OFFSET);
}

/**
 * @brief Convert an I2C speed configuration macro to a frequency in Hz.
 *
 * @param speed_config The I2C speed configuration (e.g., I2C_SPEED_STANDARD).
 * @param dts_freq The frequency from the device tree, used for I2C_SPEED_DT.
 *
 * @return The corresponding frequency in Hz, or 0 if the speed is not supported.
 */
static uint32_t i2c_qcc74x_speed_to_frequency(const uint32_t speed_config, const uint32_t dts_freq)
{
	switch (speed_config) {
	case I2C_SPEED_STANDARD:
		return I2C_STANDARD_SPEED_FREQ;
	case I2C_SPEED_FAST:
		return I2C_FAST_SPEED_FREQ;
	case I2C_SPEED_FAST_PLUS:
		return I2C_FAST_PLUS_SPEED_FREQ;
	case I2C_SPEED_HIGH:
		return I2C_HIGH_SPEED_FREQ;
	case I2C_SPEED_ULTRA:
		return I2C_ULTRA_SPEED_FREQ;
	case I2C_SPEED_DT:
		return dts_freq;
	default:
		return 0U;
	}
}

/**
 * @brief Configure the I2C slave address and addressing mode (7/10-bit).
 *
 * @param dev Pointer to the I2C device structure.
 * @param address The slave address to set.
 */
static void i2c_qcc74x_set_slave_address(const struct device *const dev, const uint32_t address)
{
	struct i2c_qcc74x_data *data = dev->data;
	const struct i2c_qcc74x_config *config = dev->config;
	const uint32_t base_reg = config->base;
	uint32_t reg_val = sys_read32(base_reg + I2C_CONFIG_OFFSET);

	/* Disable sub addresses */
	reg_val &= ~I2C_CR_I2C_SUB_ADDR_EN;
	reg_val &= ~I2C_CR_I2C_SLV_ADDR_MASK;

	if (data->is_10_bits_address) {
		reg_val |= I2C_CR_I2C_10B_ADDR_EN;
		reg_val |= ((address & I2C_TEN_BIT_ADDR_MASK) << I2C_CR_I2C_SLV_ADDR_SHIFT);
	} else {
		reg_val &= ~I2C_CR_I2C_10B_ADDR_EN;
		reg_val |= ((address & I2C_SEVEN_BIT_ADDR_MASK) << I2C_CR_I2C_SLV_ADDR_SHIFT);
	}

	sys_write32(reg_val, base_reg + I2C_CONFIG_OFFSET);
}

/**
 * @brief Calculate the total number of bytes across multiple I2C messages.
 *
 * @param msgs Pointer to the array of I2C messages.
 * @param num_msgs The number of messages in the array.
 *
 * @return The sum of the lengths of all messages.
 */
static uint32_t i2c_qcc74x_calculate_total_length(const struct i2c_msg *const msgs,
						const uint8_t num_msgs)
{
	uint32_t total_len = 0U;

	for (uint8_t i = 0U; i < num_msgs; i++) {
		total_len += msgs[i].len;
	}

	return total_len;
}

/**
 * @brief Set the packet length in the I2C controller.
 *
 * @param base_reg The base register address of the I2C controller.
 * @param length The number of bytes in the packet.
 */
static void i2c_qcc74x_set_packet_length(const uint32_t base_reg, const uint32_t length)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_CONFIG_OFFSET);
	reg_val &= ~I2C_CR_I2C_PKT_LEN_MASK;
	reg_val |= (((length - 1U) << I2C_CR_I2C_PKT_LEN_SHIFT) & I2C_CR_I2C_PKT_LEN_MASK);
	sys_write32(reg_val, base_reg + I2C_CONFIG_OFFSET);
}

/**
 * @brief Set the I2C packet transfer direction (read or write).
 *
 * @param base_reg The base register address of the I2C controller.
 * @param is_read `true` for a read operation, `false` for a write operation.
 */
static void i2c_qcc74x_set_packet_direction(const uint32_t base_reg, const bool is_read)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_CONFIG_OFFSET);

	if (is_read) {
		reg_val |= I2C_CR_I2C_PKT_DIR;
	} else {
		reg_val &= ~I2C_CR_I2C_PKT_DIR;
	}

	sys_write32(reg_val, base_reg + I2C_CONFIG_OFFSET);
}

/**
 * @brief Shared setup for interrupt-based transfers.
 *
 * @param data Pointer to driver data.
 * @param base_reg Base register address.
 * @param is_read True for read transfers, false for write transfers.
 * @param total_len Total transfer length in bytes.
 * @param clear_int_flags Whether to clear interrupt flags before enabling.
 * @param state Initial transfer state.
 */
static void i2c_qcc74x_start_transfer(struct i2c_qcc74x_data *data, const uint32_t base_reg,
				    const bool is_read, const uint32_t total_len,
				    const bool clear_int_flags, const i2c_qcc74x_state_t state)
{
	i2c_qcc74x_disable_master(base_reg);
	if (is_read) {
		i2c_qcc74x_clear_rx_fifo(base_reg);
	} else {
		i2c_qcc74x_clear_tx_fifo(base_reg);
	}
	i2c_qcc74x_set_packet_length(base_reg, total_len);
	i2c_qcc74x_set_packet_direction(base_reg, is_read);
	if (clear_int_flags) {
		i2c_qcc74x_clear_int_flags(base_reg);
	}

	ring_buf_reset(&data->buffer);
	data->state = state;
	data->xfer_result = 0;
	if (is_read) {
		data->rx_total = total_len;
		data->rx_received = 0;
	} else {
		data->tx_remaining = total_len;
	}

	k_sem_reset(&data->xfer_done_sem);
}

/**
 * @brief Shared teardown for interrupt-based transfers.
 *
 * @param data Pointer to driver data.
 * @param base_reg Base register address.
 */
static void i2c_qcc74x_finish_transfer(struct i2c_qcc74x_data *data, const uint32_t base_reg)
{
	i2c_qcc74x_mask_transfer_interrupts(base_reg);
	i2c_qcc74x_disable_master(base_reg);
	data->state = I2C_QCC74X_STATE_IDLE;
}

/**
 * @brief Process a sequence of I2C read messages using interrupt-based transfer.
 *
 * @param dev Pointer to the I2C device structure.
 * @param msgs Pointer to the array of I2C messages to be read.
 * @param num_msgs The number of messages in the array.
 *
 * @retval 0 on success.
 * @retval -ETIMEDOUT if a timeout occurs while waiting for the bus or FIFO.
 * @retval -EINVAL if the total message length exceeds the hardware maximum.
 * @retval -EIO on NAK or FIFO error.
 * @retval -EAGAIN on arbitration lost.
 */
static int32_t i2c_qcc74x_process_read_messages(const struct device *const dev,
					      struct i2c_msg *const msgs, const uint8_t num_msgs)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	const uint32_t base_reg = config->base;
	struct qc7xx_device_s i2c = {.reg_base = base_reg};
	uint32_t total_len = 0U;
	int32_t ret = 0;

	ret = i2c_qcc74x_wait_bus_ready(base_reg);
	if (ret != 0U) {
		LOG_ERR("Failed on waiting for bus ready!");
		return ret;
	}

	total_len = i2c_qcc74x_calculate_total_length(msgs, num_msgs);
	if (total_len > I2C_MAX_PACKET_LENGTH) {
		LOG_ERR("Message length %u exceeds maximum %u", total_len, I2C_MAX_PACKET_LENGTH);
		return -EINVAL;
	}

	i2c_qcc74x_start_transfer(data, base_reg, true, total_len, true,
				I2C_QCC74X_STATE_READ_WAIT_DATA);

	/* Unmask RXF, END, and error interrupts */
	qc7xx_i2c_int_mask(
		&i2c, I2C_RXF_INT | I2C_END_INT | I2C_NAK_INT | I2C_ARB_INT | I2C_FER_INT, false);

	i2c_qcc74x_enable_master(base_reg);

	/* Wait for transfer completion via interrupt */
	ret = k_sem_take(&data->xfer_done_sem, I2C_WAIT_TIMEOUT_MS);

	i2c_qcc74x_finish_transfer(data, base_reg);

	if (ret != 0) {
		LOG_ERR("I2C read transfer timeout");
		i2c_qcc74x_cleanup_fifos(base_reg);
		return -ETIMEDOUT;
	}

	/* Check for errors from ISR */
	if (data->xfer_result != 0) {
		i2c_qcc74x_cleanup_fifos(base_reg);
		return data->xfer_result;
	}

	/* Copy received data from ring buffer to message buffers */
	for (uint8_t i = 0U; i < num_msgs; i++) {
		uint32_t copied = ring_buf_get(&data->buffer, msgs[i].buf, msgs[i].len);

		if (copied != msgs[i].len) {
			LOG_ERR("Failed to retrieve all read data: expected %u, got %u",
				msgs[i].len, copied);
			return -EIO;
		}
	}

	i2c_qcc74x_clear_rx_fifo(base_reg);

	return 0;
}

/**
 * @brief Process a sequence of I2C write messages using interrupt-based transfer.
 *
 * @param dev Pointer to the I2C device structure.
 * @param msgs Pointer to the array of I2C messages to be written.
 * @param num_msgs The number of messages in the array.
 *
 * @retval 0 on success.
 * @retval -ETIMEDOUT if a timeout occurs.
 * @retval -EINVAL if the total message length exceeds the hardware maximum.
 * @retval -EIO on NAK or FIFO error.
 * @retval -EAGAIN on arbitration lost.
 */
static int32_t i2c_qcc74x_process_write_messages(const struct device *const dev,
					       const struct i2c_msg *const msgs,
					       const uint8_t num_msgs)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	const uint32_t base_reg = config->base;
	struct qc7xx_device_s i2c = {.reg_base = base_reg};
	uint32_t total_len = 0U;
	int32_t ret = 0;

	ret = i2c_qcc74x_wait_bus_ready(base_reg);
	if (ret != 0U) {
		LOG_ERR("Failed on waiting for bus ready!");
		return ret;
	}

	total_len = i2c_qcc74x_calculate_total_length(msgs, num_msgs);
	if (total_len > I2C_MAX_PACKET_LENGTH) {
		LOG_ERR("Message length %u exceeds maximum %u", total_len, I2C_MAX_PACKET_LENGTH);
		return -EINVAL;
	}

	i2c_qcc74x_start_transfer(data, base_reg, false, total_len, true, I2C_QCC74X_STATE_WRITE_DATA);

	/* Copy all message data to the ring buffer */
	for (uint8_t i = 0U; i < num_msgs; i++) {
		uint32_t written = ring_buf_put(&data->buffer, msgs[i].buf, msgs[i].len);

		if (written != msgs[i].len) {
			LOG_ERR("Ring buffer full: wrote %u of %u bytes", written, msgs[i].len);
			return -ENOMEM;
		}
	}

	i2c_qcc74x_prefill_txfifo(data, base_reg, total_len);

	/* Unmask END and error interrupts; unmask TXF only if there's more data */
	uint32_t unmask_ints = I2C_END_INT | I2C_NAK_INT | I2C_ARB_INT | I2C_FER_INT;
	if (!ring_buf_is_empty(&data->buffer)) {
		unmask_ints |= I2C_TXF_INT;
	} else {
		data->state = I2C_QCC74X_STATE_WRITE_WAIT_END;
	}
	qc7xx_i2c_int_mask(&i2c, unmask_ints, false);

	i2c_qcc74x_enable_master(base_reg);

	/* Wait for transfer completion via interrupt */
	ret = k_sem_take(&data->xfer_done_sem, I2C_WAIT_TIMEOUT_MS);
	i2c_qcc74x_finish_transfer(data, base_reg);

	if (ret != 0) {
		LOG_ERR("I2C write transfer timeout");
		i2c_qcc74x_cleanup_fifos(base_reg);
		return -ETIMEDOUT;
	}

	/* Check for errors from ISR */
	if (data->xfer_result != 0) {
		i2c_qcc74x_cleanup_fifos(base_reg);
		return data->xfer_result;
	}

	return 0;
}

#ifndef CONFIG_I2C_QCC74X_DMA
/**
 * @brief Count consecutive I2C messages of the same type (read/write).
 *
 * @param msgs Pointer to the array of I2C messages.
 * @param start_index The index in the array to start counting from.
 * @param num_msgs The total number of messages in the array.
 * @param target_flags The message flags of the first message in the sequence.
 *
 * @return The number of consecutive messages of the same type.
 */
static uint8_t i2c_qcc74x_count_consecutive_messages(const struct i2c_msg *const msgs,
						   const uint8_t start_index,
						   const uint8_t num_msgs,
						   const uint32_t target_flags)
{
	uint8_t count = 0U;

	for (uint8_t i = start_index; i < num_msgs; i++) {
		if ((msgs[i].flags & I2C_MSG_RW_MASK) == (target_flags & I2C_MSG_RW_MASK) &&
		    (msgs[i].flags & I2C_MSG_STOP) == 0U) {
			count++;
		} else {
			break;
		}
	}

	if (start_index + count < num_msgs) {
		if ((msgs[start_index + count].flags & I2C_MSG_RW_MASK) !=
			    (target_flags & I2C_MSG_RW_MASK) &&
		    count > 0U) {
			count--;
		}
	}

	return count;
}
#endif /* !CONFIG_I2C_QCC74X_DMA */

#ifdef CONFIG_I2C_QCC74X_DMA
/**
 * @brief Enable DMA channel and start DMA transfer.
 *
 * @param[in] dev Pointer to device structure.
 * @param[out] buf DMA transfer source/destination buffer.
 * @param[in] len Buffer length in bytes.
 * @param[in] dir Direction of DMA transfer(TX/RX).
 * @retval 0 if successful.
 */
static int i2c_qcc74x_dma_start(const struct device *dev, uint32_t *buf, uint32_t len,
			      const uint32_t dir)
{
	const struct i2c_qcc74x_config *conf = dev->config;
	struct dma_block_config dma_blk = {0};
	struct dma_config dma_cfg = {0};
	const struct i2c_qcc74x_dma_config *dma = &conf->dma[dir];
	int ret = 0;

	dma_blk.source_address = dir == TX ? (uint32_t)buf : dma->per_fifo_addr;
	dma_blk.dest_address = dir == TX ? dma->per_fifo_addr : (uint32_t)buf;
	dma_blk.block_size = len;

	dma_cfg.dma_slot = dma->slot;
	dma_cfg.channel_direction = dir == TX ? MEMORY_TO_PERIPHERAL : PERIPHERAL_TO_MEMORY;
	dma_cfg.source_data_size = 4;
	dma_cfg.dest_data_size = 4;
	dma_cfg.source_burst_length = 1;
	dma_cfg.dest_burst_length = 1;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;

	ret = dma_config(dma->dev, dma->channel, &dma_cfg);
	if (ret) {
		LOG_ERR("Error configuring DMA: %d", ret);
		return ret;
	}

	ret = dma_start(dma->dev, dma->channel);
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
 * @param[in] dir Direction of DMA transfer(TX/RX).
 * @retval 0 if successful.
 */
static int i2c_qcc74x_dma_stop(const struct device *dev, const uint32_t dir)
{
	const struct i2c_qcc74x_config *conf = dev->config;
	const struct i2c_qcc74x_dma_config *dma = &conf->dma[dir];
	int ret = 0;

	ret = dma_stop(dma->dev, dma->channel);
	if (ret) {
		LOG_ERR("Error stopping DMA: %d", ret);
		return ret;
	}
	return 0;
}

/**
 * @brief Prepare I2C controller and DMA for a transfer.
 *
 * @param data Pointer to driver data.
 * @param base_reg Base register address.
 * @param i2c Pointer to QC7XX I2C device wrapper.
 * @param is_read True for RX transfer, false for TX transfer.
 * @param len Transfer length in bytes.
 */
static void i2c_qcc74x_dma_prepare(struct i2c_qcc74x_data *data, const uint32_t base_reg,
				 struct qc7xx_device_s *i2c, const bool is_read, const uint32_t len)
{
	i2c_qcc74x_disable_master(base_reg);
	if (is_read) {
		i2c_qcc74x_clear_rx_fifo(base_reg);
	} else {
		i2c_qcc74x_clear_tx_fifo(base_reg);
	}
	i2c_qcc74x_set_packet_length(base_reg, len);
	i2c_qcc74x_set_packet_direction(base_reg, is_read);

	k_sem_reset(&data->xfer_done_sem);

	qc7xx_i2c_int_clear(i2c, I2C_END_INT);
	qc7xx_i2c_int_mask(i2c, I2C_END_INT, false);
	if (is_read) {
		qc7xx_i2c_link_rxdma(i2c, true);
	} else {
		qc7xx_i2c_link_txdma(i2c, true);
	}
}

/**
 * @brief Complete DMA transfer and cleanup controller state.
 *
 * @param base_reg Base register address.
 * @param i2c Pointer to QC7XX I2C device wrapper.
 * @param is_read True for RX transfer, false for TX transfer.
 */
static void i2c_qcc74x_dma_complete(const uint32_t base_reg, struct qc7xx_device_s *i2c,
				  const bool is_read)
{
	qc7xx_i2c_int_mask(i2c, I2C_END_INT, true);
	if (is_read) {
		qc7xx_i2c_link_rxdma(i2c, false);
	} else {
		qc7xx_i2c_link_txdma(i2c, false);
	}
	i2c_qcc74x_disable_master(base_reg);
	if (is_read) {
		i2c_qcc74x_clear_rx_fifo(base_reg);
	} else {
		i2c_qcc74x_clear_tx_fifo(base_reg);
	}
}

/**
 * @brief Process a I2C write message using DMA.
 *
 * @param dev Pointer to the I2C device structure.
 * @param msgs Pointer I2C message to be written.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the message length exceeds the hardware maximum.
 */
static int32_t i2c_qcc74x_process_write_messages_dma(const struct device *const dev,
						   const struct i2c_msg *const msgs)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	struct qc7xx_device_s i2c = {.reg_base = config->base};
	const uint32_t base_reg = config->base;
	uint32_t len = 0U;
	uint32_t dma_len = 0U;
	int32_t ret = 0;

	ret = i2c_qcc74x_wait_bus_ready(base_reg);
	if (ret != 0U) {
		LOG_ERR("Failed on waiting for bus ready!");
		return ret;
	}

	len = msgs[0].len;
	if (len > I2C_MAX_PACKET_LENGTH) {
		LOG_ERR("Message length %u exceeds maximum %u", len, I2C_MAX_PACKET_LENGTH);
		return -EINVAL;
	}

	i2c_qcc74x_dma_prepare(data, base_reg, &i2c, false, len);
	dma_len = ROUND_UP(len, 4);
	memset(dma_tx_buffer, 0, dma_len);
	memcpy(dma_tx_buffer, msgs[0].buf, len);

	ret = i2c_qcc74x_dma_start(dev, dma_tx_buffer, dma_len, TX);
	if (ret) {
		return ret;
	}

	if (config->timeout > 0) {
		k_msleep(config->timeout);
	}

	i2c_qcc74x_enable_master(base_reg);

	ret = k_sem_take(&data->dma_done_sem, I2C_WAIT_TIMEOUT_MS);
	if (ret != 0) {
		LOG_ERR("I2C DMA write transfer timeout");
		(void)i2c_qcc74x_dma_stop(dev, TX);
		i2c_qcc74x_dma_complete(base_reg, &i2c, false);
		return -ETIMEDOUT;
	}

	ret = i2c_qcc74x_dma_stop(dev, TX);
	if (ret) {
		return ret;
	}

	i2c_qcc74x_dma_complete(base_reg, &i2c, false);

	return 0;
}

/**
 * @brief Process a I2C read message using DMA.
 *
 * @param dev Pointer to the I2C device structure.
 * @param msgs Pointer to the I2C message to be read.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the message length exceeds the hardware maximum.
 */
static int32_t i2c_qcc74x_process_read_messages_dma(const struct device *const dev,
						  struct i2c_msg *const msgs)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	struct qc7xx_device_s i2c = {.reg_base = config->base};
	const uint32_t base_reg = config->base;
	uint32_t len = 0U;
	int32_t ret = 0;

	ret = i2c_qcc74x_wait_bus_ready(base_reg);
	if (ret != 0U) {
		LOG_ERR("Failed on waiting for bus ready!");
		return ret;
	}

	len = msgs[0].len;
	if (len > I2C_MAX_PACKET_LENGTH) {
		LOG_ERR("Message length %u exceeds maximum %u", len, I2C_MAX_PACKET_LENGTH);
		return -EINVAL;
	}

	i2c_qcc74x_dma_prepare(data, base_reg, &i2c, true, len);
	ret = i2c_qcc74x_dma_start(dev, dma_rx_buffer, ROUND_UP(len, 4), RX);
	if (ret) {
		return ret;
	}

	i2c_qcc74x_enable_master(base_reg);

	ret = k_sem_take(&data->dma_done_sem, I2C_WAIT_TIMEOUT_MS);
	if (ret != 0) {
		LOG_ERR("I2C DMA read transfer timeout");
		(void)i2c_qcc74x_dma_stop(dev, RX);
		i2c_qcc74x_dma_complete(base_reg, &i2c, true);
		return -ETIMEDOUT;
	}

	ret = i2c_qcc74x_dma_stop(dev, RX);
	if (ret) {
		return ret;
	}

	i2c_qcc74x_dma_complete(base_reg, &i2c, true);

	memcpy(msgs[0].buf, dma_rx_buffer, len);

	return 0;
}
#endif /* CONFIG_I2C_QCC74X_DMA */

#ifdef CONFIG_I2C_CALLBACK
/**
 * @brief Complete an asynchronous I2C transfer.
 *
 * Cleans up transfer state and invokes the user callback with the result.
 *
 * @param dev Pointer to the I2C device structure.
 * @param data Pointer to the driver data structure.
 * @param result The result code to pass to the callback.
 */
static void i2c_qcc74x_async_done(const struct device *dev, struct i2c_qcc74x_data *data, int result)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	const uint32_t base_reg = config->base;
	i2c_callback_t cb = data->cb;
	void *userdata = data->userdata;

	i2c_qcc74x_cleanup_fifos(base_reg);
	data->msg = 0;
	data->msgs = NULL;
	data->num_msgs = 0;
	data->cb = NULL;
	data->userdata = NULL;
	data->addr = 0;

	k_sem_give(&data->async_xfer_lock);
#ifdef CONFIG_I2C_QCC74X_DMA
	/* Wake up the transfer_cb wait loop so it can exit */
	k_sem_give(&data->async_dma_work_sem);
#endif

	/* Callback may wish to start another transfer */
	if (cb != NULL) {
		cb(dev, result, userdata);
	}
}

/**
 * @brief Process a single I2C message asynchronously (write operation).
 *
 * @param dev Pointer to the I2C device structure.
 * @param msg Pointer to the I2C message to process.
 *
 * @retval 0 on success (transfer started).
 * @retval negative error code on failure.
 */
static int32_t i2c_qcc74x_async_write_message(const struct device *const dev,
					    const struct i2c_msg *const msg)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	const uint32_t base_reg = config->base;
	struct qc7xx_device_s i2c = {.reg_base = base_reg};

	i2c_qcc74x_start_transfer(data, base_reg, false, msg->len, true, I2C_QCC74X_STATE_WRITE_DATA);

	uint32_t written = ring_buf_put(&data->buffer, msg->buf, msg->len);

	if (written != msg->len) {
		LOG_ERR("Ring buffer full: wrote %u of %u bytes", written, msg->len);
		return -ENOMEM;
	}
	i2c_qcc74x_prefill_txfifo(data, base_reg, msg->len);

	/* Unmask END and error interrupts; unmask TXF only if there's more data */
	uint32_t unmask_ints = I2C_END_INT | I2C_NAK_INT | I2C_ARB_INT | I2C_FER_INT;
	if (!ring_buf_is_empty(&data->buffer)) {
		unmask_ints |= I2C_TXF_INT;
	} else {
		data->state = I2C_QCC74X_STATE_WRITE_WAIT_END;
	}
	qc7xx_i2c_int_mask(&i2c, unmask_ints, false);

	if (config->timeout > 0) {
		k_msleep(config->timeout);
	}

	i2c_qcc74x_enable_master(base_reg);

	return 0;
}

/**
 * @brief Process a single I2C message asynchronously (read operation).
 *
 * @param dev Pointer to the I2C device structure.
 * @param msg Pointer to the I2C message to process.
 *
 * @retval 0 on success (transfer started).
 * @retval negative error code on failure.
 */
static int32_t i2c_qcc74x_async_read_message(const struct device *const dev,
					   const struct i2c_msg *const msg)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	const uint32_t base_reg = config->base;
	struct qc7xx_device_s i2c = {.reg_base = base_reg};

	i2c_qcc74x_start_transfer(data, base_reg, true, msg->len, true,
				I2C_QCC74X_STATE_READ_WAIT_DATA);

	/* Unmask RXF, END, and error interrupts */
	qc7xx_i2c_int_mask(
		&i2c, I2C_RXF_INT | I2C_END_INT | I2C_NAK_INT | I2C_ARB_INT | I2C_FER_INT, false);

	i2c_qcc74x_enable_master(base_reg);

	return 0;
}

/**
 * @brief Start or continue an asynchronous I2C transfer.
 *
 * This function processes the current message in the async transfer sequence.
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_async_iter(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	const uint32_t base_reg = config->base;
	struct i2c_msg *msg;
	int ret;

	if (data->msgs == NULL || data->msg >= data->num_msgs) {
		i2c_qcc74x_async_done(dev, data, -EINVAL);
		return;
	}

	msg = &data->msgs[data->msg];

	if (msg->len > I2C_MAX_PACKET_LENGTH) {
		LOG_ERR("Message length %u exceeds maximum %u", msg->len, I2C_MAX_PACKET_LENGTH);
		i2c_qcc74x_async_done(dev, data, -EINVAL);
		return;
	}

	/* Bus is busy - for first message this is an error */
	if (data->msg == 0 &&
	    (sys_read32(base_reg + I2C_BUS_BUSY_OFFSET) & I2C_STS_I2C_BUS_BUSY) != 0U) {
		LOG_ERR("I2C bus busy");
		i2c_qcc74x_async_done(dev, data, -EBUSY);
		return;
	}

	if (data->msg == 0) {
		i2c_qcc74x_set_slave_address(dev, data->addr);
	}

	/* Start the appropriate transfer type */
#ifdef CONFIG_I2C_QCC74X_DMA
	if (msg->len > WORD_SIZE) {
		/* DMA functions block on a semaphore, so they cannot run in ISR context. Signal the
		 * transfer_cb caller thread to handle the DMA transfer (e.g. when the previous
		 * interrupt-based message completed and the ISR chains into the next message via
		 * async_handle_complete). */
		if (k_is_in_isr()) {
			k_sem_give(&data->async_dma_work_sem);
			return;
		}
		if ((msg->flags & I2C_MSG_RW_MASK) == I2C_MSG_READ) {
			ret = i2c_qcc74x_process_read_messages_dma(dev, msg);
		} else {
			ret = i2c_qcc74x_process_write_messages_dma(dev, msg);
		}
		if (ret != 0) {
			i2c_qcc74x_async_done(dev, data, ret);
			return;
		}
		/* DMA transfer completed synchronously, advance to next message */
		if (data->msg < data->num_msgs - 1) {
			data->msg++;
			i2c_qcc74x_async_iter(dev);
		} else {
			i2c_qcc74x_async_done(dev, data, 0);
		}
		return;
	}
#endif
	if ((msg->flags & I2C_MSG_RW_MASK) == I2C_MSG_READ) {
		ret = i2c_qcc74x_async_read_message(dev, msg);
	} else {
		ret = i2c_qcc74x_async_write_message(dev, msg);
	}

	if (ret != 0) {
		i2c_qcc74x_async_done(dev, data, ret);
	}
	/* Transfer started - ISR will handle completion */
}

/**
 * @brief Handle async transfer completion in ISR context.
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_async_handle_complete(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	struct i2c_msg *msg;

	if (data->cb == NULL || data->msgs == NULL) {
		return;
	}

	msg = &data->msgs[data->msg];

	/* If this was a read operation, copy data from ring buffer to message */
	if ((msg->flags & I2C_MSG_RW_MASK) == I2C_MSG_READ) {
		uint32_t copied = ring_buf_get(&data->buffer, msg->buf, msg->len);

		if (copied != msg->len) {
			LOG_ERR("Async read: expected %u bytes, got %u", msg->len, copied);
			i2c_qcc74x_async_done(dev, data, -EIO);
			return;
		}
		i2c_qcc74x_clear_rx_fifo(config->base);
	}

	/* Check if there are more messages */
	if (data->msg < data->num_msgs - 1) {
		data->msg++;
		i2c_qcc74x_async_iter(dev);
	} else {
		i2c_qcc74x_async_done(dev, data, 0);
	}
}
#endif /* CONFIG_I2C_CALLBACK */

/**
 * @brief Handle the TXF interrupt (TX FIFO ready).
 *
 * Refills the TX FIFO from the ring buffer during interrupt-based writes.
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_handle_txf_interrupt(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *const data = dev->data;
	const uint32_t base_reg = config->base;

	if (data->state != I2C_QCC74X_STATE_WRITE_DATA) {
		return;
	}

	uint32_t drained = i2c_qcc74x_drain_ringbuf_to_fifo(data, base_reg);

	if (drained > data->tx_remaining) {
		LOG_ERR("I2C TX drain overrun: drained=%u remaining=%u", drained,
			data->tx_remaining);
		data->tx_remaining = 0U;
	} else {
		data->tx_remaining -= drained;
	}

	if (ring_buf_is_empty(&data->buffer)) {
		struct qc7xx_device_s i2c = {.reg_base = base_reg};

		qc7xx_i2c_int_mask(&i2c, I2C_TXF_INT, true);
		data->state = I2C_QCC74X_STATE_WRITE_WAIT_END;
	}
}

/**
 * @brief Handle the RXF interrupt (RX FIFO ready).
 *
 * Drains the RX FIFO to the ring buffer during interrupt-based reads.
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_handle_rxf_interrupt(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *const data = dev->data;
	const uint32_t base_reg = config->base;
	struct qc7xx_device_s i2c = {.reg_base = base_reg};

	if (data->state != I2C_QCC74X_STATE_READ_WAIT_DATA) {
		return;
	}

	while (data->rx_received < data->rx_total) {
		uint32_t remaining = data->rx_total - data->rx_received;
		uint32_t bytes_to_read = MIN(remaining, WORD_SIZE);
		uint8_t fifo_buffer[WORD_SIZE];

		if ((qc7xx_i2c_get_intstatus(&i2c) & I2C_RXF_INT) == 0U) {
			break;
		}

		sys_put_le32(sys_read32(base_reg + I2C_FIFO_RDATA_OFFSET), fifo_buffer);
		ring_buf_put(&data->buffer, fifo_buffer, bytes_to_read);
		data->rx_received += bytes_to_read;
	}
}

/**
 * @brief Handle the NAK interrupt (No Acknowledge).
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_handle_nak_interrupt(const struct device *dev)
{
	i2c_qcc74x_signal_error(dev, -EIO);
}

/**
 * @brief Handle the ARB interrupt (Arbitration Lost).
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_handle_arb_interrupt(const struct device *dev)
{
	i2c_qcc74x_signal_error(dev, -EAGAIN);
}

/**
 * @brief Handle the FER interrupt (FIFO Error).
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_handle_fer_interrupt(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *const data = dev->data;
	const uint32_t base_reg = config->base;
	uint32_t fifo_status = sys_read32(base_reg + I2C_FIFO_CONFIG_0_OFFSET);

	/*
	 * TX_UNF with state=WRITE_WAIT_END and tx_remaining=0 is NOT an error:
	 * it means the TX FIFO naturally drained at end of transfer.  Treat it
	 * as normal completion so the END interrupt can finish the transfer.
	 * Only flag a real error for unexpected underflows (data still pending)
	 * or any overflow condition.
	 */
	bool tx_unf = (fifo_status & I2C_TX_FIFO_UNDERFLOW) != 0U;
	bool is_normal_drain = tx_unf &&
			       (data->state == I2C_QCC74X_STATE_WRITE_WAIT_END) &&
			       (data->tx_remaining == 0U);

	if (is_normal_drain) {
		/* Spurious TX_UNF at end of write — clear the sticky flag (W1C)
		 * and also flush the TX FIFO so the controller is clean for the
		 * next transfer.  Do NOT call signal_error(); let the END
		 * interrupt fire normally to complete the transfer. */
		uint32_t clr = sys_read32(base_reg + I2C_FIFO_CONFIG_0_OFFSET);

		clr |= I2C_TX_FIFO_UNDERFLOW;   /* W1C: write 1 to clear */
		clr |= I2C_TX_FIFO_CLR;         /* flush FIFO */
		sys_write32(clr, base_reg + I2C_FIFO_CONFIG_0_OFFSET);
		return;
	}

	LOG_ERR("I2C FIFO error, status: 0x%02x [%s%s%s%s], state: %d, tx_remaining: %u",
		fifo_status, (fifo_status & I2C_TX_FIFO_OVERFLOW) ? "TX_OVF " : "",
		tx_unf ? "TX_UNF " : "",
		(fifo_status & I2C_RX_FIFO_OVERFLOW) ? "RX_OVF " : "",
		(fifo_status & I2C_RX_FIFO_UNDERFLOW) ? "RX_UNF " : "", data->state,
		data->tx_remaining);

	i2c_qcc74x_signal_error(dev, -EIO);
}

/**
 * @brief Handle the END interrupt (transfer complete).
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_handle_end_interrupt(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct i2c_qcc74x_data *const data = dev->data;

#ifdef CONFIG_I2C_QCC74X_DMA
	if (data->state == I2C_QCC74X_STATE_IDLE || data->state == I2C_QCC74X_STATE_COMPLETE) {
		k_sem_give(&data->dma_done_sem);
		return;
	}
#endif

	if (data->state == I2C_QCC74X_STATE_READ_WAIT_DATA) {
		i2c_qcc74x_handle_rxf_interrupt(dev);
	}

	i2c_qcc74x_mask_transfer_interrupts(config->base);
	data->state = I2C_QCC74X_STATE_COMPLETE;
	k_sem_give(&data->xfer_done_sem);

#ifdef CONFIG_I2C_CALLBACK
	if (data->cb != NULL) {
		i2c_qcc74x_async_handle_complete(dev);
		return;
	}
#endif /* CONFIG_I2C_CALLBACK */
}

/**
 * @brief Interrupt Service Routine (ISR) for the I2C controller.
 *
 * @param dev Pointer to the I2C device structure.
 */
static void i2c_qcc74x_isr(const struct device *const dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	struct qc7xx_device_s i2c = {.reg_base = config->base};
	uint32_t int_status = qc7xx_i2c_get_intstatus(&i2c);

	/* Check for error conditions */
	if ((int_status & I2C_NAK_INT) != 0U) {
		i2c_qcc74x_handle_nak_interrupt(dev);
		qc7xx_i2c_int_clear(&i2c, I2C_NAK_INT);
	}

	if ((int_status & I2C_ARB_INT) != 0U) {
		i2c_qcc74x_handle_arb_interrupt(dev);
		qc7xx_i2c_int_clear(&i2c, I2C_ARB_INT);
	}

	if ((int_status & I2C_FER_INT) != 0U) {
		i2c_qcc74x_handle_fer_interrupt(dev);
	}

	if ((int_status & I2C_TXF_INT) != 0U) {
		i2c_qcc74x_handle_txf_interrupt(dev);
	}

	if ((int_status & I2C_RXF_INT) != 0U) {
		i2c_qcc74x_handle_rxf_interrupt(dev);
	}

	if ((int_status & I2C_END_INT) != 0U) {
		i2c_qcc74x_handle_end_interrupt(dev);
		qc7xx_i2c_int_clear(&i2c, I2C_END_INT);
	}
}

/**
 * @brief Perform initial interrupts configuration.
 *
 * @param base_reg The base register address of the I2C controller.
 */
static void i2c_qcc74x_init_configure_interrupts(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + I2C_INT_STS_OFFSET);
	reg_val |= (I2C_CR_I2C_END_EN | I2C_CR_I2C_TXF_EN | I2C_CR_I2C_RXF_EN | I2C_CR_I2C_NAK_EN |
		    I2C_CR_I2C_ARB_EN | I2C_CR_I2C_FER_EN);
	reg_val |= (I2C_CR_I2C_NAK_MASK | I2C_CR_I2C_ARB_MASK | I2C_CR_I2C_FER_MASK |
		    I2C_CR_I2C_TXF_MASK | I2C_CR_I2C_RXF_MASK | I2C_CR_I2C_END_MASK);
	sys_write32(reg_val, base_reg + I2C_INT_STS_OFFSET);
}

/**
 * @brief Perform initial clock configuration.
 */
void i2c_qcc74x_init_configure_clock()
{
	uint32_t reg_val = sys_read32(GLB_BASE + GLB_I2C_CFG0_OFFSET);
	reg_val = reg_val & GLB_I2C_CLK_DIV_UMSK;
	reg_val |= BIT(GLB_I2C_CLK_DIV_POS);
	sys_write32(reg_val, GLB_BASE + GLB_I2C_CFG0_OFFSET);
}

static int32_t i2c_qcc74x_configure(const struct device *const dev, const uint32_t dev_config)
{
	const struct i2c_qcc74x_config *config = dev->config;
	struct i2c_qcc74x_data *data = dev->data;
	const uint32_t base_reg = config->base;
	uint32_t speed_freq = 0U;

	speed_freq = i2c_qcc74x_speed_to_frequency(I2C_SPEED_GET(dev_config), config->dts_freq);
	if (speed_freq == 0U) {
		LOG_ERR("Unsupported I2C speed requested");
		return -ENOTSUP;
	}

	if (((dev_config & I2C_MODE_CONTROLLER) >> I2C_MODE_MASK) == 0U) {
		LOG_ERR("Target mode not supported, only controller mode");
		return -EIO;
	}

	LOG_DBG("Cleanup I2C FIFOs");
	i2c_qcc74x_cleanup_fifos(base_reg);

	LOG_DBG("Perform initial clock and interrupts configuration");
	i2c_qcc74x_init_configure_clock();
	i2c_qcc74x_init_configure_interrupts(base_reg);

	LOG_DBG("Perform I2C timing registers configuration");
	i2c_qcc74x_configure_timing(dev, speed_freq);

	data->is_10_bits_address = ((dev_config & I2C_ADDR_10_BITS) > 0U);
	if (data->is_10_bits_address) {
		LOG_DBG("10 bit addressing for I2C will be used!");
	}

	return 0;
}

static int i2c_qcc74x_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
			     uint16_t addr)
{
	uint8_t current_msg = 0U;
	int32_t ret = 0;

	__ASSERT((msgs != NULL), "Messages array is NULL");

	i2c_qcc74x_set_slave_address(dev, addr);

	LOG_DBG("Process all I2C messages, num_msgs: %d", num_msgs);
	while (current_msg < num_msgs) {
		uint8_t consecutive_msgs = 0U;
		bool is_read_operation =
			((msgs[current_msg].flags & I2C_MSG_RW_MASK) == I2C_MSG_READ);
#ifndef CONFIG_I2C_QCC74X_DMA
		/* Combine messages only if DMA not enabled */
		consecutive_msgs = i2c_qcc74x_count_consecutive_messages(msgs, current_msg, num_msgs,
								       msgs[current_msg].flags);
#endif
#ifdef CONFIG_I2C_QCC74X_DMA
		/* Only use DMA if message length is bigger than one word */
		if (msgs[current_msg].len > WORD_SIZE) {
			ret = is_read_operation
				      ? i2c_qcc74x_process_read_messages_dma(dev, &msgs[current_msg])
				      : i2c_qcc74x_process_write_messages_dma(dev,
									    &msgs[current_msg]);
		} else
#endif
		{
			ret = is_read_operation
				      ? i2c_qcc74x_process_read_messages(dev, &msgs[current_msg],
								       consecutive_msgs + 1U)
				      : i2c_qcc74x_process_write_messages(dev, &msgs[current_msg],
									consecutive_msgs + 1U);
		}

		if (ret != 0U) {
			return ret;
		}

		current_msg += consecutive_msgs + 1;
	}

	LOG_DBG("Processing I2C messages completed");
	return 0;
}

#ifdef CONFIG_I2C_CALLBACK
static int i2c_qcc74x_transfer_cb(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
				uint16_t addr, i2c_callback_t cb, void *userdata)
{
	struct i2c_qcc74x_data *data = dev->data;
	int res;

	if (msgs == NULL || num_msgs == 0 || cb == NULL) {
		return -EINVAL;
	}

	res = k_sem_take(&data->async_xfer_lock, K_NO_WAIT);
	if (res != 0) {
		return -EWOULDBLOCK;
	}

	data->msg = 0;
	data->msgs = msgs;
	data->num_msgs = num_msgs;
	data->addr = addr;
	data->cb = cb;
	data->userdata = userdata;

#ifdef CONFIG_I2C_QCC74X_DMA
	k_sem_reset(&data->async_dma_work_sem);
#endif

	/* Start the first message */
	i2c_qcc74x_async_iter(dev);

#ifdef CONFIG_I2C_QCC74X_DMA
	/* When a DMA transfer is needed from ISR context, the ISR defers it by giving
	 * async_dma_work_sem. Block here until all messages complete or a deferred DMA transfer
	 * needs to run in thread context. */
	while (data->cb != NULL) {
		k_sem_take(&data->async_dma_work_sem, K_FOREVER);
		if (data->cb != NULL) {
			i2c_qcc74x_async_iter(dev);
		}
	}
#endif

	return 0;
}
#endif /* CONFIG_I2C_CALLBACK */

/**
 * @brief Initialize the Opal I2C driver instance.
 *
 * @param dev Pointer to the I2C device structure.
 *
 * @retval 0 on success.
 * @retval -errno on failure.
 */
static int i2c_qcc74x_init(const struct device *dev)
{
	const struct i2c_qcc74x_config *const config = dev->config;
	const uint32_t base_reg = config->base;
	struct i2c_qcc74x_data *data = dev->data;
	uint32_t dev_config = 0U;
	int32_t ret = 0;
	int ret_pin_state = 0;

	LOG_DBG("Initialize I2C for Opal");

	ret = reset_line_toggle_dt(&config->reset);
	if (ret < 0) {
		LOG_ERR("I2C reset line toggle failed: %d", ret);
		return -EIO;
	}

	ret_pin_state = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret_pin_state != 0U) {
		LOG_ERR("Pin control setup failed: %d", ret_pin_state);
		return ret_pin_state;
	}

	ring_buf_init(&data->buffer, I2C_MAX_PACKET_LENGTH, data->buffer_data);
	k_sem_init(&data->async_xfer_lock, 1, 1);
	k_sem_init(&data->xfer_done_sem, 0, K_SEM_MAX_LIMIT);

	/* Initialize interrupt-based transfer state */
	data->state = I2C_QCC74X_STATE_IDLE;
	data->xfer_result = 0;
	data->tx_remaining = 0;
	data->rx_total = 0;
	data->rx_received = 0;

	i2c_qcc74x_cleanup_fifos(base_reg);

	ret = clock_control_on(config->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for I2C driver is already enabled");
		} else {
			LOG_ERR("Failed to enable I2C clock: %d", ret);
			return ret;
		}
	}

	ret = clock_control_configure(config->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)config->clock_id,
				      (void *)&config->clock_source);
	if (ret != 0) {
		LOG_ERR("Failed to configure I2C clock with source: %u err: %d",
			config->clock_source, ret);
		return ret;
	}

	LOG_DBG("Configure I2C with frequency: %u", config->dts_freq);
	dev_config = i2c_map_dt_bitrate(config->dts_freq);
	ret = i2c_qcc74x_configure(dev, I2C_MODE_CONTROLLER | dev_config);
	if (ret != 0U) {
		LOG_ERR("Failed to configure I2C during initialization: %d", ret);
		return ret;
	}

	LOG_DBG("Configure and enable interrupts");
	if (config->irq_config_func != NULL) {
		config->irq_config_func(dev);
		LOG_DBG("IRQ configuration completed");
	} else {
		LOG_WRN("IRQ configuration function is NULL");
	}
#ifdef CONFIG_I2C_QCC74X_DMA
	k_sem_init(&data->dma_done_sem, 0, 1);
#ifdef CONFIG_I2C_CALLBACK
	k_sem_init(&data->async_dma_work_sem, 0, 1);
#endif
#endif

	LOG_DBG("I2C Opal controller initialized successfully");
	return 0;
}

static const struct i2c_driver_api i2c_qcc74x_api = {
	.configure = i2c_qcc74x_configure,
	.transfer = i2c_qcc74x_transfer,
#ifdef CONFIG_I2C_CALLBACK
	.transfer_cb = i2c_qcc74x_transfer_cb,
#endif
};

#ifdef CONFIG_I2C_QCC74X_DMA
#define DMA_INIT(n, dir)                                                                           \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, dir)),                           \
		.channel = DT_INST_DMAS_CELL_BY_NAME(n, dir, channel),                             \
		.slot = DT_INST_DMAS_CELL_BY_NAME(n, dir, slot),                                   \
		.per_fifo_addr = DT_INST_DMAS_CELL_BY_NAME(n, dir, per_fifo_addr),                 \
	}

#define DMAS_DECL(n)                                                                               \
	{                                                                                          \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                          \
			   (DMA_INIT(n, tx)), ({0})),                                              \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                          \
			   (DMA_INIT(n, rx)), ({0})),                                              \
	}

#endif /* CONFIG_I2C_QCC74X_DMA */

#define I2C_QCC74X_GET_TIMEOUT(inst)                                                                 \
	(DT_FOREACH_CHILD(DT_DRV_INST(inst), I2C_QCC74X_GET_TIMEOUT_CHILD) + 0)

#define I2C_QCC74X_GET_TIMEOUT_CHILD(child)                                                          \
	COND_CODE_1(DT_NODE_HAS_PROP(child, timeout),                                              \
		   (DT_PROP(child, timeout)), (0))

#define I2C_QCC74X_IRQ_HANDLER_DECL(n) static void i2c_qcc74x_config_func_##n(const struct device *dev);

#define I2C_QCC74X_IRQ_HANDLER_FUNC(n) .irq_config_func = i2c_qcc74x_config_func_##n

#define I2C_QCC74X_IRQ_HANDLER(n)                                                                    \
	static void i2c_qcc74x_config_func_##n(const struct device *dev)                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), i2c_qcc74x_isr,               \
			    DEVICE_DT_INST_GET(n), 0U);                                            \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define I2C_QCC74X_DEVICE(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	I2C_QCC74X_IRQ_HANDLER_DECL(n)                                                               \
	static struct i2c_qcc74x_data i2c##n##_qcc74x_data;                                            \
	static const struct i2c_qcc74x_config i2c_qcc74x_cfg_##n = {                                   \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(n, source),                           \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.dts_freq = DT_INST_PROP(n, clock_frequency),                                      \
		I2C_QCC74X_IRQ_HANDLER_FUNC(n),                                                      \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
		.timeout = I2C_QCC74X_GET_TIMEOUT(n),                                                \
		IF_ENABLED(CONFIG_I2C_QCC74X_DMA, (.dma = DMAS_DECL(n),)) };                         \
	I2C_DEVICE_DT_INST_DEFINE(n, i2c_qcc74x_init, NULL, &i2c##n##_qcc74x_data, &i2c_qcc74x_cfg_##n,  \
				  POST_KERNEL, CONFIG_I2C_INIT_PRIORITY, &i2c_qcc74x_api);           \
	I2C_QCC74X_IRQ_HANDLER(n)

DT_INST_FOREACH_STATUS_OKAY(I2C_QCC74X_DEVICE)
