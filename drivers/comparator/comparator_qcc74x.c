/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_acomp

#include <zephyr/arch/common/sys_io.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <hardware/hbn_reg.h>
#include <hardware/acomp_reg.h>
#define __AON_REG_H__
#include <qcc74x_hbn.h>
#include <qc7xx_acomp.h>

#define LOG_LEVEL CONFIG_COMPARATOR_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(acomp_qcc74x);

#define ACOMP_QCC74X_SETTLE_US 15U

/* Both instances share HBN IRQ registers and ACOMP_CFG.VREF_SEL. */
static struct k_spinlock acomp_qcc74x_lock;
static bool acomp_qcc74x_vref_initialized;
static uint32_t acomp_qcc74x_vref_sel;

/* ACOMP configuration. */
struct acomp_qcc74x_config {
	const struct qc7xx_acomp_config_s acomp_config;
	void (*irq_init)(void);
	const uint32_t base_reg;
	const uint32_t irq_num;
	const uint8_t acomp_id;
};

/* ACOMP data. */
struct acomp_qcc74x_data {
	comparator_callback_t trigger_callback;
	void *user_data;
	atomic_t triggered;
	enum comparator_trigger trigger;
	bool active;
};

/**
 * @brief Helper function to map between trigger type defined in Zephyr and edge defined in QCC SDK.
 *
 * @param trigger   Comparator trigger.
 * @return Edge value or EINVAL for bad trigger param.
 */
static int8_t map_trigger_to_edge(enum comparator_trigger trigger)
{
	if (trigger == COMPARATOR_TRIGGER_RISING_EDGE) {
		return HBN_ACOMP_INT_EDGE_POSEDGE;
	}
	if (trigger == COMPARATOR_TRIGGER_FALLING_EDGE) {
		return HBN_ACOMP_INT_EDGE_NEGEDGE;
	}
	if (trigger == COMPARATOR_TRIGGER_BOTH_EDGES) {
		return HBN_ACOMP_INT_EDGE_POSEDGE_NEGEDGE;
	}

	return -EINVAL;
}

/**
 * @brief Clear comparator interrupt status.
 *
 * @param base_reg  Comparator control register base address.
 * @param acomp_id  Comparator ID
 */
static void acomp_qcc74x_clear_irq(const uint32_t base_reg, const uint8_t acomp_id)
{
	uint8_t irq_type = acomp_id == 0 ? HBN_INT_ACOMP0 : HBN_INT_ACOMP1;

	/* set clear bit */
	uint32_t reg_val = sys_read32(base_reg + HBN_IRQ_CLR_OFFSET);
	reg_val |= (1U << irq_type);
	sys_write32(reg_val, base_reg + HBN_IRQ_CLR_OFFSET);

	/* unset clear bit */
	reg_val = sys_read32(base_reg + HBN_IRQ_CLR_OFFSET);
	reg_val &= (~(1U << irq_type));
	sys_write32(reg_val, base_reg + HBN_IRQ_CLR_OFFSET);
}

/**
 * @brief Enable comparator interrupt.
 *
 * @param base_reg  Comparator control register base address.
 * @param acomp_id  Comparator ID
 * @param trigger   interrupt edge type, this parameter can be one of the following values:
 *           @arg COMPARATOR_TRIGGER_RISING_EDGE
 *           @arg COMPARATOR_TRIGGER_FALLING_EDGE
 *           @arg COMPARATOR_TRIGGER_BOTH_EDGES
 * @retval 0        Success.
 * @retval -EINVAL  Enable failed. Wrong trigger used
 */
