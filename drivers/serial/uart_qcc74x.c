/*
 * Copyright (c) 2024, MASSDRIVER EI (massdriver.space)
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 * Not a contribution.
 */

#define DT_DRV_COMPAT qcom_qcc74x_uart

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/irq.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/reset.h>
#ifdef CONFIG_UART_ASYNC_API
#include <zephyr/drivers/dma.h>
#include <zephyr/cache.h>
#endif
#include <zephyr/pm/device.h>
#include <zephyr/drivers/serial/uart_qc7xx.h>
#ifdef CONFIG_QCC74X_PM_LPFW
#include <zephyr/pm/pm.h>
#endif

#include <hardware/glb_reg.h>
#include <qc7xx_uart.h>
#include <hardware/uart_reg.h>
#include <qc7xx_core.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_uart_ll.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_qcc74x, CONFIG_UART_LOG_LEVEL);

/* UART FIFO constants */
#define UART_FIFO_TX_THRESHOLD_DEFAULT 8U
#define UART_FIFO_RX_THRESHOLD_DEFAULT 0U
#define UART_FIFO_SIZE                 32U

/* UART divider calculation constants */
#define UART_DIVIDER_ROUNDING_FACTOR 5U
#define UART_DIVIDER_MULTIPLIER      10U
#define UART_MAX_DIVIDER             UINT16_MAX
#define UART_DIVIDER_HIGH_SHIFT      16U

/* UART data bit constants */
#define UART_DATA_BITS_HW_OFFSET 4U
#define UART_DATA_BITS_MIN       4U
#define UART_DATA_BITS_MAX       8U

/* UART interrupt masks */
#define UART_INT_ENABLE_ALL    0xFFU
#define UART_INT_CLEAR_ALL     0xFFU
#define UART_INT_MASK_ALL      UINT32_MAX
#define UART_INT_RELEVANT_BITS 0xFFU

#define UART_INTERRUPT_DISABLE (true)
#define UART_INTERRUPT_ENABLE  (false)

#define UART_ASYNC_RX_BUFFER_WATERMARK_PERCENTAGE 75U

#ifdef CONFIG_UART_ASYNC_API

/**
 * @brief UART async RX state machine states
 */
enum uart_async_rx_state {
	UART_ASYNC_RX_DISABLED,
	UART_ASYNC_RX_ENABLED,
	UART_ASYNC_RX_DISABLING,
};

/**
 * @brief UART async TX state machine states
 */
enum uart_async_tx_state {
	UART_ASYNC_TX_IDLE,
	UART_ASYNC_TX_ACTIVE,
	UART_ASYNC_TX_ABORTING,
};

/**
 * @brief UART buffer type for async operations
 */
enum uart_buffer_type {
	UART_ASYNC_CURRENT_BUFFER,
	UART_ASYNC_NEXT_BUFFER
};

#endif /* CONFIG_UART_ASYNC_API */

/**
 * @brief UART device configuration structure
 */
struct uart_qcc74x_config {
	unsigned int irq_source;
	bool pm_protected;
	/** Pin control configuration */
	const struct pinctrl_dev_config *pincfg;
	/** Clock control configuration */
	const struct device *clock_dev;
	/** Clock control id */
	const clock_control_subsys_t clock_id;
	/** Clock control id */
	const uint8_t clock_src;
	/** Base register address */
	const uint32_t base_reg;
	/** TX FIFO threshold */
	const uint8_t tx_fifo_threshold;
	/** RX FIFO threshold */
	const uint8_t rx_fifo_threshold;
	/** Reset controller */
	struct reset_dt_spec reset;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	/** Interrupt configuration function */
	uart_irq_config_func_t irq_config_func;
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_ASYNC_API
	/** DMA configuration */
	const struct device *dma_dev;
	uint32_t dma_tx_channel;
	uint32_t dma_rx_channel;
	uint32_t dma_tx_slot;
	uint32_t dma_rx_slot;
	uint32_t dma_tx_addr;
	uint32_t dma_rx_addr;
#endif
};

/**
 * @brief UART device runtime data structure
 */
struct uart_qcc74x_data {
	/** Pointer to the device structure */
	const struct device *dev;
	/** Current UART configuration */
	struct uart_config current_cfg;
	/** Config to call methods from hal */
	struct qc7xx_device_s hal_uart_cfg;
	/** Spinlock for synchronization */
	struct k_spinlock lock;
#ifdef CONFIG_PM_DEVICE
	uint32_t pm_users;
	uint32_t saved_irq_mask;
	bool saved_irq_enabled;
	bool suspended;
	bool transitioning;
#ifdef CONFIG_QCC74X_PM_LPFW
	bool pds15_snapshot;
	struct uart_qc7xx_pm_stats pm_stats;
#ifdef CONFIG_RISCV_HAS_CLIC
	uint8_t saved_clic_attr;
	uint8_t saved_clic_ctl;
#endif
#endif
#endif
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	/** User callback function */
	uart_irq_callback_user_data_t user_cb;
	/** User callback data */
	void *user_data;
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_ASYNC_API
	/** Async callback function */
	uart_callback_t async_cb;
	/** Async callback user data */
	void *async_user_data;

	/** TX async data */
	struct {
		/** TX buffer pointer */
		const uint8_t *buf;
		/** Total TX buffer length */
		size_t len;
		/** Number of bytes transmitted */
		size_t transmitted;
		/** TX timeout work */
		struct k_work_delayable timeout_work;
		/** TX state */
		enum uart_async_tx_state state;
	} async_tx;

	/** RX async data */
	struct {
		/** Current RX buffer */
		uint8_t *buf;
		/** Current RX buffer length */
		size_t len;
		/** Offset in current buffer */
		size_t offset;
		/** Next RX buffer (for buffer chaining) */
		uint8_t *next_buf;
		/** Next RX buffer length */
		size_t next_buf_len;
		/** RX timeout work */
		struct k_work_delayable timeout_work;
		/** RX state */
		enum uart_async_rx_state state;
		/** Counter for RX bytes since last timeout */
		size_t pending_bytes;
	} async_rx;

	/** DMA structures */
	struct {
		struct dma_config dma_cfg;
		struct dma_block_config dma_blk;
	} dma_tx;

	struct {
		struct dma_config dma_cfg;
		struct dma_block_config dma_blk;
	} dma_rx;

#endif /* CONFIG_UART_ASYNC_API */
};

/*
 * Keep PM out of MMIO, DMA setup and completion callbacks without holding a
 * spinlock across polling, DMA APIs or application callbacks.
 */
static bool uart_qcc74x_access_get(struct uart_qcc74x_data *data)
{
#ifdef CONFIG_PM_DEVICE
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	bool active = !data->suspended && !data->transitioning;

	if (active) {
		data->pm_users++;
	}
	k_spin_unlock(&data->lock, key);
	return active;
#else
	ARG_UNUSED(data);
	return true;
#endif
}

static void uart_qcc74x_access_put(struct uart_qcc74x_data *data)
{
#ifdef CONFIG_PM_DEVICE
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	data->pm_users--;
	k_spin_unlock(&data->lock, key);
#else
	ARG_UNUSED(data);
#endif
}

struct uart_qcc74x_global_data {
	/* Flag keeping information: Is clock already initialized? */
	bool is_clk_init;
	/* Clock divider value set by the first initialized instance */
	uint8_t div_val;
	/* Clock source used by the first UART instance */
	uint8_t clk_src;
};

/* Stucture keeping data used between instances */
struct uart_qcc74x_global_data driver_global_data = {
	.is_clk_init = 0,
	.div_val = 0,
	.clk_src = 0,
};

#ifdef CONFIG_UART_ASYNC_API
/* Forward declarations */
void uart_qcc74x_async_send_pending_data(const struct device *dev, struct uart_qcc74x_data *data);
static void uart_qcc74x_async_tx_timeout(struct k_work *work);
static void uart_qcc74x_async_rx_timeout(struct k_work *work);
static int uart_qcc74x_tx_abort_async(const struct device *dev);
static int uart_qcc74x_rx_disable_async(const struct device *dev);
static void uart_qcc74x_dma_tx_callback(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status);
static void uart_qcc74x_dma_rx_callback(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status);
static int uart_qcc74x_dma_tx_configure(const struct device *dev);
static int uart_qcc74x_dma_rx_configure(const struct device *dev);
static int uart_qcc74x_rx_enable_async(const struct device *dev, uint8_t *buf, size_t len,
				     int32_t timeout);
static inline void uart_qcc74x_async_event_send_to_app(const struct device *dev,
						     struct uart_event *event);
static inline void uart_qcc74x_async_event_rx_buf_request(const struct device *dev);
static inline void uart_qcc74x_rto_interrupt_control(const struct uart_qcc74x_config *config,
						   struct uart_qcc74x_data *data, bool enable,
						   uint32_t timeout_bits);
static void uart_qcc74x_async_handle_rto_interrupt(const struct device *dev,
						 const struct uart_qcc74x_config *config,
						 struct uart_qcc74x_data *data);
#endif

static int uart_qcc74x_poll_in_active(const struct device *dev, unsigned char *c);

static int uart_qcc74x_poll_in(const struct device *dev, unsigned char *c)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_poll_in_active(dev, c);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_poll_in_active(const struct device *dev, unsigned char *c)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	const int character = qc7xx_uart_getchar(hal_cfg);

	if (character < 0) {
		return -1;
	}

	*c = (unsigned char)((uint32_t)character & 0xFFU);
	return 0;
}

static void uart_qcc74x_poll_out_active(const struct device *dev, const unsigned char c);

static void uart_qcc74x_poll_out(const struct device *dev, const unsigned char c)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_poll_out_active(dev, c);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_poll_out_active(const struct device *dev, const unsigned char c)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	if (qc7xx_uart_putchar(hal_cfg, (int)c) != 0) {
		LOG_ERR("Timeout on polling to send data!");
	}
}

/**
 * @brief Enable or disable UART transmission and reception
 *
 * @param dev UART device instance
 * @param enable true to enable, false to disable
 */
static void uart_qcc74x_set_enabled(const struct device *dev, const bool enable)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	enable ? qc7xx_uart_enable(hal_cfg) : qc7xx_uart_disable(hal_cfg);

	LOG_DBG("UART state changed to %s", enable ? "enabled" : "disabled");
}

/**
 * @brief Configure UART parity settings
 *
 * @param base_reg UART base register address
 * @param parity Parity configuration from uart_config
 * @return 0 on success, negative errno on failure
 *
 * @note UART shall be disabled when calling this function.
 */
static int32_t uart_qcc74x_configure_parity(const uint32_t base_reg,
					  const enum uart_config_parity parity)
{
	uint32_t tx_config = 0U;
	uint32_t rx_config = 0U;

	/* Read current configuration */
	tx_config = sys_read32(base_reg + UART_UTX_CONFIG_OFFSET);
	rx_config = sys_read32(base_reg + UART_URX_CONFIG_OFFSET);

	switch (parity) {
	case UART_CFG_PARITY_NONE:
		LOG_DBG("Configuring parity: NONE");
		tx_config &= ~UART_CR_UTX_PRT_EN;
		rx_config &= ~UART_CR_URX_PRT_EN;
		break;

	case UART_CFG_PARITY_ODD:
		LOG_DBG("Configuring parity: ODD");
		tx_config |= (UART_CR_UTX_PRT_EN | UART_CR_UTX_PRT_SEL);
		rx_config |= (UART_CR_URX_PRT_EN | UART_CR_URX_PRT_SEL);
		break;

	case UART_CFG_PARITY_EVEN:
		LOG_DBG("Configuring parity: EVEN");
		tx_config |= UART_CR_UTX_PRT_EN;
		tx_config &= ~UART_CR_UTX_PRT_SEL;
		rx_config |= UART_CR_URX_PRT_EN;
		rx_config &= ~UART_CR_URX_PRT_SEL;
		break;

	default:
		LOG_ERR("Unsupported parity mode: %d", parity);
		return -ENOTSUP;
	}

	/* Write configuration back */
	sys_write32(tx_config, base_reg + UART_UTX_CONFIG_OFFSET);
	sys_write32(rx_config, base_reg + UART_URX_CONFIG_OFFSET);

	return 0;
}

