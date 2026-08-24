/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/clock/qcc75x-clock.h>
#include "qc7xx_clock.h"
#include "qcc75x_glb.h"
#include "qc7xx_flash.h"
#include "qc7xx_xip_sflash.h"

#define FLASHCTRL_NODE DT_NODELABEL(flashctrl)
#define FLASH_CLK_SRC  DT_CLOCKS_CELL_BY_IDX(FLASHCTRL_NODE, 0, source)
#define FLASH_CLK_DIV  DT_PROP_OR(FLASHCTRL_NODE, flash_clk_div, 0)

#ifndef CONFIG_PSRAM
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
	[0].sdmin = 5825422,
	[0].delay_value = 180,

	[1].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[1].flash_clock_div = 3,
	[1].sdmin = 5991862,
	[1].delay_value = 175,

	[2].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[2].flash_clock_div = 3,
	[2].sdmin = 6168094,
	[2].delay_value = 170,

	[3].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[3].flash_clock_div = 3,
	[3].sdmin = 6355006,
	[3].delay_value = 165,

	[4].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[4].flash_clock_div = 3,
	[4].sdmin = 6553600,
	[4].delay_value = 160,

	[5].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[5].flash_clock_div = 3,
	[5].sdmin = 6765006,
	[5].delay_value = 155,

	[6].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[6].flash_clock_div = 3,
	[6].sdmin = 6990506,
	[6].delay_value = 150,

	[7].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[7].flash_clock_div = 1,
	[7].sdmin = 5423668,
	[7].delay_value = 145,

	[8].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[8].flash_clock_div = 1,
	[8].sdmin = 5617371,
	[8].delay_value = 140,

	[9].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[9].flash_clock_div = 1,
	[9].sdmin = 5825422,
	[9].delay_value = 135,

	[10].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[10].flash_clock_div = 1,
	[10].sdmin = 6049476,
	[10].delay_value = 130,

	[11].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[11].flash_clock_div = 1,
	[11].sdmin = 6291456,
	[11].delay_value = 125,

	[12].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[12].flash_clock_div = 1,
	[12].sdmin = 6553600,
	[12].delay_value = 120,

	[13].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[13].flash_clock_div = 1,
	[13].sdmin = 6838539,
	[13].delay_value = 115,

	[14].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[14].flash_clock_div = 1,
	[14].sdmin = 5957818,
	[14].delay_value = 110,

	[15].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[15].flash_clock_div = 1,
	[15].sdmin = 6241523,
	[15].delay_value = 105,

	[16].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[16].flash_clock_div = 1,
	[16].sdmin = 6553600,
	[16].delay_value = 100,

	[17].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[17].flash_clock_div = 1,
	[17].sdmin = 6898526,
	[17].delay_value = 95,

	[18].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[18].flash_clock_div = 1,
	[18].sdmin = 5825422,
	[18].delay_value = 90,

	[19].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[19].flash_clock_div = 1,
	[19].sdmin = 6168094,
	[19].delay_value = 85,

	[20].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[20].flash_clock_div = 1,
	[20].sdmin = 6553600,
	[20].delay_value = 80,

	[21].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[21].flash_clock_div = 1,
	[21].sdmin = 6990506,
	[21].delay_value = 75,

	[22].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[22].flash_clock_div = 0,
	[22].sdmin = 5617371,
	[22].delay_value = 70,

	[23].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[23].flash_clock_div = 0,
	[23].sdmin = 6049476,
	[23].delay_value = 65,

	[24].flash_clock = GLB_SFLASH_CLK_WIFIPLL_80M,
	[24].flash_clock_div = 0,
	[24].sdmin = 6553600,
	[24].delay_value = 60,

	[25].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[25].flash_clock_div = 0,
	[25].sdmin = 5957818,
	[25].delay_value = 55,

	[26].flash_clock = GLB_SFLASH_CLK_WIFIPLL_96M,
	[26].flash_clock_div = 0,
	[26].sdmin = 6553600,
	[26].delay_value = 50,

	[27].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[27].flash_clock_div = 0,
	[27].sdmin = 5825422,
	[27].delay_value = 45,

	[28].flash_clock = GLB_SFLASH_CLK_WIFIPLL_120M,
	[28].flash_clock_div = 0,
	[28].sdmin = 6553600,
	[28].delay_value = 40,
};

static uint8_t flash_org_delay = 0;
static uint8_t flash_final_delay = 0;
static uint8_t flash_added_delay = 0;
#endif
#endif

