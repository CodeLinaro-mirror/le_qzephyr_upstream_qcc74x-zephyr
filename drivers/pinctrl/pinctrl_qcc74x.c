/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/pinctrl/qc7xx-pinctrl-common.h>
#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcc74x-pinctrl.h>
#elif CONFIG_SOC_SERIES_QCC75X
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcc75x-pinctrl.h>
#endif

#include <hardware/glb_reg.h>
#include <hardware/hbn_reg.h>

#define LOG_LEVEL CONFIG_PINCTRL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pinctrl_qcc74x);

/** Peripheral function select max value */
#define SIG_X_SEL_MAX (12U)

/** Peripheral function select fields per one configuration register */
#define SIG_X_SEL_PER_WORD ((NUM_BITS(uint32_t)) / (GLB_UART_SIG_0_SEL_LEN))

/** Get gpio config register address from pin number */
#define GPIO_CFG_ADDR_FROM_PIN(pin, base) ((mem_addr_t)(base) + GLB_GPIO_CFG0_OFFSET + (pin << 2))

/**
 * @brief Helper function to select pin used as Infrared.
 *
 * @param pinnum        Pin number
 * @param glb_reg       Pointer to global register.
 */
static void pinctrl_qcc74x_select_ir_gpio(uint8_t pinnum, glb_reg_t *glb_reg)
{
	__ASSERT((glb_reg != NULL), "Passed null glb_reg!");
	if (pinnum <= GPIO_8 || pinnum >= GPIO_24) {
		LOG_ERR("Wrong pin for IR selected (%u). Only pins between %u and %u can be used.",
			pinnum, GPIO_9, GPIO_23);
		return;
	}

	glb_reg->ir_cfg1.BF.ir_rx_gpio_sel = pinnum - GPIO_8;
}

static void pinctrl_qcc74x_configure_pin(pinctrl_soc_pin_t pin)
{
	glb_reg_t *glb_reg = (volatile glb_reg_t *)(GLB_BASE);
	mm_reg_t cfg_reg = GPIO_CFG_ADDR_FROM_PIN(pin.pinnum, GLB_BASE);
	uint32_t new_cfg_val = 0U;

	/* disable RC32K muxing to be xtal32k */
	if (pin.pinnum == GPIO_16 || pin.pinnum == GPIO_17) {
		int regval = sys_read32(HBN_BASE + HBN_PAD_CTRL_0_OFFSET);
		if (pin.pinnum == GPIO_16) {
			regval &= ~(BIT(HBN_REG_EN_AON_CTRL_GPIO_POS));
		} else {
			regval &= ~(BIT(HBN_REG_EN_AON_CTRL_GPIO_POS + 1));
		}
		sys_write32(regval, HBN_BASE + HBN_PAD_CTRL_0_OFFSET);
	}

	/* Configure pin */
	new_cfg_val |= pin.pinmux;

	/* Set config values not defined in pinmux */
	/* mask interrupt */
	new_cfg_val |= GLB_REG_GPIO_0_INT_MASK_MSK;
	/* output is controlled by _SET and _CLR and not value of _O*/
	new_cfg_val |= (0x1 << GLB_REG_GPIO_0_MODE_POS);

	sys_write32(new_cfg_val, cfg_reg);

	/* Set glb register values which are function specific */
	if (GET_PIN_FUN(pin.pinmux) == GPIO_FUN_UART) {
		/* UART function select */
		uint32_t signum = pin.pinnum % SIG_X_SEL_MAX;
		uint32_t sigpos = 0;
		if (signum < SIG_X_SEL_PER_WORD) {
			sigpos = signum * GLB_UART_SIG_0_SEL_LEN;
			glb_reg->uart_cfg1.WORD &= (~(0xf << sigpos));
			glb_reg->uart_cfg1.WORD |= (pin.pinsel << sigpos);
		} else { /* signum >= SIG_X_SEL_PER_WORD */
			sigpos = (signum - SIG_X_SEL_PER_WORD) * GLB_UART_SIG_0_SEL_LEN;
			glb_reg->uart_cfg2.WORD &= (~(0xf << sigpos));
			glb_reg->uart_cfg2.WORD |= (pin.pinsel << sigpos);
		}
	} else if (GET_PIN_FUN(pin.pinmux) == GPIO_FUN_PWM0) {
		/* PWM function select */
		glb_reg->pwm_cfg0.WORD |= pin.pinsel;
	} else if (pin.ir_flag) {
		pinctrl_qcc74x_select_ir_gpio(pin.pinnum, glb_reg);
	}

	LOG_DBG("Configured pin %u, func: %u internal func: %u, pinmux: %u", pin.pinnum, pin.pinsel,
		GET_PIN_FUN(pin.pinmux), pin.pinmux);
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		pinctrl_qcc74x_configure_pin(*pins++);
	}

	return 0;
}
