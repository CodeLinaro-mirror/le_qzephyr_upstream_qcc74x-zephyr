/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include "qc7xx_clock.h"
#include "qcc74x_clock.h"
#include "qcc74x_glb.h"
#include "qc7xx_flash.h"
#include "qc7xx_peri.h"
#include "qc7xx_xip_sflash.h"
#include "qc7xx_core.h"
#include "qc7xx_name.h"
#include "qc7xx_sec_aes.h"
#include "qc7xx_sec_pka.h"
#include "qc7xx_sec_sha.h"

#define FLASHCTRL_NODE DT_NODELABEL(flashctrl)
#define FLASH_CLK_SRC  DT_CLOCKS_CELL_BY_IDX(FLASHCTRL_NODE, 0, source)
#define FLASH_CLK_DIV  DT_PROP_OR(FLASHCTRL_NODE, flash_clk_div, 0)

#ifndef CONFIG_BOARD_FLASH_LOW_SPEED
typedef struct {
	uint8_t flash_clock;
	uint8_t flash_clock_div;
	uint8_t delay_value;
	uint32_t sdmin;
} flash_clock_para_t;

flash_clock_para_t flash_clock_para_list[] = {
	[0].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[0].flash_clock_div = 3,
	[0].sdmin = 23320330,
	[0].delay_value = 180,

	[1].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[1].flash_clock_div = 3,
	[1].sdmin = 23991419,
	[1].delay_value = 175,

	[2].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[2].flash_clock_div = 3,
	[2].sdmin = 24662508,
	[2].delay_value = 170,

	[3].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[3].flash_clock_div = 3,
	[3].sdmin = 25231360,
	[3].delay_value = 165,

	[4].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[4].flash_clock_div = 3,
	[4].sdmin = 26282666,
	[4].delay_value = 160,

	[5].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[5].flash_clock_div = 3,
	[5].sdmin = 27123712,
	[5].delay_value = 155,

	[6].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[6].flash_clock_div = 3,
	[6].sdmin = 27961852,
	[6].delay_value = 150,

	[7].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[7].flash_clock_div = 1,
	[7].sdmin = 21705523,
	[7].delay_value = 145,

	[8].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[8].flash_clock_div = 1,
	[8].sdmin = 22465741,
	[8].delay_value = 140,

	[9].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[9].flash_clock_div = 1,
	[9].sdmin = 23278387,
	[9].delay_value = 135,

	[10].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[10].flash_clock_div = 1,
	[10].sdmin = 24222106,
	[10].delay_value = 130,

	[11].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[11].flash_clock_div = 1,
	[11].sdmin = 25165824,
	[11].delay_value = 125,

	[12].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[12].flash_clock = 1,
	[12].sdmin = 26214400,
	[12].delay_value = 120,

	[13].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[13].flash_clock_div = 1,
	[13].sdmin = 27341619,
	[13].delay_value = 115,

	[14].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[14].flash_clock_div = 1,
	[14].sdmin = 23890944,
	[14].delay_value = 110,

	[15].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[15].flash_clock_div = 1,
	[15].sdmin = 25031611,
	[15].delay_value = 105,

	[16].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[16].flash_clock_div = 1,
	[16].sdmin = 26282666,
	[16].delay_value = 100,

	[17].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[17].flash_clock_div = 1,
	[17].sdmin = 27665134,
	[17].delay_value = 95,

	[18].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[18].flash_clock_div = 1,
	[18].sdmin = 23364239,
	[18].delay_value = 90,

	[19].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[19].flash_clock_div = 1,
	[19].sdmin = 24735143,
	[19].delay_value = 85,

	[20].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[20].flash_clock_div = 1,
	[20].sdmin = 26282666,
	[20].delay_value = 80,

	[21].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[21].flash_clock_div = 1,
	[21].sdmin = 28032040,
	[21].delay_value = 75,

	[22].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[22].flash_clock_div = 0,
	[22].sdmin = 22525296,
	[22].delay_value = 70,

	[23].flash_clock = GLB_SFLASH_CLK_MUXPLL_80M,
	[23].flash_clock_div = 0,
	[23].sdmin = 24259952,
	[23].delay_value = 65,
};

static uint8_t flash_org_delay = 0;
static uint8_t flash_final_delay = 0;
static uint8_t flash_added_delay = 0;
#endif

static void ATTR_CLOCK_SECTION __attribute__((noinline)) system_clock_init_lp(void)
{
	GLB_Power_On_XTAL_And_PLL_CLK(GLB_XTAL_40M, GLB_PLL_WIFIPLL | GLB_PLL_AUPLL);
	GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_320M);
	HBN_Set_MCU_XCLK_Sel(HBN_MCU_XCLK_XTAL);
}

