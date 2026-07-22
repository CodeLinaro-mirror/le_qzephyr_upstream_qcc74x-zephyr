/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_spi

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/crc.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/clock_control.h>
#ifdef CONFIG_SPI_QCC74X_DMA
#include <zephyr/drivers/dma/dma_qcc74x.h>
#endif

#include <hardware/spi_reg.h>
#include "qc7xx_spi.h"
#include <zephyr/drivers/clock_control/qcc7xx_clk_spi_ll.h>
#ifdef CONFIG_SPI_QCC74X_DMA
#include "qc7xx_dma.h"
#endif

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_qcc74x);
#include "spi_context.h"

#define QC7XX_GLB_SPI_MODE_ADDRESS            0x20000510U
#define QC7XX_GLB_SPI_MODE_MASTER_BIT         12U
#define QC7XX_SPI_FIFO_SIZE_BYTES             32U
#define QC7XX_SPI_MAX_FREQUENCY_HZ            MHZ(40)
#define QC7XX_SPI_WAIT_TIMEOUT_MS             250U
#define QC7XX_SPI_STATUS_CHECK_DELAY_MS       1U
#define QC7XX_SPI_SLAVE_DMA_FALLBACK_DELAY_US 100U

#define QC7XX_SPI_FRAME_BITS_8           8U
#define QC7XX_SPI_FRAME_BITS_16          16U
#define QC7XX_SPI_FRAME_BITS_24          24U
#define QC7XX_SPI_FRAME_BITS_32          32U
#define QC7XX_SPI_FRAME_SIZE_REG_8_BITS  0U
#define QC7XX_SPI_FRAME_SIZE_REG_16_BITS 1U
#define QC7XX_SPI_FRAME_SIZE_REG_24_BITS 2U
#define QC7XX_SPI_FRAME_SIZE_REG_32_BITS 3U

#define QC7XX_SPI_FRAME_BYTES_1     1U
#define QC7XX_SPI_FRAME_BYTES_2     2U
#define QC7XX_SPI_TX_FIFO_THRESHOLD 8U
#define QC7XX_SPI_RX_FIFO_THRESHOLD 0U

#define QC7XX_SPI_CLK_CALC_FACTOR_10 10U
#define QC7XX_SPI_CLK_CALC_FACTOR_5  5U
#define QC7XX_SPI_CLK_CALC_FACTOR_2  2U

#define QC7XX_SPI_FRAME_MASK_8_BITS      BIT_MASK(8)
#define QC7XX_SPI_FRAME_MASK_16_BITS     BIT_MASK(16)
#define QC7XX_SPI_FRAME_MASK_24_BITS     BIT_MASK(24)
#define QC7XX_SPI_FRAME_24BIT_HIGH_MASK  GENMASK(23, 16)
#define QC7XX_SPI_FRAME_24BIT_HIGH_SHIFT 16U

#ifdef CONFIG_SPI_QCC74X_INTERRUPT
/* spi_int_sts register (0x40019004) - Interrupt status bits */
#define SPI_INT_END BIT(0) /* spi_end_int: Transfer complete */
#define SPI_INT_TXF BIT(1) /* spi_txf_int: TX FIFO ready (has space) */
#define SPI_INT_RXF BIT(2) /* spi_rxf_int: RX FIFO ready (has data) */
#define SPI_INT_STO BIT(3) /* spi_sto_int: Slave timeout */
#define SPI_INT_TXU BIT(4) /* spi_txu_int: TX underrun */
#define SPI_INT_FER BIT(5) /* spi_fer_int: FIFO error */

/* spi_int_sts register - Mask bits (1 = masked/disabled) */
#define SPI_MASK_END BIT(8)  /* cr_spi_end_mask */
#define SPI_MASK_TXF BIT(9)  /* cr_spi_txf_mask */
#define SPI_MASK_RXF BIT(10) /* cr_spi_rxf_mask */
#define SPI_MASK_STO BIT(11) /* cr_spi_sto_mask */
#define SPI_MASK_TXU BIT(12) /* cr_spi_txu_mask */
#define SPI_MASK_FER BIT(13) /* cr_spi_fer_mask */

/* spi_int_sts register - Clear bits (write 1 to clear) */
#define SPI_CLR_END BIT(16) /* cr_spi_end_clr */
#define SPI_CLR_STO BIT(19) /* cr_spi_sto_clr */
#define SPI_CLR_TXU BIT(20) /* cr_spi_txu_clr */

#endif /* CONFIG_SPI_QCC74X_INTERRUPT */

/* spi_fifo_config_0 register (0x40019080) - Error flags */
#define SPI_FIFO_TX_OVERFLOW  BIT(4)
#define SPI_FIFO_TX_UNDERFLOW BIT(5)
#define SPI_FIFO_RX_OVERFLOW  BIT(6)
#define SPI_FIFO_RX_UNDERFLOW BIT(7)

#ifdef CONFIG_SPI_QCC74X_DMA

#ifdef CONFIG_SPI_QCC74X_DMA_SD_TX_CRC_DEBUG
#define SD_SPI_TOKEN_SINGLE      0xFEU
#define SD_SPI_TOKEN_MULTI_WRITE 0xFCU
#define SD_SPI_TOKEN_STOP_TRAN   0xFDU
#define SD_SPI_BLOCK_SIZE        512U
#endif

/**
 * @brief Enumeration for RX/TX dma trasnfer directions.
 */
enum spi_qcc74x_dma_direction {
	RX = 0,
	TX,
	NUM_OF_DIRECTION
};

/**
 * @brief Enumeration for RX/TX dma trasnfer directions.
 */
struct spi_qcc74x_dma_config {
	const struct device *dev;
	uint32_t channel;
	uint32_t slot;
	uint32_t per_fifo_addr;
};

/**
 * @brief SPI QCC74X dma related configuration structure..
 */
#define SPI_QCC74X_DMA_MAX_BLOCKS (CONFIG_DMA_CH_TRANSFER_BLOCK_MAX)

struct spi_qcc74x_dma_data {
	struct dma_config config;
	struct dma_block_config blocks[SPI_QCC74X_DMA_MAX_BLOCKS];
	uint8_t *aligned_buf;
	uint8_t *dma_buffer;
	uint32_t count;
	uint8_t *discard_buf;     /* dynamic discard buffer for NULL RX */
	uint8_t *discard_dma_buf; /* non-cache alias of discard_buf */
	size_t discard_buf_size;  /* current allocation size */
};

#endif /* CONFIG_SPI_QCC74X_DMA */

/**
 * @brief SPI Opal configuration structure.
 */
struct spi_qcc74x_config {
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const uint8_t clock_src;
	const uint32_t base_reg;
	void (*irq_config_func)(const struct device *dev);
#ifdef CONFIG_SPI_QCC74X_DMA
	const struct spi_qcc74x_dma_config dma[NUM_OF_DIRECTION];
#endif
	const struct reset_dt_spec reset;
};

/**
 * @brief SPI Opal runtime data structure.
 */
struct spi_qcc74x_data {
	const struct device *dev;
	struct spi_context ctx;
	struct qc7xx_device_s spi_hal_device;
	uint8_t clock_div;
#ifdef CONFIG_SPI_QCC74X_DMA
	struct spi_qcc74x_dma_data dma[NUM_OF_DIRECTION];
	bool dma_done;
	struct k_sem complete_sync;
	atomic_t slave_cs_seen;
	struct k_work_delayable slave_dma_fallback_work;
#endif
#ifdef CONFIG_SPI_QCC74X_INTERRUPT
	bool busy;           /* Simple state flag for interrupt transfers */
	size_t dummy_tx_len; /* Remaining dummy TX bytes needed when rx_len > tx_len */
#endif
};

#ifdef CONFIG_SPI_QCC74X_DMA

/**
 * @brief Check if dma devices are present for both channels.
 * @param dev Pointer to device structure.
 *
 * @retval true If devices are present
 */
static bool dma_enabled(const struct device *dev)
{
	const struct spi_qcc74x_config *config = dev->config;

	if (config->dma[TX].dev && config->dma[RX].dev) {
		return true;
	}
	return false;
}

/**
 * @brief Get number of enabled dma channels.
 * @param dev Pointer to device structure.
 *
 * @retval 2 If channels for RX and TX are present
 * @retval 0 No dma enabled
 */
static size_t dma_enabled_num(const struct device *dev)
{
	return dma_enabled(dev) ? 2 : 0;
}

#endif /* CONFIG_SPI_QCC74X_DMA */

/**
 * @brief Check if SPI has reached the expected status.
 *
 * @param base_reg      SPI base register address.
 * @param expect_idle   True if idle state is expected, false if busy.
 * @retval true         Status reached within timeout.
 * @retval false        Timeout occurred.
 */
static bool check_spi_status(const uint32_t base_reg, const bool expect_idle)
{
	bool is_busy = false;
	bool status_matches = false;
	uint32_t status = 0U;
	const int64_t start_time = k_uptime_get();

	LOG_DBG("Checking SPI status, expecting %s", expect_idle ? "idle" : "busy");

	while (true) {
		status = sys_read32(base_reg + SPI_BUS_BUSY_OFFSET);
		is_busy = (status & 1U) != 0U;
		status_matches = expect_idle ? !is_busy : is_busy;

		if (status_matches) {
			LOG_DBG("SPI status check completed successfully");
			return true;
		}

		if ((k_uptime_get() - start_time) > QC7XX_SPI_WAIT_TIMEOUT_MS) {
			LOG_ERR("SPI status check timeout: expected %s",
				expect_idle ? "idle" : "busy");
			return false;
		}

		k_msleep(QC7XX_SPI_STATUS_CHECK_DELAY_MS);
	}
}

/**
 * @brief Clear TX and RX FIFOs.
 *
 * @param base_reg SPI base register address.
 */
static void clear_fifos(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + SPI_FIFO_CONFIG_0_OFFSET);
	uint32_t timeout_us = 1000; /* 1ms should be more than enough for FIFO clear */

	reg_val |= (SPI_TX_FIFO_CLR | SPI_RX_FIFO_CLR);
	sys_write32(reg_val, base_reg + SPI_FIFO_CONFIG_0_OFFSET);

	/*
	 * Wait for FIFOs to actually clear.
	 * The clear bits are self-clearing, so wait until they're 0.
	 */
	do {
		reg_val = sys_read32(base_reg + SPI_FIFO_CONFIG_0_OFFSET);
		if (!(reg_val & (SPI_TX_FIFO_CLR | SPI_RX_FIFO_CLR))) {
			break;
		}
		k_busy_wait(1);
		timeout_us--;
	} while (timeout_us > 0);

	if (timeout_us == 0) {
		LOG_WRN("FIFO clear timeout - bits did not self-clear");
	}
}

/**
 * @brief Setup FIFOs.
 *
 * @param base_reg SPI base register address.
 */
static void setup_fifos(const uint32_t base_reg)
{
	uint32_t reg_val = 0U;

	clear_fifos(base_reg);

	/* Disable DMA */
	reg_val = sys_read32(base_reg + SPI_FIFO_CONFIG_0_OFFSET);
	reg_val &= ~(SPI_DMA_TX_EN | SPI_DMA_RX_EN);
	sys_write32(reg_val, base_reg + SPI_FIFO_CONFIG_0_OFFSET);

	/* Set FIFO thresholds */
	reg_val = sys_read32(base_reg + SPI_FIFO_CONFIG_1_OFFSET);
	reg_val &= ~(SPI_TX_FIFO_TH_MASK | SPI_RX_FIFO_TH_MASK);
	reg_val |= (QC7XX_SPI_TX_FIFO_THRESHOLD << SPI_TX_FIFO_TH_SHIFT);
	reg_val |= (QC7XX_SPI_RX_FIFO_THRESHOLD << SPI_RX_FIFO_TH_SHIFT);
	sys_write32(reg_val, base_reg + SPI_FIFO_CONFIG_1_OFFSET);

	LOG_DBG("SPI FIFOs configured: TX threshold=%u, RX threshold=%u",
		QC7XX_SPI_TX_FIFO_THRESHOLD, QC7XX_SPI_RX_FIFO_THRESHOLD);
}

/**
 * @brief Calculate clock divider and period registers value.
 *
 * @param peripheral_clk Peripheral clock frequency in Hz.
 * @param desired_freq   Desired SPI clock frequency in Hz.
 * @param [out] Clock divider value.
 * @param [out] Period registers value.
 *
 * @return 0 in case of success and -EINVAL if wrong values were provided
 */
