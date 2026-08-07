/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_sdhc

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <qc7xx_sdh.h>
#include <hardware/sdh_reg.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sdhc_qcc74x, CONFIG_SDHC_LOG_LEVEL);

#define QCC74X_SDHC_HS_MODE_CLOCK   (MHZ(25))
#define QCC74X_SDHC_DEFAULT_TIMEOUT (1000U)
#define QCC74X_SDHC_ADMA2_DESC_NUM  (32)

/* Configuration for a Opal SDHC controller instance */
struct sdhc_qcc74x_cfg {
	struct sdhc_host_props *props;
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const uint8_t clock_source;
	void (*irq_configure)(void);
	const struct reset_dt_spec reset;
	uint8_t instance_id;
	uint32_t base_addr;
	uint32_t irq_num;
	uint32_t irq_priority;
	uint32_t irq_flags;
};

/* Runtime data for a Opal SDHC controller instance */
struct sdhc_qcc74x_data {
	struct qc7xx_device_s sdh_dev;
	struct qc7xx_sdh_adma2_hw_desc_s *adma2_hw_desc;
	uint32_t src_clock;
	uint32_t actual_clock;
	uint32_t bus_clock;
	uint32_t bus_width;
	uint32_t timeout_ms;
	struct k_sem transfer_sem;
	struct k_mutex access_mutex;
};

/**
 * @brief Sets the SDHC bus clock frequency.
 *
 * Configures the SDHC bus clock to the specified frequency by calculating the
 * appropriate clock divider based on a 96 MHz source clock. The actual
 * frequency set may differ slightly due to divider constraints.
 *
 * @param dev Pointer to the SDHC device structure.
 * @param freq Desired bus clock frequency in Hz.
 * @return int Actual frequency set in Hz, or 0 if frequency is set to 0.
 */
int sdhc_qcc74x_set_bus_clock(const struct device *dev, uint32_t freq)
{
	int ret = 0;
	uint32_t actual_freq_hz = 0U;
	const struct sdhc_qcc74x_cfg *dev_cfg = dev->config;
	struct sdhc_qcc74x_data *dev_data = dev->data;
	struct qcc74x_clk_settings clk_settings = {
		.clk_src = dev_cfg->clock_source,
		.clk_div = 0U,
	};

	LOG_DBG("SDHC set bus clock");

	/* Disable bus clock */
	qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_BUS_CLK_EN, false);
	if (freq == 0) {
		actual_freq_hz = 0;
		return 0;
	}

	uint32_t div = dev_data->src_clock / freq;
	if ((dev_data->src_clock / div) > freq) {
		div += 1;
	}
	if (div > 8) {
		div = 8;
	} else if (div == 0) {
		div = 1;
	}

	actual_freq_hz = dev_data->src_clock / div;
	dev_data->actual_clock = actual_freq_hz;
	LOG_DBG("SDHC source/actual clock %d/%d Hz", dev_data->src_clock, dev_data->actual_clock);

	/* Set clock */
	clk_settings.clk_div = div - 1;
	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure SDH clock: %d", ret);
		return ret;
	}

	/* Set bus clock */
	ret = qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_BUS_CLK_DIV, 0);
	if (ret < 0) {
		LOG_ERR("Failed to set SDH_CMD_SET_BUS_CLK_DIV");
		return ret;
	}

	/* Enable high speed mode */
	if (actual_freq_hz > QCC74X_SDHC_HS_MODE_CLOCK) {
		qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_HS_MODE_EN, true);
	} else {
		qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_HS_MODE_EN, false);
	}

	/* Enable bus clock */
	ret = qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_BUS_CLK_EN, true);
	if (ret < 0) {
		LOG_ERR("Failed to set SDH_CMD_SET_BUS_CLK_EN");
		return ret;
	}

	return actual_freq_hz;
}

/**
 * @brief Sets the SDHC data timeout value.
 *
 * Configures the data timeout counter based on the specified timeout in
 * milliseconds and the source clock frequency. The actual timeout may be
 * adjusted to the nearest supported value.
 *
 * @param dev Pointer to the SDHC device structure.
 * @param timeout_ms Desired timeout in milliseconds.
 * @return int 0 on success, or negative error code if configuration fails.
 */