/**
 * @brief Configure UART data bits
 *
 * @param base_reg UART base register address
 * @param data_bits Number of data bits from uart_config
 * @return 0 on success, negative errno on failure
 *
 * @note UART shall be disabled when calling this function.
 */
static int32_t uart_qcc74x_configure_data_bits(const uint32_t base_reg,
					     const enum uart_config_data_bits data_bits)
{
	uint32_t tx_config = 0U;
	uint32_t rx_config = 0U;
	uint32_t bit_count = 0U;

	/* Convert to hardware representation (add 4) */
	bit_count = (uint32_t)data_bits + UART_DATA_BITS_HW_OFFSET;

	/* Validate range */
	if ((bit_count < UART_DATA_BITS_MIN) || (bit_count > UART_DATA_BITS_MAX)) {
		LOG_ERR("Invalid data bits configuration: %d", data_bits);
		return -EINVAL;
	}

	LOG_DBG("Configuring data bits: %u (HW value: %u)", data_bits, bit_count);

	/* Read current configuration */
	tx_config = sys_read32(base_reg + UART_UTX_CONFIG_OFFSET);
	rx_config = sys_read32(base_reg + UART_URX_CONFIG_OFFSET);

	/* Update data bits */
	tx_config &= ~UART_CR_UTX_BIT_CNT_D_MASK;
	tx_config |= bit_count << UART_CR_UTX_BIT_CNT_D_SHIFT;

	rx_config &= ~UART_CR_URX_BIT_CNT_D_MASK;
	rx_config |= bit_count << UART_CR_URX_BIT_CNT_D_SHIFT;

	/* Write configuration back */
	sys_write32(tx_config, base_reg + UART_UTX_CONFIG_OFFSET);
	sys_write32(rx_config, base_reg + UART_URX_CONFIG_OFFSET);

	return 0;
}

/**
 * @brief Configure UART stop bits
 *
 * @param base_reg UART base register address
 * @param stop_bits Stop bits configuration from uart_config
 * @return 0 on success, negative errno on failure
 *
 * @note UART shall be disabled when calling this function.
 */
static int32_t uart_qcc74x_configure_stop_bits(const uint32_t base_reg,
					     const enum uart_config_stop_bits stop_bits)
{
	uint32_t tx_config = 0U;

	LOG_DBG("Configuring stop bits: %d", stop_bits);

	/* Read current configuration */
	tx_config = sys_read32(base_reg + UART_UTX_CONFIG_OFFSET);

	/* Update stop bits */
	tx_config &= ~UART_CR_UTX_BIT_CNT_P_MASK;
	tx_config |= (uint32_t)stop_bits << UART_CR_UTX_BIT_CNT_P_SHIFT;

	/* Write configuration back */
	sys_write32(tx_config, base_reg + UART_UTX_CONFIG_OFFSET);

	return 0;
}

/**
 * @brief Configure UART flow control
 *
 * @param base_reg UART base register address
 * @param flow_ctrl Flow control configuration from uart_config
 * @return 0 on success, negative errno on failure
 *
 * @note UART shall be disabled when calling this function.
 */
static int32_t uart_qcc74x_configure_flow_control(const uint32_t base_reg,
						const enum uart_config_flow_control flow_ctrl)
{
	uint32_t tx_config = 0U;
	uint32_t sw_mode = 0U;

	/* Read current configuration */
	tx_config = sys_read32(base_reg + UART_UTX_CONFIG_OFFSET);
	sw_mode = sys_read32(base_reg + UART_SW_MODE_OFFSET);

	switch (flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		LOG_DBG("Configuring flow control: NONE");
		tx_config &= ~UART_CR_UTX_CTS_EN;
		/* Enable software control for RTS */
		sw_mode |= UART_CR_URX_RTS_SW_MODE;
		LOG_DBG("RTS in software mode, CTS disabled");
		break;

	case UART_CFG_FLOW_CTRL_RTS_CTS:
		LOG_DBG("Configuring flow control: RTS/CTS");
		tx_config |= UART_CR_UTX_CTS_EN;
		/* Enable hardware control for RTS */
		sw_mode &= ~UART_CR_URX_RTS_SW_MODE;
		LOG_DBG("RTS in hardware mode, CTS enabled");
		break;
	default:
		LOG_ERR("Unsupported flow control: %d", flow_ctrl);
		return -ENOTSUP;
	}

	/* Write configurations */
	sys_write32(tx_config, base_reg + UART_UTX_CONFIG_OFFSET);
	sys_write32(sw_mode, base_reg + UART_SW_MODE_OFFSET);

	return 0;
}

/**
 * @brief Configure UART FIFO thresholds and clear FIFOs
 *
 * @param base_reg UART base register address
 * @param tx_threshold TX FIFO threshold
 * @param rx_threshold RX FIFO threshold
 *
 * @note UART shall be disabled when calling this function.
 */
static void uart_qcc74x_configure_fifo(const uint32_t base_reg, const uint8_t tx_threshold,
				     const uint8_t rx_threshold)
{
	uint32_t fifo_config = 0U;

	LOG_DBG("Configuring FIFO - TX threshold: %u, RX threshold: %u", tx_threshold,
		rx_threshold);

	/* Configure FIFO thresholds */
	fifo_config = sys_read32(base_reg + UART_FIFO_CONFIG_1_OFFSET);
	fifo_config &= ~(UART_TX_FIFO_TH_MASK | UART_RX_FIFO_TH_MASK);
	fifo_config |= ((uint32_t)tx_threshold << UART_TX_FIFO_TH_SHIFT) & UART_TX_FIFO_TH_MASK;
	fifo_config |= ((uint32_t)rx_threshold << UART_RX_FIFO_TH_SHIFT) & UART_RX_FIFO_TH_MASK;
	sys_write32(fifo_config, base_reg + UART_FIFO_CONFIG_1_OFFSET);

	/* Clear FIFOs and disable DMA */
	fifo_config = sys_read32(base_reg + UART_FIFO_CONFIG_0_OFFSET);
	fifo_config |= (UART_TX_FIFO_CLR | UART_RX_FIFO_CLR);
	fifo_config &= ~(UART_DMA_TX_EN | UART_DMA_RX_EN);
	sys_write32(fifo_config, base_reg + UART_FIFO_CONFIG_0_OFFSET);
}

/**
 * @brief Configure UART miscellaneous settings
 *
 * @param base_reg UART base register address
 *
 * @note UART shall be disabled when calling this function.
 */
static void uart_qcc74x_configure_misc(const uint32_t base_reg)
{
	uint32_t config_value = 0U;

	/* Disable RX de-glitch function */
	config_value = sys_read32(base_reg + UART_URX_CONFIG_OFFSET);
	config_value &= ~UART_CR_URX_DEG_EN;
	sys_write32(config_value, base_reg + UART_URX_CONFIG_OFFSET);

	/* Disable data inversion */
	config_value = sys_read32(base_reg + UART_DATA_CONFIG_OFFSET);
	config_value &= ~UART_CR_UART_BIT_INV;
	sys_write32(config_value, base_reg + UART_DATA_CONFIG_OFFSET);

	/* Enable TX free run */
	config_value = sys_read32(base_reg + UART_UTX_CONFIG_OFFSET);
	config_value |= UART_CR_UTX_FRM_EN;
	sys_write32(config_value, base_reg + UART_UTX_CONFIG_OFFSET);

	LOG_DBG("Miscellaneous UART settings configured");
}

/**
 * @brief Calculate clock divider and period register value.
 *
 * This function goal is to find divider and period register values that
 * will suite user expectations. Calculations done in this function base
 * on the documentation (regarding period registers):
 * https://docs.qualcomm.com/doc/80-58740-1/topic/uart.html#uart
 * Section "Baud rate setting" shows how period registers work:
 *
 * Baudrate = UART_clk / (uart_prd + 1)
 *
 * In order to calculate UART_clk frequency we need source clock frequency
 * and divider value. It can be deduced from previous driver implementation
 * (file: qccsdk/drivers/lhal/src/qcc74x_uart.c).
 * and from GLB_Set_UART_CLK function logic:
 * modules/hal/hal_qcom/qcc7xx/drivers/soc/qcc74x/std/src/qcc74x_glb.c
 * that divider value is also incremented in hardware:
 *
 * Uart_clk = Source_frequency / (divider_value + 1)
 *
 * So, final formula is following:
 *
 * Baudrate = Source_frequency / ((uart_prd + 1) * (divider_value + 1))
 *
 * @param peripheral_clk Peripheral clock frequency in Hz.
 * @param desired_freq   Desired UART baudrate in Hz.
 * @param [out] Clock divider value.
 * @param [out] Period register value.
 *
 * @return 0 in case of success and -EINVAL if such configuration is impossible
 */
static int uart_qcc74x_calculate_settings(const uint32_t peripheral_clk, const uint32_t desired_freq,
					uint8_t *clk_divider, uint16_t *period_divider)
{
	uint32_t period_tmp = 0U;
	uint32_t period_proposal = 0U;
	uint32_t divider_proposal = 0U;
	uint32_t actual_freq = 0U;
	uint32_t error = UINT32_MAX;
	uint32_t smallest_error = UINT32_MAX;
	uint8_t iter = 0U;

	/* Is desired baudrate not achievable just with period dividers? */
	if ((peripheral_clk / desired_freq) >
	    ((QC7XX_UART_MAX_CLK_PRD + 1) * (driver_global_data.div_val + 1))) {
		/* If other instance already set the divider to the value
		 * that doesn't make desired baudrate possible
		 * -> return error */
		if (driver_global_data.is_clk_init) {
			LOG_ERR("Could not achieve desired baudrate: %u \n"
				"with divider set to %u "
				"by previous UART instance.",
				desired_freq, (driver_global_data.div_val + 1));
			return -EINVAL;
		}

		/* If this is the first instance and we need both divider
		 * and period registers - calculate it */
		for (iter = 1; iter <= QC7XX_UART_MAX_CLK_DIV; iter++) {
			period_tmp = peripheral_clk / (desired_freq * (iter + 1));

			if (period_tmp > QC7XX_UART_MAX_CLK_PRD) {
				continue;
			}

			actual_freq = peripheral_clk / ((iter + 1) * period_tmp);

			error = abs(actual_freq - desired_freq);

			if (error == 0) {
				divider_proposal = iter;
				period_proposal = period_tmp;
				break;
			}

			if (error < smallest_error) {
				divider_proposal = iter;
				period_proposal = period_tmp;
				smallest_error = error;
			}
		}
	} else {
		/* Desired baudrate is achievable with only period dividers */
		period_proposal =
			peripheral_clk / (desired_freq * (driver_global_data.div_val + 1));

		divider_proposal = driver_global_data.div_val;
	}
	*period_divider = period_proposal - 1U;
	*clk_divider = divider_proposal;

	return 0;
}

/**
 * @brief Configure UART baudrate
 *
 * @param dev UART device instance
 * @param baudrate Desired baudrate
 * @return 0 on success, negative errno on failure
 *
 * @note UART shall be disabled when calling this function.
 */
