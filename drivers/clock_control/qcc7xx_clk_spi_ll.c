/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#include <zephyr/kernel.h>
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

#include <zephyr/drivers/clock_control/qcc7xx_clk_spi_ll.h>
#include <qc7xx_clock.h>
#include <hardware/spi_reg.h>

void qcc7xx_clk_spi_ll_set_period(uint8_t period_value, const uint32_t base_reg)
{
	uint32_t reg_val = 0U;

	reg_val |= (period_value << SPI_CR_SPI_PRD_D_PH_0_SHIFT);
	reg_val |= (period_value << SPI_CR_SPI_PRD_D_PH_1_SHIFT);
	reg_val |= (period_value << SPI_CR_SPI_PRD_S_SHIFT);
	reg_val |= (period_value << SPI_CR_SPI_PRD_P_SHIFT);
	sys_write32(reg_val, base_reg + SPI_PRD_0_OFFSET);

	reg_val = sys_read32(base_reg + SPI_PRD_1_OFFSET);
	reg_val &= ~SPI_CR_SPI_PRD_I_MASK;
	reg_val |= (period_value << SPI_CR_SPI_PRD_I_SHIFT);
	sys_write32(reg_val, base_reg + SPI_PRD_1_OFFSET);
}
