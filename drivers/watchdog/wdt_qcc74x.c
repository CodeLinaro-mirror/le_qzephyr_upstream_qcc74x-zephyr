/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_watchdog

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_timer_ll.h>
#include <qc7xx_wdg.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wdt_qcc74x, CONFIG_WDT_LOG_LEVEL);

/* Watchdog data */
struct wdt_qcc74x_data {
	struct qc7xx_device_s dev_cfg;
	uint32_t freq;
#ifdef CONFIG_REBOOT
	bool reboot;
#endif
};

/* Watchdog configuration */
struct wdt_qcc74x_config {
	struct wdt_timeout_cfg wdt_info;
	struct qc7xx_wdg_config_s config;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const clock_control_subsys_t clock_gate_id;
	void (*irq_cfg_func)(void);
};

static int wdt_qcc74x_enable(const struct device *dev, uint8_t options)
{
	ARG_UNUSED(options);
	struct wdt_qcc74x_data *data = dev->data;

	LOG_DBG("Starting %s", data->dev_cfg.name);

	uint32_t key = irq_lock();
	qc7xx_wdg_start(&data->dev_cfg);
	irq_unlock(key);

	return 0;
}

static int wdt_qcc74x_disable(const struct device *dev)
{
	struct wdt_qcc74x_data *data = dev->data;

	uint32_t key = irq_lock();
	qc7xx_wdg_stop(&data->dev_cfg);
	irq_unlock(key);
	LOG_DBG("%s stopped", data->dev_cfg.name);

	return 0;
}

static int wdt_qcc74x_feed(const struct device *dev, int channel_id)
{
	struct wdt_qcc74x_data *data = dev->data;

	if (channel_id != 0) {
		LOG_ERR("Invalid watchdog channel");
		return -EINVAL;
	}

	qc7xx_wdg_reset_countervalue(&data->dev_cfg);
	LOG_DBG("%s reset", data->dev_cfg.name);
	return 0;
}

static int wdt_qcc74x_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	struct wdt_qcc74x_config *config = (struct wdt_qcc74x_config *)dev->config;
	struct wdt_qcc74x_data *data = dev->data;
	uint64_t ticks = 0;

	LOG_DBG("%s installing timeout: %u - %u", data->dev_cfg.name, cfg->window.min,
		cfg->window.max);

	if (cfg->window.min != 0U || cfg->window.max == 0U) {
		LOG_ERR("Invalid watchdog window defined");
		return -EINVAL;
	}

	/* Timeout ticks = (ms * freq) / 1000 */
	ticks = ((uint64_t)cfg->window.max * data->freq) / 1000U;

	/* Check if the requested time exceeds hardware register limits */
	if (ticks > UINT16_MAX) {
		LOG_ERR("Timeout too long for hardware registers! \n"
			"It need to be max UINT16_MAX and currently: %llu \n"
			"Try to increase hardware divider",
			ticks);
		return -EINVAL;
	}

	config->config.comp_val = (uint16_t)ticks;
	config->wdt_info.callback = cfg->callback;

	/* Set mode of watchdog and callback */
	switch (cfg->flags) {
	case WDT_FLAG_RESET_SOC:
		if (cfg->callback == NULL) {
			config->config.mode = WDG_MODE_RESET;
#ifdef CONFIG_REBOOT
			data->reboot = 0;
#endif
			LOG_DBG("Configuring reset SOC mode");
		} else {
#ifdef CONFIG_REBOOT
			config->config.mode = WDG_MODE_INTERRUPT;
			data->reboot = 1;
			LOG_DBG("Configuring reset SOC mode with a callback");
#else
			LOG_ERR("Reset mode with callback requires CONFIG_REBOOT enabled");
			return -ENOTSUP;
#endif
		}
		break;

	case WDT_FLAG_RESET_CPU_CORE:
		config->config.mode = WDG_MODE_INTERRUPT;
		LOG_ERR("Reset CPU core mode not supportted");
		return -ENOTSUP;

	case WDT_FLAG_RESET_NONE:
		config->config.mode = WDG_MODE_INTERRUPT;
#ifdef CONFIG_REBOOT
		data->reboot = 0;
#endif
		if (cfg->callback == NULL) {
			LOG_ERR("Configuring non-reset mode without callback");
			return -EINVAL;
		}
		LOG_DBG("Configuring non-reset mode");
		break;

	default:
		LOG_ERR("Unsupported watchdog config flag");
		return -ENOTSUP;
	}
	uint32_t key = irq_lock();
	qc7xx_wdg_init(&data->dev_cfg, &config->config);
	irq_unlock(key);
	return 0;
}

/**
 * @brief Watchdog interrupt service routine
 *
 * @param dev Watchdog device
 */
static void wdt_qcc74x_isr(const struct device *dev)
{
	const struct wdt_qcc74x_config *cfg = dev->config;
	struct wdt_qcc74x_data *data = dev->data;

	LOG_DBG("%s interrupt", data->dev_cfg.name);
	qc7xx_wdg_compint_clear(&data->dev_cfg);
	if (cfg->wdt_info.callback) {
		LOG_DBG("%s calling callback", data->dev_cfg.name);
		cfg->wdt_info.callback(dev, 0);
	}

#ifdef CONFIG_REBOOT
	if (data->reboot) {
		/* Wait 100ms for any uart buffers to flush */
		k_busy_wait(100 * USEC_PER_MSEC);
		sys_reboot(SYS_REBOOT_COLD);
	}
#endif
}

