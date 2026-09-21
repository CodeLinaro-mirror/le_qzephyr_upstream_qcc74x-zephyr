/*
 * Copyright (c) 2024 MASSDRIVER EI (massdriver.space)
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 * Not a contribution.
 */

#define DT_DRV_COMPAT qcom_qcc74x_gpio

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/sys/slist.h>

#include <hardware/glb_reg.h>
#include <hardware/hbn_reg.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_qcc74x, CONFIG_GPIO_LOG_LEVEL);

#define HBN_EN_AON_CTRL_GPIO_16 (20)
#define HBN_EN_AON_CTRL_GPIO_17 (21)

/* Define interrupts triggering modes */
#define SYNC_FALLING_EDGE_TRIG           (0U)
#define SYNC_RISING_EDGE_TRIG            (1U)
#define SYNC_LOW_LEVEL_TRIG              (2U)
#define SYNC_HIGH_LEVEL_TRIG             (3U)
#define SYNC_RISING_OR_FALLING_EDGE_TRIG (4U)

/* Offsets for DTS defined GPIO addresses */
#define GPIO0_OUTPUT_REG_OFFSET (544U)
#define GPIO1_OUTPUT_REG_OFFSET (420U)
#define GPIO0_INPUT_REG_OFFSET  (512U)
#define GPIO1_INPUT_REG_OFFSET  (388U)

/**
 * @brief Opal GPIO driver instance config
 */
struct gpio_qcc74x_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common; /**< Commonn config struct for the GPIO. */
	uint32_t base_reg;                /**< Register base address. */
	uint32_t id;                      /**< GPIO entity id. */
	uint32_t ngpios;                  /**< Number of GPIO pins for this device. */
	void (*irq_config_func)(const struct device *dev); /**< IRQ config func. */
	void (*irq_enable_func)(const struct device *dev); /**< IRQ enable func. */
};

/**
 * @brief Opal interrupt source node
 */
struct gpio_qcc74x_int_src {
	sys_snode_t node; /**< List node for tracked interrupt source pins. */
	gpio_pin_t pin;   /**< GPIO pin number. */
};

/**
 * @brief Opal GPIO driver instance data
 */
struct gpio_qcc74x_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common; /**< Commonn data struct for the GPIO. */
	sys_slist_t callbacks;          /**< Callback list for the GPIO. */
	sys_slist_t int_src_pin_list;   /**< Pins configured as interrupt sources. */
	struct gpio_qcc74x_int_src int_src_nodes[GPIO_MAX_PINS_PER_PORT];
};

/**
 * @brief System clock settle.
 */
static void system_clock_settle(void)
{
	__asm__ volatile(".rept 15 ; nop ; .endr");
}

/**
 * @brief Track a GPIO pin as an interrupt source.
 *
 * The pin node is first removed from the tracked list (if present) and then
 * appended, ensuring there is only one list entry for the pin.
 *
 * @param data	GPIO driver instance data.
 * @param pin	GPIO pin to track.
 */
static void gpio_qcc74x_int_src_track(struct gpio_qcc74x_data *data, gpio_pin_t pin)
{
	struct gpio_qcc74x_int_src *src = &data->int_src_nodes[pin];

	src->pin = pin;
	(void)sys_slist_find_and_remove(&data->int_src_pin_list, &src->node);
	sys_slist_append(&data->int_src_pin_list, &src->node);
}

/**
 * @brief Untrack a GPIO pin from interrupt sources.
 *
 * @param data	GPIO driver instance data.
 * @param pin	GPIO pin to untrack.
 */
static void gpio_qcc74x_int_src_untrack(struct gpio_qcc74x_data *data, gpio_pin_t pin)
{
	(void)sys_slist_find_and_remove(&data->int_src_pin_list, &data->int_src_nodes[pin].node);
}

/**
 * @brief Get GPIO output register address
 *
 * @param 	cfg 		GPIO driver instance config.
 * @return 	uint32_t 	GPIO output address for the GPIO entity.
 */
uint32_t get_gpio_output_addr(const struct gpio_qcc74x_config *cfg)
{
	uint32_t gpio_output_addr = 0U;
	if (cfg->id == 0) {
		gpio_output_addr = cfg->base_reg + GPIO0_OUTPUT_REG_OFFSET;
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio1))
	} else {
		gpio_output_addr = cfg->base_reg + GPIO1_OUTPUT_REG_OFFSET;
#endif
	}
	return gpio_output_addr;
}