static int calculate_settings(const uint32_t peripheral_clk, const uint32_t desired_freq,
			      uint8_t *clk_divider, uint8_t *period_divider)
{
	uint8_t clock_div = 0U;
	uint8_t period_div = 0U;
	uint8_t iter;
	uint8_t period_tmp;
	uint32_t actual_freq;
	uint32_t error;
	uint32_t smallest_error = UINT32_MAX;

	/*
	 * Desired frequency formula:
	 *
	 * desired_frequency = peripheral_clock / ((2 * divider - 1) * period_value)
	 *
	 * To validate the request, it is required that total division ratio
	 * (peripheral_clock / desired_frequency) does not exceed the hardware's
	 * maximum possible divisor which is:
	 * ((2 * divider_MAX - 1) * period_value_MAX)
	 */
	if ((peripheral_clk / desired_freq) >
	    ((QC7XX_SPI_CLK_CALC_FACTOR_2 * QC7XX_SPI_MAX_CLK_DIV - 1) * QC7XX_SPI_PRD_MAX_DIV)) {
		return -EINVAL;
	}

	/* Divider is sufficient to obtain desired frequency */
	if ((peripheral_clk / desired_freq) <=
	    ((QC7XX_SPI_CLK_CALC_FACTOR_2 * QC7XX_SPI_MAX_CLK_DIV - 1))) {
		/* Use scaling factor (QC7XX_SPI_CLK_CALC_FACTOR_10)
		 * to improve integer division accuracy and rounding
		 */
		clock_div = (peripheral_clk / QC7XX_SPI_CLK_CALC_FACTOR_2 *
				     QC7XX_SPI_CLK_CALC_FACTOR_10 / desired_freq +
			     QC7XX_SPI_CLK_CALC_FACTOR_5) /
			    QC7XX_SPI_CLK_CALC_FACTOR_10;
		period_div = 1;
	} else {
		/* Period registers need to be used */
		for (iter = QC7XX_SPI_MAX_CLK_DIV; iter >= 1; iter--) {
			period_tmp = peripheral_clk /
				     ((QC7XX_SPI_CLK_CALC_FACTOR_2 * iter - 1) * desired_freq);

			if (period_tmp > QC7XX_SPI_PRD_MAX_DIV) {
				continue;
			}

			actual_freq = peripheral_clk /
				      ((QC7XX_SPI_CLK_CALC_FACTOR_2 * iter - 1) * period_tmp);

			error = abs(actual_freq - desired_freq);

			if (error == 0) {
				clock_div = iter;
				period_div = period_tmp - 1;
				break;
			}

			if (error < smallest_error) {
				clock_div = iter;
				period_div = period_tmp;
				smallest_error = error;
			}
		}
	}
	clock_div = (clock_div != 0U) ? (clock_div - 1U) : 0U;
	period_div = (period_div != 0U) ? (period_div - 1U) : 0U;

	*clk_divider = clock_div;
	*period_divider = period_div;

	LOG_DBG("For source freq:%u and desired frequency%u "
		" following dividers has been chosen:\n"
		"divider:%u period_reg_val:%u",
		peripheral_clk, desired_freq, clock_div, period_div);

	return 0;
}

/**
 * @brief Configure SPI frequency.
 *
 * @param dev       SPI device
 * @param base_reg  SPI base register address.
 * @param frequency Desired frequency in Hz.
 *
 * @retval 0        On success.
 * @retval -EINVAL  On invalid frequency.
 */
static int32_t set_spi_frequency(const struct device *dev, const uint32_t base_reg,
				 const uint32_t frequency)
{
	const struct spi_qcc74x_config *const config = dev->config;
	int ret;
	uint32_t src_clk_rate;
	uint8_t clk_divider;
	uint8_t period_divider;
	struct qcc74x_clk_settings spi_settings = {
		.enable = CLOCK_ENABLE,
		.clk_src = config->clock_src,
		/* This needs to be zero to avoid interfering with get_rate(). */
		.clk_div = 0,
	};

	if (frequency > QC7XX_SPI_MAX_FREQUENCY_HZ) {
		LOG_ERR("Frequency %u Hz exceeds maximum %u Hz", frequency,
			QC7XX_SPI_MAX_FREQUENCY_HZ);
		return -EINVAL;
	}

	if (frequency == 0U) {
		LOG_ERR("Frequency cannot be zero");
		return -EINVAL;
	}

	/* Dummy clock configuration to ensure that source clock
	 * is set correctly for get_rate function
	 */
	ret = clock_control_configure(config->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)config->clock_id,
				      (void *)&spi_settings);

	if (ret != 0) {
		LOG_ERR("Failed to configure spi clock with source:%d and div:%u!",
			spi_settings.clk_src, spi_settings.clk_div);
	} else {
		LOG_DBG("Successfully configured spi clock with source:%d and div:%u!",
			spi_settings.clk_src, spi_settings.clk_div);
	}

	ret = clock_control_get_rate(config->clock_dev,
				     (clock_control_subsys_t)(uintptr_t)config->clock_id,
				     &src_clk_rate);
	if (ret != 0) {
		LOG_ERR("Failed to get clock rate for source:%d err:%d!", (int)config->clock_id,
			ret);
	} else {
		LOG_DBG("Clock peripheral id: %d frequency: %u Hz.", (int)config->clock_id,
			src_clk_rate);
	}

	ret = calculate_settings(src_clk_rate, frequency, &clk_divider, &period_divider);

	if (ret != 0) {
		LOG_ERR("Can not obtain %u frequency from %u source "
			"with divider and period registers",
			frequency, config->clock_src);
		return -EINVAL;
	}

	LOG_DBG("Configuring SPI frequency to %u Hz", frequency);

	/* Proper configuration with calculated values */
	spi_settings.clk_div = clk_divider;

	ret = clock_control_configure(config->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)config->clock_id,
				      (void *)&spi_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure spi clock with source:%d and div:%u!",
			spi_settings.clk_src, spi_settings.clk_div);
	} else {
		LOG_DBG("Successfully configured spi clock with source:%d and div:%u!",
			spi_settings.clk_src, spi_settings.clk_div);
	}

	qcc7xx_clk_spi_ll_set_period(period_divider, base_reg);

	return 0;
}

/**
 * @brief Configure SPI mode and bit order.
 *
 * @param base_reg   SPI base register address.
 * @param spi_config Pointer to SPI configuration.
 */
static void set_spi_mode(const uint32_t base_reg, const struct spi_config *const spi_config)
{
	bool cpol = false;
	bool cpha = false;
	uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);

	if ((spi_config->operation & SPI_TRANSFER_LSB) != 0U) {
		reg_val |= SPI_CR_SPI_BIT_INV;
		LOG_DBG("SPI configured for LSB first");
	} else {
		reg_val &= ~SPI_CR_SPI_BIT_INV;
		LOG_DBG("SPI configured for MSB first");
	}

	reg_val &= ~SPI_CR_SPI_BYTE_INV;

	cpol = (spi_config->operation & SPI_MODE_CPOL) != 0U;
	cpha = (spi_config->operation & SPI_MODE_CPHA) != 0U;

	if (!cpol && !cpha) {
		/* Mode 0: CPOL=0, CPHA=0 */
		reg_val &= ~SPI_CR_SPI_SCLK_POL;
		reg_val |= SPI_CR_SPI_SCLK_PH;
		LOG_DBG("SPI mode 0 (CPOL=0, CPHA=0)");
	} else if (!cpol && cpha) {
		/* Mode 1: CPOL=0, CPHA=1 */
		reg_val &= ~SPI_CR_SPI_SCLK_POL;
		reg_val &= ~SPI_CR_SPI_SCLK_PH;
		LOG_DBG("SPI mode 1 (CPOL=0, CPHA=1)");
	} else if (cpol && !cpha) {
		/* Mode 2: CPOL=1, CPHA=0 */
		reg_val |= SPI_CR_SPI_SCLK_POL;
		reg_val |= SPI_CR_SPI_SCLK_PH;
		LOG_DBG("SPI mode 2 (CPOL=1, CPHA=0)");
	} else {
		/* Mode 3: CPOL=1, CPHA=1 */
		reg_val |= SPI_CR_SPI_SCLK_POL;
		reg_val &= ~SPI_CR_SPI_SCLK_PH;
		LOG_DBG("SPI mode 3 (CPOL=1, CPHA=1)");
	}

	sys_write32(reg_val, base_reg + SPI_CONFIG_OFFSET);
}

/**
 * @brief Configure SPI frame size.
 *
 * @param base_reg   SPI base register address.
 * @param frame_bits Frame size in bits (8, 16, 24, or 32).
 * @retval 0         On success.
 * @retval -EINVAL   On invalid frame size.
 */
static int32_t set_frame_size(const uint32_t base_reg, const uint32_t frame_bits)
{
	uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);
	uint32_t frame_reg_val = 0U;

	switch (frame_bits) {
	case QC7XX_SPI_FRAME_BITS_8:
		frame_reg_val = QC7XX_SPI_FRAME_SIZE_REG_8_BITS;
		break;
	case QC7XX_SPI_FRAME_BITS_16:
		frame_reg_val = QC7XX_SPI_FRAME_SIZE_REG_16_BITS;
		break;
	case QC7XX_SPI_FRAME_BITS_24:
		frame_reg_val = QC7XX_SPI_FRAME_SIZE_REG_24_BITS;
		break;
	case QC7XX_SPI_FRAME_BITS_32:
		frame_reg_val = QC7XX_SPI_FRAME_SIZE_REG_32_BITS;
		break;
	default:
		LOG_ERR("Unsupported frame size: %u bits. Supported: 8, 16, 24, 32", frame_bits);
		return -EINVAL;
	}

	reg_val &= ~SPI_CR_SPI_FRAME_SIZE_MASK;
	reg_val |= (frame_reg_val << SPI_CR_SPI_FRAME_SIZE_SHIFT);
	sys_write32(reg_val, base_reg + SPI_CONFIG_OFFSET);

	LOG_DBG("Frame size set to %u bits", frame_bits);
	return 0;
}

/**
 * @brief Configure master/slave mode in global register.
 *
 * @param is_master True for master mode, false for slave mode.
 */
static void set_global_master_mode(const bool is_master)
{
	uint32_t reg_val = sys_read32(QC7XX_GLB_SPI_MODE_ADDRESS);

	if (is_master) {
		reg_val |= BIT(QC7XX_GLB_SPI_MODE_MASTER_BIT);
		LOG_DBG("SPI configured as master");
	} else {
		reg_val &= ~BIT(QC7XX_GLB_SPI_MODE_MASTER_BIT);
		LOG_DBG("SPI configured as slave");
	}

	sys_write32(reg_val, QC7XX_GLB_SPI_MODE_ADDRESS);
}

/**
 * @brief Get frame size in bytes from register configuration.
 *
 * @param base_reg SPI base register address.
 * @return Frame size in bytes.
 */
static uint8_t get_frame_size_bytes(const uint32_t base_reg)
{
	const uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);
	const uint32_t frame_size_reg =
		((reg_val & SPI_CR_SPI_FRAME_SIZE_MASK) >> SPI_CR_SPI_FRAME_SIZE_SHIFT);

	return (frame_size_reg + QC7XX_SPI_FRAME_BYTES_1);
}

#ifndef CONFIG_SPI_QCC74X_DMA
/**
 * @brief Convert buffer data to frame format.
 *
 * @param base_reg SPI base register address.
 * @param buff     Pointer to buffer data.
 * @return Frame data formatted for hardware.
 */
static uint32_t buffer_to_frame(const uint32_t base_reg, const void *buff)
{
	uint32_t frame = 0U;
	const uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);
	const uint32_t frame_size_reg =
		((reg_val & SPI_CR_SPI_FRAME_SIZE_MASK) >> SPI_CR_SPI_FRAME_SIZE_SHIFT);

	if (buff == NULL) {
		LOG_ERR("NULL buffer received");
		return 0U;
	}

	switch (frame_size_reg) {
	case QC7XX_SPI_FRAME_SIZE_REG_8_BITS:
		frame = *(const uint8_t *)buff;
		break;
	case QC7XX_SPI_FRAME_SIZE_REG_16_BITS:
		frame = *(const uint16_t *)buff;
		break;
	case QC7XX_SPI_FRAME_SIZE_REG_24_BITS:
		frame = *(const uint32_t *)buff & QC7XX_SPI_FRAME_MASK_24_BITS;
		break;
	case QC7XX_SPI_FRAME_SIZE_REG_32_BITS:
		frame = *(const uint32_t *)buff;
		break;
	default:
		LOG_ERR("Invalid frame size configuration: %u", frame_size_reg);
		break;
	}

	return frame;
}