static int acomp_qcc74x_enable_irq(const uint32_t base_reg, const uint8_t acomp_id,
				  const enum comparator_trigger trigger)
{
	const int8_t edge = map_trigger_to_edge(trigger);
	if (edge < 0) {
		LOG_ERR("Requested interrupt with invalid trigger value: %d", trigger);
		return -EINVAL;
	}

	uint32_t reg_val = sys_read32(base_reg + HBN_IRQ_MODE_OFFSET);
	if (acomp_id == AON_ACOMP0_ID) {
		reg_val = QC7XX_SET_REG_BITS_VAL(reg_val, HBN_IRQ_ACOMP0_EN, edge);
	} else {
		reg_val = QC7XX_SET_REG_BITS_VAL(reg_val, HBN_IRQ_ACOMP1_EN, edge);
	}

	sys_write32(reg_val, base_reg + HBN_IRQ_MODE_OFFSET);
	return 0;
}

/**
 * @brief Disable comparator interrupt. This disables interrupts for all edges.
 *
 * @param base_reg  Comparator control register base address.
 * @param acomp_id  Comparator ID
 */
static void acomp_qcc74x_disable_irq(const uint32_t base_reg, const uint8_t acomp_id)
{
	uint32_t reg_val = sys_read32(base_reg + HBN_IRQ_MODE_OFFSET);

	if (acomp_id == AON_ACOMP0_ID) {
		reg_val = QC7XX_SET_REG_BITS_VAL(reg_val, HBN_IRQ_ACOMP0_EN, 0U);
	} else {
		reg_val = QC7XX_SET_REG_BITS_VAL(reg_val, HBN_IRQ_ACOMP1_EN, 0U);
	}

	sys_write32(reg_val, base_reg + HBN_IRQ_MODE_OFFSET);
}

/**
 * @brief Check if interrupt is on for given comparator ID
 *
 * @param acomp_id  Comparator ID
 * @retval 1        If IRQ is on.
 */
static uint8_t acomp_qcc74_get_irq_state(uint8_t acomp_id)
{
	const uint8_t irq_type = acomp_id == AON_ACOMP0_ID ? HBN_INT_ACOMP0 : HBN_INT_ACOMP1;
	const uint32_t reg_val = sys_read32(HBN_BASE + HBN_IRQ_STAT_OFFSET);

	if (reg_val & (1U << irq_type)) {
		return 1U;
	} else {
		return 0U;
	}
}

/**
 * @brief Initialize the Opal Analog Comparator driver.
 *
 * @param dev           Pointer to device structure.
 * @retval 0            On success.
 */
/* The caller must hold acomp_qcc74x_lock. */
static void acomp_qcc74x_set_enabled(const struct device *dev, bool enabled)
{
	const struct acomp_qcc74x_config *config = dev->config;
	const uint32_t base_reg = config->base_reg +
		(config->acomp_id == AON_ACOMP0_ID ? AON_ACOMP_REG_ACOMP0_CTRL_OFFSET :
						     AON_ACOMP_REG_ACOMP1_CTRL_OFFSET);
	uint32_t reg_val = sys_read32(base_reg);

	if (enabled) {
		reg_val |= AON_ACOMP_EN;
	} else {
		reg_val &= ~AON_ACOMP_EN;
	}
	sys_write32(reg_val, base_reg);
}

