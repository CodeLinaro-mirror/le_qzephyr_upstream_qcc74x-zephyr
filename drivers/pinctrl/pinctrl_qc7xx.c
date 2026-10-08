/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/pinctrl/qc7xx-pinctrl-common.h>
#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcc74x-pinctrl.h>
#include "qcc74x_glb.h"
#elif defined(CONFIG_SOC_SERIES_QCC75X)
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcc75x-pinctrl.h>
#include "qcc75x_glb.h"
#elif defined(CONFIG_SOC_SERIES_QCR373)
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcr373-pinctrl.h>
#include "qcr373_glb.h"
#endif
#include <qc7xx_gpio.h>
#include <hardware/glb_reg.h>

static int qc7xx_pin_configure(const uint32_t pin_mux, const uint32_t pin_cfg)
{
	uint32_t pin_num = QC7XX_PIN_GET_NUM(pin_mux);
	uint32_t func = QC7XX_PIN_GET_FUNC(pin_mux);
	uint32_t func_cfg = QC7XX_PIN_GET_FUNC_CFG(pin_mux);
	uint32_t uart_sig = QC7XX_PIN_GET_UART_SIG(pin_mux);
	uint32_t ir_flag = QC7XX_PIN_GET_IR_FLAG(pin_mux);
	struct qc7xx_device_s *gpio;

	if (pin_num >= GPIO_PIN_MAX) {
		return -EINVAL;
	}

    gpio = qc7xx_device_get_by_name("gpio");
	if(func == GPIO_FUNC_UART ){
		qc7xx_gpio_uart_init(gpio, pin_num, uart_sig);
	}else{
		qc7xx_gpio_init(gpio, pin_num, func_cfg);
		if(func == GPIO_FUNC_CAM ){
			if(pin_num<24){
				/* TODO: select CAM0 and CAM1 */
			}
		}else if(func == GPIO_FUNC_GPIO){
			if(ir_flag){
				GLB_IR_RX_GPIO_Sel(pin_num);
			}
		}else if(func == GPIO_FUN_PWM0){
			/* PWM channel select: bit 14 of cfgset encodes SEL0/SEL1.
			 * Only set the SEL bit when SEL1 is requested; never clear it,
			 * so other fixed-channel pins (SEL0) don't override GPIO12's SEL1.
			 */
			uint32_t pwm_sel = (func_cfg >> 14) & 0x1;
			if (pwm_sel) {
				uint32_t reg = getreg32(GLB_BASE + GLB_PWM_CFG0_OFFSET);
				reg |= GLB_REG_PWM1_IO_SEL_MSK;
				putreg32(reg, GLB_BASE + GLB_PWM_CFG0_OFFSET);
			}
		}
	}
	return 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt,
			   uintptr_t reg)
{
	uint32_t pin_mux, pin_cfg;
	int ret = 0;

	ARG_UNUSED(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		pin_mux = pins[i].pinmux;
		pin_cfg = pins[i].pincfg;

		ret = qc7xx_pin_configure(pin_mux, pin_cfg);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}