/**
 * @brief Convert frame data to buffer format.
 *
 * @param base_reg SPI base register address.
 * @param frame    Frame data from hardware.
 * @param buff     Pointer to destination buffer.
 */
static void frame_to_buffer(const uint32_t base_reg, const uint32_t frame, void *buff)
{
	const uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);
	const uint32_t frame_size_reg =
		(reg_val & SPI_CR_SPI_FRAME_SIZE_MASK) >> SPI_CR_SPI_FRAME_SIZE_SHIFT;

	if (buff == NULL) {
		LOG_ERR("NULL buffer received");
		return;
	}

	switch (frame_size_reg) {
	case QC7XX_SPI_FRAME_SIZE_REG_8_BITS:
		*(uint8_t *)buff = frame & QC7XX_SPI_FRAME_MASK_8_BITS;
		break;
	case QC7XX_SPI_FRAME_SIZE_REG_16_BITS:
		*(uint16_t *)buff = frame & QC7XX_SPI_FRAME_MASK_16_BITS;
		break;
	case QC7XX_SPI_FRAME_SIZE_REG_24_BITS:
		*(uint16_t *)buff = frame & QC7XX_SPI_FRAME_MASK_16_BITS;
		*((uint8_t *)buff + QC7XX_SPI_FRAME_BYTES_2) =
			((frame & QC7XX_SPI_FRAME_24BIT_HIGH_MASK) >>
			 QC7XX_SPI_FRAME_24BIT_HIGH_SHIFT);
		break;
	case QC7XX_SPI_FRAME_SIZE_REG_32_BITS:
		*(uint32_t *)buff = frame;
		break;
	default:
		LOG_ERR("Invalid frame size configuration: %u", frame_size_reg);
		break;
	}
}

/**
 * @brief Get FIFO counts for TX and RX.
 *
 * @param base_reg SPI base register address.
 * @param rx_cnt   Pointer to store RX FIFO count.
 * @param tx_cnt   Pointer to store TX FIFO count.
 */
static void get_fifo_counts(const uint32_t base_reg, uint8_t *rx_cnt, uint8_t *tx_cnt)
{
	const uint32_t reg_val = sys_read32(base_reg + SPI_FIFO_CONFIG_1_OFFSET);

	if (rx_cnt == NULL || tx_cnt == NULL) {
		LOG_ERR("NULL pointer for FIFO count storage");
		return;
	}

	*rx_cnt = ((reg_val & SPI_RX_FIFO_CNT_MASK) >> SPI_RX_FIFO_CNT_SHIFT);
	*tx_cnt = ((reg_val & SPI_TX_FIFO_CNT_MASK) >> SPI_TX_FIFO_CNT_SHIFT);
}

#if !defined(CONFIG_SPI_QCC74X_INTERRUPT) || !defined(CONFIG_SPI_QCC74X_INTERRUPT_SYNC)
/**
 * @brief Wait for TX transmission to complete.
 *
 * @param base_reg SPI base register address.
 * @retval 0          On success.
 * @retval -ETIMEDOUT On timeout.
 */
static int32_t wait_tx_complete(const uint32_t base_reg)
{
	uint8_t tx_cnt = 0U;
	uint32_t reg_val = 0U;
	const int64_t start_time = k_uptime_get();

	LOG_DBG("Waiting for TX completion...");

	do {
		if ((k_uptime_get() - start_time) > QC7XX_SPI_WAIT_TIMEOUT_MS) {
			LOG_ERR("Timeout waiting for TX completion");
			return -ETIMEDOUT;
		}

		reg_val = sys_read32(base_reg + SPI_FIFO_CONFIG_1_OFFSET);
		tx_cnt = (reg_val & SPI_TX_FIFO_CNT_MASK) >> SPI_TX_FIFO_CNT_SHIFT;

		k_msleep(QC7XX_SPI_STATUS_CHECK_DELAY_MS);
	} while (tx_cnt < QC7XX_SPI_FIFO_SIZE_BYTES);

	LOG_DBG("TX transmission completed successfully");
	return 0;
}
#endif /* !CONFIG_SPI_QCC74X_INTERRUPT || !CONFIG_SPI_QCC74X_INTERRUPT_SYNC */

#endif /* CONFIG_SPI_QCC74X_DMA */

/**
 * @brief Configure SPI peripheral.
 *
 * @param base_reg SPI base register address.
 */
static void configure_spi_peripheral(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);

	reg_val &= ~(SPI_CR_SPI_DEG_EN | SPI_CR_SPI_DEG_CNT_MASK);
	reg_val |= SPI_CR_SPI_M_CONT_EN;
	reg_val &= ~SPI_CR_SPI_RXD_IGNR_EN;
	reg_val &= ~(SPI_CR_SPI_S_EN | SPI_CR_SPI_M_EN);

	sys_write32(reg_val, base_reg + SPI_CONFIG_OFFSET);
	LOG_DBG("SPI peripheral configured");
}

/**
 * @brief Disable SPI.
 *
 * @param base_reg SPI base register address.
 */
static void disable_spi(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + SPI_CONFIG_OFFSET);

	reg_val &= ~(SPI_CR_SPI_S_EN | SPI_CR_SPI_M_EN);
	sys_write32(reg_val, base_reg + SPI_CONFIG_OFFSET);
	LOG_DBG("SPI disabled");
}

/**
 * @brief Log information about pin configuration for debug purposes.
 *
 * @param pcfg Pin control configuration.
 */
static void log_pin_information(const struct pinctrl_dev_config *const pcfg)
{
	if (pcfg != NULL && pcfg->state_cnt > 0U &&
	    pcfg->states[PINCTRL_STATE_DEFAULT].pin_cnt > 0U) {
		const struct pinctrl_state *const state = &pcfg->states[PINCTRL_STATE_DEFAULT];

		LOG_DBG("SPI pinctrl default state uses %u pin(s):", state->pin_cnt);
		for (size_t i = 0U; i < state->pin_cnt; ++i) {
			LOG_DBG("SPI pinctrl | Pin number: %u, Pin function: 0x%02x, "
				"Pincfg: 0x%08x",
				(uint32_t)(state->pins[i].pinmux & GPIO_PIN_MSK),
				(uint32_t)GET_PIN_FUN(state->pins[i].pinmux),
				state->pins[i].pincfg);
		}
	} else {
		LOG_DBG("Pin control information not available");
	}
}

/**
 * @brief Configure SPI parameters.
 *
 * @param dev        Pointer to device structure.
 * @param spi_config Pointer to SPI configuration.
 * @retval 0         On success.
 * @retval -EINVAL   On invalid configuration.
 * @retval -ENOTSUP  On unsupported operation.
 * @retval -errno    Negative errno for other failures.
 */
static int32_t spi_qcc74x_configure(const struct device *dev, const struct spi_config *spi_config)
{
	const struct spi_qcc74x_config *const config = dev->config;
	struct spi_qcc74x_data *const data = dev->data;
	uint32_t frame_size = 0U;
	int32_t result = 0;
	uint32_t spi_frequency = 0;

	if (spi_config == NULL) {
		LOG_ERR("SPI configuration is NULL");
		return -EINVAL;
	}

	if (spi_context_configured(&data->ctx, spi_config)) {
		LOG_DBG("SPI already configured");
		return 0;
	}

	LOG_INF("Configuring SPI: freq=%u Hz, operation=0x%x", spi_config->frequency,
		spi_config->operation);

	disable_spi(config->base_reg);

	if (SPI_OP_MODE_GET(spi_config->operation) == SPI_OP_MODE_MASTER) {
		set_global_master_mode(true);
		spi_frequency = spi_config->frequency;
	} else {
		set_global_master_mode(false);
		spi_frequency = QC7XX_SPI_MAX_FREQUENCY_HZ;
	}

	result = set_spi_frequency(dev, config->base_reg, spi_frequency);
	if (result != 0) {
		LOG_ERR("Failed to set frequency: %d", result);
		return result;
	}

	configure_spi_peripheral(config->base_reg);
	set_spi_mode(config->base_reg, spi_config);

	frame_size = SPI_WORD_SIZE_GET(spi_config->operation);
	result = set_frame_size(config->base_reg, frame_size);
	if (result != 0) {
		LOG_ERR("Failed to set frame size: %d", result);
		return result;
	}

	setup_fifos(config->base_reg);
	qc7xx_spi_tcint_mask(&data->spi_hal_device, false);

	data->ctx.config = spi_config;
	LOG_INF("SPI configuration completed successfully");
	return 0;
}

/**
 * @brief Calculate total bytes in a buffer set
 *
 * @param bufs Pointer to the SPI buffer set, or NULL
 *
 * @return Total length in bytes across all buffers, or 0 if bufs is NULL
 */
static size_t spi_qcc74x_buf_set_total_len(const struct spi_buf_set *bufs)
{
	size_t total = 0;

	if (bufs != NULL) {
		for (size_t i = 0; i < bufs->count; i++) {
			total += bufs->buffers[i].len;
		}
	}
	return total;
}

/**
 * @brief Calculate total bytes across non-NULL buffers in a buffer set.
 *
 * Used for sizing RX bounce buffers where NULL RX segments go to the
 * discard buffer and don't consume bounce space.
 *
 * @param bufs Pointer to the SPI buffer set, or NULL.
 *
 * @return Total length in bytes across non-NULL buffers, or 0 if bufs is NULL.
 */
#ifdef CONFIG_SPI_QCC74X_DMA
static size_t spi_qcc74x_buf_set_total_nonnull_len(const struct spi_buf_set *bufs)
{
	size_t total = 0;

	if (bufs != NULL) {
		for (size_t i = 0; i < bufs->count; i++) {
			if (bufs->buffers[i].buf != NULL) {
				total += bufs->buffers[i].len;
			}
		}
	}
	return total;
}

/**
 * @brief Check whether a buffer set contains NULL data segments.
 *
 * @param bufs Pointer to the SPI buffer set, or NULL.
 *
 * @return true if any segment has buf == NULL and len > 0, false otherwise.
 */
static bool spi_qcc74x_buf_set_has_null_segments(const struct spi_buf_set *bufs)
{
	if (bufs != NULL) {
		for (size_t i = 0; i < bufs->count; i++) {
			if ((bufs->buffers[i].buf == NULL) && (bufs->buffers[i].len > 0U)) {
				return true;
			}
		}
	}

	return false;
}
#endif

#ifdef CONFIG_SPI_QCC74X_DMA

/**
 * @brief Callback function fired with DMA channel completion.
 *
 * @param dma_dev    Pointer to DMA device structure.
 * @param arg        User data. Here - SPI device structure.
 * @param channel    DMA channel number.
 * @param status     DMA operations status.
 */
static void spi_qcc74x_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel,
				  int status);

#ifdef CONFIG_SPI_QCC74X_DMA_SD_TX_CRC_DEBUG
/**
 * @brief One-shot diagnostic for SD write frame CRC in TX bounce buffer.
 *
 * For SD SPI writes, verifies that bounce_buffer contains:
 * [token][512-byte payload][crc16][optional pad...]
 * and logs computed CRC16(payload) vs transmitted CRC bytes.
 */
static void spi_qcc74x_log_sd_write_crc_once(const uint8_t *tx_bounce, size_t total_tx,
					   size_t total_rx)
{
	static bool logged_once;
	uint8_t token;
	size_t crc_off;
	uint16_t crc_calc, crc_tx;

	if (logged_once || tx_bounce == NULL) {
		return;
	}

	/* Need at least token(1) + data(512) + crc16(2). */
	if (total_tx < (1U + SD_SPI_BLOCK_SIZE + 2U)) {
		return;
	}

	/* Expected SD block write frame size used by sdhc_spi: 515/516 bytes. */
	if ((total_tx != 515U) && (total_tx != 516U) && (total_rx != 515U) && (total_rx != 516U)) {
		return;
	}

	token = tx_bounce[0];
	if ((token != SD_SPI_TOKEN_SINGLE) && (token != SD_SPI_TOKEN_MULTI_WRITE)) {
		return;
	}

	crc_off = 1U + SD_SPI_BLOCK_SIZE;
	crc_calc = crc16_itu_t(0, &tx_bounce[1], SD_SPI_BLOCK_SIZE);
	crc_tx = ((uint16_t)tx_bounce[crc_off] << 8) | tx_bounce[crc_off + 1U];

	LOG_DBG("SD write TX bounce CRC diag: token=0x%02x tx=%u rx=%u calc=0x%04x tx_crc=0x%04x "
		"data[0]=0x%02x data[511]=0x%02x",
		token, (unsigned int)total_tx, (unsigned int)total_rx, crc_calc, crc_tx,
		tx_bounce[1], tx_bounce[crc_off - 1U]);
	logged_once = true;
}
#endif

