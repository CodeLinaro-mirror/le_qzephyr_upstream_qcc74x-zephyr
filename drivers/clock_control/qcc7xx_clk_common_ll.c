/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* TODO: This file should be moved to HAL in the future */

#include <zephyr/sys/sys_io.h>

#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <qcc74x_clock.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <zephyr/dt-bindings/clock/qcc75x-clock.h>
#include <qcc75x_clock.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCR373
#include <zephyr/dt-bindings/clock/qcr373-clock.h>
#include <qcr373_clock.h>
#endif

#ifdef CONFIG_SOC_SERIES_QCC74X

uint32_t qcc7xx_clk_common_Clock_Get_AUPLL_Output(CLOCK_AUPLL_Type clockAupllType)
{
	uint8_t xtalType = 0;
	uint32_t tmpVal = 0;
	uint32_t vcoFreq = 0;
	uint32_t calculationDiv = (1 << 11);

	tmpVal =
		QC7XX_GET_REG_BITS_VAL(QC7XX_RD_REG(CCI_BASE, CCI_AUDIO_PLL_CFG6), CCI_AUPLL_SDMIN);

	if (HBN_Get_Xtal_Type(&xtalType) == SUCCESS) {
		switch (xtalType) {
		/* Calculation method:xtal_freq * 1000 * aupll_sdmin / calculationDiv * 1000 /
		 * cpupll_refdiv_ratio */
		case GLB_XTAL_NONE:
			return (0);
		case GLB_XTAL_24M:
			/* 491519500;451576000;399996000;3840000000 */
			vcoFreq = 24 * 1000 * tmpVal / calculationDiv * (1000 / 2);
			break;
		case GLB_XTAL_32M:
			/* 491519500;451578000;400000000;3840000000 */
			vcoFreq = 32 * 1000 * tmpVal / calculationDiv * (1000 / 4);
			break;
		case GLB_XTAL_38P4M:
			/* 491517000;451579500;399998250;3840000000 */
			vcoFreq = 384 * 100 * tmpVal / calculationDiv * (1000 / 4);
			break;
		case GLB_XTAL_40M:
			/* 491518500;451577000;400000000;383999000 */
			vcoFreq = 40 * 1000 * tmpVal / calculationDiv * (1000 / 4);
			break;
		case GLB_XTAL_26M:
			/* 491518000;451578500;399997500;383995000 */
			vcoFreq = 26 * 1000 * tmpVal / calculationDiv * (1000 / 2);
			break;
		case GLB_XTAL_RC32M:
			/* 491519500;451578000;400000000;3840000000 */
			vcoFreq = 32 * 1000 * tmpVal / calculationDiv * (1000 / 4);
			break;
		default:
			return (0);
		}
	} else {
		return (0);
	}

	if ((vcoFreq >= 491000000) && (vcoFreq <= 492000000)) {
		/* vcoFreq:491517000~491519500 */
		vcoFreq = 491520000;
	} else if ((vcoFreq >= 451000000) && (vcoFreq <= 452000000)) {
		/* vcoFreq:451577000~451579500 */
		vcoFreq = 451580000;
	} else if ((vcoFreq >= 399000000) && (vcoFreq <= 401000000)) {
		/* vcoFreq:451577000~451579500 */
		vcoFreq = 400000000;
	} else if ((vcoFreq >= 383000000) && (vcoFreq <= 385000000)) {
		/* vcoFreq:383995000~3840000000 */
		vcoFreq = 384000000;
	} else {
		return (0);
	}

	switch (clockAupllType) {
	case CLOCK_AUPLL_DIV1:
		return (vcoFreq);
	case CLOCK_AUPLL_DIV2:
		return (vcoFreq / 2);
	case CLOCK_AUPLL_DIV2P5:
		return (vcoFreq * 2 / 5);
	case CLOCK_AUPLL_DIV3:
		return (vcoFreq / 3);
	case CLOCK_AUPLL_DIV4:
		return (vcoFreq / 4);
	case CLOCK_AUPLL_DIV5:
		return (vcoFreq / 5);
	case CLOCK_AUPLL_DIV6:
		return (vcoFreq / 6);
	case CLOCK_AUPLL_DIV10:
		return (vcoFreq / 10);
	case CLOCK_AUPLL_DIV15:
		return (vcoFreq / 15);
	default:
		return 0;
	}
}
#endif

uint32_t qcc7xx_clk_common_Clock_Get_Audio_PLL_Output()
{
#ifdef CONFIG_SOC_SERIES_QCC74X
	uint32_t tmpVal = 0;

	tmpVal = QC7XX_GET_REG_BITS_VAL(QC7XX_RD_REG(CCI_BASE, CCI_AUDIO_PLL_CFG1),
					CCI_AUPLL_POSTDIV);

	return qcc7xx_clk_common_Clock_Get_AUPLL_Output(CLOCK_AUPLL_DIV1) / tmpVal;
#endif
#if defined(CONFIG_SOC_SERIES_QCC75X) || defined(CONFIG_SOC_SERIES_QCR373)
	return 0;
#endif
}

uint32_t qcc7xx_clk_common_Clock_Get_mtimer_src_freq(uint8_t mtimer_src)
{
	switch (mtimer_src) {
	case QC7XX_MTIMER_CLK_MCU_XCLK:
		return Clock_System_Clock_Get(QC7XX_SYSTEM_CLOCK_XCLK);
	case QC7XX_MTIMER_CLK_MCU_ROOT_CLK:
		return Clock_System_Clock_Get(QC7XX_SYSTEM_CLOCK_MCU_CLK);
	default:
		return 0U;
	}
}
