/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_COMMON_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_COMMON_LL_H_

#include <stdint.h>

#ifdef CONFIG_SOC_SERIES_QCC74X
#include <qcc74x_clock.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <qcc75x_clock.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCR3XX
#include <qcr373_clock.h>
#endif

#define CLOCK_DISABLE (0U)
#define CLOCK_ENABLE  (1U)

/* Generic structure defining clock properties needed for configuration. */
struct qcc74x_clk_settings {
	uint8_t enable;
	uint8_t clk_src;
	uint16_t clk_div;
	uint16_t clk_src2;
};

/**
 * @brief Get the Audio PLL clock output rate
 *
 * Inspired by Clock_Get_Audio_PLL_Output() implementation
 *  from hal_qcom/qcc7xx/drivers/soc/qcc74x/std/src/qcc74x_clock.c
 *
 * @return clock rate in Hz
 */
uint32_t qcc7xx_clk_common_Clock_Get_Audio_PLL_Output(void);

/**
 * @brief Get mtimer source clock frequency in Hz for selected source
 *
 * @param mtimer_src Mtimer source selector (QC7XX_MTIMER_CLK_*)
 * @return source frequency in Hz or 0 if unsupported
 */
uint32_t qcc7xx_clk_common_Clock_Get_mtimer_src_freq(uint8_t mtimer_src);

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_COMMON_LL_H_ */
