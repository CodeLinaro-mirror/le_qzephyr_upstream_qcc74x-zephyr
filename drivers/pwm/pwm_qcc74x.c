/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_pwm

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/pm/device.h>

#include <hardware/glb_reg.h>
#include <hardware/pwm_v2_reg.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_pwm_ll.h>

#define LOG_LEVEL CONFIG_PWM_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pwm_qcc74x);

#define QC7XX_PWM_THRESHOLD_REG_SIZE      16U /* size of threshold in pwm_mc0_ch0_thre register */
#define QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE 4U  /* size of channel in pwm_mc0_config1 register */
#define QC7XX_PWM_PER_CHANNEL_CONFIG_REG_SIZE                                                      \
	2U /* size of config per channel in pwm_mc0_config1 register */

#define QC7XX_PWM_MAX_CHANNELS          4U
#define QC7XX_PWM_STATUS_DELAY_CHECK_MS 5U
#define QC7XX_PWM_TIMEOUT_MS            100U
#define QC7XX_PWM_MIN_PERIOD_CYCLES     2U
#define QC7XX_PWM_REAL_CLOCK_WORKAROUND 8U

/* PWM configuration. */
struct pwm_qcc74x_config {
	const struct pinctrl_dev_config *pcfg;
	const uint32_t base_reg;
	const struct device *const clock_dev;
	const clock_control_subsys_t clock_id;
	const struct qcc74x_clk_settings clock_settings;
	const uint16_t min_period_cycles;
	const struct reset_dt_spec reset;
};

/* PWM data. */
struct pwm_qcc74x_data {
	struct k_mutex lock; /* Protects shared register R-M-W sequences */
};

/**
 * @brief Check if PWM has reached the expected run/stop status.
 *
 * @param base_reg          PWM base register address.
 * @param expect_stopped    Is stop expected
 * @retval true             Status reached within timeout.
 * @retval false            Timeout occurred.
 */
static bool pwm_qcc74x_check_pwm_status(const uint32_t base_reg, const bool expect_stopped)
{
	uint32_t reg_val = 0U;
	bool is_expected = false;

	/*
	 * This is used by system-managed Device PM while the idle thread enters
	 * and resumes PDS. Do not use k_msleep() here: that would enqueue the
	 * idle thread in Zephyr's timeout queue. Do not use uptime for the
	 * timeout either: the system clock can be paused during PDS resume.
	 */
	for (uint32_t elapsed_ms = 0U;
	     elapsed_ms <= QC7XX_PWM_TIMEOUT_MS;
	     elapsed_ms += QC7XX_PWM_STATUS_DELAY_CHECK_MS) {
		reg_val = sys_read32(base_reg + PWM_MC0_CONFIG0_OFFSET);
		reg_val &= PWM_STS_STOP;
		is_expected = expect_stopped ? (reg_val != 0) : (reg_val == 0);
		if (is_expected) {
			return true;
		}

		k_busy_wait(QC7XX_PWM_STATUS_DELAY_CHECK_MS * USEC_PER_MSEC);
	}

	LOG_ERR("PWM failed to check PWM state within timeout.");
	return false;
}

/**
 * @brief Set PWM channel thresholds (duty cycle).
 *
 * @param base_reg          PWM base register address.
 * @param channel           PWM channel number.
 * @param low_threshold     Low threshold value.
 * @param high_threshold    High threshold value.
 */
static void pwm_qcc74x_set_channel_threshold(const uint32_t base_reg, const uint32_t channel,
					   const uint16_t low_threshold,
					   const uint16_t high_threshold)
{
	sys_write32(((uint32_t)high_threshold << QC7XX_PWM_THRESHOLD_REG_SIZE) | low_threshold,
		    base_reg + PWM_MC0_CH0_THRE_OFFSET +
			    channel * QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE);
}