int sdhc_qcc74x_set_timeout_ms(const struct device *dev, uint32_t timeout_ms)
{
	int ret = 0;
	uint32_t tmclk = 0U;
	uint32_t timeout_cnt = 0U;
	uint32_t cnt_value = 0U;
	uint32_t actual_timeout_ms = 0U;
	struct sdhc_qcc74x_data *dev_data = dev->data;

	tmclk = dev_data->actual_clock;

	timeout_cnt = tmclk / 1000 * timeout_ms;

	for (cnt_value = 1; cnt_value < 32; cnt_value++) {
		if ((timeout_cnt >> cnt_value) == 0) {
			break;
		}
	}
	cnt_value -= 1;

	if (cnt_value >= 27) {
		cnt_value = 27;
	} else if (cnt_value < 13) {
		cnt_value = 13;
	} else if (timeout_cnt & (~(1 << cnt_value))) {
		cnt_value += 1;
	}

	actual_timeout_ms = (int64_t)(1 << cnt_value) * 1000 / tmclk;
	if (dev_data->timeout_ms == actual_timeout_ms) {
		return 0;
	}

	dev_data->timeout_ms = actual_timeout_ms;

	cnt_value -= 13;

	/* Data timeout_s = (2^(cnt_vaule+13)) / tmclk_freq */
	ret = qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_DATA_TIMEOUT_CNT_VAL,
					cnt_value);
	if (ret < 0) {
		LOG_ERR("Failed to set SDH_CMD_SET_DATA_TIMEOUT_CNT_VAL");
		return ret;
	}

	return 0;
}

/**
 * @brief Interrupt service routine for SDHC.
 *
 * Handles SDHC interrupts by disabling all interrupts and signaling the
 * transfer semaphore to indicate completion or error.
 *
 * @param dev Pointer to the SDHC device structure.
 */
static void sdhc_qcc74x_isr(const struct device *dev)
{
	struct sdhc_qcc74x_data *dev_data = dev->data;

	/* Disable all interrupts (normal and error) */
	qc7xx_sdh_sta_int_en(&dev_data->sdh_dev, 0xffffffff, false);

	/* Give semb */
	k_sem_give(&dev_data->transfer_sem);
}

/**
 * @brief Initializes the SDHC controller.
 *
 * Configures the SDHC hardware, including clock, pinctrl, and interrupts, and
 * initializes the mutex and semaphore for data transfers.
 *
 * @param dev Pointer to the SDHC device structure.
 * @return int 0 on success, negative error code on failure
 * (e.g., -ENODEV if device not found).
 */
static int sdhc_qcc74x_init(const struct device *dev)
{
	int ret = 0;
	const struct sdhc_qcc74x_cfg *dev_cfg = dev->config;
	struct sdhc_qcc74x_data *dev_data = dev->data;
	struct qcc74x_clk_settings clk_settings = {
		.clk_src = dev_cfg->clock_source,
		.clk_div = 0U,
	};

	LOG_DBG("SDHC%d init, name:%s, freq:%d~%d, is_bus_4bit:%d", dev_cfg->instance_id, dev->name,
		dev_cfg->props->f_min, dev_cfg->props->f_max,
		dev_cfg->props->bus_4_bit_support);

	dev_data->sdh_dev.reg_base = dev_cfg->base_addr;
	dev_data->sdh_dev.irq_num = dev_cfg->irq_num;

	k_mutex_init(&dev_data->access_mutex);
	k_sem_init(&dev_data->transfer_sem, 0, 1);

	dev_cfg->irq_configure();

	ret = pinctrl_apply_state(dev_cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Failed to configure SDHC pins");
		return ret;
	}

	/* Reset sdh */
	ret = reset_line_toggle_dt(&dev_cfg->reset);
	if (ret < 0) {
		LOG_ERR("SDHC reset line toggle failed: %d", ret);
		return -EIO;
	}

	/* Enable clock */
	ret = clock_control_on(dev_cfg->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for SDH driver is already enabled");
		} else {
			LOG_ERR("Failed to enable SDH clock: %d", ret);
			return ret;
		}
	}

	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure SDH clock: %d", ret);
		return ret;
	}

	/* Get base clock rate */
	uint32_t rate = 0;
	ret = clock_control_get_rate(dev_cfg->clock_dev,
				     (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id, &rate);
	if (ret != 0) {
		LOG_ERR("Failed to get SDH clock: %d", ret);
		return ret;
	}
	dev_data->src_clock = rate;
	LOG_DBG("SDHC source clock %d Hz", dev_data->src_clock);

	/* Init sdh */
	struct qc7xx_sdh_config_s cfg = {
		.dma_burst = SDH_DMA_BURST_64,
		.dma_fifo_th = SDH_DMA_FIFO_THRESHOLD_128,
		.power_vol = 0x07, /* 3.3v */
	};
	qc7xx_sdh_init(&dev_data->sdh_dev, &cfg);
	ret = qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_SD_BUS_POWER, true);
	if (ret < 0) {
		LOG_ERR("Failed to set SDH_CMD_SET_SD_BUS_POWER");
		return ret;
	}

	LOG_DBG("SDHC Init done");
	return 0;
}

