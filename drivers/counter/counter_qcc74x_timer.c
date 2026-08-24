/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_timer

#include <stdint.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_timer_ll.h>
#include <hardware/timer_reg.h>
#include <qc7xx_timer.h>
#include <qc7xx_clock.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(timer_qcc74x, CONFIG_COUNTER_LOG_LEVEL);

/*  qcc74x timer hw sets the interrupt active two ticks after a compare match. */
#define QCC7XX_COUNTER_TICKS_DELAY    2U
#define QCC7XX_COUNTER_ALARM_CH_COUNT TIMER_COMP_NONE
#define QCC7XX_COUNTER_TOP_CHANNEL    TIMER_COMP_ID_2

/* Timer configuration */
struct timer_qcc74x_config {
	struct counter_config_info counter_info;
	struct qc7xx_timer_config_s config;
	void (*irq_cfg_func)(void);
	uint8_t start_at_init;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const clock_control_subsys_t clock_gate_id;
};

struct timer_qcc74x_alarm_data {
	struct counter_alarm_cfg cfg;
	bool active;
};

/* Timer data */
struct timer_qcc74x_data {
	struct timer_qcc74x_alarm_data alarm[QCC7XX_COUNTER_ALARM_CH_COUNT];
	struct counter_top_cfg top_cfg;
	struct qc7xx_device_s dev_cfg;
	bool top_cfg_set;
};

/**
 * @brief Adjust compare value by the hardware interrupt latency.
 *
 * @param ticks Requested counter ticks.
 *
 * @return Compare value adjusted for qcc7xx timer latency.
 */
static uint32_t counter_qcc74x_compare_adjust(uint32_t ticks)
{
	if (ticks >= QCC7XX_COUNTER_TICKS_DELAY) {
		return ticks - QCC7XX_COUNTER_TICKS_DELAY;
	}

	return ticks;
}

/**
 * @brief Resolve instance-specific TSR register offset.
 *
 * @param data Driver instance data.
 * @param tsr_offset Output pointer for instance TSR register offset.
 *
 * @retval true The instance index is valid and offset was resolved.
 * @retval false The instance index is not supported by this driver.
 */
static bool counter_qcc74x_get_tsr_offset(const struct timer_qcc74x_data *data, uint32_t *tsr_offset)
{
	switch (data->dev_cfg.idx) {
	case 0U:
		*tsr_offset = TIMER_TSR0_OFFSET;
		return true;
	case 1U:
		*tsr_offset = TIMER_TSR1_OFFSET;
		return true;
	default:
		return false;
	}
}

/**
 * @brief Resolve instance-specific clear bit in the shared TCER register.
 *
 * @param data Driver instance data.
 * @param clear_bit Output pointer for instance counter-clear bit mask.
 *
 * @retval true The instance index is valid and clear bit was resolved.
 * @retval false The instance index is not supported by this driver or NULL ptr provided.
 */
static bool counter_qcc74x_get_tcer_clear_bit(const struct timer_qcc74x_data *data, uint32_t *clear_bit)
{
	if (clear_bit == NULL) {
		return false;
	}

	switch (data->dev_cfg.idx) {
	case 0U:
		*clear_bit = TIMER_TCR0_CNT_CLR;
		return true;
	case 1U:
		*clear_bit = TIMER_TCR1_CNT_CLR;
		return true;
	default:
		return false;
	}
}

/**
 * @brief Read pending compare interrupt bits from instance TSR register.
 *
 * @param data Driver instance data.
 *
 * @return Bitmask of pending compare interrupt sources.
 */
static uint32_t counter_qcc74x_tsr_get(const struct timer_qcc74x_data *data)
{
	uint32_t tsr_offset = 0U;
	uint32_t reg = 0U;

	if (!counter_qcc74x_get_tsr_offset(data, &tsr_offset)) {
		LOG_ERR("%s: unsupported timer instance index %u", data->dev_cfg.name,
			(uint32_t)data->dev_cfg.idx);
		return 0U;
	}

	reg = sys_read32(data->dev_cfg.reg_base + tsr_offset);

	return reg & BIT_MASK(QCC7XX_COUNTER_ALARM_CH_COUNT);
}

/**
 * @brief Clear timer counter value for the active instance.
 *
 * @param data Driver instance data.
 */