/**
 * @brief Enable positive output for a PWM channel.
 *
 * @param base_reg PWM base register address.
 * @param channel  PWM channel number.
 */
static void pwm_qcc74x_positive_channel_start(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	reg_val |= (PWM_CH0_PEN << (QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE * channel));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Enable negative output for a PWM channel.
 *
 * @param base_reg PWM base register address.
 * @param channel  PWM channel number.
 */
static void pwm_qcc74x_negative_channel_start(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	reg_val |= (PWM_CH0_NEN << (QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE * channel));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Disable positive output for a PWM channel.
 *
 * @param base_reg PWM base register address.
 * @param channel  PWM channel number.
 */
static void pwm_qcc74x_positive_channel_stop(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	reg_val &= ~(PWM_CH0_PEN << (QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE * channel));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Disable negative output for a PWM channel.
 *
 * @param base_reg PWM base register address.
 * @param channel  PWM channel number.
 */
static void pwm_qcc74x_negative_channel_stop(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	reg_val &= ~(PWM_CH0_NEN << (QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE * channel));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Stop the PWM and wait for stop status.
 *
 * @param base_reg      PWM base register address.
 * @retval 0            On success.
 * @retval -ETIMEDOUT   On timeout.
 */
static int32_t pwm_qcc74x_stop_pwm(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG0_OFFSET);
	reg_val |= PWM_STOP_EN;
	reg_val |= PWM_STOP_MODE;
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG0_OFFSET);
	LOG_DBG("Initiated PWM stop. Waiting for stop status...");

	const bool is_stop_expected = true;
	if (!pwm_qcc74x_check_pwm_status(base_reg, is_stop_expected)) {
		return -ETIMEDOUT;
	}
	return 0;
}

/**
 * @brief Start the PWM and wait for run status.
 *
 * @param base_reg      PWM base register address.
 * @retval 0            On success.
 * @retval -ETIMEDOUT   On timeout.
 */
static int32_t pwm_qcc74x_start_pwm(const uint32_t base_reg)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG0_OFFSET);
	reg_val &= ~PWM_STOP_EN;
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG0_OFFSET);
	LOG_DBG("Initiated PWM start. Waiting for start status...");

	const bool is_stop_expected = false;
	if (!pwm_qcc74x_check_pwm_status(base_reg, is_stop_expected)) {
		return -ETIMEDOUT;
	}
	return 0;
}

/**
 * @brief Set the PWM period (in cycles).
 *
 * @param base_reg  PWM base register address.
 * @param period    Period in cycles.
 * @retval 0        On success.
 * @retval -EINVAL  On invalid period.
 */
static int32_t pwm_qcc74x_config_set_period(const uint32_t base_reg, const uint32_t period)
{
	if (period < QC7XX_PWM_MIN_PERIOD_CYCLES) {
		LOG_ERR("Invalid period cycles specified: %d. Must be >= %d.", period,
			QC7XX_PWM_MIN_PERIOD_CYCLES);
		return -EINVAL;
	}

	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_PERIOD_OFFSET);
	reg_val &= ~PWM_PERIOD_MASK;
	reg_val |= (uint32_t)period << PWM_PERIOD_SHIFT;
	sys_write32(reg_val, base_reg + PWM_MC0_PERIOD_OFFSET);
	LOG_DBG("Set PWM period to %d cycles.", period);

	return 0;
}

/**
 * @brief Configure a channel's stop state to inactive.
 *
 * @param base_reg  PWM base register address.
 * @param channel   PWM channel number.
 */