static int gpio_qcc74x_port_get_raw(const struct device *dev, uint32_t *value)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;

	if (cfg->id == 0) {
		*value = sys_read32(cfg->base_reg + GPIO0_INPUT_REG_OFFSET);
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio1))
	} else {
		*value = sys_read32(cfg->base_reg + GPIO1_INPUT_REG_OFFSET);
#endif
	}
	LOG_DBG("Port got raw: 0x%08x", *value);
	return 0;
}

static int gpio_qcc74x_port_set_masked_raw(const struct device *dev, uint32_t mask, uint32_t value)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	uint32_t tmp_val = 0U;
	uint32_t gpio_output_reg = 0U;

	gpio_output_reg = get_gpio_output_addr(cfg);

	tmp_val = sys_read32(gpio_output_reg);
	tmp_val = (tmp_val & ~mask) | (mask & value);
	sys_write32(tmp_val, gpio_output_reg);
	LOG_DBG("Port set masked raw bits: 0x%08x", tmp_val);
	return 0;
}

static int gpio_qcc74x_port_set_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	uint32_t tmp_val = 0U;
	uint32_t gpio_output_reg = 0U;

	gpio_output_reg = get_gpio_output_addr(cfg);

	tmp_val = sys_read32(gpio_output_reg);
	tmp_val = tmp_val | mask;
	sys_write32(tmp_val, gpio_output_reg);
	LOG_DBG("Port set raw bits: 0x%08x", tmp_val);
	return 0;
}

static int gpio_qcc74x_port_clear_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	uint32_t tmp_val = 0U;
	uint32_t gpio_output_reg = 0U;

	gpio_output_reg = get_gpio_output_addr(cfg);

	tmp_val = sys_read32(gpio_output_reg);
	tmp_val = tmp_val & ~mask;
	sys_write32(tmp_val, gpio_output_reg);
	LOG_DBG("Port cleared raw bits: 0x%08x", tmp_val);
	return 0;
}

static int gpio_qcc74x_port_toggle_bits(const struct device *dev, uint32_t mask)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	uint32_t tmp_val = 0U;
	uint32_t gpio_output_reg = 0U;

	gpio_output_reg = get_gpio_output_addr(cfg);

	tmp_val = sys_read32(gpio_output_reg);
	tmp_val ^= mask;
	sys_write32(tmp_val, gpio_output_reg);
	LOG_DBG("Port toggled bits: 0x%08x", tmp_val);
	return 0;
}

/**
 * @brief Read back the configured direction of the requested pins.
 *
 * Each pin owns a per-pin config word at base_reg + (pin << 2). The IE
 * (input-enable) and OE (output-enable) bits written by gpio_qcc74x_config()
 * are simply read back here: a pin is reported as input when IE is set and as
 * output when OE is set (both can be set for input-output mode, neither for a
 * disconnected pin).
 *
 * @param dev		GPIO driver device struct.
 * @param map		Bitmap of pins whose direction is queried.
 * @param inputs	If non-NULL, filled with the pins configured as input.
 * @param outputs	If non-NULL, filled with the pins configured as output.
 * @return		0 on success.
 */
#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_qcc74x_port_get_direction(const struct device *dev, gpio_port_pins_t map,
					  gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	uint32_t gpio_pin_offset_addr = cfg->base_reg;
	gpio_port_pins_t ip = 0U;
	gpio_port_pins_t op = 0U;
	uint32_t reg = 0U;

	/* Restrict the query to pins that actually exist on this port. */
	map &= cfg->common.port_pin_mask;

	while (map != 0U) {
		gpio_pin_t pin = find_lsb_set(map) - 1;

		reg = sys_read32(gpio_pin_offset_addr + (pin << 2));
		if ((reg & GLB_REG_GPIO_0_IE_MSK) != 0U) {
			ip |= BIT(pin);
		}
		if ((reg & GLB_REG_GPIO_0_OE_MSK) != 0U) {
			op |= BIT(pin);
		}
		map &= ~BIT(pin);
	}

	if (inputs != NULL) {
		*inputs = ip;
	}
	if (outputs != NULL) {
		*outputs = op;
	}

	LOG_DBG("Port direction: inputs=0x%08x outputs=0x%08x", ip, op);
	return 0;
}
#endif /* CONFIG_GPIO_GET_DIRECTION */

/**
 * @brief Configure GPIO interrupt mode.
 *
 * @param	dev 	GPIO driver device struct.
 * @param	pin	Pin number to be configured.
 * @param 	mode 	Interrupt mode.
 * @param	trig	Interrrupt triggering mode.
 */