static void counter_qcc74x_clear(const struct device *dev)
{
	uint32_t tcer_addr = 0U;
	uint32_t tcer_val = 0U;
	uint32_t clear_bit = 0U;
	struct timer_qcc74x_data *data = dev->data;

	if (!counter_qcc74x_get_tcer_clear_bit(data, &clear_bit)) {
		LOG_ERR("%s: unsupported timer instance index %u", data->dev_cfg.name,
			(uint32_t)data->dev_cfg.idx);
		return;
	}

	tcer_addr = data->dev_cfg.reg_base + TIMER_TCER_OFFSET;
	tcer_val = sys_read32(tcer_addr);
	sys_write32(tcer_val | clear_bit, tcer_addr);
	/* Wait for the changes to take effect */
	k_busy_wait(5);
	sys_write32(tcer_val & ~clear_bit, tcer_addr);
}

/**
 * @brief Validate alarm channel id against HW and current top mode usage.
 *
 * @param dev Counter device.
 * @param chan Alarm channel id.
 *
 * @retval 0 Channel is valid and available.
 * @retval -ENOTSUP Channel is not supported in current configuration.
 */
static int counter_qcc74x_chan_validate(const struct device *dev, uint8_t chan)
{
	const struct timer_qcc74x_config *cfg = dev->config;
	struct timer_qcc74x_data *data = dev->data;

	if ((chan >= cfg->counter_info.channels) || (chan >= QCC7XX_COUNTER_ALARM_CH_COUNT)) {
		LOG_ERR("%s: unsupported alarm channel %u", data->dev_cfg.name, (uint32_t)chan);
		return -ENOTSUP;
	}

	if (data->top_cfg_set && (chan == QCC7XX_COUNTER_TOP_CHANNEL)) {
		LOG_ERR("%s: channel %u is used by top value mode", data->dev_cfg.name,
			(uint32_t)chan);
		return -ENOTSUP;
	}

	return 0;
}

static int counter_qcc74x_start(const struct device *dev)
{
	struct timer_qcc74x_data *data = dev->data;

	uint32_t key = irq_lock();
	qc7xx_timer_start(&data->dev_cfg);
	irq_unlock(key);

	return 0;
}

static int counter_qcc74x_stop(const struct device *dev)
{
	struct timer_qcc74x_data *data = dev->data;
	const struct timer_qcc74x_config *cfg = dev->config;

	LOG_DBG("Stopping: %s", data->dev_cfg.name);
	uint32_t key = irq_lock();
	qc7xx_timer_stop(&data->dev_cfg);
	for (uint32_t i = 0; i < TIMER_COMP_NONE; i++) {
		qc7xx_timer_compint_mask(&data->dev_cfg, i, true);
		qc7xx_timer_compint_clear(&data->dev_cfg, i);
		data->alarm[i].active = false;
	}
	data->top_cfg_set = false;
	data->top_cfg.ticks = cfg->counter_info.max_top_value;
	irq_unlock(key);

	return 0;
}

static int counter_qcc74x_get_value(const struct device *dev, uint32_t *ticks)
{
	struct timer_qcc74x_data *data = dev->data;

	*ticks = qc7xx_timer_get_countervalue(&data->dev_cfg);
	LOG_DBG("%s value read: %u", data->dev_cfg.name, *ticks);

	return 0;
}

/**
 * @brief Initialize a counter
 *
 * @param dev Counter device
 * @return int Always zero
 */
static int counter_qcc74x_init(const struct device *dev)
{
	struct timer_qcc74x_data *data = dev->data;
	struct timer_qcc74x_config *cfg = (struct timer_qcc74x_config *)dev->config;
	const struct qc7xx_timer_config_s *timer_cfg = &cfg->config;
	uint32_t in_freq = 0U;

	LOG_DBG("Initializing qcc74x counter driver: %s", data->dev_cfg.name);

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("Clock controller (%s) is NOT ready! Counter cannot start.",
			cfg->clock_dev->name);
		return -ENODEV;
	}

	if (data->dev_cfg.idx != 0 && data->dev_cfg.idx != 1) {
		LOG_ERR("Timer index should be 0 (TIMER0) or 1 (TIMER1). Current val=%u",
			data->dev_cfg.idx);
		return -ENODEV;
	}

	/* Dummy clock config to ensure that clock source is set correctly
	 * for clock_control_get_rate function */
	qc7xx_timer_init(&data->dev_cfg, timer_cfg);

	clock_control_get_rate(cfg->clock_dev, cfg->clock_id, &in_freq);
	uint32_t divisor = (cfg->config.clock_div + 1);
	uint32_t count_freq = in_freq / divisor;

	cfg->counter_info.freq = count_freq;
	data->top_cfg.ticks = cfg->counter_info.max_top_value;
	data->top_cfg_set = false;

	LOG_DBG("%s: Clock_id %ld, clock %u / %u = %u", data->dev_cfg.name,
		(uintptr_t)cfg->clock_gate_id, in_freq, divisor, count_freq);
	LOG_DBG("%s: Gate clock_id %lu", data->dev_cfg.name, (uintptr_t)cfg->clock_gate_id);

	uint32_t key = irq_lock();
	qc7xx_timer_init(&data->dev_cfg, timer_cfg);

	clock_control_on(cfg->clock_dev, (clock_control_subsys_t)(uintptr_t)cfg->clock_gate_id);

	/* Call the interrupt setup routine if defined */
	if (cfg->irq_cfg_func) {
		cfg->irq_cfg_func();
	}
	irq_unlock(key);

	if (cfg->start_at_init != 0) {
		(void)counter_qcc74x_start(dev);
	}

	return 0;
}