/**
 * @brief Sends active clock pulses to the SD card.
 *
 * Sends 80 clock pulses to activate the SD card as part of the initialization
 * process.
 *
 * @param dev Pointer to the SDHC device structure.
 * @return int 0 on success.
 */
static int sdhc_qcc74x_send_active_clk(const struct device *dev)
{
	struct sdhc_qcc74x_data *dev_data = dev->data;
	qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_ACTIVE_CLK_OUT, 80);

	return 0;
}

/**
 * @brief Starts an SDHC command or data transfer.
 *
 * Configures and initiates an SDHC command or data transfer, including setting
 * up command and data configurations, handling cache alignment, and starting
 * the transfer.
 *
 * @param dev Pointer to the SDHC device structure.
 * @param cmd Pointer to the SDHC command structure.
 * @param data Pointer to the SDHC data structure, or NULL if no data transfer.
 * @param cmd_cfg Pointer to the QC7XX command configuration structure.
 * @param data_cfg Pointer to the QC7XX data configuration structure.
 * @return int 0 on success, negative error code on failure (e.g., -ENOTSUP for
 * unsupported commands).
 */
static int sdhc_qcc74x_transfer_start(const struct device *dev, struct sdhc_command *cmd,
				    struct sdhc_data *data, struct qc7xx_sdh_cmd_cfg_s *cmd_cfg,
				    struct qc7xx_sdh_data_cfg_s *data_cfg)
{
	struct sdhc_qcc74x_data *dev_data = dev->data;

	/* cmd cfg */
	cmd_cfg->index = cmd->opcode;
	cmd_cfg->argument = cmd->arg;
	cmd_cfg->cmd_type = SDH_CMD_TPYE_NORMAL;
	cmd_cfg->resp_type = cmd->response_type & SDHC_NATIVE_RESPONSE_MASK;

	if (data) {
		/* data cfg */
		struct qc7xx_sdh_data_tranfer_s data_tranfer = {
			.address = data->data,
			.int_en = 0,
			.length = data->block_size * data->blocks,
		};
		data_cfg->adma_tranfer = &data_tranfer;
		data_cfg->adma_tranfer_cnt = 1;
		data_cfg->adma2_hw_desc_raw_mode = false;
		data_cfg->auto_cmd_mode = SDH_AUTO_CMD_DISABLE;
		data_cfg->block_size = data->block_size;
		data_cfg->block_count = data->blocks;
		data_cfg->adma2_hw_desc = dev_data->adma2_hw_desc;
		data_cfg->adma2_hw_desc_cnt = QCC74X_SDHC_ADMA2_DESC_NUM;

		/* auto cmd */
		if (data_cfg->block_count > 1) {
			data_cfg->auto_cmd_mode = SDH_AUTO_CMD_CMD12;
		}

		switch (cmd->opcode) {
		case SD_WRITE_SINGLE_BLOCK:
		case SD_WRITE_MULTIPLE_BLOCK:
			data_cfg->data_dir = SDH_TRANSFER_DIR_WR;
			break;

		case SD_READ_SINGLE_BLOCK:
		case SD_READ_MULTIPLE_BLOCK:
		case SD_APP_SEND_SCR:
		case SD_SWITCH:
		case SD_APP_SEND_NUM_WRITTEN_BLK:
			data_cfg->data_dir = SDH_TRANSFER_DIR_RD;
			break;

		default:
			LOG_ERR("CMD%d not support data transfer", cmd->opcode);
			return -ENOTSUP;
			break;
		}

		if ((uintptr_t)data->data % sys_cache_data_line_size_get() != 0) {
			LOG_WRN("SDHC addr is not cache line aligned: %p\r\n", data->data);
		}

		/* cache */
		if (data_cfg->data_dir == SDH_TRANSFER_DIR_RD) {
			/* Processing cache boundaries */
			if ((uintptr_t)data->data % sys_cache_data_line_size_get() != 0) {
				sys_cache_data_flush_range(data->data, 1);
				sys_cache_data_flush_range(
					(void *)((uintptr_t)data->data +
						 data->block_size * data->blocks - 1),
					1);
			}
			sys_cache_data_invd_range(data->data, data->block_size * data->blocks);
		} else {
			sys_cache_data_flush_range(data->data, data->block_size * data->blocks);
		}
	} else {
		data_cfg = NULL;
	}