/* The caller must hold acomp_qcc74x_lock. The comparator remains disabled. */
static int acomp_qcc74x_apply_config(const struct device *dev)
{
	const struct acomp_qcc74x_config *config = dev->config;
	const uint32_t ctrl_reg = config->base_reg +
		(config->acomp_id == AON_ACOMP0_ID ? AON_ACOMP_REG_ACOMP0_CTRL_OFFSET :
						     AON_ACOMP_REG_ACOMP1_CTRL_OFFSET);
	uint32_t reg_val;

	if (acomp_qcc74x_vref_initialized &&
	    acomp_qcc74x_vref_sel != config->acomp_config.vio_sel) {
		return -EINVAL;
	}
	acomp_qcc74x_vref_sel = config->acomp_config.vio_sel;
	acomp_qcc74x_vref_initialized = true;

	reg_val = sys_read32(ctrl_reg);
	reg_val &= ~AON_ACOMP_EN;
	if (config->acomp_config.mux_en) {
		reg_val |= AON_ACOMP_MUX_EN;
	} else {
		reg_val &= ~AON_ACOMP_MUX_EN;
	}
	reg_val &= ~AON_ACOMP_POS_SEL_MASK;
	reg_val |= config->acomp_config.pos_chan_sel << AON_ACOMP_POS_SEL_SHIFT;
	reg_val &= ~AON_ACOMP_NEG_SEL_MASK;
	reg_val |= config->acomp_config.neg_chan_sel << AON_ACOMP_NEG_SEL_SHIFT;
	reg_val &= ~AON_ACOMP_LEVEL_SEL_MASK;
	reg_val |= config->acomp_config.scaling_factor << AON_ACOMP_LEVEL_SEL_SHIFT;
	reg_val &= ~AON_ACOMP_BIAS_PROG_MASK;
	reg_val |= config->acomp_config.bias_prog << AON_ACOMP_BIAS_PROG_SHIFT;
	reg_val &= ~AON_ACOMP_HYST_SELP_MASK;
	reg_val |= config->acomp_config.hysteresis_pos_volt << AON_ACOMP_HYST_SELP_SHIFT;
	reg_val &= ~AON_ACOMP_HYST_SELN_MASK;
	reg_val |= config->acomp_config.hysteresis_neg_volt << AON_ACOMP_HYST_SELN_SHIFT;
	sys_write32(reg_val, ctrl_reg);

	reg_val = sys_read32(config->base_reg + AON_ACOMP_REG_ACOMP_CFG_OFFSET);
	reg_val &= ~AON_ACOMP_VREF_SEL_MASK;
	reg_val |= config->acomp_config.vio_sel << AON_ACOMP_VREF_SEL_SHIFT;
	sys_write32(reg_val, config->base_reg + AON_ACOMP_REG_ACOMP_CFG_OFFSET);

	return 0;
}

static int acomp_qcc74x_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct acomp_qcc74x_config *config = dev->config;
	struct acomp_qcc74x_data *data = dev->data;
	k_spinlock_key_t key;
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		key = k_spin_lock(&acomp_qcc74x_lock);
		acomp_qcc74x_disable_irq(config->base_reg, config->acomp_id);
		acomp_qcc74x_clear_irq(config->base_reg, config->acomp_id);
		acomp_qcc74x_set_enabled(dev, false);
		data->active = false;
		k_spin_unlock(&acomp_qcc74x_lock, key);
		break;

	case PM_DEVICE_ACTION_RESUME:
		key = k_spin_lock(&acomp_qcc74x_lock);
		acomp_qcc74x_disable_irq(config->base_reg, config->acomp_id);
		ret = acomp_qcc74x_apply_config(dev);
		if (ret < 0) {
			k_spin_unlock(&acomp_qcc74x_lock, key);
			LOG_ERR("ACOMP instances require one shared VREF (%u != %u)",
				acomp_qcc74x_vref_sel, config->acomp_config.vio_sel);
			return ret;
		}
		acomp_qcc74x_set_enabled(dev, true);
		k_busy_wait(ACOMP_QCC74X_SETTLE_US);
		acomp_qcc74x_clear_irq(config->base_reg, config->acomp_id);
		if (data->trigger != COMPARATOR_TRIGGER_NONE) {
			(void)acomp_qcc74x_enable_irq(config->base_reg, config->acomp_id,
						 data->trigger);
		}
		irq_enable(config->irq_num);
		data->active = true;
		k_spin_unlock(&acomp_qcc74x_lock, key);
		break;

	default:
		return -ENOTSUP;
	}

	/*
	 * Future wake-source support belongs in the PDS15 pre-entry path. Devices
	 * enabled through pm_device_wakeup_enable() are skipped by system-managed
	 * suspend, and must additionally configure the QCC74x LP wake routing.
	 */
	return 0;
}