/**
 * @brief Ensure the dynamic discard buffer for NULL RX segments is large enough.
 *
 * Allocates or grows a cache-line-aligned discard buffer.  Reused across
 * transfers — only reallocated when a larger segment is encountered.
 *
 * @param dma_data  Pointer to the RX DMA data (contains discard_buf state).
 * @param need      Required size in bytes.
 *
 * @retval 0       On success.
 * @retval -ENOMEM If allocation fails.
 */
static int spi_qcc74x_ensure_discard_buf(struct spi_qcc74x_dma_data *dma_data, size_t need)
{
	if (dma_data->discard_buf_size >= need) {
		return 0;
	}

	if (dma_data->discard_buf != NULL) {
		k_free(dma_data->discard_buf);
	}

	dma_data->discard_buf = k_aligned_alloc(QC7XX_CACHE_LINE_SIZE, need);
	if (dma_data->discard_buf == NULL) {
		dma_data->discard_dma_buf = NULL;
		dma_data->discard_buf_size = 0;
		LOG_ERR("Failed to allocate %zu-byte discard buffer", need);
		return -ENOMEM;
	}

	dma_data->discard_dma_buf =
		qc7xx_get_no_cache_addr((const void *)(uintptr_t)dma_data->discard_buf);
	qc7xx_l1c_dcache_invalidate_range((void *)(uintptr_t)dma_data->discard_dma_buf, need);
	dma_data->discard_buf_size = need;
	return 0;
}

/**
 * @brief Setup DMA channel for multi-block scatter-gather.
 *
 * Builds a dma_block_config chain covering all segments in one direction.
 * TX: iterates tx_bufs segments, appends a pad block if total_tx < total_rx.
 * RX: iterates rx_bufs segments, routing NULL segments to the discard buffer.
 *
 * @param dev      Pointer to device structure.
 * @param dir      Transfer direction (TX or RX).
 * @param tx_bufs  TX buffer set.
 * @param rx_bufs  RX buffer set.
 *
 * @retval 0       On success.
 * @retval -errno  On failure.
 */
static int spi_qcc74x_dma_setup(const struct device *dev, const uint32_t dir,
			      const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	const struct spi_qcc74x_config *config = dev->config;
	struct spi_qcc74x_data *data = dev->data;
	struct spi_qcc74x_dma_data *dma_data = &data->dma[dir];
	struct dma_config *dma_cfg = &dma_data->config;
	struct dma_block_config *blocks = dma_data->blocks;
	const struct spi_qcc74x_dma_config *dma = &config->dma[dir];
	const uint8_t word_bytes = SPI_WORD_SIZE_GET(data->ctx.config->operation) / 8;
	int ret;

	memset(dma_cfg, 0, sizeof(*dma_cfg));
	memset(blocks, 0, sizeof(dma_data->blocks));

	/*
	 * RX (peripheral-to-memory) uses burst-of-4 to drain the RX FIFO
	 * faster than the SPI clock fills it, preventing overflow on large
	 * transfers.  Fall back to burst-of-1 for transfers not aligned to
	 * the burst size.  TX always uses burst-of-1.
	 */
	if (dir == RX) {
		const size_t total_tx_len = spi_qcc74x_buf_set_total_len(tx_bufs);
		const size_t total_rx_len = spi_qcc74x_buf_set_total_len(rx_bufs);
		const size_t total = MAX(total_tx_len, total_rx_len);
		uint32_t reg_val;
		const bool tx_only = (total_rx_len == 0U) && (total_tx_len > 0U);

		/*
		 * Use burst=4 when we have enough bytes.  Always pair
		 * burst=4 with threshold=3 so the DMA request only
		 * fires when 4+ bytes are in the FIFO — even for the
		 * discard path.  Reading from a partially-filled FIFO
		 * in burst mode can cause hardware side effects that
		 * corrupt the concurrent TX stream.
		 *
		 * For non-4-aligned discard transfers the DMA block
		 * size is rounded down to the nearest 4; the tail
		 * bytes stay in the FIFO and are drained at the start
		 * of the next transfer.
		 */
		if (!tx_only && total >= 4 && ((total % 4) == 0)) {
			dma_cfg->source_burst_length = 4;
			dma_cfg->dest_burst_length = 4;

			reg_val = sys_read32(config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);
			reg_val &= ~SPI_RX_FIFO_TH_MASK;
			reg_val |= (3U << SPI_RX_FIFO_TH_SHIFT);
			sys_write32(reg_val, config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);
		} else {
			dma_cfg->source_burst_length = 1;
			dma_cfg->dest_burst_length = 1;
			/* Restore default threshold for burst-of-1 */
			reg_val = sys_read32(config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);
			reg_val &= ~SPI_RX_FIFO_TH_MASK;
			reg_val |= (QC7XX_SPI_RX_FIFO_THRESHOLD << SPI_RX_FIFO_TH_SHIFT);
			sys_write32(reg_val, config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);
		}
	} else {
		dma_cfg->source_burst_length = 1;
		dma_cfg->dest_burst_length = 1;
	}
	dma_cfg->user_data = (void *)dev;
	dma_cfg->complete_callback_en = 1;
	dma_cfg->error_callback_dis = 0;
	dma_cfg->dma_callback = spi_qcc74x_dma_callback;
	dma_cfg->channel_direction = dir == TX ? MEMORY_TO_PERIPHERAL : PERIPHERAL_TO_MEMORY;
	dma_cfg->dma_slot = dma->slot;
	dma_cfg->source_data_size = word_bytes;
	dma_cfg->dest_data_size = word_bytes;

	if (dir == TX) {
		const struct spi_buf_set *bufs = tx_bufs;
		const size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
		const size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);
		const bool use_bounce = (dma_data->dma_buffer != NULL);
		size_t bounce_offset = 0;
		size_t nblocks = 0;

		if (use_bounce) {
			/*
			 * With bounce enabled we already have one contiguous buffer
			 * (including TX padding if needed). Use a single DMA block to
			 * avoid relying on intermediate LLI callbacks.
			 */
			blocks[0].block_size = MAX(total_tx, total_rx) * word_bytes;
			blocks[0].dest_address = dma->per_fifo_addr;
			blocks[0].source_address = (uint32_t)dma_data->dma_buffer;
			dma_cfg->block_count = 1;
			dma_cfg->head_block = &blocks[0];
			goto dma_configure;
		}

		for (size_t i = 0; i < bufs->count; i++) {
			const struct spi_buf *sb = &bufs->buffers[i];

			if (sb->len == 0) {
				continue;
			}
			if (nblocks >= SPI_QCC74X_DMA_MAX_BLOCKS) {
				LOG_ERR("TX block count exceeds max %d", SPI_QCC74X_DMA_MAX_BLOCKS);
				return -ENOMEM;
			}
			blocks[nblocks].block_size = sb->len * word_bytes;
			blocks[nblocks].dest_address = dma->per_fifo_addr;

			if (use_bounce) {
				blocks[nblocks].source_address =
					(uint32_t)(dma_data->dma_buffer + bounce_offset);
				bounce_offset += sb->len;
			} else {
				if (sb->buf == NULL ||
				    !qc7xx_check_cache_addr_aligned((uintptr_t)sb->buf)) {
					LOG_ERR("TX segment requires bounce buffer");
					return -EINVAL;
				}
				blocks[nblocks].source_address = (uint32_t)sb->buf;
			}
			nblocks++;
		}

		/* Pad TX with dummy clocks if total_tx < total_rx */
		if (total_tx < total_rx) {
			const size_t pad = (total_rx - total_tx) * word_bytes;

			if (nblocks >= SPI_QCC74X_DMA_MAX_BLOCKS) {
				LOG_ERR("No room for TX pad block");
				return -ENOMEM;
			}
			blocks[nblocks].block_size = pad;
			blocks[nblocks].dest_address = dma->per_fifo_addr;
			if (use_bounce) {
				blocks[nblocks].source_address =
					(uint32_t)(dma_data->dma_buffer + bounce_offset);
				bounce_offset += (total_rx - total_tx);
			} else {
				LOG_ERR("TX pad requires bounce buffer");
				return -EINVAL;
			}
			nblocks++;
		}

		/* Link the chain */
		for (size_t i = 0; i + 1 < nblocks; i++) {
			blocks[i].next_block = &blocks[i + 1];
		}
		dma_cfg->block_count = nblocks;
		dma_cfg->head_block = &blocks[0];

	} else { /* RX */
		const struct spi_buf_set *bufs = rx_bufs;
		const size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
		const size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);
		const bool use_bounce = (dma_data->dma_buffer != NULL);
		size_t bounce_offset = 0;
		size_t nblocks = 0;
		size_t max_null_len = 0;

		/*
		 * TX-only transfer still shifts data into RX path.
		 * Keep RX DMA active and discard received bytes to avoid stalling.
		 */
		if ((total_rx == 0U) && (total_tx > 0U)) {
			ret = spi_qcc74x_ensure_discard_buf(dma_data, total_tx * word_bytes);
			if (ret < 0) {
				return ret;
			}
			/* Drain all shifted-in bytes for TX-only transfers. */
			blocks[0].block_size = total_tx * word_bytes;
			blocks[0].source_address = dma->per_fifo_addr;
			blocks[0].dest_address = (uint32_t)dma_data->discard_dma_buf;
			dma_cfg->block_count = 1;
			dma_cfg->head_block = &blocks[0];
			goto dma_configure;
		}

		/* First pass: find largest NULL segment for discard buffer sizing */
		for (size_t i = 0; i < bufs->count; i++) {
			if (bufs->buffers[i].buf == NULL && bufs->buffers[i].len > max_null_len) {
				max_null_len = bufs->buffers[i].len;
			}
		}

		/* Ensure discard buffer is large enough */
		if (max_null_len > 0) {
			ret = spi_qcc74x_ensure_discard_buf(dma_data, max_null_len * word_bytes);
			if (ret < 0) {
				return ret;
			}
		}

		/*
		 * Fast path for contiguous RX bounce transfers: one DMA block.
		 */
		if (use_bounce && (max_null_len == 0U)) {
			blocks[0].block_size = total_rx * word_bytes;
			blocks[0].source_address = dma->per_fifo_addr;
			blocks[0].dest_address = (uint32_t)dma_data->dma_buffer;
			dma_cfg->block_count = 1;
			dma_cfg->head_block = &blocks[0];
			goto dma_configure;
		}

		for (size_t i = 0; i < bufs->count; i++) {
			const struct spi_buf *sb = &bufs->buffers[i];

			if (sb->len == 0) {
				continue;
			}
			if (nblocks >= SPI_QCC74X_DMA_MAX_BLOCKS) {
				LOG_ERR("RX block count exceeds max %d", SPI_QCC74X_DMA_MAX_BLOCKS);
				return -ENOMEM;
			}
			blocks[nblocks].block_size = sb->len * word_bytes;
			blocks[nblocks].source_address = dma->per_fifo_addr;

			if (sb->buf != NULL) {
				if (use_bounce) {
					blocks[nblocks].dest_address =
						(uint32_t)(dma_data->dma_buffer + bounce_offset);
					bounce_offset += sb->len;
				} else {
					if (!qc7xx_check_cache_addr_aligned((uintptr_t)sb->buf)) {
						LOG_ERR("RX segment requires bounce buffer");
						return -EINVAL;
					}
					blocks[nblocks].dest_address = (uint32_t)sb->buf;
				}
			} else {
				/* NULL RX segment — route to discard buffer */
				blocks[nblocks].dest_address = (uint32_t)dma_data->discard_dma_buf;
			}
			nblocks++;
		}

		/* Link the chain */
		for (size_t i = 0; i + 1 < nblocks; i++) {
			blocks[i].next_block = &blocks[i + 1];
		}
		dma_cfg->block_count = nblocks;
		dma_cfg->head_block = &blocks[0];
	}

dma_configure:
	ret = dma_config(dma->dev, dma->channel, dma_cfg);
	if (ret < 0) {
		LOG_ERR("dma_config %p failed %d", dma->dev, ret);
		return ret;
	}

	ret = dma_start(dma->dev, dma->channel);
	if (ret < 0) {
		LOG_ERR("dma_start %p failed %d", dma->dev, ret);
		return ret;
	}
	return 0;
}