	if (cmd->opcode == SD_GO_IDLE_STATE) {
		sdhc_qcc74x_send_active_clk(dev);
	}

	sdhc_qcc74x_set_timeout_ms(dev, (data && data->timeout_ms) ? data->timeout_ms : cmd->timeout_ms);

	k_sem_reset(&dev_data->transfer_sem);
	/* clr comp sta */
	qc7xx_sdh_sta_clr(&dev_data->sdh_dev, SDH_NORMAL_STA_CMD_COMP | SDH_NORMAL_STA_TRAN_COMP);
	/* clr all error sta */
	qc7xx_sdh_sta_clr(&dev_data->sdh_dev, SDH_ERROR_ALL_MASK);
	int ret = qc7xx_sdh_tranfer_start(&dev_data->sdh_dev, cmd_cfg, data_cfg);
	if (ret < 0) {
		LOG_ERR("CMD%d send failed, agr 0x%08X", cmd_cfg->index, cmd_cfg->argument);
		return -EIO;
	}

	return 0;
}

/**
 * @brief Waits for SDHC command or data transfer completion.
 *
 * Waits for the completion of an SDHC command or data transfer, retrieves the
 * response, and handles cache invalidation for read operations.
 *
 * @param dev Pointer to the SDHC device structure.
 * @param cmd Pointer to the SDHC command structure.
 * @param data Pointer to the SDHC data structure, or NULL if no data transfer.
 * @param cmd_cfg Pointer to the QC7XX command configuration structure.
 * @param data_cfg Pointer to the QC7XX data configuration structure.
 * @return int 0 on success, negative error code on failure (e.g., -ETIMEDOUT for timeout).
 */
static int sdhc_qcc74x_wait_done(const struct device *dev, struct sdhc_command *cmd,
			       struct sdhc_data *data, struct qc7xx_sdh_cmd_cfg_s *cmd_cfg,
			       struct qc7xx_sdh_data_cfg_s *data_cfg)
{
	struct sdhc_qcc74x_data *dev_data = dev->data;
	uint32_t sta = 0U;
	int ret = 0;

	/* Wait for the command to complete */
	qc7xx_sdh_sta_int_en(&dev_data->sdh_dev, SDH_NORMAL_STA_CMD_COMP | SDH_ERROR_ALL_MASK,
			     true);
	ret = k_sem_take(&dev_data->transfer_sem, K_MSEC(cmd->timeout_ms * 2));
	if (ret < 0) {
		LOG_ERR("CMD%d wait timeout", cmd_cfg->index);
		return -ETIMEDOUT;
	}
	sta = qc7xx_sdh_sta_get(&dev_data->sdh_dev);
	/* Get response */
	if (sta & SDH_NORMAL_STA_CMD_COMP) {
		qc7xx_sdh_sta_clr(&dev_data->sdh_dev, SDH_NORMAL_STA_CMD_COMP);
		qc7xx_sdh_get_resp(&dev_data->sdh_dev, cmd_cfg);
		if (cmd_cfg->resp_type == SDH_RESP_TPYE_R2) {
			memcpy((uint8_t *)cmd->response + 1, cmd_cfg->resp,
			       sizeof(cmd->response) - 1);
		} else {
			memcpy(cmd->response, cmd_cfg->resp, sizeof(cmd->response));
		}
	} else {
		LOG_ERR("CMD%d wait failed, arg:%d, sta:%d", cmd_cfg->index, cmd_cfg->argument,
			sta);
		return -EIO;
	}