static void set_channel_inactive(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	LOG_DBG("Setting channel %d to constant inactive state.", channel);
	reg_val &= ~(PWM_CH0_PSI << (channel * QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE));
	reg_val &= ~(PWM_CH0_NSI << (channel * QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Configure a channel's stop state to active.
 *
 * @param base_reg  PWM base register address.
 * @param channel   PWM channel number.
 */
static void set_channel_active(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	LOG_DBG("Setting channel %d to constant active state.", channel);
	reg_val |= (PWM_CH0_PSI << (channel * QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE));
	reg_val |= (PWM_CH0_NSI << (channel * QC7XX_PWM_CHANNEL_CONFIG_REG_SIZE));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Configure a channel's polarity to normal.
 *
 * @param base_reg  PWM base register address.
 * @param channel   PWM channel number.
 */
static void set_normal_polarity(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	LOG_DBG("Setting channel %d to normal polarity.", channel);
	reg_val |= (PWM_CH0_PPL << (channel * QC7XX_PWM_PER_CHANNEL_CONFIG_REG_SIZE));
	reg_val |= (PWM_CH0_NPL << (channel * QC7XX_PWM_PER_CHANNEL_CONFIG_REG_SIZE));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Configure a channel's polarity to inverted.
 *
 * @param base_reg  PWM base register address.
 * @param channel   PWM channel number.
 */
static void set_inverted_polarity(const uint32_t base_reg, const uint32_t channel)
{
	uint32_t reg_val = sys_read32(base_reg + PWM_MC0_CONFIG1_OFFSET);
	LOG_DBG("Setting channel %d to inverted polarity.", channel);
	reg_val &= ~(PWM_CH0_PPL << (channel * QC7XX_PWM_PER_CHANNEL_CONFIG_REG_SIZE));
	reg_val &= ~(PWM_CH0_NPL << (channel * QC7XX_PWM_PER_CHANNEL_CONFIG_REG_SIZE));
	sys_write32(reg_val, base_reg + PWM_MC0_CONFIG1_OFFSET);
}

/**
 * @brief Set a channel to a constant inactive level.
 *
 * @param base_reg  PWM base register address.
 * @param channel   PWM channel number.
 */
static void handle_const_inactive_level(const uint32_t base_reg, const uint32_t channel)
{
	set_channel_inactive(base_reg, channel);
	pwm_qcc74x_positive_channel_stop(base_reg, channel);
	pwm_qcc74x_negative_channel_stop(base_reg, channel);
}

/**
 * @brief Set a channel to a constant active level.
 *
 * @param base_reg  PWM base register address.
 * @param channel   PWM channel number.
 */
static void handle_const_active_level(const uint32_t base_reg, const uint32_t channel)
{
	set_channel_active(base_reg, channel);
	pwm_qcc74x_positive_channel_stop(base_reg, channel);
	pwm_qcc74x_negative_channel_stop(base_reg, channel);
}

/**
 * @brief Get the current PWM period (in cycles).
 *
 * @param base_reg  PWM base register address.
 * @return          Period in cycles.
 */
static __unused uint32_t pwm_qcc74x_get_period(const uint32_t base_reg)
{
	const uint32_t reg_val = sys_read32(base_reg + PWM_MC0_PERIOD_OFFSET);
	return ((reg_val & PWM_PERIOD_MASK) >> PWM_PERIOD_SHIFT);
}


/**
 * @brief Get the clock frequency for the PWM.
 *
 * @param config    Pointer to PWM config struct.
 * @param rate      Pointer to store clock rate (Hz).
 * @retval 0        On success.
 * @retval -ENODEV  If clock controller device is not ready.
 * @retval -errno   Negative errno for other failures.
 */
static int32_t pwm_qcc74x_get_clock_freq(const struct pwm_qcc74x_config *const config,
				       uint32_t *const rate)
{
	int32_t result = 0;

	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("Clock controller device is not ready.");
		return -ENODEV;
	}

	result = clock_control_get_rate(
		config->clock_dev, (clock_control_subsys_t)(uintptr_t)config->clock_id, rate);
	if (result != 0) {
		LOG_ERR("Failed to get clock rate for source %d: %d!", (int)config->clock_id, result);
	} else {
		LOG_DBG("Clock peripheral id: %d frequency: %d Hz.", (int)config->clock_id, *rate);
	}
	return result;
}

/**
 * @brief Log information about pin configuration for PWM for debug purposes
 *
 * @param config Pin control configuration.
 */
void log_pin_information(const struct pinctrl_dev_config *const pcfg)
{
	if (pcfg != NULL && pcfg->state_cnt > 0 &&
	    pcfg->states[PINCTRL_STATE_DEFAULT].pin_cnt > 0) {
		const struct pinctrl_state *const state = &pcfg->states[PINCTRL_STATE_DEFAULT];
		LOG_DBG("PWM pinctrl default state uses %d pin(s):", state->pin_cnt);
		for (size_t i = 0U; i < state->pin_cnt; ++i) {
			LOG_DBG("PWM pinctrl | Pin number: %u, Pin function: 0x%02x, "
				"Pincfg: 0x%08x",
				(uint32_t)(state->pins[i].pinmux & GPIO_PIN_MSK),
				(uint32_t)GET_PIN_FUN(state->pins[i].pinmux),
				state->pins[i].pincfg); /* Refer to `zephyr/dt-bindings/pinctrl`
				      for details */
		}
	} else {
		LOG_DBG("Logging information about pinctrl is not possible!");
	}
}

/**
 * @brief Initialize the QCC74x PWM driver. It is linked with Zephyr API.
 *
 * @param dev           Pointer to device structure.
 * @retval 0            On success.
 * @retval -ETIMEDOUT   If PWM fails to stop or start within timeout.
 * @retval -errno       Negative errno for other failures.
 */
static int pwm_qcc74x_init(const struct device *dev)
{
	const struct pwm_qcc74x_config *const config = dev->config;
	struct pwm_qcc74x_data *data = dev->data;
	uint32_t base_reg = 0U;
	int32_t result = 0;
	uint32_t clock_rate = 0U;

	k_mutex_init(&data->lock);

	base_reg = config->base_reg;

	result = reset_line_toggle_dt(&config->reset);
	if (result < 0) {
		LOG_ERR("PWM reset line toggle failed: %d", result);
		return -EIO;
	}

	/* Enable pinctrl */
	result = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (result < 0) {
		LOG_ERR("Failed to configure pinctrl: %d.", result);
		return result;
	}

	/* Log information about pin configuration */
	log_pin_information(config->pcfg);

	/* Enable peripheral clock */
	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("Clock controller device is not ready.");
		return -ENODEV;
	}

	result = clock_control_on(config->clock_dev,
				  (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (result != 0) {
		if (result == -EALREADY) {
			LOG_WRN("Clock for PWM driver is already enabled");
		} else {
			LOG_ERR("Failed to enable PWM clock: %d", result);
			return result;
		}
	}

	/* Stop PWM if already running */
	result = pwm_qcc74x_stop_pwm(base_reg);
	if (result != 0) {
		LOG_ERR("Failed to stop PWM during initialization (%d)!", result);
		return result;
	}

	/* Configure clock and clock divider*/
	result = clock_control_configure(config->clock_dev,
					 (clock_control_subsys_t)(uintptr_t)config->clock_id,
					 (void *)&config->clock_settings);
	if (result != 0) {
		LOG_ERR("Failed to configure PWM clock during initialization (%d)!", result);
		return result;
	}

	/* Log clock source and frequency - as reference only */
	(void)pwm_qcc74x_get_clock_freq(config, &clock_rate);

	/* Set default period */
	result = pwm_qcc74x_config_set_period(base_reg, config->min_period_cycles);
	if (result != 0) {
		LOG_ERR("Failed to set initial PWM period (%d)!", result);
		return result;
	}

	/* Start pwm */
	result = pwm_qcc74x_start_pwm(base_reg);
	if (result != 0) {
		LOG_ERR("Failed to start PWM after initialization (%d)!", result);
		return result;
	}

	LOG_DBG("PWM successfully initialized.");
	return 0;
}

static int pwm_qcc74x_set_cycles(const struct device *dev, uint32_t channel, uint32_t period,
			       uint32_t pulse, pwm_flags_t flags)
{
	LOG_DBG("Set cycles for QCC74x.");

	const struct pwm_qcc74x_config *const config = dev->config;
	struct pwm_qcc74x_data *data = dev->data;
	uint32_t base_reg = config->base_reg;
	uint16_t low_threshold = 0U;
	int32_t result = 0;

	if (channel >= QC7XX_PWM_MAX_CHANNELS) {
		LOG_ERR("Invalid PWM channel: %d", channel);
		return -EINVAL;
	}

	if (pulse > UINT16_MAX) {
		LOG_ERR("Provided pulse: %d is too high. Device supports max pulse: %d.", pulse,
			UINT16_MAX);
		return -EINVAL;
	}

	if (period > UINT16_MAX) {
		LOG_ERR("Provided period: %d is too high. Device supports max period: %d.", period,
			UINT16_MAX);
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (pulse == 0U) {
		handle_const_inactive_level(base_reg, channel);
		k_mutex_unlock(&data->lock);
		return 0;
	}

	if (pulse == period) {
		handle_const_active_level(base_reg, channel);
		k_mutex_unlock(&data->lock);
		return 0;
	}

	uint16_t period_reg_val = (uint16_t)period;
	uint16_t high_threshold = (uint16_t)pulse;

	/* Set period */
	result = pwm_qcc74x_config_set_period(base_reg, period_reg_val);
	if (result != 0) {
		LOG_ERR("Failed to configure period for PWM (%d)!", result);
		k_mutex_unlock(&data->lock);
		return result;
	}

	/* Set duty cycle (per-channel register, no race) */
	pwm_qcc74x_set_channel_threshold(base_reg, channel, low_threshold, high_threshold);

	/* Configure polarity (R-M-W on shared CONFIG1) */
	if ((flags & PWM_POLARITY_INVERTED) != 0) {
		set_inverted_polarity(base_reg, channel);
	} else {
		set_normal_polarity(base_reg, channel);
	}

	/* Enable channel outputs (R-M-W on shared CONFIG1) */
	pwm_qcc74x_positive_channel_start(base_reg, channel);
	pwm_qcc74x_negative_channel_start(base_reg, channel);

	k_mutex_unlock(&data->lock);

	LOG_INF("Enabled positive and negative outputs for channel %d with period %d and pulse %d.",
		channel, period_reg_val, high_threshold);

	return 0;
}

static int pwm_qcc74x_get_cycles_per_sec(const struct device *dev, uint32_t channel, uint64_t *cycles)
{
	LOG_DBG("Get output frequency for QCC74x.");

	const struct pwm_qcc74x_config *const config = dev->config;
	uint32_t base_reg = config->base_reg;
	uint32_t clock_rate = 0U;
	uint16_t actual_clock_divider = 1U;
	uint32_t reg_val = 0U;
	int32_t result = 0;
	uint32_t hw_reg_val_clk_div = 0U;

	/* Get clock source frequency */
	result = pwm_qcc74x_get_clock_freq(config, &clock_rate);
	if (result != 0) {
		LOG_ERR("Failed to get clock rate: %d.", result);
		return result;
	}

	/* Get actual clock divider value from register. */
	reg_val = sys_read32(base_reg + PWM_MC0_CONFIG0_OFFSET);
	hw_reg_val_clk_div = (reg_val & PWM_CLK_DIV_MASK) >> PWM_CLK_DIV_SHIFT;
	if (hw_reg_val_clk_div == 0) {
		LOG_ERR("Clk div cannot be 0. Defval will be used for calculation equal to %d.",
			actual_clock_divider);
	} else {
		actual_clock_divider = hw_reg_val_clk_div;
	}

	if (config->clock_settings.clk_div != actual_clock_divider) {
		LOG_ERR("Clock divider set in device tree and read from registers does not "
			"match! reg: %d != dt: %d",actual_clock_divider, config->clock_settings.clk_div);
		return -EINVAL;
	}

	*cycles = (uint64_t)((float)clock_rate / (float)actual_clock_divider);

	LOG_INF("PWM effective frequency: %lld Hz (Source: %d Hz, Actual Div: %d).", *cycles,
		clock_rate, actual_clock_divider);
	return 0;
}

static struct pwm_driver_api pwm_qcc74x_driver_api = {
	.set_cycles = pwm_qcc74x_set_cycles,
	.get_cycles_per_sec = pwm_qcc74x_get_cycles_per_sec,
};

#ifdef CONFIG_PM_DEVICE
static int pwm_qcc74x_pm_action(const struct device *dev,
				enum pm_device_action action)
{
	const struct pwm_qcc74x_config *const config = dev->config;
	uint32_t base_reg = config->base_reg;
	int32_t result = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		/* Disable all channel outputs */
		for (uint32_t ch = 0; ch < QC7XX_PWM_MAX_CHANNELS; ch++) {
			pwm_qcc74x_positive_channel_stop(base_reg, ch);
			pwm_qcc74x_negative_channel_stop(base_reg, ch);
		}
		/* Stop PWM */
		result = pwm_qcc74x_stop_pwm(base_reg);
		if (result != 0) {
			LOG_ERR("PM suspend: failed to stop PWM (%d)", result);
			return result;
		}
		/* Gate clock */
		result = clock_control_off(config->clock_dev,
					   (clock_control_subsys_t)(uintptr_t)config->clock_id);
		if (result != 0 && result != -ENOSYS) {
			LOG_ERR("PM suspend: failed to disable clock (%d)", result);
			return result;
		}
		LOG_DBG("PWM suspended.");
		break;

	case PM_DEVICE_ACTION_RESUME:
		/* Re-enable clock */
		result = clock_control_on(config->clock_dev,
					  (clock_control_subsys_t)(uintptr_t)config->clock_id);
		if (result != 0 && result != -EALREADY) {
			LOG_ERR("PM resume: failed to enable clock (%d)", result);
			return result;
		}
		/* Restore clock divider */
		result = clock_control_configure(
			config->clock_dev,
			(clock_control_subsys_t)(uintptr_t)config->clock_id,
			(void *)&config->clock_settings);
		if (result != 0) {
			LOG_ERR("PM resume: failed to configure clock (%d)", result);
			return result;
		}
		/* Restart PWM */
		result = pwm_qcc74x_start_pwm(base_reg);
		if (result != 0) {
			LOG_ERR("PM resume: failed to start PWM (%d)", result);
			return result;
		}
		LOG_DBG("PWM resumed.");
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

#define PWM_QCC74X_INIT(inst)                                                                        \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static struct pwm_qcc74x_data pwm_qcc74x_data##inst;                                           \
	static const struct pwm_qcc74x_config pwm_qcc74x_config##inst = {                              \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.base_reg = DT_INST_REG_ADDR(inst),                                                \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, id),                 \
		.clock_settings = {                                                                \
				.enable = CLOCK_ENABLE,                                           \
				.clk_src = (uint8_t)DT_INST_CLOCKS_CELL(inst, source),             \
				.clk_div = DT_INST_PROP(inst, divider),                            \
		},                                                                                 \
		.min_period_cycles = DT_INST_PROP_OR(inst, min_period_cycles, 2),                  \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                             \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, pwm_qcc74x_pm_action);                                      \
	DEVICE_DT_INST_DEFINE(inst, pwm_qcc74x_init, PM_DEVICE_DT_INST_GET(inst),                  \
			      &pwm_qcc74x_data##inst, &pwm_qcc74x_config##inst,                    \
			      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY,                               \
			      &pwm_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_QCC74X_INIT)