#ifndef CONFIG_PSRAM
static void ATTR_CLOCK_SECTION __attribute__((noinline)) system_clock_init(void)
{
	if (GLB_CORE_ID_AP == GLB_Get_Core_Type()) {
		/* wifipll/cpupll */
		GLB_Power_On_XTAL_And_PLL_CLK(GLB_XTAL_40M, GLB_PLL_WIFIPLL | GLB_PLL_CPUPLL);
		GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_320M);
		HBN_Set_MCU_XCLK_Sel(HBN_MCU_XCLK_RC32M);
		GLB_Set_WL_MCU_System_CLK(GLB_WL_MCU_SYS_CLK_CPUPLL_DIV1, 0, 2);
		GLB_Set_WL_XCLK_Sel(GLB_WL_MCU_XCLK_XTAL);
		/* RC32M=32M */
		*(volatile uint32_t *)(0x2008F994) = 0x5A000000;
	}

	CPU_Set_MTimer_CLK(ENABLE, QC7XX_MTIMER_SOURCE_CLOCK_MCU_XCLK,
			   Clock_System_Clock_Get(QC7XX_SYSTEM_CLOCK_XCLK) / 1000000 - 1);
}
#endif

static void peripheral_clock_init(void)
{
	PERIPHERAL_CLOCK_ADC_DAC_ENABLE();
	PERIPHERAL_CLOCK_DMA0_ENABLE();
	PERIPHERAL_CLOCK_UART0_ENABLE();
	PERIPHERAL_CLOCK_UART1_ENABLE();
	PERIPHERAL_CLOCK_SPI0_ENABLE();
	PERIPHERAL_CLOCK_SPI1_ENABLE();
	PERIPHERAL_CLOCK_I2C0_ENABLE();
	PERIPHERAL_CLOCK_PWM0_ENABLE();
	PERIPHERAL_CLOCK_TIMER0_1_WDG_ENABLE();
	PERIPHERAL_CLOCK_I2S_ENABLE();
	PERIPHERAL_CLOCK_CAN_ENABLE();
	PERIPHERAL_CLOCK_CKS_ENABLE();
	PERIPHERAL_CLOCK_PEC_ENABLE();

	GLB_Set_UART_CLK(ENABLE, HBN_UART_CLK_XCLK, 0);
	GLB_Set_SPI_CLK(ENABLE, GLB_SPI_CLK_WIFIPLL_160M, 0);
	GLB_Set_SPI1_CLK(ENABLE, GLB_SPI_CLK_XCLK, 0);
	GLB_Set_I2C_CLK(ENABLE, GLB_I2C_CLK_XCLK, 0);
	GLB_Set_ADC_CLK(ENABLE, GLB_ADC_CLK_XCLK, 1);
	GLB_Set_PWM1_IO_Sel(GLB_PWM1_IO_SINGLE_END);
	GLB_Set_CAM_CLK(ENABLE, GLB_CAM_CLK_WIFIPLL_96M, 3);
	GLB_Set_PEC_CLK(ENABLE, GLB_PEC_CLK_MUXPLL_160M, 0);

	GLB_Swap_MCU_SPI_0_MOSI_With_MISO(0);
	/* enable wifi clock */
	GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_IP_WIFI_PHY | GLB_AHB_CLOCK_IP_WIFI_MAC_PHY |
			     GLB_AHB_CLOCK_IP_WIFI_PLATFORM);
}

