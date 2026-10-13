/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <hardware/sf_ctrl_reg.h>
#include <hardware/glb_reg.h>
#include <hardware/tzc_sec_reg.h>

#ifdef CONFIG_PSRAM
#include "qcr373_ef_cfg.h"
#include "qcr373_glb.h"
/* qc7xx_glb.h via qcr373_glb.h */
#include "qcr373_psram.h"
#include "qcr373_tzc_sec.h"
#if defined(CONFIG_WIFI_QCR373)
#include "soc_mem.h"
#endif
#endif

LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);

#ifdef CONFIG_PSRAM
static void qc7xx_init_psram_gpio(void)
{
	struct qc7xx_device_s *gpio;

	gpio = qc7xx_device_get_by_name("gpio");
	for (uint8_t i = 0; i < 12; i++) {
		qc7xx_gpio_init(gpio, (59 + i), GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
	}
}

#define TEST_MEMORY_SIZE (1 * 1024 * 1024)

uint8_t psram_rw_check(void)
{
    uint32_t i = 0;
    volatile uint32_t *p = (uint32_t *)QC7XX_LHAL_PSRAM_BASE;
    volatile uint32_t q = 0;
    uint32_t test_pattern_val[] = {
        0xaa55aa55,
        0x33cc33cc,
        0x55aa55aa,
        0xcc33cc33,
    };

    p[0] = test_pattern_val[0];
    p[1] = test_pattern_val[1];
    p[2] = test_pattern_val[2];
    p[3] = test_pattern_val[3];

    __ISB();

    for (i = 0; i < 4; i++) {
        q = p[i];
        if (q != test_pattern_val[i]) {
            return ERROR;
        }
    }

    /* clear PSRAM 1M Byte */
    for (i = 0; i < TEST_MEMORY_SIZE; i++) {
        p[i] = 0xffffffff;
    }

    /* check PSRAM 1M Byte */
    for (i = 0; i < TEST_MEMORY_SIZE; i++) {
        p[i] = i;
    }

    for (i = 0; i < TEST_MEMORY_SIZE; i++) {
        q = p[i];

        if (q != i) {
            return ERROR;
        }
    }

    return SUCCESS;
}

static uint32_t psram_winbond_default_init(uint8_t psram_info)
{
	qc7xx_ef_ctrl_com_trim_t trim;
	uint16_t dqs_val[] = {
		0x8000,
		0xC000,
		0xE000,
		0xF000,
		0xF800,
		0xFC00,
		0xFE00,
		0xFF00,
		0xFF80,
		0xFFC0,
		0xFFE0,
		0xFFF0,
		0xFFF8,
		0xFFFC,
		0xFFFE,
		0xFFFF,
	};
	int32_t left_flag = 0, right_flag = 0, c_val = 0;
	uint16_t reg_read = 0;
	int32_t dqs_win_min = 16, dqs_win_max = 0;
	uint16_t psram_id_value = 0x0c96;
	uint32_t temp_min = 16;
	uint32_t temp_len = 0;
	uint32_t dqs_win_max_len = 0;
	PSRAM_Ctrl_Cfg_Type default_psram_ctrl_cfg = {
		.vendor = PSRAM_CTRL_VENDOR_WINBOND,
		.ioMode = PSRAM_CTRL_X8_MODE,
		.size = PSRAM_SIZE_4MB,
		.dqs_delay = 0xfffc,
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

	switch (psram_info) {
		case 1:
			default_psram_ctrl_cfg.size = PSRAM_SIZE_4MB;
			default_winbond_cfg.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_35_OHMS_FOR_4M;
			psram_id_value = 0x5f;
			break;
		case 2:
			default_psram_ctrl_cfg.size = PSRAM_SIZE_8MB;
			default_winbond_cfg.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_25_OHMS_FOR_8M;
			psram_id_value = 0xc86;
			break;
		case 3:
			default_psram_ctrl_cfg.size = PSRAM_SIZE_16MB;
			default_winbond_cfg.driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_25_OHMS_FOR_16M;
			psram_id_value = 0x0c96;
			break;
		default:
			break;
	}

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
		for (uint32_t dqs_index = 0; dqs_index < 16; dqs_index++) {
			default_psram_ctrl_cfg.dqs_delay = dqs_val[dqs_index];
			reg_read = 0;
			/* reinit psram controller */
			PSram_Ctrl_Init(PSRAM0_ID, &default_psram_ctrl_cfg);
			PSram_Ctrl_Winbond_Write_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR0, &default_winbond_cfg);
			PSram_Ctrl_Winbond_Read_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_ID0, &reg_read);

			if (reg_read == psram_id_value) {
				if (psram_rw_check() == SUCCESS) {
					temp_len++;
					if (temp_len == 1) { // new
						temp_min = dqs_index;
					}
					if (temp_len > dqs_win_max_len) { // max new
						dqs_win_min = temp_min;
						dqs_win_max = dqs_index;
						dqs_win_max_len = temp_len;
					}
					printf("Y ");
				} else {
					temp_len = 0;
					printf("N ");
				}
			} else {
				temp_len = 0;
				printf("* ");
			}
		}

		left_flag = dqs_win_min;
		right_flag = dqs_win_max;
		c_val = ((left_flag + right_flag) >> 1);
		printf("\r\nwindow: 0x%02x ~ 0x%02x; c_val: 0x%02x; dqs:0x%04x; code num:%d\r\n", left_flag, right_flag, c_val, dqs_val[c_val], (right_flag - left_flag));
		default_psram_ctrl_cfg.dqs_delay = dqs_val[c_val];
	}

	PSram_Ctrl_Init(PSRAM0_ID, &default_psram_ctrl_cfg);
	PSram_Ctrl_Winbond_Write_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR0, &default_winbond_cfg);
	PSram_Ctrl_Winbond_Read_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_ID0, &reg_read);
	printf("psram id:0x%x\r\n", reg_read);

	return reg_read;
}