/**
 * @brief Start DMA transfer using multi-block scatter-gather.
 *
 * Builds a full DMA chain for each direction from the buffer sets,
 * issues a single dma_config() + dma_start() per direction.
 *
 * @param dev      SPI device.
 * @param tx_bufs  TX buffer set.
 * @param rx_bufs  RX buffer set.
 *
 * @retval 0       On success.
 * @retval -errno  On failure.
 */
static int spi_qcc74x_start_dma_transceive(const struct device *dev,
					 const struct spi_buf_set *tx_bufs,
					 const struct spi_buf_set *rx_bufs)
{
	const struct spi_qcc74x_config *config = dev->config;
	struct spi_qcc74x_data *data = dev->data;
	const size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
	const size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);
	const bool tx_dma_active = (total_tx > 0U) || (total_rx > 0U);
	/*
	 * RX DMA must be active whenever TX is active: the SPI peripheral
	 * always shifts data into the RX FIFO during transmission.  Without
	 * RX DMA draining it, the 32-byte FIFO overflows and halts SPI.
	 * The RX setup path already handles the TX-only case by routing
	 * received bytes into a discard buffer.
	 */
	const bool rx_dma_active = tx_dma_active;
	int ret = 0;

	/* enable dma */
	qc7xx_spi_link_txdma(&data->spi_hal_device, tx_dma_active);
	qc7xx_spi_link_rxdma(&data->spi_hal_device, rx_dma_active);

	/*
	 * Arm RX first, then TX.
	 * For short transfers TX can complete quickly; if RX is not active yet,
	 * the initial clocks are missed and RX DMA may wait forever.
	 */
	if (rx_dma_active) {
		ret = spi_qcc74x_dma_setup(dev, RX, tx_bufs, rx_bufs);
		if (ret < 0) {
			goto on_error;
		}
	}

	if (tx_dma_active) {
		ret = spi_qcc74x_dma_setup(dev, TX, tx_bufs, rx_bufs);
		if (ret < 0) {
			goto on_error;
		}
	}

	return 0;

on_error:
	for (size_t i = 0; i < dma_enabled_num(dev); i++) {
		dma_stop(config->dma[i].dev, config->dma[i].channel);
	}
	return ret;
}

/**
 * @brief Wait till the whole transfer finishes
 *
 * @param data Pointer to device data structure.
 *
 * @retval 0  On success.
 */
int spi_qcc74x_wait_for_completion(const struct device *dev, struct spi_qcc74x_data *data)
{
	if (dma_enabled(dev)) {
		/*
		 * DMA completion only means memory/FIFO movement finished.
		 * Also wait for the SPI transfer-complete interrupt so master
		 * mode does not return before the last frame has fully shifted.
		 */
		k_sem_take(&data->complete_sync, K_FOREVER);
	}

	int ret = spi_context_wait_for_completion(&data->ctx);

	if (ret == -ETIMEDOUT) {
		const struct spi_qcc74x_config *config = dev->config;
		uint32_t fifo_sts = sys_read32(config->base_reg + SPI_FIFO_CONFIG_0_OFFSET);
		uint32_t fifo_cfg1 = sys_read32(config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);
		uint32_t spi_cfg = sys_read32(config->base_reg + SPI_CONFIG_OFFSET);
		uint32_t rx_cnt = (fifo_cfg1 & SPI_RX_FIFO_CNT_MASK) >> SPI_RX_FIFO_CNT_SHIFT;
		uint32_t tx_cnt = (fifo_cfg1 & SPI_TX_FIFO_CNT_MASK) >> SPI_TX_FIFO_CNT_SHIFT;

		LOG_ERR("SPI regs: cfg=0x%08x fifo0=0x%08x fifo1=0x%08x tx_cnt=%u rx_cnt=%u",
			spi_cfg, fifo_sts, fifo_cfg1, tx_cnt, rx_cnt);

		for (size_t i = 0; i < dma_enabled_num(dev); i++) {
			struct dma_status st = {0};

			if (dma_get_status(config->dma[i].dev, config->dma[i].channel, &st) == 0) {
				LOG_ERR("DMA timeout diag ch%u busy=%u pending=%u count=%u",
					(unsigned int)config->dma[i].channel, st.busy,
					st.pending_length, data->dma[i].count);
			} else {
				LOG_ERR("DMA timeout diag ch%u status read failed",
					(unsigned int)config->dma[i].channel);
			}
		}
		if (fifo_sts & SPI_FIFO_RX_OVERFLOW) {
			LOG_ERR("RX FIFO overflow detected");
		}
		if (fifo_sts & SPI_FIFO_TX_UNDERFLOW) {
			LOG_ERR("TX FIFO underflow detected");
		}
	}

	return ret;
}

#endif /* CONFIG_SPI_QCC74X_DMA */

#ifdef CONFIG_SPI_QCC74X_INTERRUPT

/**
 * @brief Check if transfer is still ongoing
 *
 * Transfer is ongoing if there's still TX/RX data OR dummy bytes to send.
 *
 * @param data Pointer to the SPI driver data structure
 *
 * @return true if transfer is ongoing, false otherwise
 */
static inline bool spi_qcc74x_transfer_ongoing(struct spi_qcc74x_data *data)
{
	return spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx) ||
	       data->dummy_tx_len > 0;
}

/**
 * @brief Check for SPI errors
 *
 * Check errors first in ISR to prevent cascading failures.
 * Reads spi_int_sts and spi_fifo_config_0 for error conditions.
 *
 * @return 0 on success, negative errno on error
 */
static int spi_qcc74x_get_err(const struct device *dev)
{
	const struct spi_qcc74x_config *config = dev->config;
	uint32_t int_sts = sys_read32(config->base_reg + SPI_INT_STS_OFFSET);
	uint32_t fifo_sts = sys_read32(config->base_reg + SPI_FIFO_CONFIG_0_OFFSET);

	/* Check FIFO error interrupt (bit 5: spi_fer_int) */
	if (int_sts & SPI_INT_FER) {
		/* Determine specific error from spi_fifo_config_0 */
		if (fifo_sts & SPI_FIFO_RX_OVERFLOW) {
			LOG_ERR("RX FIFO overflow - data lost");
		}
		if (fifo_sts & SPI_FIFO_TX_UNDERFLOW) {
			LOG_ERR("TX FIFO underflow");
		}
		/* Clear by clearing FIFOs */
		clear_fifos(config->base_reg);
		return -EIO;
	}

	/* Check TX underrun (bit 4: spi_txu_int) - slave mode */
	if (int_sts & SPI_INT_TXU) {
		LOG_ERR("Slave TX underrun - TX not ready");
		/* Clear by writing 1 to cr_spi_txu_clr (bit 20) */
		sys_write32(SPI_CLR_TXU, config->base_reg + SPI_INT_STS_OFFSET);
		return -EIO;
	}

	/* Check slave timeout (bit 3: spi_sto_int) - not fatal, just warn */
	if (int_sts & SPI_INT_STO) {
		LOG_WRN("Slave timeout - no clock received");
		/* Clear by writing 1 to cr_spi_sto_clr (bit 19) */
		sys_write32(SPI_CLR_STO, config->base_reg + SPI_INT_STS_OFFSET);
		/* Continue - timeout may be expected in some protocols */
	}

	return 0;
}

/**
 * @brief Complete interrupt-driven transfer
 *
 * Mask all interrupts before signaling completion.
 * Handles both sync (semaphore) and async (callback) completion.
 *
 * @param dev SPI device
 * @param status 0 on success, negative errno on error
 */
static void spi_qcc74x_int_complete(const struct device *dev, int status)
{
	const struct spi_qcc74x_config *config = dev->config;
	struct spi_qcc74x_data *data = dev->data;
	uint32_t reg_val;

	/*
	 * Mask all interrupt sources by setting mask bits to 1.
	 * Read-modify-write to preserve enable bits and other fields.
	 */
	reg_val = sys_read32(config->base_reg + SPI_INT_STS_OFFSET);
	reg_val |= (SPI_MASK_TXF | SPI_MASK_RXF | SPI_MASK_FER | SPI_MASK_TXU);
	sys_write32(reg_val, config->base_reg + SPI_INT_STS_OFFSET);

	/* Mark not busy */
	data->busy = false;

	/* Cleanup for async transfers (sync does this in transceive function) */
#ifdef CONFIG_SPI_ASYNC
	if (data->ctx.asynchronous) {
		spi_context_cs_control(&data->ctx, false);
		disable_spi(config->base_reg);
	}
#endif

	/*
	 * Signal completion via spi_context:
	 * - Sync: gives semaphore, wakes waiting thread
	 * - Async: calls user callback, releases lock
	 */
	spi_context_complete(&data->ctx, dev, status);
}

/**
 * @brief Read data from RX FIFO into buffer
 *
 * Reads from spi_fifo_rdata while rx_fifo_cnt > 0.
 * Advances spi_context buffer pointer after each frame.
 */
static void spi_qcc74x_shift_fifo_rx(const struct device *dev)
{
	struct spi_qcc74x_data *data = dev->data;
	const struct spi_qcc74x_config *config = dev->config;
	const uint8_t frame_size = get_frame_size_bytes(config->base_reg);
	uint8_t rx_cnt, tx_cnt;

	get_fifo_counts(config->base_reg, &rx_cnt, &tx_cnt);

	/* Read while FIFO has data (rx_fifo_cnt > 0) and we need more */
	while (rx_cnt > 0 && spi_context_rx_on(&data->ctx)) {
		uint32_t frame = sys_read32(config->base_reg + SPI_FIFO_RDATA_OFFSET);

		/* Only store if buffer provided (not NULL RX) */
		if (spi_context_rx_buf_on(&data->ctx)) {
			frame_to_buffer(config->base_reg, frame, (void *)data->ctx.rx_buf);
		}

		/* Advance context pointer and decrement remaining count */
		spi_context_update_rx(&data->ctx, 1, frame_size);
		rx_cnt--;
	}
}

/**
 * @brief Write data from buffer to TX FIFO
 *
 * Writes to spi_fifo_wdata while tx_fifo_cnt > 0 (space available).
 * Advances spi_context buffer pointer after each frame.
 *
 * In master mode, we must continue sending dummy bytes when TX is exhausted
 * but RX still expects more data, since the master generates the clock.
 */
static void spi_qcc74x_shift_fifo_tx(const struct device *dev)
{
	struct spi_qcc74x_data *data = dev->data;
	const struct spi_qcc74x_config *config = dev->config;
	const uint8_t frame_size = get_frame_size_bytes(config->base_reg);
	uint8_t rx_cnt, tx_cnt;

	get_fifo_counts(config->base_reg, &rx_cnt, &tx_cnt);

	/* Write while FIFO has space (tx_fifo_cnt > 0) and we have TX data */
	while (tx_cnt > 0 && spi_context_tx_on(&data->ctx)) {
		uint32_t frame = 0;

		/* Only read from buffer if provided (not NULL TX) */
		if (spi_context_tx_buf_on(&data->ctx)) {
			frame = buffer_to_frame(config->base_reg, (void *)data->ctx.tx_buf);
		}

		sys_write32(frame, config->base_reg + SPI_FIFO_WDATA_OFFSET);

		/* Advance context pointer and decrement remaining count */
		spi_context_update_tx(&data->ctx, 1, frame_size);
		tx_cnt--;
	}

	/*
	 * In master mode, if TX is exhausted but we still need to send dummy
	 * bytes to generate clock cycles for RX (rx_len > tx_len case).
	 * Use pre-calculated dummy_tx_len to avoid sending too many.
	 */
	while (tx_cnt > 0 && data->dummy_tx_len > 0) {
		/* Send dummy byte (0x00) to generate clock */
		sys_write32(0, config->base_reg + SPI_FIFO_WDATA_OFFSET);
		data->dummy_tx_len--;
		tx_cnt--;
	}
}

/**
 * @brief Configure FIFO thresholds for interrupt mode
 *
 * Configures spi_fifo_config_1 register:
 * - tx_fifo_th (bits 20:16): TX threshold
 * - rx_fifo_th (bits 28:24): RX threshold
 */
