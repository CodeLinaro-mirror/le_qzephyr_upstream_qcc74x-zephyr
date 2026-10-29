/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#include <zephyr/sys/sys_io.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <hardware/qcc74x.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <zephyr/dt-bindings/clock/qcc75x-clock.h>
#include <hardware/qcc75x.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCR3XX
#include <zephyr/dt-bindings/clock/qcr373-clock.h>
#include <hardware/qcr373.h>
#endif

#include <zephyr/drivers/clock_control/qcc7xx_clk_timer_ll.h>
#include <qc7xx_timer.h>
#include <hardware/timer_reg.h>
#include <qc7xx_clock.h>

uint8_t qcc7xx_clk_timer_ll_get_clk_source(uint32_t peripheral_id)
{
	uint32_t tccr_reg_addr = (TIMER_BASE + TIMER_TCCR_OFFSET);
	uint32_t tccr_reg_val = sys_read32(tccr_reg_addr);
	uint32_t mask = 0U;
	uint32_t shift = 0U;
	uint32_t clk_src_val = 0U;

	switch (peripheral_id) {
	case QC7XX_PERIPHERAL_TIMER0:
		mask = TIMER_CS_0_MASK;
		shift = TIMER_CS_0_SHIFT;
		break;
	case QC7XX_PERIPHERAL_TIMER1:
		mask = TIMER_CS_1_MASK;
		shift = TIMER_CS_1_SHIFT;
		break;
	case QC7XX_PERIPHERAL_WATCHDOG:
		mask = TIMER_CS_WDT_MASK;
		shift = TIMER_CS_WDT_SHIFT;
		break;

	default:
		return 0;
	}

	clk_src_val = (tccr_reg_val & mask) >> shift;

	return clk_src_val;
}

uint32_t qcc7xx_clk_timer_ll_get_rate(uint32_t peripheral_id)
{
	uint32_t src;
	uint32_t clk_src_val = 0U;

	clk_src_val = qcc7xx_clk_timer_ll_get_clk_source(peripheral_id);

	switch (clk_src_val) {
	case TIMER_CLKSRC_BCLK:
		src = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_PBCLK);
		break;
	case TIMER_CLKSRC_32K:
		src = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_32K_CLK);
		break;
	case TIMER_CLKSRC_1K:
		src = KHZ(1);
		break;
	case TIMER_CLKSRC_XTAL:
		src = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_XCLK);
		break;
	case TIMER_CLKSRC_GPIO:
		src = 0;
		break;
	case TIMER_CLKSRC_NO:
		src = 0;
		break;
	default:
		src = 0;
		break;
	}

	return src;
}

void qcc7xx_clk_timer_ll_set_source(uint32_t peripheral_id, uint8_t source)
{
	uint32_t tccr_reg_addr = TIMER_BASE + TIMER_TCCR_OFFSET;
	uint32_t tccr_reg_val = sys_read32(tccr_reg_addr);
	uint32_t tccr_reg_new_val = tccr_reg_val;

	switch (peripheral_id) {
	case QC7XX_PERIPHERAL_TIMER0:
		tccr_reg_new_val &= ~TIMER_CS_0_MASK;
		tccr_reg_new_val |= (TIMER_CS_0_MASK & (source << TIMER_CS_0_SHIFT));
		break;
	case QC7XX_PERIPHERAL_TIMER1:
		tccr_reg_new_val &= ~TIMER_CS_1_MASK;
		tccr_reg_new_val |= (TIMER_CS_1_MASK & (source << TIMER_CS_1_SHIFT));
		break;
	case QC7XX_PERIPHERAL_WATCHDOG:
		tccr_reg_new_val &= ~TIMER_CS_WDT_MASK;
		tccr_reg_new_val |= (TIMER_CS_WDT_MASK & (source << TIMER_CS_WDT_SHIFT));
		break;
	}

	sys_write32(tccr_reg_new_val, TIMER_BASE + TIMER_TCCR_OFFSET);
}
