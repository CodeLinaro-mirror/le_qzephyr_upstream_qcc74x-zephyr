/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_TIMER_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_TIMER_LL_H_

/**
 * @brief Get Timer/Watchdog clock source
 *
 * @param peripheral_id  Timer ID according to the list in file:
 *                       include/zephyr/dt-bindings/clock/qcc74x-clock.h
 *
 * @return Clock source. Possible values:
 *			QC7XX_TIMER_CLK_BCLK
 *			QC7XX_TIMER_CLK_32K
 *			QC7XX_TIMER_CLK_1K
 *			QC7XX_TIMER_CLK_XTAL
 *			QC7XX_TIMER_CLK_GPIO
 *			QC7XX_TIMER_CLK_NO
 */
uint8_t qcc7xx_clk_timer_ll_get_clk_source(uint32_t peripheral_id);

/**
 * @brief Get Timer clock source rate in Hz
 *
 * @param peripheral_id  Timer ID according to the list in file:
 *                       include/zephyr/dt-bindings/clock/qcc74x-clock.h
 *
 * @return int clock rate in Hz or 0 in case of error
 */
uint32_t qcc7xx_clk_timer_ll_get_rate(uint32_t peripheral_id);

/**
 * @brief Set Timer clock source
 *
 * @param peripheral_id  Timer ID according to the list in file:
 *                       include/zephyr/dt-bindings/clock/qcc74x-clock.h
 * @param source  Source ID. One of:
 *                  QC7XX_TIMER_CLK_BCLK
 *                  QC7XX_TIMER_CLK_32K
 *                  QC7XX_TIMER_CLK_1K
 *                  QC7XX_TIMER_CLK_XTAL
 *                  QC7XX_TIMER_CLK_GPIO
 *                  QC7XX_TIMER_CLK_NO
 */
void qcc7xx_clk_timer_ll_set_source(uint32_t peripheral_id, uint8_t source);

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_TIMER_LL_H_ */
