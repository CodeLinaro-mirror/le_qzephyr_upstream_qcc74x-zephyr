/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_ir

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ir.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>

#include <qc7xx_ir.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ir_qcc74x, CONFIG_IR_LOG_LEVEL);

/* IR configuration. */
struct ir_qcc74x_config {
	const struct qc7xx_ir_rx_config_s rx_cfg;
	const struct pinctrl_dev_config *pcfg;
	const uint32_t reg_base;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	uint8_t clock_source;
	const uint8_t clock_div;
};

static struct qc7xx_device_s irrx;

/**
 * @brief Initialize the Opal IR driver.
 *
 * @param dev           Pointer to device structure.
 * @retval 0            On success.
 */
static int ir_qcc74x_init(const struct device *dev)
{
	int ret = 0;
	const struct ir_qcc74x_config *config = dev->config;
	struct qcc74x_clk_settings clk_settings = {
		.enable = CLOCK_ENABLE,
		.clk_src = config->clock_source,
		.clk_div = config->clock_div,
	};
	LOG_DBG("%s initialization started", dev->name);

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Pinctrl apply state failed, ret: %d", ret);
		return ret;
	}

	ret = clock_control_on(config->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for IR driver is already enabled");
		} else {
			LOG_ERR("Failed to enable IR clock: %d", ret);
			return ret;
		}
	}

	ret = clock_control_configure(config->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)config->clock_id,
				      (void *)&clk_settings);
	if (ret != 0) {
		LOG_ERR("Failed to configure IR clock: %d", ret);
	}

	irrx.reg_base = config->reg_base;
	qc7xx_ir_rx_init(&irrx, &config->rx_cfg);

	return ret;
}

static void ir_qcc74x_enable(const struct device *dev, bool enable)
{
	qc7xx_ir_rx_enable(&irrx, enable);
}

static int ir_qcc74x_receive(const struct device *dev, uint64_t *data)
{
	const struct ir_qcc74x_config *config = dev->config;

	if (config->rx_cfg.rx_mode == IR_RX_SWM) {
		LOG_ERR("IR configured in SWM mode. Use 'swm_receive' function instead");
		return -EINVAL;
	}

	return qc7xx_ir_receive(&irrx, data);
}

static int ir_qcc74x_swm_receive(const struct device *dev, uint16_t *data, uint8_t length)
{
	const struct ir_qcc74x_config *config = dev->config;
	if (config->rx_cfg.rx_mode != IR_RX_SWM) {
		LOG_ERR("IR not configured in SWM mode. Cannot read data");
		return -EINVAL;
	}

	return qc7xx_ir_swm_receive(&irrx, data, length);
}

static int ir_qcc74x_get_rxfifo_cnt(const struct device *dev)
{
	return qc7xx_ir_get_rxfifo_cnt(&irrx);
}

static int ir_qcc74x_get_rxint_status(const struct device *dev)
{
	return qc7xx_ir_get_rxint_status(&irrx);
}

static void ir_qcc74x_rxfifo_clear(const struct device *dev)
{
	qc7xx_ir_rxfifo_clear(&irrx);
}

static void ir_qcc74x_rxint_clear(const struct device *dev)
{
	qc7xx_ir_rxint_clear(&irrx);
}

static DEVICE_API(ir, ir_qcc74x_driver_api) = {
	.receive = &ir_qcc74x_receive,
	.enable = &ir_qcc74x_enable,
	.swm_receive = &ir_qcc74x_swm_receive,
	.get_rxfifo_cnt = &ir_qcc74x_get_rxfifo_cnt,
	.get_rxint_status = &ir_qcc74x_get_rxint_status,
	.rxfifo_clear = &ir_qcc74x_rxfifo_clear,
	.rxint_clear = &ir_qcc74x_rxint_clear,
};

#define IR_DEVICE(inst)                                                                            \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static const struct ir_qcc74x_config ir_qcc74x_config##inst = {                                \
		.rx_cfg = {.rx_mode = DT_INST_STRING_TOKEN(inst, rx_mode),                         \
			   .input_inverse = DT_INST_PROP(inst, input_inverse),                     \
			   .deglitch_enable = DT_INST_PROP(inst, deglitch_enable),                 \
			   .deglitch_cnt = DT_INST_PROP(inst, deglitch_cnt),                       \
			   .data_threshold = DT_INST_PROP(inst, data_threshold),                   \
			   .end_threshold = DT_INST_PROP(inst, end_threshold),                     \
			   .fifo_threshold = DT_INST_PROP(inst, fifo_threshold)},                  \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.reg_base = DT_INST_REG_ADDR(inst),                                                \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(inst, source),                        \
		.clock_div = DT_INST_PROP(inst, divider)};                                         \
	DEVICE_DT_INST_DEFINE(inst, ir_qcc74x_init, NULL, NULL, &ir_qcc74x_config##inst, POST_KERNEL,  \
			      CONFIG_IR_INIT_PRIORITY, &ir_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IR_DEVICE)