static void gpio_qcc74x_port_interrupt_configure_mode(const struct device *dev, const uint32_t pin,
						    const enum gpio_int_mode mode,
						    const enum gpio_int_trig trig)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	uint32_t tmp_val = 0U;
	/* default to 'sync' mode */
	uint8_t trig_mode = SYNC_FALLING_EDGE_TRIG;
	uint32_t gpio_pin_offset_addr = cfg->base_reg;
	uint32_t key = irq_lock();

	tmp_val = sys_read32(gpio_pin_offset_addr + (pin << 2));
	/* clear modes */
	tmp_val &= GLB_REG_GPIO_0_INT_MODE_SET_UMSK;

	if ((mode & GPIO_INT_EDGE) == 0) {
		/* level trigger*/
		if ((trig & GPIO_INT_HIGH_1) == 0) {
			trig_mode = SYNC_LOW_LEVEL_TRIG;
		} else {
			trig_mode = SYNC_HIGH_LEVEL_TRIG;
		}
	} else {
		/* edge trigger*/
		if (((trig & GPIO_INT_HIGH_1) != 0) &&
		    ((trig & GPIO_INT_LOW_0) != 0)) {
			trig_mode = SYNC_RISING_OR_FALLING_EDGE_TRIG;
		} else if ((trig & GPIO_INT_HIGH_1) != 0) {
			trig_mode = SYNC_RISING_EDGE_TRIG;
		}
	}

	tmp_val |= (trig_mode << GLB_REG_GPIO_0_INT_MODE_SET_POS);
	sys_write32(tmp_val, gpio_pin_offset_addr + (pin << 2));
	irq_unlock(key);
}

/**
 * @brief Clear GPIO interrupt(s).
 *
 * @param	dev 	GPIO driver device struct.
 * @param	mask	Pin mask to be cleared.
 */
static void gpio_qcc74x_pin_interrupt_clear(const struct device *dev, const uint32_t mask)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	struct gpio_qcc74x_data *data = dev->data;
	struct gpio_qcc74x_int_src *src;
	uint32_t tmp_val = 0U;
	uint32_t gpio_pin_offset_addr = cfg->base_reg;
	const uint32_t key = irq_lock();

	SYS_SLIST_FOR_EACH_CONTAINER(&data->int_src_pin_list, src, node) {
		if ((mask & BIT(src->pin)) == 0U) {
			continue;
		}

		tmp_val = sys_read32(gpio_pin_offset_addr + (src->pin << 2));
		tmp_val |= GLB_REG_GPIO_0_INT_CLR_MSK;
		sys_write32(tmp_val, gpio_pin_offset_addr + (src->pin << 2));
		system_clock_settle();
		tmp_val &= GLB_REG_GPIO_0_INT_CLR_UMSK;
		sys_write32(tmp_val, gpio_pin_offset_addr + (src->pin << 2));
		LOG_DBG("Interrupt cleared: pin=%u", src->pin);
	}
	irq_unlock(key);
}

static int gpio_qcc74x_pin_interrupt_configure(const struct device *dev, const gpio_pin_t pin,
					     const enum gpio_int_mode mode,
					     const enum gpio_int_trig trig)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	struct gpio_qcc74x_data *data = dev->data;
	uint32_t tmp_val = 0U;
	uint32_t gpio_pin_offset_addr = cfg->base_reg;

	const uint32_t key = irq_lock();

	/* Disable the interrupt. */
	tmp_val = sys_read32(gpio_pin_offset_addr + (pin << 2));
	tmp_val |= GLB_REG_GPIO_0_INT_MASK_MSK;
	sys_write32(tmp_val, gpio_pin_offset_addr + (pin << 2));

	gpio_qcc74x_port_interrupt_configure_mode(dev, pin, mode, trig);

	if (mode != GPIO_INT_MODE_DISABLED) {
		gpio_qcc74x_int_src_track(data, pin);
		/* clear */
		gpio_qcc74x_pin_interrupt_clear(dev, BIT(pin));
		/* unmask */
		tmp_val = sys_read32(gpio_pin_offset_addr + (pin << 2));
		tmp_val &= GLB_REG_GPIO_0_INT_MASK_UMSK;
		sys_write32(tmp_val, gpio_pin_offset_addr + (pin << 2));
		LOG_DBG("Pin interrupt enabled: pin=%u", pin);
		/* wait for hw to apply the changes*/
		k_busy_wait(500);
	} else {
		/* mask */
		tmp_val = sys_read32(gpio_pin_offset_addr + (pin << 2));
		tmp_val |= GLB_REG_GPIO_0_INT_MASK_MSK;
		sys_write32(tmp_val, gpio_pin_offset_addr + (pin << 2));
		gpio_qcc74x_pin_interrupt_clear(dev, BIT(pin));
		gpio_qcc74x_int_src_untrack(data, pin);
		LOG_DBG("Pin interrupt disabled: pin=%u", pin);
	}

	/* enable clic interrupt for us as it gets cleared in soc init*/
	cfg->irq_enable_func(dev);
	irq_unlock(key);
	return 0;
}