#ifndef CONFIG_PSRAM
#ifndef CONFIG_BOARD_FLASH_LOW_SPEED
static uint32_t sdmin_range_list[] = {0x6E0000, 0x640000, 0x5A0000, 0x500000, 0x460000, 0x3C0000};
static int ATTR_TCM_SECTION clk_pll_set(uint32_t sdmin)
{
	uint32_t regval = 0;
	int vco_speed_bit = 1;
	int ret = -1;

	regval = QC7XX_RD_WORD(RF_BASE + RF_ANA1_WIFIPLL_SDMIN_OFFSET);
	regval = regval & RF_ANA1_WIFIPLL_SDM_IN_UMSK;
	regval = regval | sdmin;
	QC7XX_WR_WORD(RF_BASE + RF_ANA1_WIFIPLL_SDMIN_OFFSET, regval);

	GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_RC32M);
	GLB_Set_MCU_System_CLK_Div(0, 0);

	/* Calculate vco_speed_bit based on sdmin_range_list */
	for (int i = 0; i < sizeof(sdmin_range_list) / sizeof(sdmin_range_list[0]); i++) {
		if (sdmin > sdmin_range_list[i]) {
			vco_speed_bit =
				sizeof(sdmin_range_list) / sizeof(sdmin_range_list[0]) - i + 1;
			break;
		}
	}

	/* set vco_speed, dtc_r_sel = 3, pu_wifipll = 1, wifipll_rstb = 1 */
	regval = QC7XX_RD_WORD(RF_BASE + RF_ANA1_WIFIPLL_ANA_CTRL_OFFSET);
	regval = QC7XX_SET_REG_BITS_VAL(regval, RF_ANA1_WIFIPLL_VCO_SPEED, vco_speed_bit);
	regval = QC7XX_SET_REG_BITS_VAL(regval, RF_ANA1_WIFIPLL_DTC_R_SEL, 3);
	regval = QC7XX_SET_REG_BITS_VAL(regval, RF_ANA1_PU_WIFIPLL, 1);
	regval = QC7XX_SET_REG_BITS_VAL(regval, RF_ANA1_WIFIPLL_RSTB, 1);
	QC7XX_WR_WORD(RF_BASE + RF_ANA1_WIFIPLL_ANA_CTRL_OFFSET, regval);

	/* delay > 2us */
	arch_delay_us(3);

	/* wifipll_rstb = 0 */
	regval = QC7XX_RD_WORD(RF_BASE + RF_ANA1_WIFIPLL_ANA_CTRL_OFFSET);
	regval = QC7XX_SET_REG_BITS_VAL(regval, RF_ANA1_WIFIPLL_RSTB, 0);
	QC7XX_WR_WORD(RF_BASE + RF_ANA1_WIFIPLL_ANA_CTRL_OFFSET, regval);

	/* delay > 1us */
	arch_delay_us(2);

	/* wifipll_rstb = 1 */
	regval = QC7XX_RD_WORD(RF_BASE + RF_ANA1_WIFIPLL_ANA_CTRL_OFFSET);
	regval = QC7XX_SET_REG_BITS_VAL(regval, RF_ANA1_WIFIPLL_RSTB, 1);
	QC7XX_WR_WORD(RF_BASE + RF_ANA1_WIFIPLL_ANA_CTRL_OFFSET, regval);

	arch_delay_us(270);

	regval = QC7XX_RD_WORD(RF_BASE + RF_ANA1_WIFIPLL_READBACK_OFFSET);
	if (QC7XX_GET_REG_BITS_VAL(regval, RF_ANA1_WIFIPLL_LO_LOCK) == 1) {
		ret = 0;
	}

	GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_240M);
	GLB_Set_MCU_System_CLK_Div(0, 3);

	return ret;
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
	regval = QC7XX_RD_WORD(RF_BASE + RF_ANA1_WIFIPLL_SDMIN_OFFSET);
	pre_sdmin = regval & RF_ANA1_WIFIPLL_SDM_IN_MSK;

	for (int index = 0;
	     index < sizeof(flash_clock_para_list) / sizeof(flash_clock_para_list[0]); index++) {
		GLB_Set_SF_CLK(1, flash_clock_para_list[index].flash_clock,
			       flash_clock_para_list[index].flash_clock_div);
		if (clk_pll_set(flash_clock_para_list[index].sdmin) != 0) {
			break;
		}
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
	sf_ctrl_0 = QC7XX_RD_WORD(0x20082000);
	sf_ctrl_1 = QC7XX_RD_WORD(0x20082004);
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
			GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_WIFIPLL_80M, 0);
			config_change = 1;
		} else if (flash_org_delay >= 50 && flash_org_delay <= 70) {
			/* flash can run @ 80M @1T */
			sf_ctrl_cfg.clk_delay = 1;
			sf_ctrl_cfg.clk_invert = 1;
			sf_ctrl_cfg.rx_clk_invert = 0;
			qc7xx_sflash_init(&sf_ctrl_cfg, NULL);
			GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_WIFIPLL_80M, 0);
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
					GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_WIFIPLL_80M, 0);
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
		QC7XX_WR_WORD(0x20082000, sf_ctrl_0);
		QC7XX_WR_WORD(0x20082004, sf_ctrl_1);
		QC7XX_WR_REG(GLB_BASE, GLB_SF_CFG0, sf_clk_cfg);
	} else {
		p_flash_cfg->clk_delay = sf_ctrl_cfg.clk_delay;
		p_flash_cfg->clk_invert = sf_ctrl_cfg.clk_invert | (sf_ctrl_cfg.rx_clk_invert << 1);
	}

	/* to make sure bus interface is system bus */
	qc7xx_sf_ctrl_set_owner(SF_CTRL_OWNER_SAHB);
	qc7xx_xip_sflash_state_restore(p_flash_cfg, offset, 0, 0);
	qc7xx_xip_sflash_opt_exit(is_aes_enable);
	qc7xx_irq_restore(flag);
}
#endif
#endif

int hardware_init(void)
{
#ifndef CONFIG_BOARD_FLASH_INIT_SKIP
	qc7xx_flash_init();
#ifndef CONFIG_PSRAM
#ifndef CONFIG_BOARD_FLASH_LOW_SPEED
	if ((FLASH_CLK_SRC == GLB_SFLASH_CLK_WIFIPLL_80M) && (FLASH_CLK_DIV == 0)) {
		board_set_flash_80m();
	} else {
		GLB_Set_SF_CLK(1, (uint8_t)FLASH_CLK_SRC, (uint8_t)FLASH_CLK_DIV);
	}
#endif
#endif
#endif

#ifndef CONFIG_PSRAM
	system_clock_init();
#endif

	peripheral_clock_init();
	return 0;
}

int hardware_init_cpu1(void)
{
	CPU_Set_MTimer_CLK(ENABLE, QC7XX_MTIMER_SOURCE_CLOCK_MCU_XCLK,
			   Clock_System_Clock_Get(QC7XX_SYSTEM_CLOCK_WL_XCLK) / 1000000 - 1);
	return 0;
}

void sys_arch_reboot(int type)
{
	GLB_SW_POR_Reset();
}