	if ((data == NULL) && (cmd_cfg->resp_type != SDH_RESP_TPYE_R1b) &&
	    (cmd_cfg->resp_type != SDH_RESP_TPYE_R5b)) {
		/* No data and busy, end */
		return 0;
	}

	/* Wait DATA/busy done */
	qc7xx_sdh_sta_int_en(&dev_data->sdh_dev, SDH_NORMAL_STA_TRAN_COMP | SDH_ERROR_ALL_MASK,
			     true);
	ret = k_sem_take(&dev_data->transfer_sem, K_MSEC(data ? data->timeout_ms : cmd->timeout_ms * 2));
	if (ret < 0) {
		LOG_ERR("CMD%d data wait timeout", cmd_cfg->index);
		return -ETIMEDOUT;
	}
	sta = qc7xx_sdh_sta_get(&dev_data->sdh_dev);
	if (sta & SDH_NORMAL_STA_TRAN_COMP) {
		qc7xx_sdh_sta_clr(&dev_data->sdh_dev, SDH_NORMAL_STA_TRAN_COMP);
	} else {
		LOG_ERR("CMD%d data wait failed, arg:%d, sta:%d", cmd_cfg->index, cmd_cfg->argument,
			sta);
		return -EIO;
	}

	if (data && data_cfg->data_dir == SDH_TRANSFER_DIR_RD) {
		sys_cache_data_invd_range(data->data, data->block_size * data->blocks);
	}

	return 0;
}

static int sdhc_qcc74x_reset(const struct device *dev)
{
	int ret = 0;
	const struct sdhc_qcc74x_cfg *dev_cfg = dev->config;
	struct sdhc_qcc74x_data *dev_data = dev->data;
	struct qcc74x_clk_settings clk_settings = {
		.clk_src = dev_cfg->clock_source,
		.clk_div = 0U,
	};

	LOG_DBG("SDHC reset");

	k_mutex_lock(&dev_data->access_mutex, K_FOREVER);

	/* Reset sdh */
	reset_line_toggle_dt(&dev_cfg->reset);

	/* Reset clock */
	ret = clock_control_configure(dev_cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure SDH clock: %d", ret);
		return ret;
	}

	/* Get base clock rate */
	uint32_t rate = 0;
	ret = clock_control_get_rate(dev_cfg->clock_dev,
				     (clock_control_subsys_t)(uintptr_t)dev_cfg->clock_id, &rate);
	if (ret != 0) {
		LOG_ERR("Failed to get SDH clock: %d", ret);
		return ret;
	}
	dev_data->src_clock = rate;
	LOG_DBG("SDHC source clock after reset %d Hz", dev_data->src_clock);

	dev_data->bus_width = 0;
	dev_data->bus_clock = 0;
	dev_data->timeout_ms = 0;

	/* Init sdh */
	struct qc7xx_sdh_config_s cfg = {
		.dma_burst = SDH_DMA_BURST_64,
		.dma_fifo_th = SDH_DMA_FIFO_THRESHOLD_128,
		.power_vol = 0x07, /* 3.3v */
	};
	qc7xx_sdh_init(&dev_data->sdh_dev, &cfg);
	qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_SD_BUS_POWER, true);

	k_mutex_unlock(&dev_data->access_mutex);

	return 0;
}

static int sdhc_qcc74x_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	const struct sdhc_qcc74x_cfg *dev_cfg = dev->config;

	memcpy(props, dev_cfg->props, sizeof(struct sdhc_host_props));

	return 0;
}

