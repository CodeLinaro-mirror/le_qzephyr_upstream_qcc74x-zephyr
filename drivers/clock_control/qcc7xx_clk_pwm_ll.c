/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/sys/sys_io.h>
#include <zephyr/devicetree.h>

#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <hardware/qcc74x.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <zephyr/dt-bindings/clock/qcc75x-clock.h>
#include <hardware/qcc75x.h>
#endif

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(qcc7xx_clk_pwm_ll, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#ifdef CONFIG_SOC_SERIES_QCR3XX
#include <zephyr/dt-bindings/clock/qcr373-clock.h>
#include <hardware/qcr373.h>
#endif

#include <zephyr/drivers/clock_control/qcc7xx_clk_pwm_ll.h>
#include <hardware/pwm_v2_reg.h>
#include <qc7xx_clock.h>

void qcc7xx_clk_pwm_ll_set(uint8_t enable, uint8_t new_clk_src, uint16_t div)
{
	uint32_t config0_reg_addr = (PWM_BASE + PWM_MC0_CONFIG0_OFFSET);
	uint32_t config0_reg_val = sys_read32(config0_reg_addr);
	uint32_t new_config0_reg_val;

	if (!enable) {
		LOG_ERR("Error: PWM doesn't support clock disabling!");
		return;
	}

	LOG_INF("Set PWM clk source: %u, and div: %u", new_clk_src, div);

	config0_reg_val &= ~(PWM_REG_CLK_SEL_MASK | PWM_CLK_DIV_MASK);
	new_config0_reg_val = config0_reg_val | (new_clk_src << PWM_REG_CLK_SEL_SHIFT) |
			      (div << PWM_CLK_DIV_SHIFT);
	sys_write32(new_config0_reg_val, config0_reg_addr);
}

uint32_t qcc7xx_clk_pwm_ll_get_rate()
{
	uint32_t config0_reg_addr = (PWM_BASE + PWM_MC0_CONFIG0_OFFSET);
	uint32_t config0_reg_val = sys_read32(config0_reg_addr);
	uint8_t clk_src_val = (config0_reg_val & PWM_REG_CLK_SEL_MASK) >> PWM_REG_CLK_SEL_SHIFT;
	uint32_t src;

	switch (clk_src_val) {
	case QC7XX_PWM_CLK_XCLK:
		src = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_XCLK);
		break;
	case QC7XX_PWM_CLK_BCLK:
		src = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_PBCLK);
		break;
	default:
		src = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_32K_CLK);
		break;
	}

	LOG_INF("get_rate: src:%u", src);

	return src;
}
