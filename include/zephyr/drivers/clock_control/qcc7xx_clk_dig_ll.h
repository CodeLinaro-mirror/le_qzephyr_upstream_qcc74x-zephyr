/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* TODO: This file should be moved to HAL in the future */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_DIG_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_DIG_LL_H_

#define QC7XX_DIG_32K_MAX_CLK_DIV  0x7FF
#define QC7XX_DIG_512K_MAX_CLK_DIV 0x7F

/**
 * @brief Get the DIG source clock rate (without divider)
 *
 * Implementation inspired by Clock_F32k_Mux_Output(uint8_t sel)
 *  from hal_qcom/qcc7xx/drivers/soc/qcc74x/std/src/qcc74x_clock.c
 *
 * @return clock rate in Hz
 */
uint32_t qcc7xx_clk_dig_ll_get_rate(void);

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_DIG_LL_H_ */