static int sdhc_qcc74x_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct sdhc_qcc74x_cfg *dev_cfg = dev->config;
	struct sdhc_qcc74x_data *dev_data = dev->data;
	uint32_t bus_clock = 0U;
	uint8_t bus_width = 0U;
	int ret = 0;

	LOG_INF("SDHC I/O, bus width %d, clock %dHz, card power %s, voltage %s", ios->bus_width,
		ios->clock, ios->power_mode == SDHC_POWER_ON ? "ON" : "OFF",
		ios->signal_voltage == SD_VOL_1_8_V ? "1.8V" : "3.3V");

	/* Set bus clock freq */
	if (ios->clock) {
		/* Check for frequency boundaries supported by host */
		if (ios->clock > dev_cfg->props->f_max || ios->clock < dev_cfg->props->f_min) {
			LOG_ERR("Proposed clock %dHz outside supported host range", ios->clock);
			return -EINVAL;
		}

		bus_clock = ios->clock;

		if (dev_data->bus_clock != bus_clock) {
			dev_data->bus_clock = sdhc_qcc74x_set_bus_clock(dev, bus_clock);
			LOG_DBG("Set bus clk, want %dhz, actual %dhz", ios->clock,
				dev_data->bus_clock);
		}
	}

	/* Set bus width */
	switch (ios->bus_width) {
	case SDHC_BUS_WIDTH1BIT:
		bus_width = 1;
		break;
	case SDHC_BUS_WIDTH4BIT:
		bus_width = 4;
		break;
	default:
		LOG_ERR("Bus width %d not supported", ios->bus_width);
		return -ENOTSUP;
	}
	if (dev_data->bus_width != bus_width) {
		ret = qc7xx_sdh_feature_control(&dev_data->sdh_dev, SDH_CMD_SET_BUS_WIDTH,
						bus_width);
		if (ret < 0) {
			LOG_ERR("Error configuring bus width");
			return -ENOTSUP;
		}
		dev_data->bus_width = bus_width;
	}

	if (ios->bus_mode && ios->bus_mode != SDHC_BUSMODE_PUSHPULL) {
		LOG_ERR("Bus mode %d not supported", ios->bus_mode);
		return -ENOTSUP;
	}
	if (ios->signal_voltage && ios->signal_voltage != SD_VOL_3_3_V) {
		LOG_ERR("Signal voltage %d not supported", ios->signal_voltage);
		return -ENOTSUP;
	}
	if (ios->driver_type && ios->driver_type != SD_DRIVER_TYPE_B) {
		LOG_ERR("Driver type %d not supported", ios->driver_type);
		return -ENOTSUP;
	}

	return 0;
}

static int sdhc_qcc74x_get_card_present(const struct device *dev)
{
	LOG_DBG("Get_card_present...");

	return 1;
}

static int sdhc_qcc74x_request(const struct device *dev, struct sdhc_command *cmd,
			     struct sdhc_data *data)
{
	struct sdhc_qcc74x_data *dev_data = dev->data;
	int ret = 0;

	struct qc7xx_sdh_cmd_cfg_s cmd_cfg = {0};
	struct qc7xx_sdh_data_cfg_s data_cfg = {0};

	LOG_DBG("Request CMD:%d, arg:0x%08X, response_type:%d, %s", cmd->opcode, cmd->arg,
		(cmd->response_type & SDHC_NATIVE_RESPONSE_MASK),
		data ? "with DATA" : "without DATA");

	ret = k_mutex_lock(&dev_data->access_mutex, K_MSEC(cmd->timeout_ms));
	if (ret) {
		LOG_ERR("Could not access card");
		return -EBUSY;
	}

	do {
		ret = sdhc_qcc74x_transfer_start(dev, cmd, data, &cmd_cfg, &data_cfg);
		if (ret < 0) {
			continue;
		}

		ret = sdhc_qcc74x_wait_done(dev, cmd, data, &cmd_cfg, &data_cfg);
		if (ret < 0) {
			continue;
		}
	} while (ret < 0 && (cmd->retries-- > 0));

	k_mutex_unlock(&dev_data->access_mutex);

	return ret;
}

static int sdhc_qcc74x_card_busy(const struct device *dev)
{
	struct sdhc_qcc74x_data *dev_data = dev->data;
	uint32_t reg_present = sys_read32(dev_data->sdh_dev.reg_base + SDH_PRESENT_STATE_1_OFFSET);
	if (reg_present &
	    (SDH_CMD_INHIBIT_CMD_MSK | SDH_CMD_INHIBIT_DAT_MSK | SDH__DAT_ACTIVE_MSK)) {
		return 1;
	}

	return 0;
}

static DEVICE_API(sdhc, sdhc_qcc74x_api) = {
	.reset = sdhc_qcc74x_reset,
	.get_host_props = sdhc_qcc74x_get_host_props,
	.set_io = sdhc_qcc74x_set_io,
	.get_card_present = sdhc_qcc74x_get_card_present,
	.request = sdhc_qcc74x_request,
	.card_busy = sdhc_qcc74x_card_busy,
};