/**
 * @brief Handle top value interrupt for channel 2
 *
 * @param dev Counter device
 * @param data Timer data structure
 * @param now Current counter value
 */
static void handle_top_value_interrupt(const struct device *dev, struct timer_qcc74x_data *data,
				       uint32_t now)
{
	qc7xx_timer_compint_clear(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL);

	if (data->top_cfg.callback) {
		LOG_DBG("%s: Calling top callback", data->dev_cfg.name);
		data->top_cfg.callback(dev, data->top_cfg.user_data);
	}

	/* Re-arm the compare for next period in non-reset mode. */
	if (data->top_cfg.flags & COUNTER_TOP_CFG_DONT_RESET) {
		qc7xx_timer_set_compvalue(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL,
					  counter_qcc74x_compare_adjust(now + data->top_cfg.ticks));
		return;
	}

	/* In reset mode emulate wrap-around by clearing the counter. */
	counter_qcc74x_clear(dev);
	qc7xx_timer_set_compvalue(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL,
				  counter_qcc74x_compare_adjust(data->top_cfg.ticks));
}

/**
 * @brief Handle regular alarm interrupt for channels 0, 1 and 2
 *
 * @param dev Counter device
 * @param data Timer data structure
 * @param channel Channel number
 * @param now Current counter value
 */
static void handle_alarm_interrupt(const struct device *dev, struct timer_qcc74x_data *data,
				   uint8_t channel, uint32_t now)
{
	struct counter_alarm_cfg cfg = data->alarm[channel].cfg;

	qc7xx_timer_compint_mask(&data->dev_cfg, channel, true);
	qc7xx_timer_compint_clear(&data->dev_cfg, channel);

	if (!data->alarm[channel].active) {
		return;
	}

	data->alarm[channel].active = false;

	if (cfg.callback) {
		LOG_DBG("%s: Calling callback for ch: %u", data->dev_cfg.name, channel);
		cfg.callback(dev, channel, now, cfg.user_data);
	}
}

/**
 * @brief Counter interrupt service routine
 *
 * @param dev Counter device
 */
void counter_qcc74x_isr(const struct device *dev)
{
	struct timer_qcc74x_data *data = dev->data;
	uint32_t tsr_mask = 0U;
	uint32_t now = 0U;

	LOG_DBG("%s: Interrupt", data->dev_cfg.name);

	(void)counter_qcc74x_get_value(dev, &now);
	tsr_mask = counter_qcc74x_tsr_get(data);

	/* Compare IRQs are latched in TSRx until cleared via TICRx. In normal flow
	 * this mask is non-zero here; keep the check for spurious/late-cleared IRQs.
	 */
	if (tsr_mask == 0U) {
		return;
	}

	for (uint32_t i = 0U; i < TIMER_COMP_NONE; i++) {
		if ((tsr_mask & BIT(i)) == 0U) {
			continue;
		}

		if (data->top_cfg_set && (i == QCC7XX_COUNTER_TOP_CHANNEL)) {
			handle_top_value_interrupt(dev, data, now);
		} else {
			handle_alarm_interrupt(dev, data, i, now);
		}
	}
}