static void __attribute__((noinline)) peripheral_clock_init_lp(void)
{
    struct qc7xx_device_s *aes = qc7xx_device_get_by_name(QC7XX_NAME_SEC_AES);
#ifdef CONFIG_QCC74X_SEC_PKA
    struct qc7xx_device_s *pka = qc7xx_device_get_by_name(QC7XX_NAME_SEC_PKA);
#endif
    struct qc7xx_device_s *sha = qc7xx_device_get_by_name(QC7XX_NAME_SEC_SHA);

    /* Match Bouffalo SDK board_recovery() LP path. 
    tmp_val = BIT(GLB_CGEN_M_CPU_POS) | BIT(GLB_CGEN_M_DMA_POS) |
              BIT(GLB_CGEN_M_SEC_POS) | BIT(GLB_CGEN_M_SDU_POS);
    QC7XX_WR_REG(GLB_BASE, GLB_CGEN_CFG0, tmp_val);

    tmp_val = BIT(GLB_CGEN_S1_RSVD0_POS) |
              BIT(GLB_CGEN_S1_EF_CTRL_POS) |
              BIT(GLB_CGEN_S1_SF_CTRL_POS) |
              BIT(GLB_CGEN_S1_DMA_POS) |
              BIT(GLB_CGEN_S1A_UART0_POS) |
              BIT(GLB_CGEN_S1A_UART1_POS) |
              BIT(GLB_CGEN_S1_SEC_ENG_POS);
    QC7XX_WR_REG(GLB_BASE, GLB_CGEN_CFG1, tmp_val);

    tmp_val = BIT(GLB_CGEN_S2_WIFI_POS) |
              BIT(GLB_CGEN_S1_EXT_EMI_MISC_POS) |
              BIT(GLB_CGEN_S1_EXT_PIO_POS);
    QC7XX_WR_REG(GLB_BASE, GLB_CGEN_CFG2, tmp_val);*/

    GLB_Set_UART_CLK(ENABLE, HBN_UART_CLK_XCLK, 0);
    GLB_Set_PKA_CLK_Sel(GLB_PKA_CLK_MCU_MUXPLL_160M);

    if (aes != NULL) {
        qc7xx_group0_request_aes_access(aes);
        qc7xx_aes_link_init(aes);
    }

#ifdef CONFIG_QCC74X_SEC_PKA
    if (pka != NULL) {
        qc7xx_pka_init(pka);
    }
#endif

    if (sha != NULL) {
        qc7xx_group0_request_sha_access(sha);
        qc7xx_sha_link_init(sha);
    }

#ifdef CONFIG_BSP_SDH_SDCARD
    PERIPHERAL_CLOCK_SDH_ENABLE();
    GLB_AHB_MCU_Software_Reset(GLB_AHB_MCU_SW_EXT_SDH);
#endif

    PERIPHERAL_CLOCK_ADC_DAC_ENABLE();
    GLB_Set_ADC_CLK(ENABLE, GLB_ADC_CLK_XCLK, 1);
}

static void peripheral_clock_init(void)
{
	GLB_Swap_MCU_SPI_0_MOSI_With_MISO(0);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(cam), okay)
	/* Configure cam_ref_clk for the sensor MCLK output on GPIO23 (CHIP_CLK_OUT_2).
	 * Must run before any camera sensor device init so MCLK is present when the
	 * sensor releases from reset and its internal PLL tries to lock.
	 * Source: WIFIPLL_96M (96 MHz), div=3 ? 96/(3+1) = 24 MHz. */
	GLB_Set_CAM_CLK(1, GLB_CAM_CLK_WIFIPLL_96M, 3);
	GLB_Set_Chip_Clock_Out2_Sel(GLB_CHIP_CLK_OUT_2_CAM_REF_CLK);
#endif
}

#ifndef CONFIG_BOARD_FLASH_LOW_SPEED
static void ATTR_TCM_SECTION clk_pll_set(uint32_t sdmin)
{
	uint32_t regval = 0;

	regval = QC7XX_RD_WORD(0x20000828);
	regval = regval & 0xFC000000;
	regval = regval | sdmin;
	QC7XX_WR_WORD(0x20000828, regval);

	GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_RC32M);
	GLB_Set_MCU_System_CLK_Div(0, 0);

	/* toggle sdm_reset (pulse 0 > 1us) */
	/* cfg0 : wifipll_sdm_reset */
	regval = QC7XX_RD_WORD(0x20000810 + 4 * 0);
	regval = QC7XX_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_SDM_RSTB, 0);
	regval = QC7XX_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_FBDV_RSTB, 0);
	QC7XX_WR_WORD(0x20000810 + 4 * 0, regval);

	arch_delay_us(2);

	regval = QC7XX_RD_WORD(0x20000810 + 4 * 0);
	regval = QC7XX_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_SDM_RSTB, 1);
	QC7XX_WR_WORD(0x20000810 + 4 * 0, regval);

	arch_delay_us(2);

	regval = QC7XX_RD_WORD(0x20000810 + 4 * 0);
	regval = QC7XX_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_FBDV_RSTB, 1);
	QC7XX_WR_WORD(0x20000810 + 4 * 0, regval);

	arch_delay_us(40);

	GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_240M);
	GLB_Set_MCU_System_CLK_Div(0, 3);
}