static int32_t uart_qcc74x_configure_baudrate(const struct device *dev, const uint32_t baudrate)
{
	const struct uart_qcc74x_config *config = dev->config;
	const uint32_t base_reg = config->base_reg;
	uint32_t src_clock_rate = 0U;
	uint8_t divider = 0U;
	uint16_t period_divider = 0U;
	int ret;
	struct qcc74x_clk_settings uart_settings = {
		.enable = CLOCK_ENABLE,
		.clk_src = config->clock_src,
		.clk_div = 0, // This need to be zero not to interfere with "get_rate" function
	};

	/* If this is the first instance to configure the clock */
	if (!driver_global_data.is_clk_init) {
		/* Dummy clock configuration to ensure that source clock
		 * is set correctly for get_rate function */
		ret = clock_control_configure(config->clock_dev,
					      (clock_control_subsys_t)(uintptr_t)config->clock_id,
					      (void *)&uart_settings);

		if (ret != 0) {
			LOG_ERR("Failed to configure uart clock with source:%d and div:%u!",
				uart_settings.clk_src, uart_settings.clk_div);
			return -EINVAL;
		} else {
			LOG_DBG("Successfully configured uart clock with source:%d and div:%u!",
				uart_settings.clk_src, uart_settings.clk_div);
		}
	} else {
		if (driver_global_data.clk_src != config->clock_src) {
			LOG_ERR("UART driver doesn't support different "
				"clock sources for different instances! \n"
				" clock source used by first instance: %u \n"
				" clock source set for this instance: %u",
				driver_global_data.clk_src, config->clock_src);
			return -EINVAL;
		}
	}

	/* Get base clock rate */
	ret = clock_control_get_rate(config->clock_dev, config->clock_id, &src_clock_rate);
	if (ret < 0) {
		LOG_ERR("Failed to get UART clock rate: %d", ret);
		return -EIO;
	}

	/* clock_control_get_rate function returns clock rate with divider */
	src_clock_rate = (src_clock_rate * (driver_global_data.div_val + 1U));

	LOG_DBG("UART source clock rate %uHz", src_clock_rate);

	ret = uart_qcc74x_calculate_settings(src_clock_rate, baudrate, &divider, &period_divider);
	if (ret != 0) {
		LOG_ERR("Can not obtain %u frequency from %u source "
			"with current divider and period registers",
			baudrate, config->clock_src);
		return -EINVAL;
	}

	qcc7xx_clk_uart_ll_set_period(period_divider, base_reg);

	LOG_DBG("Baudrate configured: %u (divider: %u period_reg: %u)", baudrate, divider,
		period_divider);

	LOG_DBG("Period reg val: %u", sys_read32(base_reg + UART_BIT_PRD_OFFSET));

	/* Save global data to inform other instances */
	driver_global_data.is_clk_init = true;
	driver_global_data.div_val = divider;
	driver_global_data.clk_src = config->clock_src;

	return 0;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static int32_t uart_qcc74x_err_check_active(const struct device *dev);

static int32_t uart_qcc74x_err_check(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_err_check_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_err_check_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	uint32_t int_status = 0U;
	uint32_t int_clear = 0U;
	int32_t errors = 0;

	int_status = sys_read32(config->base_reg + UART_INT_STS_OFFSET);
	int_clear = sys_read32(config->base_reg + UART_INT_CLEAR_OFFSET);

	if (int_status & UART_URX_FER_INT) {
		errors |= UART_ERROR_OVERRUN;
		LOG_ERR("UART RX FIFO overrun detected");
	}

	if (int_status & UART_UTX_FER_INT) {
		errors |= UART_ERROR_OVERRUN;
		LOG_ERR("UART TX FIFO overrun detected");
	}

	if (int_status & UART_URX_PCE_INT) {
		int_clear |= UART_CR_URX_PCE_CLR;
		errors |= UART_ERROR_PARITY;
		LOG_ERR("UART parity error detected");
	}

	if (int_clear != 0) {
		LOG_DBG("Clearing error interrupts: 0x%08x", int_clear);
		sys_write32(int_clear, config->base_reg + UART_INT_CLEAR_OFFSET);
	}

	return errors;
}

static int32_t uart_qcc74x_irq_tx_ready_active(const struct device *dev);

static int32_t uart_qcc74x_irq_tx_ready(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return 0;
	}
	int ret = uart_qcc74x_irq_tx_ready_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_irq_tx_ready_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	uint32_t int_mask = 0U;

	/* Check if TX FIFO has space */
	if (!qc7xx_uart_txready(hal_cfg)) {
		return 0;
	}

	/* Check if TX interrupt is enabled (when UART_CR_UTX_FIFO_MASK bit is CLEAR)*/
	int_mask = sys_read32(config->base_reg + UART_INT_MASK_OFFSET);
	if (int_mask & UART_CR_UTX_FIFO_MASK) {
		return 0;
	}

	LOG_DBG("TX ready: FIFO has space and interrupt enabled");
	return 1;
}

static int32_t uart_qcc74x_irq_rx_ready_active(const struct device *dev);

static int32_t uart_qcc74x_irq_rx_ready(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return 0;
	}
	int ret = uart_qcc74x_irq_rx_ready_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_irq_rx_ready_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	uint32_t int_mask = 0U;

	/* Check if RX FIFO has data */
	if (!qc7xx_uart_rxavailable(hal_cfg)) {
		return 0;
	}

	/* Check if RX interrupt is enabled (UART_CR_URX_FIFO_MASK bit is CLEAR) */
	int_mask = sys_read32(config->base_reg + UART_INT_MASK_OFFSET);
	if (int_mask & UART_CR_URX_FIFO_MASK) {
		return 0;
	}

	LOG_DBG("RX ready: FIFO has data and interrupt enabled");
	return 1;
}

static int32_t uart_qcc74x_fifo_fill_active(const struct device *dev, const uint8_t *tx_data,
				   const int32_t len);

static int32_t uart_qcc74x_fifo_fill(const struct device *dev, const uint8_t *tx_data,
				   const int32_t len)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return 0;
	}
	int ret = uart_qcc74x_fifo_fill_active(dev, tx_data, len);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_fifo_fill_active(const struct device *dev, const uint8_t *tx_data,
				   const int32_t len)
{
	struct uart_qcc74x_data *data = dev->data;
	int32_t num_tx = 0;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	while ((num_tx < len) && (uart_qcc74x_irq_tx_ready(dev) == 1)) {
		uart_qcc74x_poll_out(dev, (unsigned char)tx_data[num_tx]);
		++num_tx;
	}

	k_spin_unlock(&data->lock, key);

	/*
	 * Workaround delay for loopback mode - allows TX -> RX propagation.
	 * At default 115200 baud, one byte takes ~87us to transmit.
	 * This delay ensures RX interrupt can fire before callback loop exits.
	 */
	// TODO: Do more investigation here!
	if (num_tx > 0) {
		k_busy_wait(100); // 100µs = ~1 byte time
	}

	LOG_DBG("Filled TX FIFO with %d bytes", num_tx);
	return num_tx;
}

static int32_t uart_qcc74x_fifo_read_active(const struct device *dev, uint8_t *rx_data, const int32_t size);

static int32_t uart_qcc74x_fifo_read(const struct device *dev, uint8_t *rx_data, const int32_t size)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return 0;
	}
	int ret = uart_qcc74x_fifo_read_active(dev, rx_data, size);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_fifo_read_active(const struct device *dev, uint8_t *rx_data, const int32_t size)
{
	int32_t num_rx = 0;

	while ((num_rx < size) && (uart_qcc74x_irq_rx_ready(dev) == 1)) {
		if (uart_qcc74x_poll_in(dev, &rx_data[num_rx]) != 0) {
			break;
		}
		++num_rx;
	}

	if (num_rx != size) {
		LOG_DBG("Failed to read all bytes! Read %d of total %d bytes", num_rx, size);
	}

	LOG_DBG("Read %d bytes from RX FIFO", num_rx);
	return num_rx;
}

static void uart_qcc74x_irq_tx_enable_active(const struct device *dev);