static int counter_qcc74x_set_alarm(const struct device *dev, uint8_t chan,
				  const struct counter_alarm_cfg *alarm_cfg)
{
	struct timer_qcc74x_data *data = dev->data;
	int err = 0;
	uint32_t key = 0U;
	uint32_t curr_ticks_value = 0U;
	uint32_t alarm_ticks_value = 0U;

	if (alarm_cfg == NULL) {
		return -EINVAL;
	}

	err = counter_qcc74x_chan_validate(dev, chan);
	if (err != 0) {
		return err;
	}

	if (data->top_cfg_set && ((data->top_cfg.flags & COUNTER_TOP_CFG_DONT_RESET) == 0U) &&
	    (alarm_cfg->ticks > data->top_cfg.ticks)) {
		return -EINVAL;
	}

	if (alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE) {
		alarm_ticks_value = alarm_cfg->ticks;
	} else {
		curr_ticks_value = qc7xx_timer_get_countervalue(&data->dev_cfg);
		alarm_ticks_value = alarm_cfg->ticks + curr_ticks_value;

		if (data->top_cfg_set &&
		    ((data->top_cfg.flags & COUNTER_TOP_CFG_DONT_RESET) == 0U) &&
		    (alarm_ticks_value >= data->top_cfg.ticks)) {
			alarm_ticks_value %= data->top_cfg.ticks;
		}
	}

	LOG_DBG("%s: Setting alarm ch %u at %u ticks", data->dev_cfg.name, (uint32_t)chan,
		alarm_cfg->ticks);

	key = irq_lock();
	data->alarm[chan].cfg = *alarm_cfg;
	data->alarm[chan].active = true;
	qc7xx_timer_compint_clear(&data->dev_cfg, chan);
	/* qcc7xx timer asserts IRQ two ticks after a compare match. */
	qc7xx_timer_set_compvalue(&data->dev_cfg, chan,
				  counter_qcc74x_compare_adjust(alarm_ticks_value));
	qc7xx_timer_compint_mask(&data->dev_cfg, chan, false);
	irq_unlock(key);

	return 0;
}

static int counter_qcc74x_cancel_alarm(const struct device *dev, uint8_t chan)
{
	struct timer_qcc74x_data *data = dev->data;
	int err = 0;

	err = counter_qcc74x_chan_validate(dev, chan);
	if (err != 0) {
		return err;
	}

	LOG_DBG("%s: Cancelling alarm ch: %u", data->dev_cfg.name, (uint32_t)chan);
	uint32_t key = irq_lock();
	qc7xx_timer_compint_mask(&data->dev_cfg, chan, true);
	qc7xx_timer_compint_clear(&data->dev_cfg, chan);
	data->alarm[chan].active = false;
	irq_unlock(key);

	return 0;
}

static int counter_qcc74x_set_top_value(const struct device *dev, const struct counter_top_cfg *cfg)
{
	struct timer_qcc74x_data *data = dev->data;
	const struct timer_qcc74x_config *config = dev->config;
	uint32_t key = 0U;

	if (!cfg) {
		return -EINVAL;
	}

	if (cfg->ticks == 0 || cfg->ticks > config->counter_info.max_top_value) {
		return -EINVAL;
	}

	LOG_DBG("%s: Setting top value to %u ticks", data->dev_cfg.name, cfg->ticks);

	key = irq_lock();

	/* Restore default top and release channel 2 for regular alarms. */
	if ((cfg->ticks == config->counter_info.max_top_value) && (cfg->callback == NULL) &&
	    ((cfg->flags & COUNTER_TOP_CFG_DONT_RESET) == 0U)) {
		data->top_cfg_set = false;
		qc7xx_timer_compint_mask(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL, true);
		qc7xx_timer_compint_clear(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL);
		irq_unlock(key);
		return 0;
	}

	if (data->alarm[QCC7XX_COUNTER_TOP_CHANNEL].active) {
		LOG_ERR("%s: cannot enable top mode while channel %u alarm is active",
			data->dev_cfg.name, (uint32_t)QCC7XX_COUNTER_TOP_CHANNEL);
		irq_unlock(key);
		return -EBUSY;
	}

	data->top_cfg = *cfg;
	data->top_cfg_set = true;
	qc7xx_timer_compint_clear(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL);
	qc7xx_timer_set_compvalue(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL,
				  counter_qcc74x_compare_adjust(cfg->ticks));
	qc7xx_timer_compint_mask(&data->dev_cfg, QCC7XX_COUNTER_TOP_CHANNEL, false);
	irq_unlock(key);

	return 0;
}