static int ATTR_TCM_SECTION flash_check_bootheader(spi_flash_cfg_type *pflash_cfg, int index)
{
	uint8_t read_data[256];
	uint32_t stat = 0;
	uint32_t crc32 = 0;
	uint8_t *pcrc32 = read_data + 252;

	stat = qc7xx_sflash_read(pflash_cfg, SF_CTRL_QIO_MODE, 0, 0x00000000, read_data,
				 sizeof(read_data));
	if (0 != stat) {
		return -1;
	}

	crc32 = qc7xx_soft_crc32(read_data, 252);
	if (arch_memcmp(&crc32, pcrc32, 4) != 0) {
		return -1;
	}
	return 0;
}

static int ATTR_TCM_SECTION board_get_flash_delay(spi_flash_cfg_type *p_flash_cfg)
{
	uint8_t success_sts = 0;
	uint8_t cal_delay = 0;
	uint32_t regval;
	uint32_t pre_sdmin;

	/* save register changed by calibration */
	regval = QC7XX_RD_WORD(0x20000828);
	pre_sdmin = regval & 0x3ffffff;

	for (int index = 0;
	     index < sizeof(flash_clock_para_list) / sizeof(flash_clock_para_list[0]); index++) {
		GLB_Set_SF_CLK(1, flash_clock_para_list[index].flash_clock,
			       flash_clock_para_list[index].flash_clock_div);
		clk_pll_set(flash_clock_para_list[index].sdmin);
		if (flash_check_bootheader(p_flash_cfg, index) != 0) {
			/* fail */
			if (success_sts == 1) {
				success_sts = 2;
				cal_delay = flash_clock_para_list[index].delay_value;
				break;
			}
		} else {
			/* success  */
			if (success_sts == 0) {
				success_sts = 1;
			}
		}
	}
	clk_pll_set(pre_sdmin);

	return cal_delay;
}