static int gpio_qcc74x_config(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_qcc74x_config *const conf = dev->config;
	uint32_t cfg = 0U;
	uint32_t tmp_val = 0U;
	uint32_t gpio_pin_offset_addr = conf->base_reg;
	uint32_t gpio_output_reg = 0U;

	if ((flags & GPIO_SINGLE_ENDED) != 0U) {
		return -ENOTSUP;
	}

	gpio_output_reg = get_gpio_output_addr(conf);
	uint32_t key = irq_lock();
	LOG_DBG("Configuring pin: %d, flags: 0x%04x", pin, flags);

	/* disable RC32K muxing */
	if (pin == 16) {
		*(volatile uint32_t *)(HBN_BASE + HBN_PAD_CTRL_0_OFFSET) &=
			~(BIT(HBN_EN_AON_CTRL_GPIO_16));
	} else if (pin == 17) {
		*(volatile uint32_t *)(HBN_BASE + HBN_PAD_CTRL_0_OFFSET) &=
			~(BIT(HBN_EN_AON_CTRL_GPIO_17));
	}

	cfg = sys_read32(gpio_pin_offset_addr + (pin << 2));

	/* Check first if both input and output are disabled */
	if (((flags & GPIO_INPUT) == 0) && ((flags & GPIO_OUTPUT) == 0)) {
		cfg &= GLB_REG_GPIO_0_IE_UMSK;
		cfg &= GLB_REG_GPIO_0_OE_UMSK;
	} else {
		if ((flags & GPIO_INPUT) != 0) {
			cfg |= GLB_REG_GPIO_0_IE_MSK;
			LOG_DBG("Pin %d set as INPUT", pin);
		} else {
			cfg &= GLB_REG_GPIO_0_IE_UMSK;
		}

		if ((flags & GPIO_OUTPUT) != 0) {
			cfg |= GLB_REG_GPIO_0_OE_MSK;
			LOG_DBG("Pin %d set as OUTPUT", pin);

			if (flags & GPIO_OUTPUT_INIT_HIGH) {
				tmp_val = sys_read32(gpio_output_reg);
				tmp_val |= BIT(pin);
				sys_write32(tmp_val, gpio_output_reg);
				LOG_DBG("Pin %d output initialized HIGH", pin);
			}
			if (flags & GPIO_OUTPUT_INIT_LOW) {
				tmp_val = sys_read32(gpio_output_reg);
				tmp_val &= ~(BIT(pin));
				sys_write32(tmp_val, gpio_output_reg);
				LOG_DBG("Pin %d output initialized LOW", pin);
			}
		} else {
			cfg &= GLB_REG_GPIO_0_OE_UMSK;
		}
	}

	if ((flags & GPIO_PULL_UP) != 0) {
		cfg &= GLB_REG_GPIO_0_PD_UMSK;
		cfg |= GLB_REG_GPIO_0_PU_MSK;
	} else if ((flags & GPIO_PULL_DOWN) != 0) {
		cfg |= GLB_REG_GPIO_0_PD_MSK;
		cfg &= GLB_REG_GPIO_0_PU_UMSK;
	} else {
		cfg &= GLB_REG_GPIO_0_PD_UMSK;
		cfg &= GLB_REG_GPIO_0_PU_UMSK;
	}

	/* SMT is enable for GPIO */
	cfg |= GLB_REG_GPIO_0_SMT_MSK;

	/* drive is 1 */
	cfg &= GLB_REG_GPIO_0_DRV_UMSK;
	cfg |= (1 << GLB_REG_GPIO_0_DRV_POS);
	/* function is 11 = gpio */
	cfg &= GLB_REG_GPIO_0_FUNC_SEL_UMSK;
	cfg |= (11 << GLB_REG_GPIO_0_FUNC_SEL_POS);

	/* output is controlled by value of _o*/
	cfg &= GLB_REG_GPIO_0_MODE_UMSK;

	sys_write32(cfg, gpio_pin_offset_addr + (pin << 2));
	LOG_DBG("Configuration for pin: %d with flags: 0x%04x complete. REGVAL=0x%08x", pin, flags,
		cfg);
	irq_unlock(key);
	return 0;
}