static uint32_t counter_qcc74x_get_pending_int(const struct device *dev)
{
	struct timer_qcc74x_data *data = dev->data;

	if (counter_qcc74x_tsr_get(data) != 0U) {
		LOG_DBG("%s: Interrupt pending", data->dev_cfg.name);
		return 1;
	}

	LOG_DBG("%s: No interrupts pending", data->dev_cfg.name);
	return 0;
}

static uint32_t counter_qcc74x_get_top_value(const struct device *dev)
{
	const struct timer_qcc74x_config *config = dev->config;
	struct timer_qcc74x_data *data = dev->data;
	uint32_t top = data->top_cfg_set ? data->top_cfg.ticks : config->counter_info.max_top_value;

	LOG_DBG("%s: Top value read: %u", data->dev_cfg.name, top);
	return top;
}

static uint32_t counter_qcc74x_get_freq(const struct device *dev)
{
	const struct timer_qcc74x_config *cfg = dev->config;
	struct timer_qcc74x_data *data = dev->data;

	LOG_DBG("%s: Freq read: %u", data->dev_cfg.name, cfg->counter_info.freq);
	return cfg->counter_info.freq;
}

static DEVICE_API(counter, counter_qcc74x_api) = {
	.start = counter_qcc74x_start,
	.stop = counter_qcc74x_stop,
	.get_value = counter_qcc74x_get_value,
	.set_alarm = counter_qcc74x_set_alarm,
	.cancel_alarm = counter_qcc74x_cancel_alarm,
	.set_top_value = counter_qcc74x_set_top_value,
	.get_pending_int = counter_qcc74x_get_pending_int,
	.get_top_value = counter_qcc74x_get_top_value,
	.get_freq = counter_qcc74x_get_freq,
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

#define QCC74X_COUNTER_INIT(inst)                                                                    \
                                                                                                   \
	static void counter_qcc74x_irq_cfg_func_##inst(void)                                         \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(inst, 0, irq),                                      \
			    DT_INST_IRQ_BY_IDX(inst, 0, priority), counter_qcc74x_isr,               \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_IDX(inst, 0, irq));                                      \
		LOG_DBG("Timer%u: irq %u setup", DT_INST_PROP_OR(inst, periph_idx, 0),             \
			DT_INST_IRQ_BY_IDX(inst, 0, irq));                                         \
	}                                                                                          \
	static struct timer_qcc74x_data counter_qcc74x_data_##inst = {                                 \
		.dev_cfg = {.name = DT_NODE_PATH(DT_DRV_INST(inst)),                               \
			    .reg_base = DT_INST_REG_ADDR(inst),                                    \
			    .irq_num = DT_INST_IRQ_BY_IDX(inst, 0, irq),                           \
			    .idx = DT_INST_PROP_OR(inst, periph_idx, 0),                           \
			    .sub_idx = 0,                                                          \
			    .dev_type = QC7XX_DEVICE_TYPE_TIMER,                                   \
			    .user_data = NULL},                                                    \
	};                                                                                         \
                                                                                                   \
	static struct timer_qcc74x_config counter_qcc74x_config_##inst = {                             \
		.counter_info = {.max_top_value = UINT32_MAX,                                      \
				 .flags = COUNTER_CONFIG_INFO_COUNT_UP,                            \
				 .freq = 0,                                                        \
				 .channels = DT_INST_PROP(inst, channels)},                        \
		.config = {.counter_mode = DT_INST_PROP(inst, counter_mode),                       \
			   .clock_source = (uint8_t)DT_INST_CLOCKS_CELL(inst, source),             \
			   .clock_div = DT_INST_PROP(inst, clock_div),                             \
			   .trigger_comp_id = DT_INST_PROP(inst, preload_id),                      \
			   .comp0_val = DT_INST_PROP(inst, comp0_val),                             \
			   .comp1_val = DT_INST_PROP(inst, comp1_val),                             \
			   .comp2_val = DT_INST_PROP(inst, comp2_val),                             \
			   .preload_val = DT_INST_PROP(inst, preload_val)},                        \
		.irq_cfg_func = counter_qcc74x_irq_cfg_func_##inst,                                  \
		.start_at_init = DT_INST_PROP_OR(inst, start_at_init, 0),                          \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clock_gate_id = QCC74X_GATE_CLOCK_ID_FROM_TIMER0(),                                 \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, counter_qcc74x_init, NULL, &counter_qcc74x_data_##inst,            \
			      &counter_qcc74x_config_##inst, POST_KERNEL,                            \
			      CONFIG_COUNTER_INIT_PRIORITY, &counter_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(QCC74X_COUNTER_INIT);
