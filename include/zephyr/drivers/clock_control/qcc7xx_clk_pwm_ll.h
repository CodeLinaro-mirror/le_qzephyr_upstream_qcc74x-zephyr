/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_PWM_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_PWM_LL_H_

#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>

/* Macro for determining if clock source is available for PWM peripheral */
#define QC7XX_IS_PWM_CLK_SOURCE(type)                                                              \
	(((type) == QC7XX_PWM_CLK_XCLK) || ((type) == QC7XX_PWM_CLK_BCLK) ||                       \
	 ((type) == QC7XX_PWM_CLK_32K_CLK))

/**
 * @brief Set the clock source and divider
 *
 * @param enable  Has to be true since PWM doesn't support clock disabling.
 * @param clk_sel Clock source identifier.
 * @param div     Expected divider value.
 */
void qcc7xx_clk_pwm_ll_set(uint8_t enable, uint8_t clk_sel, uint16_t div);

/**
 * @brief Get the clock rate of PWM (without divider)
 *
 * @return int clock rate in Hz
 */
uint32_t qcc7xx_clk_pwm_ll_get_rate();

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_PWM_LL_H_ */
