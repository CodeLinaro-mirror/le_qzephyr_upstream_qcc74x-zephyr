/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qcc75x_clock.h"
#include "qcc75x_pds.h"
#include "qcc75x_hbn.h"
#include "qcc75x_psram.h"
#include "tzc_sec_reg.h"
#include "rv_hart.h"
#include "rv_pmp.h"
#define uint32_t unsigned int

#ifndef CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU1
/* sf_ctrl_2 */
#define SF_CTRL_2_OFFSET       (0x70)
#define SF_CTRL_SF_IF_BK2_MODE (1 << 29U)
#define SF_CTRL_SF_IF_BK2_EN   (1 << 30U)

static void Tzc_Sec_PSRAMB_Access_Set_Not_Lock(uint8_t region, uint32_t startAddr, uint32_t endAddr,
					       uint8_t group)
{
	uint32_t tmpVal = 0;

	tmpVal = QC7XX_RD_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL);
	tmpVal &= (~(3 << (region * 2)));
	tmpVal |= (group << (region * 2));
	QC7XX_WR_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL, tmpVal);

	tmpVal = ((((endAddr >> 10) & 0xffff) - 1) & 0xffff) | (((startAddr >> 10) & 0xffff) << 16);
	QC7XX_WR_WORD(TZC_SEC_BASE + TZC_SEC_TZC_PSRAMB_TZSRG_R0_OFFSET + region * 4, tmpVal);

	/* set enable but not lock */
	tmpVal = QC7XX_RD_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL);
	tmpVal |= 1 << (region + 16);
	QC7XX_WR_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL, tmpVal);
}

static void flash_bank2_access_init(void)
{
	uint32_t reg_base = 0;
	uint32_t regval = 0;
	reg_base = QC7XX_SF_CTRL_BASE;
	regval = getreg32(reg_base + SF_CTRL_2_OFFSET);
	regval |= SF_CTRL_SF_IF_BK2_EN;
	regval |= SF_CTRL_SF_IF_BK2_MODE;
	putreg32(regval, reg_base + SF_CTRL_2_OFFSET);
}

void System_BOD_Init(void)
{
	uint8_t enableBod = 1;
	/* 0:BOD not trigger interrupt, 1:trigger interrupt */
	uint8_t enableBodInt = 1;
	/* 0:2.2v, 1:2.3v, 2:2.4v ... 7:2.9v */
	uint8_t bodThreshold = 7;

	AON_Set_BOD_Config(enableBod, bodThreshold, enableBodInt);
}
#endif

void SystemInit(void)
{
	uint32_t i = 0;

#ifndef CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU1
	/* CPU Prefetching barrier */
	if (QCC75X_PSRAM_INIT_DONE == 0) {
		Tzc_Sec_PSRAMB_Access_Set_Not_Lock(0, 0x0, 64 * 1024 * 1024, 0);
	}
	flash_bank2_access_init();
#endif

	/* enable mstatus FS */
	uint32_t mstatus = __get_MSTATUS();
	mstatus |= (1 << 13);
	__set_MSTATUS(mstatus);

	/* enable mxstatus THEADISAEE */
	uint32_t mxstatus = __get_MXSTATUS();
	mxstatus |= (1 << 22);
	/* enable mxstatus MM */
	mxstatus |= (1 << 15);
	__set_MXSTATUS(mxstatus);

	/* get interrupt level from info */
	CLIC->CLICCFG =
		(((CLIC->CLICINFO & CLIC_INFO_CLICINTCTLBITS_Msk) >> CLIC_INFO_CLICINTCTLBITS_Pos)
		 << CLIC_CLICCFG_NLBIT_Pos);

	/* Every interrupt should be clear by software*/
	for (i = 0; i < IRQn_LAST; i++) {
		CLIC->CLICINT[i].IE = 0;
		CLIC->CLICINT[i].IP = 0;
#ifdef CONFIG_IRQ_USE_VECTOR
		CLIC->CLICINT[i].ATTR = 1; /* use vector interrupt */
#else
		CLIC->CLICINT[i].ATTR = 0; /* use no vector interrupt */
#endif
	}

	/* tspend interrupt will be clear auto*/
	/* tspend use positive interrupt */
#ifdef CONFIG_IRQ_USE_VECTOR
	CLIC->CLICINT[MSOFT_IRQn].ATTR = 0x3;
#else
	CLIC->CLICINT[MSOFT_IRQn].ATTR = 0x2;
#endif
	csi_dcache_enable();
	csi_icache_enable();

	/* disable mexstatus SPUSHEN and SPSWAPEN for ipush/ipop*/
	uint32_t mexstatus = __get_MEXSTATUS();
	mexstatus &= ~(0x3 << 16);
	__set_MEXSTATUS(mexstatus);

#ifndef CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU1
	QC7XX_WR_REG(GLB_BASE, GLB_UART_CFG1, 0xffffffff);
	QC7XX_WR_REG(GLB_BASE, GLB_UART_CFG2, 0x0000ffff);
#endif
}

extern void hardware_init(void);
extern void hardware_init_cpu1(void);

void System_Post_Init(void)
{
	csi_dcache_clean();
	csi_icache_invalid();

#ifndef CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU1
	System_BOD_Init();
	PDS_Trim_RC32M();
	hardware_init();
#else
	hardware_init_cpu1();
#endif
}
