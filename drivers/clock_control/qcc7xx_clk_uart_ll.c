/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_uart_ll.h>
#include <hardware/uart_reg.h>

void qcc7xx_clk_uart_ll_set_period(uint16_t period_value, const uint32_t base_reg)
{
	uint32_t period_reg = 0U;

	period_reg = (period_value << UART_CR_URX_BIT_PRD_SHIFT) |
		     (period_value & QC7XX_UART_MAX_CLK_PRD);
	sys_write32(period_reg, base_reg + UART_BIT_PRD_OFFSET);
}
