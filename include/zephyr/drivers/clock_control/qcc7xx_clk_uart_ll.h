/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* TODO: This file should be moved to HAL in the future */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_UART_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_UART_LL_H_

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>

#define QC7XX_UART_MAX_CLK_DIV 0x7
#define QC7XX_UART_MAX_CLK_PRD 0xFFFF

/**
 * @brief Set UART period values
 *
 * This function sets both period values of uart_bit_prd register to the same
 * value provided as a parameter. This makes user able to control UART
 * baudrate.
 *
 * @param period_value  Period value to be set (Unit: UART source clock period)
 *                      Important: this value is incremented in hardware logic
 *                       with 1. So period_value = 0 will not divide the
 *                       source clock at all (effective value 1).
 * @param base_reg      Base address of UART registers.
 */
void qcc7xx_clk_uart_ll_set_period(uint16_t period_value, const uint32_t base_reg);

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_UART_LL_H_ */