static void uart_qcc74x_irq_tx_enable(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_irq_tx_enable_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_irq_tx_enable_active(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;

	qc7xx_uart_txint_mask(hal_cfg, UART_INTERRUPT_ENABLE);

	LOG_DBG("TX interrupt enabled");
}

static void uart_qcc74x_irq_tx_disable_active(const struct device *dev);

static void uart_qcc74x_irq_tx_disable(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_irq_tx_disable_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_irq_tx_disable_active(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;

	qc7xx_uart_txint_mask(hal_cfg, UART_INTERRUPT_DISABLE);

	LOG_DBG("TX interrupt disabled");
}

static int32_t uart_qcc74x_irq_tx_complete_active(const struct device *dev);

static int32_t uart_qcc74x_irq_tx_complete(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return 0;
	}
	int ret = uart_qcc74x_irq_tx_complete_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_irq_tx_complete_active(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	int result = 0;

	result = qc7xx_uart_wait_tx_done(hal_cfg);

	if (result != 0) {
		LOG_ERR("Failed on waiting for TX transfer done: %d", result);
		return 0;
	}

	if (!qc7xx_uart_txready(hal_cfg)) {
		LOG_ERR("Failed on check for free space in fifo after transfer complete!");
		return 0;
	}

	return 1;
}

static void uart_qcc74x_irq_rx_enable_active(const struct device *dev);

static void uart_qcc74x_irq_rx_enable(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_irq_rx_enable_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_irq_rx_enable_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	uint32_t int_clear = 0U;
	uint32_t rx_config = 0U;

	/* Clear pending RX interrupts BEFORE enabling */
	int_clear = sys_read32(config->base_reg + UART_INT_CLEAR_OFFSET);
	int_clear |= UART_CR_URX_END_CLR | UART_CR_URX_RTO_CLR;
	sys_write32(int_clear, config->base_reg + UART_INT_CLEAR_OFFSET);

	/*
	 * When hardware flow control is enabled, we must enable cr_urx_en to allow
	 * RTS to go LOW (ready to receive)"
	 */
	if (data->current_cfg.flow_ctrl == UART_CFG_FLOW_CTRL_RTS_CTS) {
		rx_config = sys_read32(config->base_reg + UART_URX_CONFIG_OFFSET);
		rx_config |= UART_CR_URX_EN;
		sys_write32(rx_config, config->base_reg + UART_URX_CONFIG_OFFSET);
	}

	qc7xx_uart_rxint_mask(hal_cfg, UART_INTERRUPT_ENABLE);

	LOG_DBG("RX interrupt enabled");
}

static void uart_qcc74x_irq_rx_disable_active(const struct device *dev);

static void uart_qcc74x_irq_rx_disable(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_irq_rx_disable_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_irq_rx_disable_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	uint32_t rx_config = 0U;

	/*
	 * When hardware flow control is enabled, we must disable cr_urx_en to make
	 * RTS go HIGH (stop transmitting)"
	 */
	if (data->current_cfg.flow_ctrl == UART_CFG_FLOW_CTRL_RTS_CTS) {
		rx_config = sys_read32(config->base_reg + UART_URX_CONFIG_OFFSET);
		rx_config &= ~UART_CR_URX_EN;
		sys_write32(rx_config, config->base_reg + UART_URX_CONFIG_OFFSET);
	}

	qc7xx_uart_rxint_mask(hal_cfg, UART_INTERRUPT_DISABLE);

	LOG_DBG("RX interrupt disabled");
}

static void uart_qcc74x_irq_err_enable_active(const struct device *dev);

static void uart_qcc74x_irq_err_enable(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_irq_err_enable_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_irq_err_enable_active(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;

	qc7xx_uart_errint_mask(hal_cfg, UART_INTERRUPT_ENABLE);
	LOG_DBG("Error interrupts enabled");
}

static void uart_qcc74x_irq_err_disable_active(const struct device *dev);

static void uart_qcc74x_irq_err_disable(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_irq_err_disable_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_irq_err_disable_active(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;

	qc7xx_uart_errint_mask(hal_cfg, UART_INTERRUPT_DISABLE);
	LOG_DBG("Error interrupts disabled");
}

static int32_t uart_qcc74x_irq_is_pending_active(const struct device *dev);

static int32_t uart_qcc74x_irq_is_pending(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return 0;
	}
	int ret = uart_qcc74x_irq_is_pending_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int32_t uart_qcc74x_irq_is_pending_active(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *hal_cfg = &data->hal_uart_cfg;
	const uint32_t uart_interrupt_status = qc7xx_uart_get_intstatus(hal_cfg);

	if ((uart_interrupt_status & (UART_URX_FIFO_INT | UART_URX_RTO_INT)) != 0U) {
		return qc7xx_uart_rxavailable(hal_cfg);
	}
	if ((uart_interrupt_status & UART_UTX_FIFO_INT) != 0U) {
		return qc7xx_uart_txready(hal_cfg);
	}

	return 0;
}

static int32_t uart_qcc74x_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1;
}

static void uart_qcc74x_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
				       void *user_data)
{
	struct uart_qcc74x_data *data = dev->data;
	unsigned int key = irq_lock();

	data->user_cb = cb;
	data->user_data = user_data;

	irq_unlock(key);
	LOG_DBG("IRQ callback set");
}

/**
 * @brief UART interrupt service routine
 *
 * @param dev UART device instance
 */
static void uart_qcc74x_isr_active(const struct device *dev);

static void uart_qcc74x_isr(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_isr_active(dev);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_isr_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	uint32_t int_clear = 0U;

#ifdef CONFIG_UART_ASYNC_API
	if (config->dma_dev != NULL) {
		uint32_t int_status = sys_read32(config->base_reg + UART_INT_STS_OFFSET);
		if ((int_status & UART_URX_RTO_INT) &&
		    (data->async_rx.state == UART_ASYNC_RX_ENABLED)) {
			uart_qcc74x_async_handle_rto_interrupt(dev, config, data);
		} else {
			LOG_DBG("RTO interrupt handler will not be called: int_status=0x%08x, "
				"rx_state=%d",
				int_status, data->async_rx.state);
		}
	}
#endif /* CONFIG_UART_ASYNC_API */

	/* Handle interrupt-driven mode */
	if (data->user_cb != NULL) {
		data->user_cb(dev, data->user_data);
		LOG_DBG("UART user callback was called!");
	}

	/* Clear interrupts that require acknowledgment */
	int_clear = sys_read32(config->base_reg + UART_INT_CLEAR_OFFSET);
	int_clear |= UART_CR_URX_RTO_CLR;
	sys_write32(int_clear, config->base_reg + UART_INT_CLEAR_OFFSET);
}

#ifdef CONFIG_UART_ASYNC_API
/**
 * @brief Handle Receive Timeout interrupt
 *
 * @param dev UART device instance
 * @param config Pointer to device configuration
 * @param data Pointer to runtime data structure
 */
static void uart_qcc74x_async_handle_rto_interrupt(const struct device *dev,
						 const struct uart_qcc74x_config *config,
						 struct uart_qcc74x_data *data)
{
	unsigned int key = irq_lock();
	struct dma_status dma_stat = {};
	int ret = dma_get_status(config->dma_dev, config->dma_rx_channel, &dma_stat);

	if (ret == 0) {
		size_t received = data->async_rx.len - dma_stat.pending_length;
		LOG_DBG("RTO interrupt: received=%zu, offset=%zu", received, data->async_rx.offset);

		if (received > data->async_rx.offset) {
			/* New data received - send RX_RDY event */
			size_t new_bytes = received - data->async_rx.offset;
			struct uart_event event = {.type = UART_RX_RDY,
						   .data.rx.buf = data->async_rx.buf,
						   .data.rx.offset = data->async_rx.offset,
						   .data.rx.len = new_bytes};

			data->async_rx.offset = received;
			irq_unlock(key);

			uart_qcc74x_async_event_send_to_app(dev, &event);

			/* The callback (e.g. test_single_read_callback) may write
			 * receive-count fields that share a D-cache line with the
			 * DMA destination buffer. Flush those CPU writes before DMA
			 * fills more bytes, then invalidate stale cache entries. */
			if (qc7xx_check_cache_addr(data->async_rx.buf)) {
				sys_cache_data_flush_and_invd_range(data->async_rx.buf,
								    data->async_rx.len);
			}

			key = irq_lock();

			/* Request next buffer if getting full */
			const size_t watermark =
				(data->async_rx.len * UART_ASYNC_RX_BUFFER_WATERMARK_PERCENTAGE) /
				100U;
			if (data->async_rx.offset >= watermark && data->async_rx.next_buf == NULL) {
				LOG_DBG("Buffer %u%% full, requesting next "
					"buffer",
					UART_ASYNC_RX_BUFFER_WATERMARK_PERCENTAGE);
				irq_unlock(key);
				uart_qcc74x_async_event_rx_buf_request(dev);
				key = irq_lock();
			}
		}
	} else {
		LOG_ERR("Failed to get DMA status %d", ret);
	}
	irq_unlock(key);
}
#endif /* CONFIG_UART_ASYNC_API */
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static int uart_qcc74x_configure_active(const struct device *dev, const struct uart_config *cfg);

static int uart_qcc74x_configure(const struct device *dev, const struct uart_config *cfg)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_configure_active(dev, cfg);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_configure_active(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	int32_t ret = 0;

	LOG_DBG("Configuring UART %s", dev->name);

	/* Disable UART before configuring */
	uart_qcc74x_set_enabled(dev, false);

	/* Configure baudrate */
	ret = uart_qcc74x_configure_baudrate(dev, cfg->baudrate);
	if (ret < 0) {
		LOG_ERR("Failed to configure baudrate: %d", ret);
		uart_qcc74x_set_enabled(dev, true);
		return ret;
	}

	/* Configure parity */
	ret = uart_qcc74x_configure_parity(config->base_reg, cfg->parity);
	if (ret < 0) {
		LOG_ERR("Failed to configure parity: %d", ret);
		uart_qcc74x_set_enabled(dev, true);
		return ret;
	}

	/* Configure data bits */
	ret = uart_qcc74x_configure_data_bits(config->base_reg, cfg->data_bits);
	if (ret < 0) {
		LOG_ERR("Failed to configure data bits: %d", ret);
		uart_qcc74x_set_enabled(dev, true);
		return ret;
	}

	/* Configure stop bits */
	ret = uart_qcc74x_configure_stop_bits(config->base_reg, cfg->stop_bits);
	if (ret < 0) {
		LOG_ERR("Failed to configure stop bits: %d", ret);
		uart_qcc74x_set_enabled(dev, true);
		return ret;
	}

	/* Configure flow control */
	ret = uart_qcc74x_configure_flow_control(config->base_reg, cfg->flow_ctrl);
	if (ret < 0) {
		LOG_ERR("Failed to configure flow control: %d", ret);
		uart_qcc74x_set_enabled(dev, true);
		return ret;
	}

	/* Configure miscellaneous settings */
	uart_qcc74x_configure_misc(config->base_reg);

	/* Configure FIFO thresholds */
	uart_qcc74x_configure_fifo(config->base_reg, config->tx_fifo_threshold,
				 config->rx_fifo_threshold);

	/* Save configuration */
	data->current_cfg = *cfg;

	/* Re-enable UART */
	uart_qcc74x_set_enabled(dev, true);

	LOG_DBG("UART configuration complete");
	return 0;
}

static int32_t uart_qcc74x_config_get(const struct device *dev, struct uart_config *cfg)
{
	const struct uart_qcc74x_data *data = dev->data;

	if (cfg == NULL) {
		return -EINVAL;
	}

	*cfg = data->current_cfg;
	return 0;
}

#ifdef CONFIG_UART_ASYNC_API

/**
 * @brief Send event to async application callback
 *
 * @param dev UART device instance
 * @param event Event to send
 */
static inline void uart_qcc74x_async_event_send_to_app(const struct device *dev,
						     struct uart_event *event)
{
	const struct uart_qcc74x_data *data = dev->data;

	if (data->async_cb) {
		data->async_cb(dev, event, data->async_user_data);
		LOG_DBG("Async event type=%d sent to application", event->type);
	} else {
		LOG_ERR("UART Async callback not provided. Failed to send notification!");
	}
}

/**
 * @brief Send TX done event
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_event_tx_done(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct uart_event event = {0};

	event.type = UART_TX_DONE;
	event.data.tx.buf = data->async_tx.buf;
	event.data.tx.len = data->async_tx.transmitted;

	LOG_DBG("UART_TX_DONE: transmitted %zu bytes", data->async_tx.transmitted);

	data->async_tx.state = UART_ASYNC_TX_IDLE;
	data->async_tx.buf = NULL;
	data->async_tx.len = 0;
	data->async_tx.transmitted = 0;

	uart_qcc74x_async_event_send_to_app(dev, &event);
}

/**
 * @brief Send TX abort event
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_event_tx_abort(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	struct uart_event event = {0};

	event.type = UART_TX_ABORTED;
	event.data.tx.buf = data->async_tx.buf;
	event.data.tx.len = data->async_tx.transmitted;

	LOG_DBG("UART_TX_ABORTED after %zu bytes", event.data.tx.len);

	data->async_tx.state = UART_ASYNC_TX_IDLE;
	data->async_tx.buf = NULL;
	data->async_tx.len = 0;
	data->async_tx.transmitted = 0;

	uart_qcc74x_async_event_send_to_app(dev, &event);
}

/**
 * @brief Send RX disabled event
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_event_rx_disabled(const struct device *dev)
{
	struct uart_event event = {0};

	event.type = UART_RX_DISABLED;
	LOG_DBG("UART_RX_DISABLED event");
	uart_qcc74x_async_event_send_to_app(dev, &event);
}

/**
 * @brief Request new RX buffer
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_event_rx_buf_request(const struct device *dev)
{
	struct uart_event event = {0};

	event.type = UART_RX_BUF_REQUEST;
	LOG_DBG("Requesting new RX buffer using UART_RX_BUF_REQUEST");
	uart_qcc74x_async_event_send_to_app(dev, &event);
}

/**
 * @brief Release RX buffer
 *
 * @param dev UART device instance
 * @param type Buffer type to release
 */
static inline void uart_qcc74x_async_event_rx_buf_release(const struct device *dev,
							enum uart_buffer_type type)
{
	struct uart_qcc74x_data *data = dev->data;
	struct uart_event event = {0};

	if (type == UART_ASYNC_CURRENT_BUFFER) {
		if (data->async_rx.buf == NULL) {
			LOG_DBG("No current buffer to release!");
			return;
		}
		event.data.rx_buf.buf = data->async_rx.buf;
		LOG_DBG("Releasing current buffer %p", data->async_rx.buf);
	} else {
		if (data->async_rx.next_buf == NULL) {
			LOG_DBG("No next buffer to release!");
			return;
		}
		event.data.rx_buf.buf = data->async_rx.next_buf;
		LOG_DBG("Releasing next buffer %p", data->async_rx.next_buf);
	}

	event.type = UART_RX_BUF_RELEASED;
	uart_qcc74x_async_event_send_to_app(dev, &event);
}

/**
 * @brief Reset RX parameters
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_reset_rx_parameters(struct uart_qcc74x_data *data)
{
	data->async_rx.buf = NULL;
	data->async_rx.len = 0;
	data->async_rx.offset = 0;
	data->async_rx.next_buf = NULL;
	data->async_rx.next_buf_len = 0;
	data->async_rx.pending_bytes = 0;

	LOG_DBG("RX parameters reset");
}

/**
 * @brief Switch to next RX buffer
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_switch_buffer(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;

	data->async_rx.buf = data->async_rx.next_buf;
	data->async_rx.len = data->async_rx.next_buf_len;
	data->async_rx.offset = 0;
	data->async_rx.next_buf = NULL;
	data->async_rx.next_buf_len = 0;

	LOG_DBG("Switched to new buffer %p, len %zu", data->async_rx.buf, data->async_rx.len);
}

/**
 * @brief Send pending RX data event to the application
 *
 * @param dev UART device instance
 * @param data Pointer to runtime data structure
 */
void uart_qcc74x_async_send_pending_data(const struct device *dev, struct uart_qcc74x_data *data)
{
	if (data && data->async_rx.pending_bytes > 0) {
		struct uart_event event = {.type = UART_RX_RDY,
					   .data.rx.buf = data->async_rx.buf,
					   .data.rx.offset = data->async_rx.offset -
							     data->async_rx.pending_bytes,
					   .data.rx.len = data->async_rx.pending_bytes};

		uart_qcc74x_async_event_send_to_app(dev, &event);
		data->async_rx.pending_bytes = 0;
	} else {
		LOG_DBG("No pending data to send with UART_RX_RDY");
	}
}

/**
 * @brief Check if we should request for a new buffer
 *
 * @param dev UART device instance
 */
static inline void uart_qcc74x_async_request_new_buffer_if_needed(const struct device *dev)
{
	struct uart_qcc74x_data *data = dev->data;
	const size_t new_buffer_watermark =
		(data->async_rx.len * UART_ASYNC_RX_BUFFER_WATERMARK_PERCENTAGE) / 100U;

	if (data->async_rx.offset >= new_buffer_watermark && data->async_rx.next_buf == NULL) {
		LOG_DBG("Buffer %u%% full, requesting next buffer",
			UART_ASYNC_RX_BUFFER_WATERMARK_PERCENTAGE);
		uart_qcc74x_async_event_rx_buf_request(dev);
	}
}

/**
 * @brief Process async TX in ISR context
 *
 * @param dev UART device instance
 */
static void uart_qcc74x_async_tx_timeout_active(struct k_work *work);

static void uart_qcc74x_async_tx_timeout(struct k_work *work)
{
	struct uart_qcc74x_data *data = CONTAINER_OF(
		k_work_delayable_from_work(work), struct uart_qcc74x_data,
		async_tx.timeout_work);
	const struct device *dev = data->dev;
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_async_tx_timeout_active(work);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_async_tx_timeout_active(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct uart_qcc74x_data *data =
		CONTAINER_OF(dwork, struct uart_qcc74x_data, async_tx.timeout_work);
	const struct device *dev = data->dev;

	LOG_WRN("TX timeout triggered");
	uart_qcc74x_tx_abort_async(dev);
}

/**
 * @brief RX timeout handler
 *
 * @param work Timeout work structure
 */
static void uart_qcc74x_async_rx_timeout_active(struct k_work *work);

static void uart_qcc74x_async_rx_timeout(struct k_work *work)
{
	struct uart_qcc74x_data *data = CONTAINER_OF(
		k_work_delayable_from_work(work), struct uart_qcc74x_data,
		async_rx.timeout_work);
	const struct device *dev = data->dev;
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_async_rx_timeout_active(work);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_async_rx_timeout_active(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct uart_qcc74x_data *data =
		CONTAINER_OF(dwork, struct uart_qcc74x_data, async_rx.timeout_work);
	const struct device *dev = data->dev;
	const struct uart_qcc74x_config *config = dev->config;
	struct dma_status dma_stat;
	unsigned int key = 0U;
	int ret = 0;

	LOG_DBG("RX Timeout triggered!");

	key = irq_lock();

	/* Only process if RX is still enabled */
	if (data->async_rx.state != UART_ASYNC_RX_ENABLED) {
		irq_unlock(key);
		LOG_DBG("RX timeout but RX not enabled, state=%d - IGNORING", data->async_rx.state);
		return;
	}

	/* Get current DMA status */
	ret = dma_get_status(config->dma_dev, config->dma_rx_channel, &dma_stat);
	if (ret == 0) {
		size_t received = data->async_rx.len - dma_stat.pending_length;

		if (received > data->async_rx.offset) {
			/* New data received */
			data->async_rx.pending_bytes = received - data->async_rx.offset;
			data->async_rx.offset = received;

			irq_unlock(key);
			uart_qcc74x_async_send_pending_data(dev, data);
			key = irq_lock();

			/* Request next buffer if getting full */
			uart_qcc74x_async_request_new_buffer_if_needed(dev);
		}
	}

	irq_unlock(key);
}

/**
 * @brief DMA TX callback
 */
static void uart_qcc74x_dma_tx_callback_active(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status);

static void uart_qcc74x_dma_tx_callback(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status)
{
	const struct device *dev = user_data;
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_dma_tx_callback_active(dma_dev, user_data, channel, status);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_dma_tx_callback_active(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status)
{
	const struct device *dev = user_data;
	struct uart_qcc74x_data *data = dev->data;

	if (status < 0) {
		struct dma_status dma_stat = {};
		const struct uart_qcc74x_config *config = dev->config;

		LOG_ERR("DMA TX error: %d - ABORTING", status);

		data->async_tx.transmitted =
			(dma_get_status(config->dma_dev, config->dma_tx_channel, &dma_stat) == 0)
				? (data->async_tx.len - dma_stat.pending_length)
				: 0;
		uart_qcc74x_async_event_tx_abort(dev);
		return;
	}

	LOG_DBG("TX transfer complete - disabling DMA");

	/* Cancel timeout */
	k_work_cancel_delayable(&data->async_tx.timeout_work);

	/* Disable UART TX DMA */
	qc7xx_uart_link_txdma(&data->hal_uart_cfg, false);
	LOG_DBG("UART TX DMA disabled");

	/* On successful completion, all bytes transmitted */
	data->async_tx.transmitted = data->async_tx.len;
	LOG_DBG("TX complete: %zu bytes transmitted", data->async_tx.transmitted);

	/* Send completion event */
	uart_qcc74x_async_event_tx_done(dev);
}

/**
 * @brief DMA RX callback - called when DMA buffer is FULL
 *
 * @param dev UART device instance
 */
static void uart_qcc74x_dma_rx_callback_active(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status);

static void uart_qcc74x_dma_rx_callback(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status)
{
	const struct device *dev = user_data;
	if (!uart_qcc74x_access_get(dev->data)) {
		return;
	}
	uart_qcc74x_dma_rx_callback_active(dma_dev, user_data, channel, status);
	uart_qcc74x_access_put(dev->data);
}

static void uart_qcc74x_dma_rx_callback_active(const struct device *dma_dev, void *user_data,
				      uint32_t channel, int status)
{
	const struct device *dev = user_data;
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	unsigned int key = 0U;
	int ret = 0;

	key = irq_lock();

	if (data->async_rx.state != UART_ASYNC_RX_ENABLED) {
		irq_unlock(key);
		LOG_WRN("RX callback but RX not enabled (state=%d)", data->async_rx.state);
		return;
	}

	if (status < 0) {
		LOG_ERR("DMA RX error: %d - DISABLING RX", status);
		irq_unlock(key);
		uart_qcc74x_rx_disable_async(dev);
		return;
	}

	/* Buffer is full - send any remaining data */
	if (data->async_rx.offset < data->async_rx.len) {
		data->async_rx.pending_bytes = data->async_rx.len - data->async_rx.offset;
		data->async_rx.offset = data->async_rx.len;
		LOG_DBG("Buffer full - sending remaining %zu bytes", data->async_rx.pending_bytes);
		uart_qcc74x_async_send_pending_data(dev, data);
	}

	/* Handle buffer switching */
	if (data->async_rx.next_buf != NULL) {
		LOG_DBG("Switching to next buffer: %p (len=%zu)", data->async_rx.next_buf,
			data->async_rx.next_buf_len);

		/* Release current buffer */
		uart_qcc74x_async_event_rx_buf_release(dev, UART_ASYNC_CURRENT_BUFFER);

		/* Switch to next buffer */
		uart_qcc74x_async_switch_buffer(dev);

		/* Request another buffer for chaining */
		uart_qcc74x_async_event_rx_buf_request(dev);

		/* Reconfigure and restart DMA with next buffer */
		data->dma_rx.dma_blk.dest_address = (uint32_t)data->async_rx.buf;
		data->dma_rx.dma_blk.block_size = data->async_rx.len;

		ret = dma_config(config->dma_dev, config->dma_rx_channel, &data->dma_rx.dma_cfg);
		if (ret < 0) {
			LOG_ERR("Failed to reconfig DMA: %d", ret);
			irq_unlock(key);
			uart_qcc74x_rx_disable_async(dev);
			return;
		}

		ret = dma_start(config->dma_dev, config->dma_rx_channel);
		if (ret < 0) {
			LOG_ERR("Failed to restart DMA: %d", ret);
			irq_unlock(key);
			uart_qcc74x_rx_disable_async(dev);
			return;
		}

		LOG_DBG("RX DMA restarted with new buffer");
		irq_unlock(key);
	} else {
		LOG_DBG("No next buffer available - requesting one");
		uart_qcc74x_async_event_rx_buf_release(dev, UART_ASYNC_CURRENT_BUFFER);
		uart_qcc74x_async_event_rx_buf_request(dev);

		if (data->async_rx.next_buf != NULL) {
			LOG_DBG("Buffer provided during callback - switching");

			uart_qcc74x_async_switch_buffer(dev);
			uart_qcc74x_async_event_rx_buf_request(dev);

			/* Reconfigure and restart DMA with next buffer */
			data->dma_rx.dma_blk.dest_address = (uint32_t)data->async_rx.buf;
			data->dma_rx.dma_blk.block_size = data->async_rx.len;

			ret = dma_config(config->dma_dev, config->dma_rx_channel,
					 &data->dma_rx.dma_cfg);
			if (ret < 0) {
				LOG_ERR("Failed to reconfig DMA: %d", ret);
				irq_unlock(key);
				uart_qcc74x_rx_disable_async(dev);
				return;
			}

			ret = dma_start(config->dma_dev, config->dma_rx_channel);
			if (ret < 0) {
				LOG_ERR("Failed to restart DMA: %d", ret);
				irq_unlock(key);
				uart_qcc74x_rx_disable_async(dev);
				return;
			}

			LOG_DBG("RX DMA restarted with new buffer");
			irq_unlock(key);
		} else {
			LOG_DBG("No buffer provided - stopping RX");
			uart_qcc74x_async_reset_rx_parameters(data);
			data->async_rx.state = UART_ASYNC_RX_DISABLED;
			uart_qcc74x_rto_interrupt_control(config, data, false, 0);
			k_work_cancel_delayable(&data->async_rx.timeout_work);
			irq_unlock(key);
			uart_qcc74x_async_event_rx_disabled(dev);
		}

		return;
	}
}

/**
 * @brief Configure DMA for async TX
 */
static int uart_qcc74x_dma_tx_configure(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;

	if (!device_is_ready(config->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	/* Setup DMA block config - dest address is UART TX FIFO */
	memset(&data->dma_tx.dma_blk, 0, sizeof(data->dma_tx.dma_blk));
	data->dma_tx.dma_blk.dest_address = config->dma_tx_addr;
	data->dma_tx.dma_blk.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->dma_tx.dma_blk.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	/* Setup DMA config - memory to peripheral */
	memset(&data->dma_tx.dma_cfg, 0, sizeof(data->dma_tx.dma_cfg));
	data->dma_tx.dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	data->dma_tx.dma_cfg.dma_slot = config->dma_tx_slot;
	data->dma_tx.dma_cfg.source_data_size = 1; /* 8-bit */
	data->dma_tx.dma_cfg.dest_data_size = 1;   /* 8-bit */
	data->dma_tx.dma_cfg.source_burst_length = 1;
	data->dma_tx.dma_cfg.dest_burst_length = 1;
	data->dma_tx.dma_cfg.block_count = 1;
	data->dma_tx.dma_cfg.head_block = &data->dma_tx.dma_blk;
	data->dma_tx.dma_cfg.dma_callback = uart_qcc74x_dma_tx_callback;
	data->dma_tx.dma_cfg.user_data = (void *)dev;

	LOG_DBG("TX DMA configured: ch=%d, slot=%d, addr=0x%08x", config->dma_tx_channel,
		config->dma_tx_slot, config->dma_tx_addr);

	return 0;
}

/**
 * @brief Configure DMA for async RX
 */
static int uart_qcc74x_dma_rx_configure(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;

	if (!device_is_ready(config->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	/* Setup DMA block config - source address is UART RX FIFO */
	memset(&data->dma_rx.dma_blk, 0, sizeof(data->dma_rx.dma_blk));
	data->dma_rx.dma_blk.source_address = config->dma_rx_addr;
	data->dma_rx.dma_blk.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->dma_rx.dma_blk.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	/* Setup DMA config - peripheral to memory */
	memset(&data->dma_rx.dma_cfg, 0, sizeof(data->dma_rx.dma_cfg));
	data->dma_rx.dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	data->dma_rx.dma_cfg.dma_slot = config->dma_rx_slot;
	data->dma_rx.dma_cfg.source_data_size = 1; /* 8-bit */
	data->dma_rx.dma_cfg.dest_data_size = 1;   /* 8-bit */
	data->dma_rx.dma_cfg.source_burst_length = 1;
	data->dma_rx.dma_cfg.dest_burst_length = 1;
	data->dma_rx.dma_cfg.block_count = 1;
	data->dma_rx.dma_cfg.head_block = &data->dma_rx.dma_blk;
	data->dma_rx.dma_cfg.dma_callback = uart_qcc74x_dma_rx_callback;
	data->dma_rx.dma_cfg.user_data = (void *)dev;

	LOG_DBG("RX DMA configured: ch=%d, slot=%d, addr=0x%08x", config->dma_rx_channel,
		config->dma_rx_slot, config->dma_rx_addr);

	return 0;
}

/**
 * @brief Enable/disable and configure RTO interrupt
 *
 * @param config Pointer to device configuration
 * @param data Pointer to runtime data structure
 * @param enable true to enable RTO interrupt, false to disable
 * @param timeout_bits Timeout value expressed in UART bit-times (hardware units)
 */
static inline void uart_qcc74x_rto_interrupt_control(const struct uart_qcc74x_config *config,
						   struct uart_qcc74x_data *data, bool enable,
						   uint32_t timeout_bits)
{
	if (enable) {
		qc7xx_uart_feature_control(&data->hal_uart_cfg, UART_CMD_SET_RTO_VALUE,
					   timeout_bits);

		/* Enable RTO interrupt */
		uint32_t int_en = sys_read32(config->base_reg + UART_INT_EN_OFFSET);
		int_en |= UART_URX_RTO_INT;
		sys_write32(int_en, config->base_reg + UART_INT_EN_OFFSET);

		LOG_DBG("RTO interrupt enabled with %u bit timeout", timeout_bits);
	} else {
		qc7xx_uart_feature_control(&data->hal_uart_cfg, UART_CMD_SET_RTO_VALUE, 0);

		/* Disable RTO interrupt */
		uint32_t int_en = sys_read32(config->base_reg + UART_INT_EN_OFFSET);
		int_en &= ~UART_URX_RTO_INT;
		sys_write32(int_en, config->base_reg + UART_INT_EN_OFFSET);

		LOG_DBG("RTO interrupt disabled");
	}
}

/**
 * @brief Configure hardware RX timeout (RTO)
 *
 * @param config Pointer to device configuration
 * @param data Pointer to runtime data structure
 * @param timeout_us Timeout in microseconds
 */
static void uart_qcc74x_async_configure_rx_timeout(const struct uart_qcc74x_config *config,
						 struct uart_qcc74x_data *data, int32_t timeout_us)
{
	const uint32_t baudrate = data->current_cfg.baudrate;
	uint64_t bits_before_timeout;

	/*
	 * Do not calculate the bit time first: integer microseconds round to
	 * zero at 10 Mbps. Calculate the number of bit periods directly and
	 * round up, so the result remains valid at the maximum supported baud.
	 */
	bits_before_timeout = ((uint64_t)(uint32_t)timeout_us * baudrate +
				      USEC_PER_SEC - 1U) / USEC_PER_SEC;

	/* Clamp to hardware limits (RTO register is 8-bit) */
	if (bits_before_timeout > UINT8_MAX) {
		bits_before_timeout = UINT8_MAX;
	} else if (bits_before_timeout < 4) {
		bits_before_timeout = 4;
	}

	/* Enable hardware RTO interrupt */
	uart_qcc74x_rto_interrupt_control(config, data, true, bits_before_timeout);

	LOG_DBG("RX timeout configured: HW=%u bits", (uint32_t)bits_before_timeout);
}

/**
 * @brief Prepare RX hardware before starting DMA
 *
 * @param config Pointer to device configuration
 * @param data Pointer to runtime data structure
 */
static void uart_qcc74x_async_rx_prepare_hardware(const struct uart_qcc74x_config *config,
						struct uart_qcc74x_data *data)
{
	/* Clear RX FIFO */
	qc7xx_uart_feature_control(&data->hal_uart_cfg, UART_CMD_CLR_RX_FIFO, 0);
	k_busy_wait(10);

	/* Clear any stale RTO interrupt */
	qc7xx_uart_int_clear(&data->hal_uart_cfg, UART_INTCLR_RTO);
	k_busy_wait(10);

	/* Flush any remaining data in FIFO */
	while (qc7xx_uart_rxavailable(&data->hal_uart_cfg)) {
		(void)qc7xx_uart_getchar(&data->hal_uart_cfg);
	}

	LOG_DBG("RX hardware prepared");
}

static int uart_qcc74x_callback_set(const struct device *dev, uart_callback_t callback,
				  void *user_data)
{
	struct uart_qcc74x_data *data = dev->data;
	k_spinlock_key_t key = {0};

	LOG_DBG("Setting async callback: %p, user_data: %p", callback, user_data);

	key = k_spin_lock(&data->lock);
	data->async_cb = callback;
	data->async_user_data = user_data;
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int uart_qcc74x_tx_async_active(const struct device *dev, const uint8_t *buf, size_t len,
			      int32_t timeout);

static int uart_qcc74x_tx_async(const struct device *dev, const uint8_t *buf, size_t len,
			      int32_t timeout)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_tx_async_active(dev, buf, len, timeout);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_tx_async_active(const struct device *dev, const uint8_t *buf, size_t len,
			      int32_t timeout)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	k_spinlock_key_t key = {0};
	int ret = 0;

	LOG_DBG("TX async: buf=%p, len=%zu, timeout=%d", buf, len, timeout);

	if (buf == NULL || len == 0) {
		LOG_ERR("Invalid TX buffer or length");
		return -EINVAL;
	}

	if (config->dma_dev == NULL) {
		LOG_ERR("DMA not configured for UART");
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);

	if (data->async_tx.state != UART_ASYNC_TX_IDLE) {
		k_spin_unlock(&data->lock, key);
		LOG_DBG("TX busy, state=%d", data->async_tx.state);
		return -EBUSY;
	}

	/* Store TX parameters */
	data->async_tx.buf = buf;
	data->async_tx.len = len;
	data->async_tx.transmitted = 0;
	data->async_tx.state = UART_ASYNC_TX_ACTIVE;

	k_spin_unlock(&data->lock, key);

	/* Configure DMA transfer */
	data->dma_tx.dma_blk.source_address = (uint32_t)buf;
	data->dma_tx.dma_blk.block_size = len;

	ret = dma_config(config->dma_dev, config->dma_tx_channel, &data->dma_tx.dma_cfg);
	if (ret < 0) {
		LOG_ERR("DMA config failed: %d", ret);
		data->async_tx.state = UART_ASYNC_TX_IDLE;
		return ret;
	}

	/* Start TX timer */
	if (timeout != SYS_FOREVER_US && timeout > 0) {
		k_work_reschedule(&data->async_tx.timeout_work, K_USEC(timeout));
		LOG_DBG("TX timeout timer started: %d us", timeout);
	}

	/* Enable UART TX DMA */
	qc7xx_uart_link_txdma(&data->hal_uart_cfg, true);

	/* Start DMA transfer */
	LOG_DBG("Starting DMA transfer on channel %d", config->dma_tx_channel);
	ret = dma_start(config->dma_dev, config->dma_tx_channel);
	if (ret < 0) {
		LOG_ERR("DMA start failed: %d", ret);
		qc7xx_uart_link_txdma(&data->hal_uart_cfg, false);
		data->async_tx.state = UART_ASYNC_TX_IDLE;
		return ret;
	}

	return 0;
}

static int uart_qcc74x_tx_abort_async_active(const struct device *dev);

static int uart_qcc74x_tx_abort_async(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_tx_abort_async_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_tx_abort_async_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct dma_status dma_stat = {};
	unsigned int key = 0U;
	int ret = 0;

	LOG_DBG("TX abort requested");

	key = irq_lock();

	if (data->async_tx.state != UART_ASYNC_TX_ACTIVE) {
		irq_unlock(key);
		LOG_WRN("No TX in progress to abort");
		return -EINVAL;
	}
	/* Save original length before clearing state */
	size_t tx_requested = data->async_tx.len;

	dma_stop(config->dma_dev, config->dma_tx_channel);

	/* Get DMA status to determine how much was actually transmitted */
	ret = dma_get_status(config->dma_dev, config->dma_tx_channel, &dma_stat);
	if (ret == 0) {
		data->async_tx.transmitted = tx_requested - dma_stat.pending_length;
		LOG_DBG("TX abort: requested=%zu, pending=%u, transmitted=%zu", tx_requested,
			dma_stat.pending_length, data->async_tx.transmitted);
	} else {
		data->async_tx.transmitted = 0;
		LOG_WRN("Failed to get DMA status: %d, assuming 0 bytes transmitted", ret);
	}

	/* Disable UART TX DMA */
	qc7xx_uart_link_txdma(&data->hal_uart_cfg, false);
	/* Cancel timeout */
	k_work_cancel_delayable(&data->async_tx.timeout_work);

	irq_unlock(key);

	/* Send abort event with actual transmitted count */
	uart_qcc74x_async_event_tx_abort(dev);

	return 0;
}

static int uart_qcc74x_rx_enable_async_active(const struct device *dev, uint8_t *buf, size_t len,
				     int32_t timeout);

static int uart_qcc74x_rx_enable_async(const struct device *dev, uint8_t *buf, size_t len,
				     int32_t timeout)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_rx_enable_async_active(dev, buf, len, timeout);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_rx_enable_async_active(const struct device *dev, uint8_t *buf, size_t len,
				     int32_t timeout)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	k_spinlock_key_t key = {0};
	int ret = 0;

	LOG_DBG("RX enable: buf=%p, len=%zu, timeout=%d", buf, len, timeout);

	if (buf == NULL || len == 0) {
		LOG_ERR("Invalid RX buffer or length");
		return -EINVAL;
	}

	if (config->dma_dev == NULL) {
		LOG_ERR("DMA not configured for UART");
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);

	if (data->async_rx.state != UART_ASYNC_RX_DISABLED) {
		k_spin_unlock(&data->lock, key);
		LOG_ERR("RX already enabled, state=%d", data->async_rx.state);
		return -EBUSY;
	}

	/* Prepare hardware - clear FIFO, clear interrupts, flush data */
	uart_qcc74x_async_rx_prepare_hardware(config, data);

	/* Setup RX params */
	uart_qcc74x_async_reset_rx_parameters(data);
	data->async_rx.buf = buf;
	data->async_rx.len = len;
	data->async_rx.offset = 0;
	data->async_rx.state = UART_ASYNC_RX_ENABLED;

	if (timeout != SYS_FOREVER_US && timeout > 0) {
		uart_qcc74x_async_configure_rx_timeout(config, data, timeout);
	} else {
		uart_qcc74x_rto_interrupt_control(config, data, false, 0);
		LOG_DBG("RX timeout disabled (infinite)");
	}

	k_spin_unlock(&data->lock, key);

	data->dma_rx.dma_blk.dest_address = (uint32_t)buf;
	data->dma_rx.dma_blk.block_size = len;

	ret = dma_config(config->dma_dev, config->dma_rx_channel, &data->dma_rx.dma_cfg);
	if (ret < 0) {
		LOG_ERR("DMA config failed: %d", ret);
		data->async_rx.state = UART_ASYNC_RX_DISABLED;
		return ret;
	}

	qc7xx_uart_link_rxdma(&data->hal_uart_cfg, true);

	ret = dma_start(config->dma_dev, config->dma_rx_channel);
	if (ret < 0) {
		LOG_ERR("DMA start failed: %d", ret);
		qc7xx_uart_link_rxdma(&data->hal_uart_cfg, false);
		data->async_rx.state = UART_ASYNC_RX_DISABLED;
		return ret;
	}

	LOG_DBG("RX enabled successfully");
	return 0;
}

static int uart_qcc74x_rx_buf_rsp_async_active(const struct device *dev, uint8_t *buf, size_t len);

static int uart_qcc74x_rx_buf_rsp_async(const struct device *dev, uint8_t *buf, size_t len)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_rx_buf_rsp_async_active(dev, buf, len);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_rx_buf_rsp_async_active(const struct device *dev, uint8_t *buf, size_t len)
{
	struct uart_qcc74x_data *data = dev->data;
	k_spinlock_key_t key = {0};

	LOG_DBG("RX buffer response: buf=%p, len=%zu", buf, len);

	if (buf == NULL || len == 0) {
		LOG_ERR("Invalid buffer response");
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	if (data->async_rx.state != UART_ASYNC_RX_ENABLED) {
		k_spin_unlock(&data->lock, key);
		LOG_ERR("RX not enabled");
		return -EINVAL;
	}

	if (data->async_rx.next_buf != NULL) {
		LOG_WRN("Next buffer already set, replacing");
	}

	data->async_rx.next_buf = buf;
	data->async_rx.next_buf_len = len;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Next RX buffer set successfully");

	return 0;
}

static int uart_qcc74x_rx_disable_async_active(const struct device *dev);

static int uart_qcc74x_rx_disable_async(const struct device *dev)
{
	if (!uart_qcc74x_access_get(dev->data)) {
		return -EACCES;
	}
	int ret = uart_qcc74x_rx_disable_async_active(dev);
	uart_qcc74x_access_put(dev->data);
	return ret;
}

static int uart_qcc74x_rx_disable_async_active(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct dma_status dma_stat;
	unsigned int key = 0U;
	int ret = 0;

	LOG_DBG("RX disable requested, state=%d", data->async_rx.state);

	key = irq_lock();

	if (data->async_rx.state == UART_ASYNC_RX_DISABLED) {
		irq_unlock(key);
		LOG_DBG("RX already disabled - ignoring");
		return 0;
	}

	if (data->async_rx.state == UART_ASYNC_RX_DISABLING) {
		irq_unlock(key);
		LOG_WRN("RX disable already in progress");
		return -EBUSY;
	}

	data->async_rx.state = UART_ASYNC_RX_DISABLING;

	/* Stop DMA */
	LOG_DBG("Stopping DMA channel %d", config->dma_rx_channel);
	ret = dma_stop(config->dma_dev, config->dma_rx_channel);
	if (ret < 0) {
		LOG_ERR("DMA stop failed: %d", ret);
	}

	/* Get remaining data from DMA */
	ret = dma_get_status(config->dma_dev, config->dma_rx_channel, &dma_stat);
	if (ret == 0) {
		size_t received = data->async_rx.len - dma_stat.pending_length;
		LOG_DBG("DMA stopped - received %zu/%zu bytes", received, data->async_rx.len);

		if (received > data->async_rx.offset) {
			data->async_rx.pending_bytes = received - data->async_rx.offset;
			data->async_rx.offset = received;
			LOG_DBG("Pending data to send: %zu bytes", data->async_rx.pending_bytes);
		}
	} else {
		LOG_WRN("Failed to get DMA status: %d", ret);
	}

	/* Disable RX DMA in UART hardware register */
	qc7xx_uart_link_rxdma(&data->hal_uart_cfg, false);
	k_work_cancel_delayable(&data->async_rx.timeout_work);

	/* Disable RTO interrupt */
	uart_qcc74x_rto_interrupt_control(config, data, false, 0);

	uart_qcc74x_async_send_pending_data(dev, data);
	uart_qcc74x_async_event_rx_buf_release(dev, UART_ASYNC_CURRENT_BUFFER);
	uart_qcc74x_async_event_rx_buf_release(dev, UART_ASYNC_NEXT_BUFFER);

	uart_qcc74x_async_reset_rx_parameters(data);
	data->async_rx.state = UART_ASYNC_RX_DISABLED;
	uart_qcc74x_async_event_rx_disabled(dev);

	irq_unlock(key);

	LOG_DBG("RX disabled successfully");
	return 0;
}

#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
/**
 * @brief Initialize UART Interrupt API
 *
 * @param dev UART device instance
 */
static void uart_qcc74x_init_interrupt(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	uint32_t int_enable = 0;

	/* Disable all interrupts */
	sys_write32(0x0, config->base_reg + UART_INT_EN_OFFSET);

	/* Clear all pending interrupts */
	sys_write32(UART_INT_CLEAR_ALL, config->base_reg + UART_INT_CLEAR_OFFSET);

	/* Mask all interrupts */
	sys_write32(UART_INT_MASK_ALL, config->base_reg + UART_INT_MASK_OFFSET);

	/* Configure interrupt controller */
	if (config->irq_config_func) {
		config->irq_config_func(dev);
	} else {
		LOG_WRN("No irq_config_func provided for %s", dev->name);
	}

	/* Enable selected interrupts */
	int_enable |= UART_UTX_FIFO_INT; /* UART TxFIFO ready */
	int_enable |= UART_URX_FIFO_INT; /* UART RxFIFO ready */
	int_enable |= UART_URX_FER_INT;  /* UART RxFIFO error */
	int_enable |= UART_URX_PCE_INT;  /* UART Rx parity check error */
	sys_write32(int_enable, config->base_reg + UART_INT_EN_OFFSET);

	LOG_DBG("UART interrupts enabled: 0x%x (RTO disabled)", int_enable);

	/* Initialize interrupt-driven callback */
	data->user_cb = NULL;
	data->user_data = NULL;
}

#ifdef CONFIG_UART_ASYNC_API
/**
 * @brief Initialize UART Async
 *
 * @param dev UART device instance
 */
static void uart_qcc74x_init_async(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	int ret = 0;

	data->dev = dev;

	if (config->dma_dev == NULL) {
		LOG_WRN("UART %s: No DMA, async API disabled", dev->name);
		data->async_cb = NULL;
		data->async_user_data = NULL;
		return;
	}

	/* Initialize TX DMA configuration */
	ret = uart_qcc74x_dma_tx_configure(dev);
	if (ret < 0) {
		LOG_ERR("Failed to configure TX DMA: %d", ret);
	}

	/* Initialize RX DMA configuration */
	ret = uart_qcc74x_dma_rx_configure(dev);
	if (ret < 0) {
		LOG_ERR("Failed to configure RX DMA: %d", ret);
	}

	/* Initialize async work items */
	k_work_init_delayable(&data->async_tx.timeout_work, uart_qcc74x_async_tx_timeout);
	k_work_init_delayable(&data->async_rx.timeout_work, uart_qcc74x_async_rx_timeout);

	/* Initialize async state */
	data->async_tx.state = UART_ASYNC_TX_IDLE;
	data->async_tx.buf = NULL;
	data->async_tx.len = 0;
	data->async_tx.transmitted = 0;

	uart_qcc74x_async_reset_rx_parameters(data);
	data->async_rx.state = UART_ASYNC_RX_DISABLED;

	if (config->dma_dev != NULL) {
		uint32_t int_en = sys_read32(config->base_reg + UART_INT_EN_OFFSET);
		int_en &= ~UART_URX_RTO_INT;
		sys_write32(int_en, config->base_reg + UART_INT_EN_OFFSET);
		LOG_DBG("RTO interrupt disabled during init");
	}

	data->async_cb = NULL;
	data->async_user_data = NULL;

	LOG_DBG("Async API initialized");
}

#endif /* CONFIG_UART_ASYNC_API */

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

/**
 * @brief Initialize UART device
 *
 * @param dev UART device instance
 * @return 0 on success, negative errno on failure
 */
static int32_t uart_qcc74x_init(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct uart_config initial_cfg = {0};
	int ret = 0;

	LOG_DBG("Initializing UART %s", dev->name);

	ret = reset_line_toggle_dt(&config->reset);
	if (ret < 0) {
		LOG_ERR("UART reset line toggle failed: %d", ret);
		return -EIO;
	}

	/* Apply pin configuration */
	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Failed to apply pinctrl state: %d", ret);
		return ret;
	}

#ifdef CONFIG_UART_ASYNC_API
	uart_qcc74x_init_async(dev);
#endif /* CONFIG_UART_ASYNC_API */

	/* Initialize default UART configuration from DTS */
	initial_cfg.baudrate = data->current_cfg.baudrate;
	initial_cfg.parity = data->current_cfg.parity;
	initial_cfg.stop_bits = data->current_cfg.stop_bits;
	initial_cfg.data_bits = data->current_cfg.data_bits;
	initial_cfg.flow_ctrl = data->current_cfg.flow_ctrl;

	/* Configure UART with initial settings */
	ret = uart_qcc74x_configure(dev, &initial_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure UART: %d", ret);
		return ret;
	}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_qcc74x_init_interrupt(dev);
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

	/* Enable UART */
	uart_qcc74x_set_enabled(dev, true);

	LOG_DBG("UART %s initialization complete", dev->name);
	return 0;
}

#ifdef CONFIG_PM_DEVICE
/* Restore hardware only; preserve application callbacks and DMA buffers. */
static int uart_qcc74x_hw_restore(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qcc74x_clk_settings settings = {
		.enable = CLOCK_ENABLE,
		.clk_src = driver_global_data.clk_src,
		.clk_div = driver_global_data.div_val,
	};
	int ret = clock_control_configure(config->clock_dev, config->clock_id, &settings);

	if (ret != 0) {
		return ret;
	}
	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}
	sys_write32(UINT32_MAX, config->base_reg + UART_INT_MASK_OFFSET);
	ret = uart_qcc74x_configure_active(dev, &data->current_cfg);
	qc7xx_uart_int_clear(&data->hal_uart_cfg,
		UART_INTCLR_TX_END | UART_INTCLR_RX_END | UART_INTCLR_RTO |
		UART_INTCLR_PCE | UART_INTCLR_RX_LSE | UART_INTCLR_RX_BCR |
		UART_INTCLR_RX_ADS | UART_INTCLR_RX_AD5);
	return ret;
}

#ifdef CONFIG_QCC74X_PM_LPFW
static bool uart_qc7xx_system_pds15(void)
{
	unsigned int key = irq_lock();
	const struct pm_state_info *state = pm_state_next_get(0);
	bool allowed = !arch_irq_unlocked(key) &&
		state->state == PM_STATE_SUSPEND_TO_RAM &&
		state->substate_id == QCC74X_PM_LPFW_SUBSTATE_ID &&
		!state->pm_device_disabled;

	irq_unlock(key);
	return allowed;
}

static FUNC_NORETURN void uart_qc7xx_pm_stop(struct uart_qcc74x_data *data,
					 int rc)
{
	(void)irq_lock();
	data->pm_stats.last_rc = rc;
	data->pm_stats.failures++;
	/* Do not let the manager continue after a failed system RESUME. */
	for (;;) {
		arch_nop();
	}
}
#endif

static void uart_qc7xx_pm_irq_restore(const struct device *dev)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *uart = &data->hal_uart_cfg;

	irq_disable(config->irq_source);
#if defined(CONFIG_QCC74X_PM_LPFW) && \
	defined(CONFIG_RISCV_HAS_CLIC)
	/* Include UARTs explicitly suspended before the system transition. */
	CLIC->CLICINT[config->irq_source].ATTR = data->saved_clic_attr;
	CLIC->CLICINT[config->irq_source].CTL = data->saved_clic_ctl;
#endif
	sys_write32(data->saved_irq_mask,
		    uart->reg_base + UART_INT_MASK_OFFSET);
	if (data->saved_irq_enabled) {
		irq_enable(config->irq_source);
	}
}

static int uart_qc7xx_pm_action(const struct device *dev,
			       enum pm_device_action action)
{
	const struct uart_qcc74x_config *config = dev->config;
	struct uart_qcc74x_data *data = dev->data;
	struct qc7xx_device_s *uart = &data->hal_uart_cfg;
	k_spinlock_key_t key;
	int ret = 0;
#ifdef CONFIG_QCC74X_PM_LPFW
	bool system_pds15 = uart_qc7xx_system_pds15();

	/* Shallow idle retains UART power and interrupt service. */
	if (action == PM_DEVICE_ACTION_SUSPEND &&
	    pm_state_next_get(0)->state != PM_STATE_ACTIVE && !system_pds15) {
		return -ENOTSUP;
	}
#endif

	if (action != PM_DEVICE_ACTION_SUSPEND &&
	    action != PM_DEVICE_ACTION_RESUME) {
		return -ENOTSUP;
	}
	if (uart == NULL) {
		return -ENODEV;
	}
	if (action == PM_DEVICE_ACTION_SUSPEND && config->pm_protected) {
#ifdef CONFIG_QCC74X_PM_LPFW
		if (!system_pds15) {
			return -EBUSY;
		}
#else
		return -EBUSY;
#endif
	}

	key = k_spin_lock(&data->lock);
	if (data->transitioning || data->pm_users != 0U) {
		ret = -EBUSY;
		goto out;
	}

	if (action == PM_DEVICE_ACTION_SUSPEND) {
#ifdef CONFIG_QCC74X_PM_LPFW
		/* An empty FIFO does not exclude queued shell TX. */
		if (system_pds15 &&
		    !(sys_read32(uart->reg_base + UART_INT_MASK_OFFSET) &
		      UART_CR_UTX_FIFO_MASK)) {
			ret = -EBUSY;
			goto out;
		}
#endif
#ifdef CONFIG_UART_ASYNC_API
		if (data->async_tx.state != UART_ASYNC_TX_IDLE ||
		    data->async_rx.state != UART_ASYNC_RX_DISABLED ||
		    k_work_busy_get(&data->async_tx.timeout_work.work) ||
		    k_work_busy_get(&data->async_rx.timeout_work.work)) {
			ret = -EBUSY;
			goto out;
		}
#endif
		if (!qc7xx_uart_txempty(uart) || qc7xx_uart_rxavailable(uart) ||
		    (sys_read32(uart->reg_base + UART_STATUS_OFFSET) &
		     (UART_STS_UTX_BUS_BUSY | UART_STS_URX_BUS_BUSY)) ||
		    qc7xx_uart_get_intstatus(uart)) {
			ret = -EBUSY;
			goto out;
		}
		data->saved_irq_mask =
			sys_read32(uart->reg_base + UART_INT_MASK_OFFSET);
		data->saved_irq_enabled = irq_is_enabled(config->irq_source);
#ifdef CONFIG_QCC74X_PM_LPFW
		data->pds15_snapshot = system_pds15;
#ifdef CONFIG_RISCV_HAS_CLIC
		data->saved_clic_attr = CLIC->CLICINT[config->irq_source].ATTR;
		data->saved_clic_ctl = CLIC->CLICINT[config->irq_source].CTL;
#endif
#endif
		irq_disable(config->irq_source);
		sys_write32(UINT32_MAX, uart->reg_base + UART_INT_MASK_OFFSET);
		qc7xx_uart_disable(uart);
		data->suspended = true;
	} else {
		/* Pinctrl may lock: restore with access gated. */
		data->transitioning = true;
		k_spin_unlock(&data->lock, key);
		ret = uart_qcc74x_hw_restore(dev);
#ifdef CONFIG_QCC74X_PM_LPFW
		if (ret != 0 && data->pds15_snapshot) {
			data->pm_stats.retries++;
			ret = uart_qcc74x_hw_restore(dev);
		}
#endif
		key = k_spin_lock(&data->lock);
		if (ret == 0) {
			uart_qc7xx_pm_irq_restore(dev);
			data->suspended = false;
		}
		data->transitioning = false;
	}
out:
#ifdef CONFIG_QCC74X_PM_LPFW
	data->pm_stats.last_rc = ret;
	if (action == PM_DEVICE_ACTION_SUSPEND) {
		if (ret == 0) {
			data->pm_stats.suspends++;
		} else {
			data->pm_stats.rejected++;
		}
	} else if (ret == 0) {
		data->pm_stats.resumes++;
		data->pds15_snapshot = false;
	} else if (data->pds15_snapshot) {
		uart_qc7xx_pm_stop(data, ret);
	}
#endif
	k_spin_unlock(&data->lock, key);
	return ret;
}
#endif

#ifdef CONFIG_QCC74X_PM_LPFW
void uart_qc7xx_pm_stats_get(const struct device *dev,
			   struct uart_qc7xx_pm_stats *stats)
{
	struct uart_qcc74x_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	*stats = data->pm_stats;
	stats->suspended = data->suspended;
	k_spin_unlock(&data->lock, key);
}
#endif

/* UART driver API structure */
static const struct uart_driver_api uart_qcc74x_driver_api = {
	.poll_in = uart_qcc74x_poll_in,
	.poll_out = uart_qcc74x_poll_out,
	.configure = uart_qcc74x_configure,
	.config_get = uart_qcc74x_config_get,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.err_check = uart_qcc74x_err_check,
	.fifo_fill = uart_qcc74x_fifo_fill,
	.fifo_read = uart_qcc74x_fifo_read,
	.irq_tx_enable = uart_qcc74x_irq_tx_enable,
	.irq_tx_disable = uart_qcc74x_irq_tx_disable,
	.irq_tx_ready = uart_qcc74x_irq_tx_ready,
	.irq_tx_complete = uart_qcc74x_irq_tx_complete,
	.irq_rx_enable = uart_qcc74x_irq_rx_enable,
	.irq_rx_disable = uart_qcc74x_irq_rx_disable,
	.irq_rx_ready = uart_qcc74x_irq_rx_ready,
	.irq_err_enable = uart_qcc74x_irq_err_enable,
	.irq_err_disable = uart_qcc74x_irq_err_disable,
	.irq_is_pending = uart_qcc74x_irq_is_pending,
	.irq_update = uart_qcc74x_irq_update,
	.irq_callback_set = uart_qcc74x_irq_callback_set,
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_ASYNC_API
	.callback_set = uart_qcc74x_callback_set,
	.tx = uart_qcc74x_tx_async,
	.tx_abort = uart_qcc74x_tx_abort_async,
	.rx_enable = uart_qcc74x_rx_enable_async,
	.rx_buf_rsp = uart_qcc74x_rx_buf_rsp_async,
	.rx_disable = uart_qcc74x_rx_disable_async,
#endif /* CONFIG_UART_ASYNC_API */
};

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define UART_QCC74X_IRQ_HANDLER_DECL(n)                                                              \
	static void uart_qcc74x_config_func_##n(const struct device *dev);

#define UART_QCC74X_IRQ_HANDLER_FUNC(n) .irq_config_func = uart_qcc74x_config_func_##n

#define UART_QCC74X_IRQ_HANDLER(n)                                                                   \
	static void uart_qcc74x_config_func_##n(const struct device *dev)                            \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), uart_qcc74x_isr,              \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}
#else /* CONFIG_UART_INTERRUPT_DRIVEN */
#define UART_QCC74X_IRQ_HANDLER_DECL(n)
#define UART_QCC74X_IRQ_HANDLER_FUNC(n)
#define UART_QCC74X_IRQ_HANDLER(n)
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_ASYNC_API
/* Determine init level based on DMA presence */
#define UART_QCC74X_INIT_LEVEL(n)                                                                    \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                                  \
		    (POST_KERNEL),  /* Has DMA -> POST_KERNEL */                                \
		    (PRE_KERNEL_1)) /* No DMA -> PRE_KERNEL_1 */

#define UART_QCC74X_INIT_PRIORITY(n)                                                                 \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                                  \
		    (CONFIG_SERIAL_INIT_PRIORITY),  /* Has DMA -> default priority */              \
		    (CONFIG_KERNEL_INIT_PRIORITY_DEFAULT)) /* No DMA -> earlier priority */

#define UART_QCC74X_DMA_INIT(n)                                                                      \
	.dma_dev = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                       \
			       (DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, tx))), (NULL)),                          \
		 .dma_tx_channel = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                \
				      (DT_INST_DMAS_CELL_BY_NAME(n, tx, channel)), (0)),                           \
			  .dma_rx_channel = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                \
				      (DT_INST_DMAS_CELL_BY_NAME(n, rx, channel)), (0)),                  \
				   .dma_tx_slot = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                   \
				   (DT_INST_DMAS_CELL_BY_NAME(n, tx, slot)), (0)),            \
					    .dma_rx_slot = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                   \
				   (DT_INST_DMAS_CELL_BY_NAME(n, rx, slot)), (0)),   \
						     .dma_tx_addr = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                   \
				   (DT_INST_DMAS_CELL_BY_NAME(n, tx, per_fifo_addr)), (0)),                                \
							      .dma_rx_addr = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                   \
				   (DT_INST_DMAS_CELL_BY_NAME(n, rx, per_fifo_addr)), (0)),

