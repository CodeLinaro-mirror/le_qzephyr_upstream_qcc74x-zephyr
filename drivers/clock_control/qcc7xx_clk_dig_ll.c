/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#include <zephyr/sys/sys_io.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>

#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <qcc74x_clock.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <zephyr/dt-bindings/clock/qcc75x-clock.h>
#include <qcc75x_clock.h>
#endif

uint32_t qcc7xx_clk_dig_ll_get_rate()
{
	uint32_t xtal_val = 0U;
	uint32_t tmp_val = 0U;
	uint8_t sel = 0U;

	tmp_val = QC7XX_RD_REG(GLB_BASE, GLB_DIG_CLK_CFG0);
	sel = QC7XX_GET_REG_BITS_VAL(tmp_val, GLB_DIG_CLK_SRC_SEL);

	switch (sel) {
	case QC7XX_DIG_CLK_WIFIPLL_32M:
		return (32 * 1000 * 1000);
	case QC7XX_DIG_CLK_XCLK:
		HBN_Get_Xtal_Value(&xtal_val);
		return xtal_val;
	case QC7XX_DIG_CLK_AUPLL:
		ATTR_FALLTHROUGH();
	default:
		return qcc7xx_clk_common_Clock_Get_Audio_PLL_Output();
	}
}
