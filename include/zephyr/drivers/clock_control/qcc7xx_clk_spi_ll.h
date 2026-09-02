/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_SPI_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_SPI_LL_H_

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>

#define QC7XX_IS_SPI_CLK_SOURCE(type)                                                              \
	(((type) == QC7XX_SPI_CLK_MCU_MUXPLL_160M) || ((type) == QC7XX_SPI_SPI_CLK_XCLK))

#define QC7XX_SPI_MAX_CLK_DIV 0x1FU
#define QC7XX_SPI_PRD_MAX_DIV 0xFFU

/**
 * @brief Set period registers values to specified one
 *
 * This function sets all period values of spi_prd_0 and spi_prd_1 registers
 * to the value provided as a parameter. This makes us able to control
 * SPI clock frequency.
 *
 * @param period_value  Period value to be set (Unit: SPI source clock period)
 *                      Important: this value is incremented in hardware logic
 *                       with 1. So period_value = 0 will not divide the
 *                       source clock at all (effective value 1).
 * @param base_reg      Base address of SPI registers.
 */
void qcc7xx_clk_spi_ll_set_period(uint8_t period_value, const uint32_t base_reg);

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_SPI_LL_H_ */