static int acomp_qcc74x_init(const struct device *dev)
{
	const struct acomp_qcc74x_config *config = dev->config;

	LOG_DBG("%s initialization started", dev->name);
	config->irq_init();
	return pm_device_driver_init(dev, acomp_qcc74x_pm_action);
}

/**
 * @brief Comparator change state interrupt handler
 *
 * @param dev           Pointer to device structure.
 */
static void acomp_qcc74x_isr(const struct device *dev)
{
	const struct acomp_qcc74x_config *config = dev->config;
	struct acomp_qcc74x_data *data = dev->data;
	comparator_callback_t callback;
	void *user_data;
	k_spinlock_key_t key = k_spin_lock(&acomp_qcc74x_lock);

	/* Interrupt could be caused by comparator with different ID. Ignore in such case. */
	if (!acomp_qcc74_get_irq_state(config->acomp_id)) {
		k_spin_unlock(&acomp_qcc74x_lock, key);
		LOG_DBG("Interrupt is expected by different device. This device is %s. ISR skipped",
			dev->name);
		return;
	}

	acomp_qcc74x_clear_irq(config->base_reg, config->acomp_id);
	callback = data->trigger_callback;
	user_data = data->user_data;
	if (callback == NULL) {
		atomic_set_bit(&data->triggered, 0);
		k_spin_unlock(&acomp_qcc74x_lock, key);
		return;
	}
	k_spin_unlock(&acomp_qcc74x_lock, key);

	LOG_DBG("ISR executed for device %s", dev->name);
	callback(dev, user_data);
}

static int acomp_qcc74x_get_output(const struct device *dev)
{
	const struct acomp_qcc74x_config *config = dev->config;
	const uint32_t base_reg = config->base_reg + AON_ACOMP_REG_ACOMP_CFG_OFFSET;
	struct acomp_qcc74x_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&acomp_qcc74x_lock);

	if (!data->active) {
		k_spin_unlock(&acomp_qcc74x_lock, key);
		return -EIO;
	}

	const uint32_t reg_val = sys_read32(base_reg);

	int ret_val = 0;
	if (AON_ACOMP0_ID == config->acomp_id) {
		ret_val = (reg_val & AON_ACOMP0_OUT_RAW_DATA_MASK) >> AON_ACOMP0_OUT_RAW_DATA_SHIFT;
	} else {
		ret_val = (reg_val & AON_ACOMP1_OUT_RAW_DATA_MASK) >> AON_ACOMP1_OUT_RAW_DATA_SHIFT;
	}
	k_spin_unlock(&acomp_qcc74x_lock, key);

	LOG_DBG("Output state for device %s = %d", dev->name, ret_val);
	return ret_val;
}

static int acomp_qcc74x_set_trigger(const struct device *dev, enum comparator_trigger trigger)
{
	const struct acomp_qcc74x_config *config = dev->config;
	struct acomp_qcc74x_data *data = dev->data;
	k_spinlock_key_t key;

	if ((trigger != COMPARATOR_TRIGGER_NONE) && (map_trigger_to_edge(trigger) < 0)) {
		return -EINVAL;
	}

	key = k_spin_lock(&acomp_qcc74x_lock);
	if (data->active) {
		acomp_qcc74x_disable_irq(config->base_reg, config->acomp_id);
		acomp_qcc74x_clear_irq(config->base_reg, config->acomp_id);
	}
	atomic_clear_bit(&data->triggered, 0);
	data->trigger = trigger;

	if (!data->active) {
		k_spin_unlock(&acomp_qcc74x_lock, key);
		return 0;
	}

	int ret_val = 0;
	if (trigger != COMPARATOR_TRIGGER_NONE) {
		LOG_DBG("Trigger set for device %s, edge = %d", dev->name, trigger);
		ret_val = acomp_qcc74x_enable_irq(config->base_reg, config->acomp_id, trigger);
		if (ret_val != 0) {
			LOG_ERR("Enable IRQ failed for device %s, with return value %d", dev->name,
				ret_val);
		}
	}
	k_spin_unlock(&acomp_qcc74x_lock, key);

	return ret_val;
}