uint32_t board_psram_x8_init()
{
	qc7xx_efuse_device_info_type device_info;
	uint16_t reg_read = 0;

	/* enable PSRAM access in BMX config */
	*((volatile uint32_t *)0x20000338) &= (~(1 << 30));

	/* Check if the chip supports PSRAM */
	qc7xx_efuse_get_device_info(&device_info);
	if (device_info.psram_info == 0) {
		LOG_ERR("This chip has no psram, please disable CONFIG_PSRAM\r\n");
		while (1) {
		}
	}

	/* Verify whether the PSRAM initialization has been completed on the chip */
	if (QCR373_PSRAM_INIT_DONE) {
		printf("psram already init\r\n");
		return 0;
	}

	/* power init */
	AON_Set_Ldo18_AON_Power_Switch_For_PSRAM(1);

	/* clk init */
	GLB_Set_PSRAMB_CLK_Sel(ENABLE, GLB_PSRAMB_CLK_480M, 0);

	qc7xx_init_psram_gpio();

	Tzc_Sec_PSRAMB_Access_Release();

	/* psram init*/
	reg_read = psram_winbond_default_init(device_info.psram_info);

	return reg_read;
}
#endif

const char* hal_driver_version(const char ***ctx)
{
	extern uint8_t _version_info_section_start;
	extern uint8_t _version_info_section_end;
	const char ** const version_section_start = (const char**)&_version_info_section_start;
	const char ** const version_section_end = (const char**)&_version_info_section_end;
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

#ifdef CONFIG_BOARD_QCR373M3_EVB_QCR3XX_NP

#define QC7XX_NP_XIP_BASE               0x30000000U
#define QC7XX_GLB_BASE                  0x20000000U
#define QC7XX_GLB_BMX_CFG7_REG          (QC7XX_GLB_BASE + GLB_BMX_CFG7_OFFSET)
#define QC7XX_SF_CTRL_BASE              DT_REG_ADDR(DT_NODELABEL(flash0))
#define QC7XX_SF_ID0_OFFSET_REG \
	(QC7XX_SF_CTRL_BASE + SF_CTRL_SF_ID0_OFFSET_OFFSET)
#define QC7XX_SF_ID1_OFFSET_REG \
	(QC7XX_SF_CTRL_BASE + SF_CTRL_SF_ID1_OFFSET_OFFSET)
#define QC7XX_TZC_BASE                  0x20006000U
#define QC7XX_TZC_BMX_TZMID_REG \
	(QC7XX_TZC_BASE + TZC_SEC_TZC_BMX_TZMID_OFFSET)
#define QC7XX_TZC_BMX_TZMID_LOCK_REG \
	(QC7XX_TZC_BASE + TZC_SEC_TZC_BMX_TZMID_LOCK_OFFSET)
#define QC7XX_NP_RESET_CTRL_REG         0x24000020U
#define QC7XX_NP_RESET_ADDRESS_REG      0x24001144U
#define QC7XX_NP_RESET_BIT              BIT(8)
#define NP_START_ADDRESS (0xB0300000 - 0x11000)

#ifdef CONFIG_QCR373_MULTI_IMAGE
extern uint32_t __qcr373_start;
#endif

static void qc7xx_np_enable_access(void)
{
	/* Release slave protection and assign the NP master to TZC group 1. */
	sys_write32(0, QC7XX_GLB_BMX_CFG7_REG);
	sys_set_bits(QC7XX_TZC_BMX_TZMID_REG,
		     TZC_SEC_TZC_WMCU_TZMID_MSK |
		     TZC_SEC_TZC_WMCU_TZMID_SEL_MSK);
	sys_set_bits(QC7XX_TZC_BMX_TZMID_LOCK_REG,
		     TZC_SEC_TZC_WMCU_TZMID_LOCK_MSK);
}

static void qc7xx_np_start(uint32_t flash_offset, uint32_t reset_address)
{
	sys_write32(flash_offset, QC7XX_SF_ID1_OFFSET_REG);
	sys_write32(reset_address, QC7XX_NP_RESET_ADDRESS_REG);
	sys_clear_bits(QC7XX_NP_RESET_CTRL_REG, QC7XX_NP_RESET_BIT);
}

static void boot_up_np(void)
{
#ifdef CONFIG_QCR373_MULTI_IMAGE
	extern uint32_t __qc7xx_multi_images[];
	const uint32_t *np_descriptor = &__qc7xx_multi_images[4];
	uint32_t ap_offset = sys_read32(QC7XX_SF_ID0_OFFSET_REG);
	uint32_t np_offset = np_descriptor[2];

	if (np_descriptor[0] != 0x0000504e || np_descriptor[1] != 0 ||
	    np_offset == 0 || np_descriptor[3] <= np_offset) {
		printf("NP image descriptor is invalid\r\n");
		return;
	}

	/* The descriptor points at NP's header; XIP follows that header. */
	np_offset += 0x1000;
	printf("Boot NP image at flash offset 0x%08x\r\n",
	       ap_offset + np_offset);
	qc7xx_np_enable_access();
	qc7xx_np_start(ap_offset + np_offset, (uint32_t)&__qcr373_start);
#else
	uint32_t *addr = (uint32_t *)NP_START_ADDRESS;

	if(addr[0] != 0x504e4642){
		printf("Magic not correct at 0x%08x,read=%08x\r\n", NP_START_ADDRESS, addr[0]);
		return;
	}

	if(addr[176/4] != 0x80000000){
		printf("Magic not correct,read=%08x\r\n", addr[176/4]);
		return;
	}
	printf("Boot up NP firmware from 0x%08x\r\n", QC7XX_NP_XIP_BASE);
	/* AP & NP run in the different code */
	qc7xx_np_enable_access();
	qc7xx_np_start(0x300000 + 0x1000, QC7XX_NP_XIP_BASE);
#endif
}
#endif

void soc_late_init_hook(void)
{
	const char *version_str;
	const char **ctx = NULL;
	while ((version_str = hal_driver_version(&ctx))) {
		puts(version_str);
	}

#ifdef CONFIG_BOARD_QCR373M3_EVB_QCR3XX_NP
#ifdef CONFIG_PSRAM
    /* psram init */
    board_psram_x8_init();
#if defined(CONFIG_WIFI_QCR373)
	soc_multi_heap_psram_add();
#endif
#endif

	/* boot up NP */
	boot_up_np();
#endif
}