static void spi_qcc74x_setup_int_fifos(const struct device *dev)
{
	const struct spi_qcc74x_config *config = dev->config;
	uint32_t reg_val;
	uint8_t rx_cnt, tx_cnt;

	/*
	 * Wait for SPI to be idle before clearing FIFOs.
	 * This ensures any in-flight byte in the shift register completes
	 * and gets pushed to the FIFO before we clear it.
	 */
	(void)check_spi_status(config->base_reg, true);

	/* Clear FIFOs */
	clear_fifos(config->base_reg);

	/*
	 * Drain any stale RX data that might remain after FIFO clear.
	 * This catches bytes that were in-flight during the clear operation.
	 */
	get_fifo_counts(config->base_reg, &rx_cnt, &tx_cnt);
	while (rx_cnt > 0) {
		/* Read and discard any stale data */
		(void)sys_read32(config->base_reg + SPI_FIFO_RDATA_OFFSET);
		get_fifo_counts(config->base_reg, &rx_cnt, &tx_cnt);
	}

	/* Configure thresholds in spi_fifo_config_1 */
	reg_val = sys_read32(config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);

	/* Clear threshold fields */
	reg_val &= ~(SPI_TX_FIFO_TH_MASK | SPI_RX_FIFO_TH_MASK);

	/* Set TX threshold */
	reg_val |= (CONFIG_SPI_QCC74X_INTERRUPT_THRESHOLD_TX << SPI_TX_FIFO_TH_SHIFT);

	/* Set RX threshold */
	reg_val |= (CONFIG_SPI_QCC74X_INTERRUPT_THRESHOLD_RX << SPI_RX_FIFO_TH_SHIFT);

	sys_write32(reg_val, config->base_reg + SPI_FIFO_CONFIG_1_OFFSET);

	LOG_DBG("FIFO thresholds: tx_fifo_th=%d, rx_fifo_th=%d",
		CONFIG_SPI_QCC74X_INTERRUPT_THRESHOLD_TX, CONFIG_SPI_QCC74X_INTERRUPT_THRESHOLD_RX);
}

/**
 * @brief Unmask SPI interrupts for transfer
 *
 * Configures spi_int_sts register mask bits:
 * - cr_spi_txf_mask (bit 9): 0 = unmask TX FIFO interrupt
 * - cr_spi_rxf_mask (bit 10): 0 = unmask RX FIFO interrupt
 * - cr_spi_fer_mask (bit 13): 0 = unmask FIFO error interrupt
 * - cr_spi_txu_mask (bit 12): 0 = unmask TX underrun (slave mode)
 */
static void spi_qcc74x_enable_interrupts(const struct device *dev)
{
	const struct spi_qcc74x_config *config = dev->config;
	struct spi_qcc74x_data *data = dev->data;
	uint32_t reg_val;

	reg_val = sys_read32(config->base_reg + SPI_INT_STS_OFFSET);

	/* Unmask (enable) TX FIFO, RX FIFO, and error interrupts */
	reg_val &= ~(SPI_MASK_TXF | SPI_MASK_RXF | SPI_MASK_FER);

	/* Also unmask TX underrun for slave mode error detection */
	if (spi_context_is_slave(&data->ctx)) {
		reg_val &= ~SPI_MASK_TXU;
	}

	sys_write32(reg_val, config->base_reg + SPI_INT_STS_OFFSET);
}

/**
 * @brief Start interrupt-driven SPI transfer
 *
 * Sequence:
 * 1. Mark busy (for ISR to know transfer is active)
 * 2. Setup FIFO thresholds
 * 3. Prime TX FIFO with initial data
 * 4. Unmask (enable) interrupts
 *
 * @return 0 on success, negative errno on error
 */
static int spi_qcc74x_interrupt_transceive(const struct device *dev)
{
	struct spi_qcc74x_data *data = dev->data;

	/* Check if there's anything to transfer */
	if (!spi_qcc74x_transfer_ongoing(data)) {
		LOG_DBG("No data to transfer");
		return 0;
	}

	/* Mark as busy for ISR */
	data->busy = true;

	/* Setup FIFOs with interrupt-optimized thresholds */
	spi_qcc74x_setup_int_fifos(dev);

	/*
	 * Prime TX FIFO before enabling interrupts.
	 * This avoids an immediate TX interrupt on empty FIFO.
	 * The ISR will continue filling as space becomes available.
	 */
	spi_qcc74x_shift_fifo_tx(dev);

	/* Unmask (enable) interrupts - transfer starts when SPI enabled */
	spi_qcc74x_enable_interrupts(dev);

	LOG_DBG("Interrupt transfer started: tx_len=%zu, rx_len=%zu", data->ctx.tx_len,
		data->ctx.rx_len);

	return 0;
}

#endif /* CONFIG_SPI_QCC74X_INTERRUPT */

static int spi_qcc74x_transceive_sync(const struct device *dev, const struct spi_config *spi_config,
				    const struct spi_buf_set *tx_bufs,
				    const struct spi_buf_set *rx_bufs)
{
	const struct spi_qcc74x_config *const config = dev->config;
	struct spi_qcc74x_data *const data = dev->data;
	struct spi_buf_set empty_buf_set = {0};
	uint8_t frame_size_bytes = 0U;
	int32_t result = 0;

	spi_context_lock(&data->ctx, false, NULL, NULL, spi_config);

	result = spi_qcc74x_configure(dev, spi_config);
	if (result != 0) {
		LOG_ERR("Failed to configure SPI: %d", result);
		spi_context_release(&data->ctx, result);
		return result;
	}

	if (rx_bufs == NULL) {
		rx_bufs = &empty_buf_set;
	}
	if (tx_bufs == NULL) {
		tx_bufs = &empty_buf_set;
	}

	LOG_DBG("Starting SPI transfer: tx_bufs=%u, rx_bufs=%u", tx_bufs->count, rx_bufs->count);

	frame_size_bytes = get_frame_size_bytes(config->base_reg);

	/* Prepare for transfer */
	disable_spi(config->base_reg);
	clear_fifos(config->base_reg);
	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

	/*
	 * Configure RX ignore per transfer (not only per SPI config).
	 * spi_qcc74x_configure() may be skipped when config is unchanged, so this
	 * bit must be refreshed here to avoid carrying TX-only state into RX ops.
	 */
	{
		uint32_t reg_val = sys_read32(config->base_reg + SPI_CONFIG_OFFSET);

		/*
		 * In DMA mode, never enable SPI_CR_SPI_RXD_IGNR_EN.
		 * RX DMA with a discard buffer drains the FIFO for
		 * TX-only transfers instead.  SPI_CR_SPI_RXD_IGNR_EN
		 * can interfere with the DMA handshake and FIFO state.
		 */
		bool use_rx_ignore = (data->ctx.rx_len == 0U);
#ifdef CONFIG_SPI_QCC74X_DMA
		if (dma_enabled(dev)) {
			use_rx_ignore = false;
		}
#endif
		if (use_rx_ignore) {
			reg_val |= SPI_CR_SPI_RXD_IGNR_EN;
		} else {
			reg_val &= ~SPI_CR_SPI_RXD_IGNR_EN;
		}
		sys_write32(reg_val, config->base_reg + SPI_CONFIG_OFFSET);
	}

	spi_context_cs_control(&data->ctx, true);
	if (SPI_OP_MODE_GET(spi_config->operation) == SPI_OP_MODE_MASTER) {
		qc7xx_spi_feature_control(&data->spi_hal_device, SPI_CMD_SET_ROLE, SPI_ROLE_MASTER);
	} else {
		qc7xx_spi_feature_control(&data->spi_hal_device, SPI_CMD_SET_ROLE, SPI_ROLE_SLAVE);
	}

#ifdef CONFIG_SPI_QCC74X_DMA
	if (dma_enabled(dev)) {
		const size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
		const size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);
		const size_t total_rx_nonnull = spi_qcc74x_buf_set_total_nonnull_len(rx_bufs);
		const bool force_single_block =
			IS_ENABLED(CONFIG_SPI_QCC74X_DMA_FORCE_SINGLE_BLOCK_TEST);
		const bool rx_has_null_segments = spi_qcc74x_buf_set_has_null_segments(rx_bufs);
		bool tx_needs_bounce = false;
		bool rx_needs_bounce = false;

		data->dma_done = false;
		k_sem_reset(&data->complete_sync);
		atomic_set(&data->slave_cs_seen, 0);
		k_work_cancel_delayable(&data->slave_dma_fallback_work);

		for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
			data->dma[i].count = 0;
			data->dma[i].aligned_buf = NULL;
			data->dma[i].dma_buffer = NULL;
		}

		if ((total_tx == 0U) && (total_rx == 0U)) {
			result = 0;
			goto dma_error;
		}

		if (force_single_block) {
			/* Force TX to one contiguous DMA block for test coverage. */
			tx_needs_bounce = ((total_tx > 0U) || (total_rx > 0U));
			/*
			 * RX can be forced to a single block only when no NULL-gapped
			 * segments are present.
			 */
			if (!rx_has_null_segments) {
				rx_needs_bounce = (total_rx_nonnull > 0U);
			} else {
				LOG_WRN("Single-block test mode: keep multi-block RX due to NULL "
					"segments");
			}
		} else {
			/*
			 * TX bounce buffer is required when:
			 * - any TX segment is NULL,
			 * - any TX segment is cache-line unaligned,
			 * - any TX segment is in cache-mapped memory,
			 * - total_tx < total_rx (TX data + pad clocks).
			 *
			 * DMA direct access to cache-mapped addresses is fragile on
			 * this platform, so use a DMA-safe no-cache bounce buffer.
			 */
			for (size_t i = 0;
			     (tx_bufs != NULL) && (i < tx_bufs->count) && !tx_needs_bounce; i++) {
				if ((tx_bufs->buffers[i].buf == NULL &&
				     tx_bufs->buffers[i].len > 0) ||
				    (tx_bufs->buffers[i].buf != NULL &&
				     (!qc7xx_check_cache_addr_aligned(
					      (uintptr_t)tx_bufs->buffers[i].buf) ||
				      qc7xx_check_cache_addr(tx_bufs->buffers[i].buf)))) {
					tx_needs_bounce = true;
				}
			}
			if (total_tx < total_rx) {
				tx_needs_bounce = true;
			}

			/*
			 * RX bounce buffer is required when any non-NULL RX segment
			 * is cache-line unaligned or cache-mapped.
			 */
			for (size_t i = 0;
			     (rx_bufs != NULL) && (i < rx_bufs->count) && !rx_needs_bounce; i++) {
				if (rx_bufs->buffers[i].buf != NULL &&
				    (!qc7xx_check_cache_addr_aligned(
					     (uintptr_t)rx_bufs->buffers[i].buf) ||
				     qc7xx_check_cache_addr(rx_bufs->buffers[i].buf))) {
					rx_needs_bounce = true;
				}
			}
		}

		if (tx_needs_bounce) {
			size_t bounce_len = MAX(total_tx, total_rx);

			data->dma[TX].aligned_buf =
				k_aligned_alloc(QC7XX_CACHE_LINE_SIZE, bounce_len);
			if (data->dma[TX].aligned_buf == NULL) {
				LOG_ERR("Failed to allocate %zu-byte TX bounce buffer", bounce_len);
				result = -ENOMEM;
				goto dma_error;
			}
			data->dma[TX].dma_buffer = qc7xx_get_no_cache_addr(
				(const void *)(uintptr_t)data->dma[TX].aligned_buf);
			qc7xx_l1c_dcache_invalidate_range(
				(void *)(uintptr_t)data->dma[TX].dma_buffer, bounce_len);
			memset((void *)data->dma[TX].dma_buffer, 0, bounce_len);

			/* Copy all TX segments into the bounce buffer contiguously */
			size_t offset = 0;

			for (size_t i = 0; (tx_bufs != NULL) && (i < tx_bufs->count); i++) {
				if (tx_bufs->buffers[i].buf != NULL &&
				    tx_bufs->buffers[i].len > 0) {
					memcpy(data->dma[TX].dma_buffer + offset,
					       tx_bufs->buffers[i].buf, tx_bufs->buffers[i].len);
				}
				offset += tx_bufs->buffers[i].len;
			}

#ifdef CONFIG_SPI_QCC74X_DMA_SD_TX_CRC_DEBUG
			spi_qcc74x_log_sd_write_crc_once(data->dma[TX].dma_buffer, total_tx,
						       total_rx);
#endif
		}

		if (rx_needs_bounce) {
			data->dma[RX].aligned_buf =
				k_aligned_alloc(QC7XX_CACHE_LINE_SIZE, total_rx_nonnull);
			if (data->dma[RX].aligned_buf == NULL) {
				LOG_ERR("Failed to allocate %zu-byte RX bounce buffer",
					total_rx_nonnull);
				result = -ENOMEM;
				goto dma_error;
			}
			data->dma[RX].dma_buffer = qc7xx_get_no_cache_addr(
				(const void *)(uintptr_t)data->dma[RX].aligned_buf);
			qc7xx_l1c_dcache_invalidate_range(
				(void *)(uintptr_t)data->dma[RX].dma_buffer, total_rx_nonnull);
			memset((void *)data->dma[RX].dma_buffer, 0, total_rx_nonnull);
		}

		LOG_DBG("DMA xfer: tx=%zu rx=%zu tx_bounce=%d rx_bounce=%d", total_tx, total_rx,
			(data->dma[TX].dma_buffer != NULL), (data->dma[RX].dma_buffer != NULL));

		result = spi_qcc74x_start_dma_transceive(dev, tx_bufs, rx_bufs);
		if (result < 0) {
			LOG_ERR("dma error");
			goto dma_error;
		}
		result = spi_qcc74x_wait_for_completion(dev, data);
		/*
		 * DMA completion only means bytes were moved between memory and FIFO.
		 * Ensure the SPI engine has fully shifted out trailing bytes before
		 * FIFO clear/reset in dma_error cleanup.
		 */
		if (result == 0) {
			if (!check_spi_status(config->base_reg, true)) {
				LOG_ERR("DMA wait for SPI idle failed");
				result = -ETIMEDOUT;
			}
		}
	}

