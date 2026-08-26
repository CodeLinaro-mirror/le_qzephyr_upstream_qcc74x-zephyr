/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <zephyr/logging/log.h>
#include "qc7xx_efuse.h"
#include "qc7xx_gpio.h"
#include "qcc74x_glb.h"
#include "qcc74x_psram.h"
#include "qcc74x_tzc_sec.h"

LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);

#ifdef CONFIG_PSRAM
#define PSRAM_NODE DT_NODELABEL(psram)
#define PSRAM_CLK_SRC DT_CLOCKS_CELL_BY_IDX(PSRAM_NODE, 0, source)
#define PSRAM_CLK_DIV DT_PROP_OR(PSRAM_NODE, psram_clk_div, 0)

static void qc7xx_init_psram_gpio(void)
{
	struct qc7xx_device_s *gpio;

	gpio = qc7xx_device_get_by_name("gpio");
	for (uint8_t i = 0; i < 12; i++) {
		qc7xx_gpio_init(gpio, (41 + i), GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
	}
}

static void psram_winbond_default_init(uint8_t psram_info)
{
	qc7xx_ef_ctrl_com_trim_t trim;
	uint16_t dqs_val[] = {
		0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
		0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF,
	};
	int32_t left_flag = 0, right_flag = 0, c_val = 0;
	PSRAM_Ctrl_Cfg_Type default_psram_ctrl_cfg = {
		.vendor = PSRAM_CTRL_VENDOR_WINBOND,
		.ioMode = PSRAM_CTRL_X8_MODE,
		.size = PSRAM_SIZE_4MB,
		.dqs_delay = 0xfff0,
	};

	PSRAM_Winbond_Cfg_Type default_winbond_cfg = {
		.rst = DISABLE,
		.clockType = PSRAM_CLOCK_DIFF,
		.inputPowerDownMode = DISABLE,
		.hybridSleepMode = DISABLE,
		.linear_dis = ENABLE,
		.PASR = PSRAM_PARTIAL_REFRESH_FULL,
		.disDeepPowerDownMode = ENABLE,
		.fixedLatency = DISABLE,
		.burstLen = PSRAM_WINBOND_BURST_LENGTH_64_BYTES,
		.burstType = PSRAM_WRAPPED_BURST,
		.latency = PSRAM_WINBOND_6_CLOCKS_LATENCY,
		.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_35_OHMS_FOR_4M,
	};

	qc7xx_ef_ctrl_read_common_trim(NULL, "psram", &trim, 1);
	if (trim.en) {
		if (trim.parity == qc7xx_ef_ctrl_get_trim_parity(trim.value, trim.len)) {
			left_flag = ((trim.value & (0xf0)) >> 0x4);
			right_flag = (trim.value & (0xf));
			c_val = ((left_flag + right_flag) >> 0x1);
			default_psram_ctrl_cfg.dqs_delay = dqs_val[c_val];
		} else {
			printf("\r\nPSRAM trim is corrupted\r\n");
		}
	} else {
		printf("\r\n!!!!!!PSRAM INIT WITHOUT PSRAM TRIM!!!!!!!!!!!!!!!!!!\r\n");
	}

	switch (psram_info) {
	case 1:
		default_psram_ctrl_cfg.size = PSRAM_SIZE_4MB;
		default_winbond_cfg.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_35_OHMS_FOR_4M;
		break;
	case 2:
		default_psram_ctrl_cfg.size = PSRAM_SIZE_8MB;
		default_winbond_cfg.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_25_OHMS_FOR_8M;
		break;
	case 3:
		default_psram_ctrl_cfg.size = PSRAM_SIZE_16MB;
		default_winbond_cfg.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_25_OHMS_FOR_16M;
		break;
	default:
		break;
	}
	PSram_Ctrl_Init(PSRAM0_ID, &default_psram_ctrl_cfg);
	// PSram_Ctrl_Winbond_Reset(PSRAM0_ID);
	PSram_Ctrl_Winbond_Write_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR0, &default_winbond_cfg);
}

uint32_t board_psram_x8_init(uint8_t psram_info)
{
	uint16_t reg_read = 0;

	GLB_Set_PSRAMB_CLK_Sel(ENABLE, (uint8_t)PSRAM_CLK_SRC, (uint8_t)PSRAM_CLK_DIV);

	qc7xx_init_psram_gpio();

	/* psram init*/
	psram_winbond_default_init(psram_info);
	/* check psram work or not */
	PSram_Ctrl_Winbond_Read_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_ID0, &reg_read);
	return reg_read;
}
#endif

const char *hal_driver_version(const char ***ctx)
{
	extern uint8_t _version_info_section_start;
	extern uint8_t _version_info_section_end;
	const char **const version_section_start = (const char **)&_version_info_section_start;
	const char **const version_section_end = (const char **)&_version_info_section_end;
	const char *version_str;

	/* init */
	if (NULL == (*ctx)) {
		(*ctx) = version_section_start;
	}
	/* check the end */
	if (version_section_end == (*ctx)) {
		return NULL;
	}
	version_str = (**ctx);
	*ctx = (*ctx) + 1;
	return version_str;
}

void soc_late_init_hook(void)
{
	const char *version_str;
	const char **ctx = NULL;
	while ((version_str = hal_driver_version(&ctx))) {
		puts(version_str);
	}

#ifdef CONFIG_PSRAM
	static qc7xx_efuse_device_info_type device_info;

	qc7xx_efuse_get_device_info(&device_info);
	if (device_info.psram_info == 0) {
		LOG_ERR("This chip has no psram, please disable CONFIG_PSRAM\r\n");
		while (1) {
		}
	}

	if (QCC74X_PSRAM_INIT_DONE == 0) {
		printf("psram init\r\n");
		board_psram_x8_init(device_info.psram_info);
		Tzc_Sec_PSRAMB_Access_Release();
	} else {
		printf("psram already init\r\n");
	}
#endif
}