static int acomp_qcc74x_set_trigger_callback(const struct device *dev, comparator_callback_t callback,
					   void *user_data)
{
	struct acomp_qcc74x_data *data = dev->data;
	bool pending;
	k_spinlock_key_t key = k_spin_lock(&acomp_qcc74x_lock);

	data->trigger_callback = callback;
	data->user_data = user_data;
	pending = callback != NULL && atomic_test_and_clear_bit(&data->triggered, 0);
	k_spin_unlock(&acomp_qcc74x_lock, key);

	if (pending) {
		callback(dev, user_data);
	}
	LOG_DBG("Trigger callback set for device %s", dev->name);
	return 0;
}

static int acomp_qcc74x_trigger_is_pending(const struct device *dev)
{
	struct acomp_qcc74x_data *data = dev->data;
	int ret_val = atomic_test_and_clear_bit(&data->triggered, 0);
	LOG_DBG("Test if trigger is pending for device %s, result = %d", dev->name, ret_val);
	return ret_val;
}

static struct comparator_driver_api acomp_qcc74x_driver_api = {
	.get_output = acomp_qcc74x_get_output,
	.set_trigger = acomp_qcc74x_set_trigger,
	.set_trigger_callback = acomp_qcc74x_set_trigger_callback,
	.trigger_is_pending = acomp_qcc74x_trigger_is_pending,
};

#define ACOMP_DEVICE(inst)                                                                         \
	static void acomp_qcc74_irq_init##inst(void)                                               \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), acomp_qcc74x_isr,       \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
                                                                                                   \
	}                                                                                          \
	static struct acomp_qcc74x_data acomp_qcc74x_data##inst = {                                    \
		.trigger_callback = NULL, .user_data = NULL, .triggered = 0,                      \
		.trigger = COMPARATOR_TRIGGER_NONE, .active = false};                              \
	static const struct acomp_qcc74x_config acomp_qcc74x_config##inst = {                          \
		.acomp_config =                                                                    \
			{                                                                          \
				.mux_en = DT_INST_PROP(inst, mux_enable),                          \
				.pos_chan_sel = DT_INST_STRING_TOKEN(inst, pos_chan_sel),          \
				.neg_chan_sel = DT_INST_STRING_TOKEN(inst, neg_chan_sel),          \
				.vio_sel = DT_INST_PROP(inst, vio_sel),                            \
				.scaling_factor = DT_INST_STRING_TOKEN(inst, scaling_factor),      \
				.bias_prog = DT_INST_STRING_TOKEN(inst, bias_prog),                \
				.hysteresis_pos_volt =                                             \
					DT_INST_STRING_TOKEN(inst, hysteresis_pos_volt),           \
				.hysteresis_neg_volt =                                             \
					DT_INST_STRING_TOKEN(inst, hysteresis_neg_volt),           \
			},                                                                         \
		.irq_init = acomp_qcc74_irq_init##inst,                                            \
		.base_reg = DT_INST_REG_ADDR(inst),                                                \
		.irq_num = DT_INST_IRQN(inst),                                                     \
		.acomp_id = DT_INST_PROP(inst, acomp_id)};                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, acomp_qcc74x_pm_action);                                       \
	DEVICE_DT_INST_DEFINE(inst, acomp_qcc74x_init, PM_DEVICE_DT_INST_GET(inst),                   \
			      &acomp_qcc74x_data##inst,                                               \
			      &acomp_qcc74x_config##inst, POST_KERNEL,                               \
			      CONFIG_COMPARATOR_INIT_PRIORITY, &acomp_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ACOMP_DEVICE)