dma_error:
	if (dma_enabled(dev)) {
		/* disable dma */
		qc7xx_spi_link_txdma(&data->spi_hal_device, false);
		qc7xx_spi_link_rxdma(&data->spi_hal_device, false);
		qc7xx_spi_feature_control(&data->spi_hal_device, SPI_CMD_CLEAR_TX_FIFO, 0);
		qc7xx_spi_feature_control(&data->spi_hal_device, SPI_CMD_CLEAR_RX_FIFO, 0);

		/* Copy back from RX bounce buffer to original user buffers */
		if (result >= 0 && data->dma[RX].dma_buffer) {
			size_t offset = 0;

			for (size_t i = 0; i < rx_bufs->count; i++) {
				if (rx_bufs->buffers[i].buf != NULL &&
				    rx_bufs->buffers[i].len > 0) {
					memcpy(rx_bufs->buffers[i].buf,
					       data->dma[RX].dma_buffer + offset,
					       rx_bufs->buffers[i].len);
					offset += rx_bufs->buffers[i].len;
				}
			}
		}

		for (size_t i = 0; i < dma_enabled_num(dev); i++) {
			dma_stop(config->dma[i].dev, config->dma[i].channel);
			if (data->dma[i].aligned_buf) {
				k_free(data->dma[i].aligned_buf);
				data->dma[i].aligned_buf = NULL;
				data->dma[i].dma_buffer = NULL;
			}
		}
	}
#elif defined(CONFIG_SPI_QCC74X_INTERRUPT) && defined(CONFIG_SPI_QCC74X_INTERRUPT_SYNC)
	/*
	 * Calculate how many dummy TX bytes needed for rx_len > tx_len case.
	 * In master mode, we must generate clock cycles for all RX bytes.
	 */
	{
		size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
		size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);

		data->dummy_tx_len = (total_rx > total_tx) ? (total_rx - total_tx) : 0;
	}

	/* Start interrupt-driven transfer */
	result = spi_qcc74x_interrupt_transceive(dev);
	if (result != 0) {
		LOG_ERR("Failed to start interrupt transfer: %d", result);
		goto cleanup;
	}

	/* Wait for completion (blocks for sync, returns immediately for async) */
	result = spi_context_wait_for_completion(&data->ctx);
	if (result != 0) {
		LOG_ERR("Interrupt transfer failed: %d", result);
	}
#else
	/* Polling mode: process buffers using spi_context helpers */
	size_t dummy_tx_len = 0U;

	/*
	 * For rx_len > tx_len, send only the exact number of dummy frames needed.
	 * Without this cap, pre-filling TX FIFO can generate extra clocks.
	 */
	{
		const size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
		const size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);

		dummy_tx_len = (total_rx > total_tx) ? (total_rx - total_tx) : 0U;
	}

	while (spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx)) {
		uint8_t rx_fifo_avail_cnt = 0U;
		uint8_t tx_fifo_free_cnt = 0U;

		get_fifo_counts(config->base_reg, &rx_fifo_avail_cnt, &tx_fifo_free_cnt);

		/* Drain RX FIFO: copy into RX context when active, otherwise discard. */
		while (rx_fifo_avail_cnt > 0U) {
			const uint32_t frame_data =
				sys_read32(config->base_reg + SPI_FIFO_RDATA_OFFSET);
			if (spi_context_rx_on(&data->ctx)) {
				if (spi_context_rx_buf_on(&data->ctx)) {
					frame_to_buffer(config->base_reg, frame_data,
							(void *)data->ctx.rx_buf);
				}
				spi_context_update_rx(&data->ctx, 1, frame_size_bytes);
			}
			rx_fifo_avail_cnt--;
		}

		/* Fill TX FIFO: write all available slots */
		while (tx_fifo_free_cnt > 0U) {
			if (spi_context_tx_on(&data->ctx)) {
				uint32_t frame = 0U;

				if (spi_context_tx_buf_on(&data->ctx)) {
					frame = buffer_to_frame(config->base_reg,
								(void *)data->ctx.tx_buf);
				}
				sys_write32(frame, config->base_reg + SPI_FIFO_WDATA_OFFSET);
				spi_context_update_tx(&data->ctx, 1, frame_size_bytes);
			} else if (spi_context_rx_on(&data->ctx) && dummy_tx_len > 0U) {
				/* TX done, RX pending: send dummy clocks. */
				sys_write32(0U, config->base_reg + SPI_FIFO_WDATA_OFFSET);
				dummy_tx_len = (dummy_tx_len > frame_size_bytes)
						       ? (dummy_tx_len - frame_size_bytes)
						       : 0U;
			} else {
				/* Both TX and RX complete; nothing to transmit */
				break;
			}
			tx_fifo_free_cnt--;
		}
	}

	if (result == 0) {
		result = wait_tx_complete(config->base_reg);
		if (result != 0) {
			LOG_ERR("TX completion timeout: %d", result);
			k_msleep(300);
		}
	}
#endif /* CONFIG_SPI_QCC74X_DMA */

#if defined(CONFIG_SPI_QCC74X_INTERRUPT) && defined(CONFIG_SPI_QCC74X_INTERRUPT_SYNC)
cleanup:
	/*
	 * Cleanup handling for interrupt mode:
	 * - Sync: always cleanup here (ISR already completed)
	 * - Async success: ISR handles cleanup (transfer still ongoing)
	 * - Async error: cleanup here
	 */
#ifdef CONFIG_SPI_ASYNC
	if (!data->ctx.asynchronous || result != 0) {
		spi_context_cs_control(&data->ctx, false);
		disable_spi(config->base_reg);
	}
#else
	spi_context_cs_control(&data->ctx, false);
	disable_spi(config->base_reg);
#endif /* CONFIG_SPI_ASYNC */
#else  /* !CONFIG_SPI_QCC74X_INTERRUPT */
	spi_context_cs_control(&data->ctx, false);
	disable_spi(config->base_reg);
#endif /* CONFIG_SPI_QCC74X_INTERRUPT && CONFIG_SPI_QCC74X_INTERRUPT_SYNC */

	if (result >= 0) {
		LOG_DBG("SPI transfer completed successfully");
	} else {
		LOG_ERR("SPI transfer failed with error: %d", result);
	}

	spi_context_release(&data->ctx, result);
	return result;
}

static int spi_qcc74x_release(const struct device *dev, const struct spi_config *spi_config)
{
	struct spi_qcc74x_data *const data = dev->data;

	ARG_UNUSED(spi_config);

	spi_context_unlock_unconditionally(&data->ctx);
	LOG_DBG("SPI device released");
	return 0;
}

#if defined(CONFIG_SPI_ASYNC) && defined(CONFIG_SPI_QCC74X_INTERRUPT)

static int spi_qcc74x_transceive_async(const struct device *dev, const struct spi_config *spi_config,
				     const struct spi_buf_set *tx_bufs,
				     const struct spi_buf_set *rx_bufs, spi_callback_t cb,
				     void *userdata)
{
	const struct spi_qcc74x_config *const config = dev->config;
	struct spi_qcc74x_data *const data = dev->data;
	struct spi_buf_set empty_buf_set = {0};
	int32_t result = 0;

	/* Lock context with async=true, which sets ctx.asynchronous */
	spi_context_lock(&data->ctx, true, cb, userdata, spi_config);

	result = spi_qcc74x_configure(dev, spi_config);
	if (result != 0) {
		LOG_ERR("Failed to configure SPI: %d", result);
		spi_context_release(&data->ctx, result);
		return result;
	}

	if (rx_bufs == NULL) {
		rx_bufs = &empty_buf_set;
	}
	if (tx_bufs == NULL) {
		tx_bufs = &empty_buf_set;
	}

	LOG_DBG("Starting async SPI transfer: tx_bufs=%u, rx_bufs=%u", tx_bufs->count,
		rx_bufs->count);

	/* Prepare for transfer */
	disable_spi(config->base_reg);
	clear_fifos(config->base_reg);
	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);
	spi_context_cs_control(&data->ctx, true);

	if (SPI_OP_MODE_GET(spi_config->operation) == SPI_OP_MODE_MASTER) {
		qc7xx_spi_feature_control(&data->spi_hal_device, SPI_CMD_SET_ROLE, SPI_ROLE_MASTER);
	} else {
		qc7xx_spi_feature_control(&data->spi_hal_device, SPI_CMD_SET_ROLE, SPI_ROLE_SLAVE);
	}

	/*
	 * Calculate how many dummy TX bytes needed for rx_len > tx_len case.
	 * In master mode, we must generate clock cycles for all RX bytes.
	 */
	{
		size_t total_tx = spi_qcc74x_buf_set_total_len(tx_bufs);
		size_t total_rx = spi_qcc74x_buf_set_total_len(rx_bufs);

		data->dummy_tx_len = (total_rx > total_tx) ? (total_rx - total_tx) : 0;
	}

	/* Start interrupt-driven transfer */
	result = spi_qcc74x_interrupt_transceive(dev);
	if (result != 0) {
		LOG_ERR("Failed to start async interrupt transfer: %d", result);
		spi_context_cs_control(&data->ctx, false);
		disable_spi(config->base_reg);
		spi_context_release(&data->ctx, result);
		return result;
	}

	/*
	 * For async: spi_context_wait_for_completion returns immediately.
	 * The ISR will call spi_context_complete() which invokes the callback.
	 */
	result = spi_context_wait_for_completion(&data->ctx);

	/*
	 * For async mode with successful start, result is 0 and transfer is ongoing.
	 * Don't cleanup here - ISR will handle it.
	 * If result != 0, it means something failed before the async wait.
	 */
	if (result != 0) {
		spi_context_cs_control(&data->ctx, false);
		disable_spi(config->base_reg);
		spi_context_release(&data->ctx, result);
	}

	return result;
}
#endif /* CONFIG_SPI_ASYNC && CONFIG_SPI_QCC74X_INTERRUPT */

#ifdef CONFIG_SPI_QCC74X_DMA

/**
 * @brief Mark the transfer as complete.
 *
 * @param dev    Pointer to device structure.
 * @param status Transfer status.
 */
static void spi_qcc74x_complete(const struct device *dev, int status)
{
	struct spi_qcc74x_data *data = dev->data;

	if (dma_enabled(dev)) {
		if (data->dma_done) {
			return;
		}
		data->dma_done = true;

		const struct spi_qcc74x_config *config = dev->config;

		for (size_t i = 0; i < dma_enabled_num(dev); i++) {
			dma_stop(config->dma[i].dev, config->dma[i].channel);
		}
	}

	spi_context_complete(&data->ctx, dev, status);
}

/**
 * @brief Handle DMA transfer completion for a single channel.
 *
 * With multi-block scatter-gather, DMA_STATUS_COMPLETE means the entire
 * chain finished.  We track per-direction completions and signal overall
 * completion when both TX and RX are done (or only the relevant direction
 * when one side has no buffers).
 *
 * @param spi_dev        Pointer to SPI device structure.
 * @param dma_dev        Pointer to DMA device structure.
 * @param channel        DMA channel number.
 */