static void ATTR_CLOCK_SECTION __attribute__((noinline)) board_set_flash_80m()
{
	uint8_t is_aes_enable = 0;
	uint32_t offset = 0;
	uintptr_t flag;
	struct sf_ctrl_cfg_type sf_ctrl_cfg = {0};
	spi_flash_cfg_type *p_flash_cfg;
	uint32_t flash_cfg_len = 0;
	uint32_t sf_ctrl_0, sf_ctrl_1, sf_clk_cfg;
	uint32_t config_change = 0;
	uint32_t delay_found = 0;

	sf_ctrl_cfg.owner = SF_CTRL_OWNER_SAHB;
	sf_ctrl_cfg.en32b_addr = 0;
	sf_ctrl_cfg.clk_delay = 0;
	sf_ctrl_cfg.clk_invert = 1;
	sf_ctrl_cfg.rx_clk_invert = 1;
	sf_ctrl_cfg.do_delay = 0;
	sf_ctrl_cfg.di_delay = 0;
	sf_ctrl_cfg.oe_delay = 0;
	/* disable interrupt */
	flag = qc7xx_irq_save();
	/* save register changed by calibration */
	sf_ctrl_0 = QC7XX_RD_WORD(0x2000b000);
	sf_ctrl_1 = QC7XX_RD_WORD(0x2000b004);
	sf_clk_cfg = QC7XX_RD_REG(GLB_BASE, GLB_SF_CFG0);

	qc7xx_flash_get_cfg((uint8_t **)&p_flash_cfg, &flash_cfg_len);
	qc7xx_xip_sflash_opt_enter(&is_aes_enable);
	qc7xx_xip_sflash_state_save(p_flash_cfg, &offset, 0, 0);

	qc7xx_sflash_init(&sf_ctrl_cfg, NULL);
	qc7xx_sflash_reset_continue_read(p_flash_cfg);

	/* cal delay value */
	flash_org_delay = board_get_flash_delay(p_flash_cfg);
	flash_final_delay = flash_org_delay;

	if (0 != flash_org_delay) {
		if (flash_org_delay >= 120 && flash_org_delay <= 155) {
			/* flash can run @ 80M @1.5T */
			sf_ctrl_cfg.clk_delay = 1;
			sf_ctrl_cfg.clk_invert = 1;
			sf_ctrl_cfg.rx_clk_invert = 1;
			qc7xx_sflash_init(&sf_ctrl_cfg, NULL);
			GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_MUXPLL_80M, 0);
			config_change = 1;
		} else if (flash_org_delay >= 50 && flash_org_delay <= 70) {
			/* flash can run @ 80M @1T */
			sf_ctrl_cfg.clk_delay = 1;
			sf_ctrl_cfg.clk_invert = 1;
			sf_ctrl_cfg.rx_clk_invert = 0;
			qc7xx_sflash_init(&sf_ctrl_cfg, NULL);
			GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_MUXPLL_80M, 0);
			config_change = 1;
		} else if (flash_org_delay > 70 && flash_org_delay < 120) {
			/* flash can run @ 80M @1.5T with added delay */
			flash_added_delay = 1;
			while (1) {
				if (delay_found) {
					/* step2:set 1.5T */
					sf_ctrl_cfg.clk_delay = 1;
					sf_ctrl_cfg.clk_invert = 1;
					sf_ctrl_cfg.rx_clk_invert = 1;
					qc7xx_sflash_init(&sf_ctrl_cfg, NULL);
					GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_MUXPLL_80M, 0);
					flash_added_delay--;
				}
				if (flash_added_delay > 6) {
					flash_final_delay = flash_org_delay;
					flash_added_delay = 0;
					break;
				}

				/* step3:set added delay */
				if (flash_added_delay <= 3) {
					/* add ~3 ns delay */
					qc7xx_sf_ctrl_set_io_delay(0, 0, flash_added_delay & 0x03,
								   0);
					qc7xx_sf_ctrl_set_io_delay(1, 0, flash_added_delay & 0x03,
								   0);
					qc7xx_sf_ctrl_set_io_delay(2, 0, flash_added_delay & 0x03,
								   0);
				} else {
					/* add ~3 ns delay */
					qc7xx_sf_ctrl_set_io_delay(0, 0, 3, 0);
					qc7xx_sf_ctrl_set_io_delay(1, 0, 3, 0);
					qc7xx_sf_ctrl_set_io_delay(2, 0, 3, 0);
					qc7xx_sf_ctrl_set_cs_clk_delay(
						0, 0, (flash_added_delay - 3) & 0x03);
					qc7xx_sf_ctrl_set_cs_clk_delay(
						1, 0, (flash_added_delay - 3) & 0x03);
					qc7xx_sf_ctrl_set_cs_clk_delay(
						2, 0, (flash_added_delay - 3) & 0x03);
				}

				/* step4:break */
				if (delay_found) {
					config_change = 1;
					break;
				}

				flash_final_delay = board_get_flash_delay(p_flash_cfg);
				/* step1: delay found */
				if (flash_final_delay >= 125) {
					delay_found = 1;
				} else if (flash_final_delay >= 110 && flash_added_delay == 6) {
					delay_found = 1;
				}
				flash_added_delay++;
			}
		}
	}

	if (config_change == 0) {
		/* restore flash config */
		QC7XX_WR_WORD(0x2000b000, sf_ctrl_0);
		QC7XX_WR_WORD(0x2000b004, sf_ctrl_1);
		QC7XX_WR_REG(GLB_BASE, GLB_SF_CFG0, sf_clk_cfg);
	} else {
		p_flash_cfg->clk_delay = sf_ctrl_cfg.clk_delay;
		p_flash_cfg->clk_invert = sf_ctrl_cfg.clk_invert | (sf_ctrl_cfg.rx_clk_invert << 1);
	}

	/* to makesure bus interface is system bus */
	qc7xx_sf_ctrl_set_owner(SF_CTRL_OWNER_SAHB);
	qc7xx_xip_sflash_state_restore(p_flash_cfg, offset, 0, 0);
	qc7xx_xip_sflash_opt_exit(is_aes_enable);
	qc7xx_irq_restore(flag);
}
#endif

int hardware_init(void)
{
#ifndef CONFIG_BOARD_FLASH_INIT_SKIP
	qc7xx_flash_init();
#ifndef CONFIG_BOARD_FLASH_LOW_SPEED
	if ((FLASH_CLK_SRC == GLB_SFLASH_CLK_MUXPLL_80M) && (FLASH_CLK_DIV == 0)) {
		board_set_flash_80m();
	} else {
		GLB_Set_SF_CLK(1, (uint8_t)FLASH_CLK_SRC, (uint8_t)FLASH_CLK_DIV);
	}
#endif
#endif

	peripheral_clock_init();

	return 0;
}

int hardware_recovery_lp(void)
{
	system_clock_init_lp();
	//GLB_Set_UART_CLK(ENABLE, HBN_UART_CLK_XCLK, 0);
	peripheral_clock_init_lp();
	return 0;
}

void sys_arch_reboot(int type)
{
	GLB_SW_POR_Reset();
}