#else /* !CONFIG_UART_ASYNC_API */
#define UART_QCC74X_INIT_LEVEL(n)                                                                    \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                                  \
		    (POST_KERNEL),  /* Has DMA in DT -> POST_KERNEL */                             \
		    (PRE_KERNEL_1)) /* No DMA in DT -> PRE_KERNEL_1 */

#define UART_QCC74X_INIT_PRIORITY(n)                                                                 \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                                  \
		    (CONFIG_SERIAL_INIT_PRIORITY),  /* Has DMA -> default priority */              \
		    (CONFIG_KERNEL_INIT_PRIORITY_DEFAULT)) /* No DMA -> earlier priority */

#define UART_QCC74X_DMA_INIT(n) /* Empty - no DMA fields in config */
#endif                        /* CONFIG_UART_ASYNC_API */

#define UART_QCC74X_IS_CHOSEN(n, chosen) \
	COND_CODE_1(DT_HAS_CHOSEN(chosen), \
		    (DT_SAME_NODE(DT_DRV_INST(n), DT_CHOSEN(chosen))), (0))

#define UART_QCC74X_DEVICE(n)                                                                        \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	UART_QCC74X_IRQ_HANDLER_DECL(n)                                                              \
	static struct uart_qcc74x_data uart##n##_data = {                                            \
		.hal_uart_cfg =                                                                    \
			{                                                                          \
				.reg_base = DT_INST_REG_ADDR(n),                                   \
			},                                                                         \
		.current_cfg =                                                                     \
			{                                                                          \
				.baudrate = DT_INST_PROP(n, current_speed),                        \
				.parity = DT_INST_ENUM_IDX_OR(n, parity, UART_CFG_PARITY_NONE),    \
				.stop_bits =                                                       \
					DT_INST_ENUM_IDX_OR(n, stop_bits, UART_CFG_STOP_BITS_1),   \
				.data_bits =                                                       \
					DT_INST_ENUM_IDX_OR(n, data_bits, UART_CFG_DATA_BITS_8),   \
				.flow_ctrl = DT_INST_PROP(n, hw_flow_control)                      \
						     ? UART_CFG_FLOW_CTRL_RTS_CTS                  \
						     : UART_CFG_FLOW_CTRL_NONE,                    \
			},                                                                         \
	};                                                                                         \
	static const struct uart_qcc74x_config uart##n##_config = {                                  \
		.irq_source = DT_INST_IRQN(n), \
		.pm_protected = UART_QCC74X_IS_CHOSEN(n, zephyr_console) || \
			UART_QCC74X_IS_CHOSEN(n, zephyr_shell_uart) || \
			UART_QCC74X_IS_CHOSEN(n, zephyr_log_uart), \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.clock_src = (uint8_t)DT_INST_CLOCKS_CELL(n, source),                              \
		.base_reg = DT_INST_REG_ADDR(n),                                                   \
		.tx_fifo_threshold = UART_FIFO_TX_THRESHOLD_DEFAULT,                               \
		.rx_fifo_threshold = UART_FIFO_RX_THRESHOLD_DEFAULT,                               \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
		UART_QCC74X_DMA_INIT(n) UART_QCC74X_IRQ_HANDLER_FUNC(n)};                              \
	IF_ENABLED(CONFIG_PM_DEVICE,                                                               \
		(PM_DEVICE_DT_INST_DEFINE(n, uart_qc7xx_pm_action);))                            \
	DEVICE_DT_INST_DEFINE(n, &uart_qcc74x_init, PM_DEVICE_DT_INST_GET(n), &uart##n##_data,        \
			      &uart##n##_config, UART_QCC74X_INIT_LEVEL(n),                          \
			      UART_QCC74X_INIT_PRIORITY(n), &uart_qcc74x_driver_api);                  \
	UART_QCC74X_IRQ_HANDLER(n)

DT_INST_FOREACH_STATUS_OKAY(UART_QCC74X_DEVICE)
