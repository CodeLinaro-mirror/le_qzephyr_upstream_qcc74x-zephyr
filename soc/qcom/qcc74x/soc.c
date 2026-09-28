/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <zephyr/logging/log.h>
#include "qc7xx_efuse.h"
#include "qc7xx_ef_ctrl.h"
#include "hardware/ef_data_reg.h"
#include "qc7xx_gpio.h"
#include "qcc74x_glb.h"
#include "qcc74x_psram.h"
#include "qcc74x_tzc_sec.h"
#include "soc_mem.h"
#include "soc.h"

LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);

#ifdef CONFIG_QCC74X_PM_PDS1
static bool psram_retention_ready;

bool qcc74x_psram_retention_ready(void)
{
	return psram_retention_ready;
}
#endif

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

#ifdef CONFIG_QCC74X_PM_PDS1
/* Called before the cold-boot PSRAM copy. */
int ATTR_TCM_SECTION qcc74x_psram_check_config(void)
{
	uint32_t fuse;
	uint8_t psram_info;
	uint32_t size;
	uint32_t ctrl;
	uint32_t density;

	/* Read the capacity field without running the process-corner test. */
	qc7xx_ef_ctrl_read_direct(NULL, EF_DATA_EF_WIFI_MAC_HIGH_OFFSET,
				 &fuse, 1, 1);
	psram_info = (fuse >> 24) & 3U;
	if (psram_info < 1U || psram_info > 3U) {
		return -ENOTSUP;
	}
	size = 0x400000U << (psram_info - 1U);
	if (size != CONFIG_PSRAM_LENGTH || size != DT_REG_SIZE(PSRAM_NODE)) {
		return -EINVAL;
	}
	if (!QCC74X_PSRAM_INIT_DONE) {
		return -EAGAIN;
	}
	ctrl = QC7XX_RD_REG(PSRAM_CTRL_BASE, PSRAM_CONFIGURE);
	density = QC7XX_GET_REG_BITS_VAL(
		QC7XX_RD_REG(PSRAM_CTRL_BASE, PSRAM_MANUAL_CONTROL2),
		PSRAM_REG_ADDR_MASK);
	if (QC7XX_GET_REG_BITS_VAL(ctrl, PSRAM_REG_VENDOR_SEL) !=
		PSRAM_CTRL_VENDOR_WINBOND ||
	    QC7XX_GET_REG_BITS_VAL(ctrl, PSRAM_REG_X16_MODE) !=
		PSRAM_CTRL_X8_MODE ||
	    density != (size / 0x100000U) - 1U) {
		return -EINVAL;
	}
	return 0;
}

static void psram_retention_configure(uint8_t psram_info)
{
	/* Winbond x8 ID0 values from the BL616 board initializer. */
	static const uint16_t ids[] = {0U, 0x005fU, 0x0c86U, 0x0c96U};
	uint16_t id = 0;
	uint16_t cr1 = 0;
	uint32_t saved;
	uint32_t pasr = UINT32_MAX;
	uint32_t pasr_mask;
	unsigned int key;
	QC7XX_Err_Type rc;

	psram_retention_ready = false;
	if (psram_info < 1U || psram_info >= ARRAY_SIZE(ids) ||
	    qcc74x_psram_check_config() != 0) {
		LOG_ERR("PDS1 PSRAM mismatch: psram_info=%u length=0x%x",
			psram_info, CONFIG_PSRAM_LENGTH);
		return;
	}
	/* A PSRAM-resident ISR must not run while the controller is in manual mode. */
	key = irq_lock();
	rc = PSram_Ctrl_Winbond_Read_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_ID0, &id);
	irq_unlock(key);
	if (rc != SUCCESS || id != ids[psram_info]) {
		LOG_ERR("PDS1 PSRAM ID mismatch: got 0x%04x expected 0x%04x",
			id, ids[psram_info]);
		return;
	}
	/* Preserve Boot2's controller/CR0 settings; only change CR1 refresh. */
	saved = QC7XX_RD_REG(PSRAM_CTRL_BASE, PSRAM_WINBOND_PSRAM_CONFIGURE);
	PSRAM_Winbond_Cfg_Type cfg = {
		.rst = DISABLE,
		.clockType = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_MCLK_TYPE),
		.inputPowerDownMode = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_IPD),
		.hybridSleepMode = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_HYBRID_SLP),
		.linear_dis = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_LINEAR_DIS),
		.PASR = PSRAM_PARTIAL_REFRESH_FULL,
		.disDeepPowerDownMode = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_DPD_DIS),
		.fixedLatency = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_FIX_LATENCY),
		.burstLen = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_BURST_LENGTH),
		.burstType = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_HYBRID_EN),
		.latency = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_LATENCY),
		.driveStrength = QC7XX_GET_REG_BITS_VAL(saved, PSRAM_REG_WB_DRIVE_ST),
	};

	key = irq_lock();
	psram_retention_ready =
		PSram_Ctrl_Winbond_Write_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR1,
					    &cfg) == SUCCESS &&
		PSram_Ctrl_Winbond_Read_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR1, &cr1) == SUCCESS;
	irq_unlock(key);
	if (psram_retention_ready) {
		/* The 8/16 MiB CR1[1:0] refresh interval is not part of PASR. */
		pasr_mask = psram_info == 1U ? GENMASK(2, 0) : GENMASK(4, 2);
		pasr = QC7XX_GET_REG_BITS_VAL(
			QC7XX_RD_REG(PSRAM_CTRL_BASE, PSRAM_WINBOND_PSRAM_STATUS),
			PSRAM_STS_WB_PASR);
		psram_retention_ready = (cr1 & pasr_mask) == 0U && (pasr & pasr_mask) == 0U;
	}
	LOG_INF("PSRAM retention ready=%u size=%u MiB ID0=0x%04x CR1=0x%04x PASR=0x%x",
		psram_retention_ready, CONFIG_PSRAM_LENGTH / 0x100000U,
		id, cr1, pasr);
}
#endif

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

static int soc_psram_init(void)
{
	qc7xx_efuse_device_info_type device_info = {0};

	qc7xx_efuse_get_device_info(&device_info);
	if (device_info.psram_info == 0) {
		LOG_ERR("This chip has no psram, please disable CONFIG_PSRAM\r\n");
		while (1) {
		}
	}

	if (QCC74X_PSRAM_INIT_DONE == 0) {
#ifdef CONFIG_PSRAM_COPY_CODE
		/* start_load() has already copied this routine to PSRAM. Reconfiguring
		 * the controller here would invalidate the executing image.
		 */
		LOG_ERR("PSRAM was not initialized before runtime code copy\r\n");
		while (1) {
		}
#else
		printf("psram init\r\n");
		board_psram_x8_init(device_info.psram_info);
		Tzc_Sec_PSRAMB_Access_Release();
#endif
	} else {
		printf("psram already init\r\n");
	}

#ifdef CONFIG_QCC74X_PM_PDS1
	psram_retention_configure(device_info.psram_info);
	if (!psram_retention_ready) {
		/* Stop before any allocator can publish invalid external memory. */
		k_panic();
	}
#endif
	return 0;
}

SYS_INIT(soc_psram_init, PRE_KERNEL_1, 0);
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

#ifdef CONFIG_QCC74X_PM_PDS1
	psram_retention_configure(device_info.psram_info);
	if (!psram_retention_ready) {
		/* Never publish a heap before validating PSRAM access. */
		return;
	}
#endif
	soc_multi_heap_psram_add();
#endif
}
