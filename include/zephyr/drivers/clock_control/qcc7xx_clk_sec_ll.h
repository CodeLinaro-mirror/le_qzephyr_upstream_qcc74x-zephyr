/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* TODO: This file should be moved to HAL in the future */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_SEC_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_SEC_LL_H_

/* Macro for determining if clock source is available for SEC peripheral */
#ifdef CONFIG_SOC_SERIES_QCC74X
#define QC7XX_IS_SEC_CLK_SOURCE(type)                                                              \
	(((type) == QC7XX_PKA_CLK_MCU_BCLK) || ((type) == QC7XX_PKA_CLK_MCU_MUXPLL_160M))
#endif
#if defined(CONFIG_SOC_SERIES_QCC75X) || defined(CONFIG_SOC_SERIES_QCR373)
#define QC7XX_IS_SEC_CLK_SOURCE(type)                                                              \
	(((type) == QC7XX_PKA_CLK_MCU_BCLK) || ((type) == QC7XX_PKA_CLK_WIFIPLL_160M))
#endif

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_SDH_LL_H_ */