/**
 * @brief GPIO IRQ configuration init.
 *
 * @param	dev	GPIO driver device struct.
 * @return 	int	Return 0 for successful execution.
 */
int gpio_qcc74x_init(const struct device *dev)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	struct gpio_qcc74x_data *data = dev->data;

	sys_slist_init(&data->int_src_pin_list);

	cfg->irq_config_func(dev);
	LOG_DBG("IRQ configuration function called for device: %s", dev->name);

	return 0;
}

/**
 * @brief	GPIO isr.
 *
 * @param	dev	GPIO driver device struct.
 */
static void gpio_qcc74x_isr(const struct device *dev)
{
	const struct gpio_qcc74x_config *const cfg = dev->config;
	struct gpio_qcc74x_data *data = dev->data;
	struct gpio_qcc74x_int_src *src;
	uint32_t int_stat = 0U;
	uint32_t tmp_val = 0U;
	uint32_t gpio_pin_offset_addr = cfg->base_reg;

	SYS_SLIST_FOR_EACH_CONTAINER(&data->int_src_pin_list, src, node) {
		tmp_val = sys_read32(gpio_pin_offset_addr + (src->pin << 2));
		if ((tmp_val & GLB_GPIO_0_INT_STAT_MSK) != 0U) {
			int_stat |= BIT(src->pin);
		}
	}

	/* clear interrupts */
	gpio_qcc74x_pin_interrupt_clear(dev, int_stat);
	gpio_fire_callbacks(&data->callbacks, dev, int_stat);
	/* wait for hw to apply the changes*/
	k_busy_wait(200);
}

static int gpio_qcc74x_manage_callback(const struct device *port, struct gpio_callback *callback,
				     bool set)
{
	struct gpio_qcc74x_data *data = port->data;
	LOG_DBG("Manage callback: %s callback at %p", set ? "Adding" : "Removing", callback);
	return gpio_manage_callback(&(data->callbacks), callback, set);
}

static const struct gpio_driver_api gpio_qcc74x_api = {
	.pin_configure = gpio_qcc74x_config,
	.port_get_raw = gpio_qcc74x_port_get_raw,
	.port_set_masked_raw = gpio_qcc74x_port_set_masked_raw,
	.port_set_bits_raw = gpio_qcc74x_port_set_bits_raw,
	.port_clear_bits_raw = gpio_qcc74x_port_clear_bits_raw,
	.port_toggle_bits = gpio_qcc74x_port_toggle_bits,
	.pin_interrupt_configure = gpio_qcc74x_pin_interrupt_configure,
	.manage_callback = gpio_qcc74x_manage_callback,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = gpio_qcc74x_port_get_direction,
#endif
};

#define GPIO_QCC74X_DEVICE(n)                                                                        \
	static void port_##n##_qcc74x_irq_config_func(const struct device *dev);                     \
	static void port_##n##_qcc74x_irq_enable_func(const struct device *dev);                     \
                                                                                                   \
	static const struct gpio_qcc74x_config port_##n##_qcc74x_config = {                            \
		.common =                                                                          \
			{                                                                          \
				.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n),               \
			},                                                                         \
		.base_reg = DT_INST_REG_ADDR(n),                                                   \
		.id = n,                                                                           \
		.ngpios = DT_INST_PROP_OR(n, ngpios, 32),                                          \
		.irq_config_func = port_##n##_qcc74x_irq_config_func,                                \
		.irq_enable_func = port_##n##_qcc74x_irq_enable_func,                                \
	};                                                                                         \
                                                                                                   \
	static struct gpio_qcc74x_data port_##n##_qcc74x_data;                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_qcc74x_init, NULL, &port_##n##_qcc74x_data,                      \
			      &port_##n##_qcc74x_config, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,    \
			      &gpio_qcc74x_api);                                                     \
                                                                                                   \
	static void port_##n##_qcc74x_irq_config_func(const struct device *dev)                      \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), gpio_qcc74x_isr,              \
			    DEVICE_DT_INST_GET(n), 0);                                             \
	}                                                                                          \
	static void port_##n##_qcc74x_irq_enable_func(const struct device *dev)                      \
	{                                                                                          \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

DT_INST_FOREACH_STATUS_OKAY(GPIO_QCC74X_DEVICE)