#define IRQ_CONFIGURE(n, idx)                                                                      \
	IRQ_CONNECT(DT_INST_IRQ_BY_IDX(idx, n, irq), DT_INST_IRQ_BY_IDX(idx, n, priority),         \
		    sdhc_qcc74x_isr, DEVICE_DT_INST_GET(idx), 0);                                    \
	irq_enable(DT_INST_IRQ_BY_IDX(idx, n, irq));

#define CONFIGURE_ALL_IRQS(idx, n) LISTIFY(n, IRQ_CONFIGURE, (), idx)

#define SDH_QCC74X_INIT(idx)                                                                         \
	PINCTRL_DT_INST_DEFINE(idx);                                                               \
                                                                                                   \
	static void sdhc_qcc74x##idx##_irq_configure(void)                                           \
	{                                                                                          \
		CONFIGURE_ALL_IRQS(idx, DT_NUM_IRQS(DT_DRV_INST(idx)));                            \
	}                                                                                          \
                                                                                                   \
	struct sdhc_host_props sdhc_qcc74x_props_##idx = {                                           \
		.is_spi = false,                                                                   \
		.f_max = DT_INST_PROP(idx, max_bus_freq),                                          \
		.f_min = DT_INST_PROP(idx, min_bus_freq),                                          \
		.power_delay = DT_INST_PROP_OR(idx, power_delay_ms, 0),                            \
		.host_caps =                                                                       \
			{                                                                          \
				.bus_8_bit_support = false,                                        \
				.adma_2_support = true,                                            \
				.high_spd_support = true,                                          \
				.sdma_support = true,                                              \
				.suspend_res_support = true,                                       \
				.vol_330_support = true,                                           \
				.vol_300_support = false,                                          \
				.vol_180_support = false,                                          \
				.sdio_async_interrupt_support = true,                              \
			},                                                                         \
		.max_current_330 = DT_INST_PROP_OR(idx, max_current_330, 0),                       \
		.max_current_300 = DT_INST_PROP_OR(idx, max_current_300, 0),                       \
		.max_current_180 = DT_INST_PROP_OR(idx, max_current_180, 0),                       \
		.bus_4_bit_support = (DT_INST_PROP(idx, bus_width) == 4) ? true : false,           \
	};                                                                                         \
                                                                                                   \
	static struct sdhc_qcc74x_cfg sdhc_config_##idx = {                                          \
		.props = &sdhc_qcc74x_props_##idx,                                                   \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                              \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(idx, id),                  \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(idx, source),                         \
		.irq_configure = sdhc_qcc74x##idx##_irq_configure,                                   \
		.base_addr = DT_INST_REG_ADDR(idx),                                                \
		.irq_num = DT_INST_IRQ_BY_IDX(idx, 0, irq),                                        \
		.irq_priority = DT_INST_IRQ_BY_IDX(idx, 0, priority),                              \
		.irq_flags = DT_INST_IRQ_BY_IDX(idx, 0, flags),                                    \
		.reset = RESET_DT_SPEC_INST_GET(idx),                                              \
		.instance_id = idx,                                                                \
	};                                                                                         \
                                                                                                   \
	static __ALIGNED(CONFIG_SDHC_BUFFER_ALIGNMENT)                                                \
		__attribute__((section(".nocache_noinit_ram")))                                         \
		struct qc7xx_sdh_adma2_hw_desc_s sdh_adma2_hw_desc_##idx[QCC74X_SDHC_ADMA2_DESC_NUM]; \
                                                                                                   \
	static struct sdhc_qcc74x_data sdhc_data_##idx = {                                           \
		.sdh_dev = {NULL, 0, 0xFF, 0, 0, 0, NULL},                                         \
		.adma2_hw_desc = sdh_adma2_hw_desc_##idx,                                          \
		.bus_clock = 0,                                                                    \
		.bus_width = 0,                                                                    \
		.timeout_ms = 0,                                                                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, &sdhc_qcc74x_init, NULL, &sdhc_data_##idx, &sdhc_config_##idx,    \
			      POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY, &sdhc_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(SDH_QCC74X_INIT)