static void spi_qcc74x_finish_dma_transfer(const struct device *spi_dev, const struct device *dma_dev,
					 uint32_t channel)
{
	const struct spi_qcc74x_config *config = spi_dev->config;
	struct spi_qcc74x_data *data = spi_dev->data;

	/* Mark the completed direction */
	for (size_t i = 0; i < ARRAY_SIZE(config->dma); i++) {
		if (dma_dev == config->dma[i].dev && channel == config->dma[i].channel) {
			data->dma[i].count = 1; /* non-zero = done */
		}
	}

	/*
	 * Both directions complete (or only the active one)?
	 * TX-only: rx_len == 0, so RX DMA may not fire.
	 * RX-only: tx still fires (generates clocks), so wait for both.
	 */
	bool tx_done = (data->dma[TX].count > 0) || (data->ctx.tx_len == 0);
	bool rx_done = (data->dma[RX].count > 0) || (data->ctx.rx_len == 0);

	if (rx_done && spi_context_is_slave(&data->ctx)) {
		spi_context_update_rx(&data->ctx, 1, data->ctx.rx_len);
	}

	if (tx_done && rx_done) {
		spi_qcc74x_complete(spi_dev, 0);
	}
}

/**
 * @brief Fallback work handler that finalizes slave DMA transfers on CS release.
 *
 * In slave mode, this handler is scheduled after CS is released to close
 * short frames where DMA completion callbacks may not fire.
 *
 * @param work Delayed work item for @ref spi_qcc74x_data::slave_dma_fallback_work.
 */
static void spi_qcc74x_slave_dma_fallback_work_handler(struct k_work *work)

{
	struct spi_qcc74x_data *data =
		CONTAINER_OF(work, struct spi_qcc74x_data, slave_dma_fallback_work.work);
	const struct device *spi_dev = data->dev;
	const struct spi_qcc74x_config *config = NULL;
	struct dma_status st = {0};

	if ((spi_dev == NULL) || data->dma_done || !spi_context_is_slave(&data->ctx)) {
		return;
	}

	if (!atomic_get(&data->slave_cs_seen)) {
		return;
	}

	config = spi_dev->config;

	if (data->ctx.current_rx != NULL &&
	    dma_get_status(config->dma[RX].dev, config->dma[RX].channel, &st) == 0) {
		spi_context_update_rx(&data->ctx, 1, data->ctx.current_rx->len - st.pending_length);
	}

	data->dma[TX].count = 1;
	data->dma[RX].count = 1;
	dma_qcc74x_dest_addr_cache_invalidate(config->dma[RX].dev, config->dma[RX].channel);
	spi_qcc74x_complete(spi_dev, 0);
}

#endif /* CONFIG_SPI_QCC74X_DMA */

/**
 * @brief SPI interrupt service routine.
 *
 * Handles interrupts from spi_int_sts register:
 * - spi_fer_int (bit 5): FIFO error
 * - spi_txu_int (bit 4): TX underrun
 * - spi_rxf_int (bit 2): RX FIFO ready (rx_fifo_cnt > rx_fifo_th)
 * - spi_txf_int (bit 1): TX FIFO ready (tx_fifo_cnt > tx_fifo_th)
 * - spi_end_int (bit 0): Transfer complete
 *
 * Structure follows pattern for FIFO-based SPI:
 * 1. Check errors first (fail fast)
 * 2. Service RX FIFO (prevent overflow)
 * 3. Service TX FIFO (keep data flowing)
 * 4. Check for completion
 *
 * @param dev Pointer to device structure.
 */
static void spi_qcc74x_isr(const struct device *dev)
{
	struct spi_qcc74x_data *data = dev->data;
	uint32_t int_status = 0U;

	int_status = qc7xx_spi_get_intstatus(&data->spi_hal_device);

#ifdef CONFIG_SPI_QCC74X_INTERRUPT
	/* Only process if we're in an active interrupt transfer */
	if (data->busy) {
		int err;

		/* 1. Error check first pattern */
		err = spi_qcc74x_get_err(dev);
		if (err != 0) {
			LOG_ERR("SPI error in ISR: %d", err);
			spi_qcc74x_int_complete(dev, err);
			return;
		}

		/* 2. Service RX FIFO first (prevent overflow) */
		if (int_status & SPI_INT_RXF) {
			spi_qcc74x_shift_fifo_rx(dev);
		}

		/* 3. Service TX FIFO */
		if (int_status & SPI_INT_TXF) {
			spi_qcc74x_shift_fifo_tx(dev);
		}

		/* 4. Check if all transfers complete */
		if (!spi_qcc74x_transfer_ongoing(data)) {
			LOG_DBG("Interrupt transfer complete");
			spi_qcc74x_int_complete(dev, 0);
			return;
		}
	}
#endif /* CONFIG_SPI_QCC74X_INTERRUPT */

	/* Handle Transfer Complete interrupt (spi_end_int, bit 0) */
	if (int_status & SPI_INTSTS_TC) {
		qc7xx_spi_int_clear(&data->spi_hal_device, SPI_INTCLR_TC);

#ifdef CONFIG_SPI_QCC74X_DMA
		k_sem_give(&data->complete_sync);

		/*
		 * In slave mode, CS release means DMA transfer should stop.
		 * But DMA completion could arrive at the same time, so forced
		 * stop is delayed.
		 */
		if (spi_context_is_slave(&data->ctx)) {
			if (!data->dma_done) {
				atomic_set(&data->slave_cs_seen, 1);
				(void)k_work_schedule(
					&data->slave_dma_fallback_work,
					K_USEC(QC7XX_SPI_SLAVE_DMA_FALLBACK_DELAY_US));
			}
		}
#endif /* CONFIG_SPI_QCC74X_DMA */
	}
}

#ifdef CONFIG_SPI_QCC74X_DMA
static void spi_qcc74x_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel,
				  int status)
{
	const struct device *spi_dev = (const struct device *)arg;
	struct spi_qcc74x_data *data = spi_dev->data;
	struct dma_status dma_status = {0};

	if (data->dma_done) {
		return;
	}

	if (status < 0) {
		LOG_ERR("dma:%p ch:%d callback gets error: %d", dma_dev, channel, status);
		spi_qcc74x_complete(spi_dev, status);
		return;
	}
	(void)dma_get_status(dma_dev, channel, &dma_status);

	if (status == DMA_STATUS_BLOCK) {
		/*
		 * Some DMA backends may report BLOCK on the final interrupt.
		 * Treat it as complete when no transfer units are pending.
		 */
		if (dma_status.pending_length == 0U) {
			status = DMA_STATUS_COMPLETE;
		} else {
			return;
		}
	}
	if (status != DMA_STATUS_COMPLETE) {
		LOG_WRN("dma:%p ch:%d unexpected callback status: %d", dma_dev, channel, status);
		return;
	}

	spi_qcc74x_finish_dma_transfer(spi_dev, dma_dev, channel);
}
#endif /* CONFIG_SPI_QCC74X_DMA */

/**
 * @brief Initialize SPI device.
 *
 * @param dev     Pointer to device structure.
 * @retval 0      On success.
 * @retval -errno Negative errno on failure.
 */
static int spi_qcc74x_init(const struct device *dev)
{
	const struct spi_qcc74x_config *const config = dev->config;
	struct spi_qcc74x_data *const data = dev->data;
	int32_t result = 0;
	const uint32_t base_reg = config->base_reg;
	const bool expect_idle = true;

	LOG_INF("Initializing SPI device at base address 0x%08x", base_reg);

	data->dev = dev;

	result = reset_line_toggle_dt(&config->reset);
	if (result < 0) {
		LOG_ERR("SPI reset line toggle failed: %d", result);
		return -EIO;
	}

	result = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (result < 0) {
		LOG_ERR("Failed to apply pin configuration: %d", result);
		return result;
	}

	result = clock_control_on(config->clock_dev,
				  (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (result != 0) {
		if (result == -EALREADY) {
			LOG_WRN("Clock for SPI driver is already enabled");
		} else {
			LOG_ERR("Failed to enable SPI clock: %d", result);
			return result;
		}
	}

	/* Log pin configuration information */
	log_pin_information(config->pcfg);

#ifdef CONFIG_SPI_QCC74X_DMA
	k_sem_init(&data->complete_sync, 0, 1);
	k_work_init_delayable(&data->slave_dma_fallback_work,
			      spi_qcc74x_slave_dma_fallback_work_handler);
	atomic_set(&data->slave_cs_seen, 0);

	if ((config->dma[RX].dev && !config->dma[TX].dev) ||
	    (config->dma[TX].dev && !config->dma[RX].dev)) {
		LOG_ERR("DMA must be enabled for both TX and RX channels");
		return -ENODEV;
	}

	for (size_t i = 0; i < dma_enabled_num(dev); i++) {
		if (!device_is_ready(config->dma[i].dev)) {
			LOG_ERR("DMA %s not ready", config->dma[i].dev->name);
			return -ENODEV;
		}
	}
#endif /* CONFIG_SPI_QCC74X_DMA */

#ifdef CONFIG_SPI_QCC74X_INTERRUPT
	/* Initialize interrupt mode state */
	data->busy = false;
#endif /* CONFIG_SPI_QCC74X_INTERRUPT */

	result = spi_context_cs_configure_all(&data->ctx);
	if (result < 0) {
		LOG_ERR("Failed to configure CS GPIOs: %d", result);
		return result;
	}

	if (config->irq_config_func != NULL) {
		config->irq_config_func(dev);
		LOG_DBG("IRQ configuration completed");
	} else {
		LOG_WRN("IRQ configuration function is NULL");
	}

	spi_context_unlock_unconditionally(&data->ctx);

	check_spi_status(base_reg, expect_idle);
	LOG_INF("SPI device initialized successfully");
	return 0;
}

static const struct spi_driver_api spi_qcc74x_driver_api = {
	.transceive = spi_qcc74x_transceive_sync,
#if defined(CONFIG_SPI_ASYNC) && defined(CONFIG_SPI_QCC74X_INTERRUPT)
	.transceive_async = spi_qcc74x_transceive_async,
#endif
	.release = spi_qcc74x_release,
};

/* Device instantiation macros */
#ifdef CONFIG_SPI_QCC74X_DMA
#define DMA_INITIALIZER(n, dir)                                                                    \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, dir)),                           \
		.channel = DT_INST_DMAS_CELL_BY_NAME(n, dir, channel),                             \
		.slot = DT_INST_DMAS_CELL_BY_NAME(n, dir, slot),                                   \
		.per_fifo_addr = DT_INST_DMAS_CELL_BY_NAME(n, dir, per_fifo_addr),                 \
	}

#define DMAS_DECL(n)                                                                               \
	{                                                                                          \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                          \
			    (DMA_INITIALIZER(n, rx)), ({0})),                                            \
			 COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                          \
			    (DMA_INITIALIZER(n, tx)), ({0})),         \
			 }

#endif /* CONFIG_SPI_QCC74X_DMA */

#define SPI_QCC74X_IRQ_HANDLER_DECL(n) static void spi_qcc74x_config_func_##n(const struct device *dev);

#define SPI_QCC74X_IRQ_HANDLER_FUNC(n) .irq_config_func = spi_qcc74x_config_func_##n

#define SPI_QCC74X_IRQ_HANDLER(n)                                                                    \
	static void spi_qcc74x_config_func_##n(const struct device *dev)                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), spi_qcc74x_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define SPI_QCC74X_DEVICE(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	SPI_QCC74X_IRQ_HANDLER_DECL(n)                                                               \
	static struct spi_qcc74x_data spi##n##_qcc74x_data = {                                         \
		SPI_CONTEXT_INIT_LOCK(spi##n##_qcc74x_data, ctx),                                    \
		SPI_CONTEXT_INIT_SYNC(spi##n##_qcc74x_data, ctx),                                    \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx).spi_hal_device.reg_base =     \
			DT_INST_REG_ADDR(n),                                                       \
		.spi_hal_device.idx = n};                                                          \
	static const struct spi_qcc74x_config spi_qcc74x_config_##n = {                                \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.clock_src = (uint8_t)DT_INST_CLOCKS_CELL(n, source),                              \
		.base_reg = DT_INST_REG_ADDR(n),                                                   \
		SPI_QCC74X_IRQ_HANDLER_FUNC(n),                                                      \
		IF_ENABLED(CONFIG_SPI_QCC74X_DMA, (.dma = DMAS_DECL(n),)) .reset =                      \
							RESET_DT_SPEC_INST_GET(n),                 \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, spi_qcc74x_init, NULL, &spi##n##_qcc74x_data, &spi_qcc74x_config_##n,   \
			      POST_KERNEL, CONFIG_SPI_INIT_PRIORITY, &spi_qcc74x_driver_api);        \
	SPI_QCC74X_IRQ_HANDLER(n)

DT_INST_FOREACH_STATUS_OKAY(SPI_QCC74X_DEVICE)