/**
 * @brief Initialize Watchdog counter
 *
 * @param dev Watchdog device
 * @return int Always zero
 */
static int wdt_qcc74x_init(const struct device *dev)
{
	struct wdt_qcc74x_data *data = dev->data;
	const struct wdt_qcc74x_config *cfg = dev->config;
	uint32_t base_freq = 0U;
	int ret = 0;

	LOG_DBG("Initializing QCC74x watchdog driver: %s", data->dev_cfg.name);

	clock_control_on(cfg->clock_dev, (clock_control_subsys_t)(uintptr_t)cfg->clock_gate_id);

#ifdef CONFIG_REBOOT
	data->reboot = 0;
#endif

	const struct qc7xx_wdg_config_s *wdt_cfg = &cfg->config;
	uint32_t key = irq_lock();
	qc7xx_wdg_init(&data->dev_cfg, wdt_cfg);

	if (cfg->irq_cfg_func) {
		cfg->irq_cfg_func();
	}
	irq_unlock(key);

	/* Get the source frequency from the clock controller */
	ret = clock_control_get_rate(cfg->clock_dev, cfg->clock_id, &base_freq);
	if (ret != 0) {
		LOG_ERR("Failed to get watchdog clock rate err: %d", ret);
		return -EIO;
	}

	/* Calculate frequency with respect to hardware divisor */
	data->freq = base_freq / (cfg->config.clock_div + 1);

	LOG_DBG("%s: Watchdog frequency set to %u Hz", data->dev_cfg.name, data->freq);
	LOG_DBG("%s: Clock_id %ld, watchdog frequency set to %u Hz", data->dev_cfg.name,
		(uintptr_t)cfg->clock_id, data->freq);
	LOG_DBG("%s: Gate clock_id %lu", data->dev_cfg.name, (uintptr_t)cfg->clock_gate_id);

	return 0;
}

static DEVICE_API(wdt, wdt_api) = {
	.setup = wdt_qcc74x_enable,
	.disable = wdt_qcc74x_disable,
	.install_timeout = wdt_qcc74x_install_timeout,
	.feed = wdt_qcc74x_feed,
};

/**
 * @brief Get the shared TIMER0 peripheral clock gate ID.
 *
 * Returns the peripheral clock gate identifier for the TIMER0 hardware
 * block, taken explicitly from the Devicetree node labeled `timer0`.
 *
 * The TIMER0 block (timer0, timer1, watchdog) shares a single hardware
 * enable gate controlled by `QC7XX_PERIPHERAL_TIMER0`. That gate clock
 * is described in Devicetree via the `clock-gate` property of the
 * canonical `timer0` node.
 *
 * This macro intentionally ignores the current driver instance and
 * always reads the value from:
 *
 *     timer0.clock-gate
 *
 * This prevents accidental changes caused by overlays that modify
 * instance-specific `clocks` properties and ensures that the shared
 * hardware gate is always derived from the canonical TIMER0 node.
 *
 * The value is cast to `clock_control_subsys_t` to match the
 * Zephyr clock_control API.
 */
#define QCC74X_GATE_CLOCK_ID_FROM_TIMER0()                                                           \
	((clock_control_subsys_t)(uintptr_t)DT_PROP(DT_NODELABEL(timer0), clock_gate))

BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(timer0), clock_gate),
	     "timer0 node must define clock-gate property");

#define QCC74X_WDT_INIT(inst)                                                                        \
                                                                                                   \
	static void wdt_qcc74x_irq_cfg_func_##inst(void)                                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(inst, 0, irq),                                      \
			    DT_INST_IRQ_BY_IDX(inst, 0, priority), wdt_qcc74x_isr,                   \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_IDX(inst, 0, irq));                                      \
		LOG_DBG("watchdog" #inst ": irq %u setup",                                         \
			(unsigned)DT_INST_IRQ_BY_IDX(inst, 0, irq));                               \
	}                                                                                          \
	static struct wdt_qcc74x_data wdt_qcc74x_data_##inst = {                                       \
		.dev_cfg = {.name = DT_NODE_PATH(DT_DRV_INST(inst)),                               \
			    .reg_base = DT_INST_REG_ADDR(inst),                                    \
			    .irq_num = DT_INST_IRQ_BY_IDX(inst, 0, irq),                           \
			    .idx = inst,                                                           \
			    .sub_idx = 0,                                                          \
			    .dev_type = QC7XX_DEVICE_TYPE_TIMER,                                   \
			    .user_data = NULL},                                                    \
	};                                                                                         \
                                                                                                   \
	static struct wdt_qcc74x_config wdt_qcc74x_config##inst = {                                    \
		.config =                                                                          \
			{                                                                          \
				.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(inst, source),        \
				.clock_div = DT_INST_PROP(inst, clock_div),                        \
			},                                                                         \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clock_gate_id = QCC74X_GATE_CLOCK_ID_FROM_TIMER0(),                                 \
		.irq_cfg_func = wdt_qcc74x_irq_cfg_func_##inst,                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, wdt_qcc74x_init, NULL, &wdt_qcc74x_data_##inst,                    \
			      &wdt_qcc74x_config##inst, POST_KERNEL,                                 \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &wdt_api)

DT_INST_FOREACH_STATUS_OKAY(QCC74X_WDT_INIT);
