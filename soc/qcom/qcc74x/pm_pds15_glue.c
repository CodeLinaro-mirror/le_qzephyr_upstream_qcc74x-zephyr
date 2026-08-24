/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>
#include <zephyr/sys/util.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15_timer.h>

#if defined(CONFIG_QCC74X_PM_PDS15_BLE)
#include <zephyr/bluetooth/bluetooth.h>
#include "btble_inc/btble_lib_api.h"
#endif

#include <compiler/compiler_ld.h>

#include "qc7xx_clock.h"
#include "qcc74x_clock.h"
#include "qcc74x_glb.h"
#include "qcc74x_sys.h"
#include "qc7xx_lp.h"
#include "qc7xx_uart.h"
#include "qcc74x_glb_gpio.h"
#include "qcc74x_gpio.h"

#ifndef CONFIG_QCC74X_PM_PDS15_LOG_LEVEL
#define CONFIG_QCC74X_PM_PDS15_LOG_LEVEL LOG_LEVEL_NONE
#endif

LOG_MODULE_REGISTER(qcc74x_pm_pds15_glue,
		    CONFIG_QCC74X_PM_PDS15_LOG_LEVEL);

#define QCC74X_PM_PDS15_LOG_DBG(...)                                      \
	do {                                                                  \
		if (IS_ENABLED(CONFIG_QCC74X_PM_PDS15_DEBUG_LOG)) {           \
			LOG_DBG(__VA_ARGS__);                                \
		}                                                             \
	} while (0)

extern int hardware_init(void);
extern int hardware_recovery_lp(void);
#if defined(CONFIG_WIFI)
extern int qc7xx_wifi_rf_resume_restore(void);
#endif

static void qcc74x_restore_uart_route_from_lp(void)
{
	/*
	 * SystemInit() resets the UART signal router to "unassigned" on cold
	 * boot before pinctrl binds TX/RX. PDS15 can lose that router state, so
	 * make the wake path follow the same reset-then-pinctrl sequence.
	 */
	QC7XX_WR_REG(GLB_BASE, GLB_UART_CFG1, 0xffffffff);
	QC7XX_WR_REG(GLB_BASE, GLB_UART_CFG2, 0x0000ffff);
}

static void qcc74x_restore_app_clocks_from_lp(void)
{
	uint32_t tmp_val = 0U;

	/*
	 * Mirror the vendor wl_ble_lp lp_exit() preconditioning step before
	 * running the generic recovery path. This is wake-only state repair,
	 * not a full cold boot.
	 */
	
	GLB_Set_MCU_System_CLK_Div(0, 3);
	CPU_Set_MTimer_CLK(ENABLE, QC7XX_MTIMER_SOURCE_CLOCK_MCU_CLK,
			   Clock_System_Clock_Get(QC7XX_SYSTEM_CLOCK_MCU_CLK) /
				   1000000U - 1U);
	
	tmp_val = BIT(GLB_CGEN_M_CPU_POS) | BIT(GLB_CGEN_M_DMA_POS) |
		  BIT(GLB_CGEN_M_SEC_POS) | BIT(GLB_CGEN_M_SDU_POS);
	QC7XX_WR_REG(GLB_BASE, GLB_CGEN_CFG0, tmp_val);
	
	tmp_val = BIT(GLB_CGEN_S1_RSVD0_POS) | BIT(GLB_CGEN_S1_EF_CTRL_POS) |
		  BIT(GLB_CGEN_S1_SF_CTRL_POS) | BIT(GLB_CGEN_S1_DMA_POS) |
		  BIT(GLB_CGEN_S1A_UART0_POS) | BIT(GLB_CGEN_S1A_UART1_POS) |
		  BIT(GLB_CGEN_S1_SEC_ENG_POS);
	QC7XX_WR_REG(GLB_BASE, GLB_CGEN_CFG1, tmp_val);

	tmp_val = BIT(GLB_CGEN_S2_WIFI_POS) |
		  BIT(GLB_CGEN_S1_EXT_EMI_MISC_POS) |
		  BIT(GLB_CGEN_S1_EXT_PIO_POS);
	QC7XX_WR_REG(GLB_BASE, GLB_CGEN_CFG2, tmp_val);
}

static int qcc74x_board_recovery_after_lp(void)
{
	int rc = hardware_recovery_lp();
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
	const struct device *const console_dev =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (rc != 0) {
		return rc;
	}

	qcc74x_restore_uart_route_from_lp();
	/*
	 * PDS15 wake resumes clocks and UART, but repeated LPFW copy/verify
	 * also needs the CPU RAM sysmap attributes restored so the 0x63xxxxxx
	 * cacheable aliases see the same OCRAM/WRAM windows as a cold boot.
	 */
	qc7xx_cpu_sysmap_init(false, false);

	if (!device_is_ready(console_dev)) {
		return -ENODEV;
	}

	rc = qc7xx_uart_force_resume(console_dev);
	if (rc != 0) {
		return rc;
	}
#endif

	return rc;
}

static void qcc74x_restore_kernel_timer_from_lp(void)
{
	/*
	 * Vendor wl_ble_lp does timer restore after board_recovery(). Keep the
	 * same ordering here, and leave elapsed-time compensation to the later
	 * deep_sleep_rebase() wrapper path.
	 */
	qcc74x_pm_pds15_timer_post_wake_resume();
}

static int qcc74x_restore_shell_uart_from_lp(void)
{
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)
	const struct device *const console_dev =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	
	if (!device_is_ready(console_dev)) {
		return -ENODEV;
	}
	/**/
	

	return qc7xx_uart_force_irq_rearm(console_dev);
#else
	return 0;
#endif
}

int qcc74x_pm_pds15_uart_late_resume(void)
{
	int shell_rc;

	shell_rc = qcc74x_restore_shell_uart_from_lp();
	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_resume: uart-only late resume shell_rc=%d",
	       shell_rc);

	return shell_rc;
}

/*
 * LPFW restore code rewrites mtvec to freertos_risc_v_trap_handler | 3.
 * In CLIC mode the mtvec base is alignment-constrained; if this shim lives in
 * normal .text, hardware can round the base down into the preceding function.
 * Keep the vendor ABI symbol in Zephyr's exception area and 64-byte aligned.
 */
__asm__(
	".section .exception.other.freertos_risc_v_trap_handler,\"ax\",%progbits\n\t"
	".balign 0x40\n\t"
	".globl freertos_risc_v_trap_handler\n\t"
	".type freertos_risc_v_trap_handler, %function\n"
	"freertos_risc_v_trap_handler:\n"
	"\tj    _isr_wrapper\n\t"
	".size freertos_risc_v_trap_handler, . - freertos_risc_v_trap_handler\n\t"
	".previous");

#define QCC74X_LPFW_DELAY_WIN_MAX 32U

static int32_t qcc74x_lpfw_abs_i32(int32_t val)
{
	return val >= 0 ? val : -val;
}

static int32_t qcc74x_lpfw_diff_i32(int32_t a, int32_t b)
{
	return a > b ? a - b : b - a;
}

/*
 * Keep the SDK long-division-free modulo helper. This function is also exposed
 * to LPFW through shared_func_p, so avoid pulling in runtime helpers while XIP
 * may still be unavailable.
 */
static uint32_t ATTR_TCM_SECTION qcc74x_lpfw_modulo64(uint64_t num,
						      uint32_t divisor)
{
	uint32_t high;
	uint32_t low;
	uint8_t bit_n = 32U;

	if (divisor == 0U) {
		return 0U;
	}

	high = (uint32_t)(num >> 32);
	low = (uint32_t)num;
	high %= divisor;

	do {
		bit_n -= 4U;
		high = (high << 4) | ((low >> bit_n) & 0x0FU);
		high %= divisor;
	} while (bit_n > 0U);

	return high;
}

static uint8_t ATTR_TCM_SECTION qcc74x_lpfw_delay_win_size(void)
{
	uint8_t size = iot2lp_para->bcn_delay_sliding_win_size;

	if (size > QCC74X_LPFW_DELAY_WIN_MAX) {
		size = QCC74X_LPFW_DELAY_WIN_MAX;
	}

	return size;
}

static void ATTR_TCM_SECTION qcc74x_lpfw_bubble_sort_i32(const int32_t *arr,
							 uint8_t *indices,
							 uint8_t size)
{
	for (uint8_t i = 0U; i < size; i++) {
		indices[i] = i;
	}

	if (size < 2U) {
		return;
	}

	for (uint8_t i = 0U; i < size - 1U; i++) {
		uint8_t swapped = 0U;

		for (uint8_t j = 0U; j < size - i - 1U; j++) {
			if (arr[indices[j]] > arr[indices[j + 1U]]) {
				uint8_t tmp = indices[j];

				indices[j] = indices[j + 1U];
				indices[j + 1U] = tmp;
				swapped = 1U;
			}
		}

		if (swapped == 0U) {
			break;
		}
	}
}

static int32_t ATTR_TCM_SECTION
lpfw_beacon_delay_sliding_win_update(int32_t beacon_delay_us)
{
	int32_t *buff = iot2lp_para->bcn_delay_sliding_win_buff;
	uint8_t size = qcc74x_lpfw_delay_win_size();

	if (buff == NULL || size == 0U) {
		return -EINVAL;
	}

	if (iot2lp_para->bcn_delay_sliding_win_point >= size) {
		iot2lp_para->bcn_delay_sliding_win_point = 0U;
	}

	buff[iot2lp_para->bcn_delay_sliding_win_point] = beacon_delay_us;
	iot2lp_para->bcn_delay_sliding_win_point++;
	if (iot2lp_para->bcn_delay_sliding_win_point >= size) {
		iot2lp_para->bcn_delay_sliding_win_point = 0U;
	}

	if (iot2lp_para->bcn_delay_sliding_win_status < size) {
		iot2lp_para->bcn_delay_sliding_win_status++;
	} else if (iot2lp_para->bcn_delay_sliding_win_status > size) {
		iot2lp_para->bcn_delay_sliding_win_status = size;
	}

	return 0;
}

int lpfw_recal_rc32k(uint64_t beacon_timestamp_now_us,
		     uint64_t rtc_timestamp_now_us,
		     uint32_t mode)
{
	uint64_t rtc_timestamp_last_us =
		iot2lp_para->last_rc32trim_stamp_rtc_us;
	uint64_t beacon_timestamp_last_us =
		iot2lp_para->last_rc32trim_stamp_beacon_us;
	int64_t rtc_us;
	int64_t beacon_us;
	int32_t diff_us;
	int32_t diff_ppm;
	int ret = 0;

	if (iot2lp_para->last_rc32trim_stamp_valid == 0U) {
		ret = -3;
		goto update_tsf;
	}

	if (iot2lp_para->rc32k_auto_cal_en == 0U) {
		ret = -4;
		goto update_tsf;
	}

	if (rtc_timestamp_now_us < rtc_timestamp_last_us ||
	    beacon_timestamp_now_us < beacon_timestamp_last_us) {
		ret = -5;
		if (mode == BEACON_STAMP_APP) {
			goto update_tsf;
		}
		return ret;
	}

	rtc_us = (int64_t)(rtc_timestamp_now_us - rtc_timestamp_last_us);
	beacon_us = (int64_t)(beacon_timestamp_now_us -
			      beacon_timestamp_last_us);

	if (rtc_us > 5LL * 1000LL * 1000LL ||
	    beacon_us > 5LL * 1000LL * 1000LL) {
		ret = 6;
		goto update_tsf;
	}

	if (rtc_us < 250LL * 1000LL || beacon_us < 250LL * 1000LL) {
		return 1;
	}

	diff_us = (int32_t)(rtc_us - beacon_us);
	diff_ppm = (int32_t)((int64_t)diff_us * 1000LL * 1000LL / beacon_us);

	if (qcc74x_lpfw_abs_i32(diff_ppm) > 10 * 1000) {
		ret = -7;
		if (mode == BEACON_STAMP_APP) {
			goto update_tsf;
		}
		return ret;
	}

	if (qc7xx_lp_get_32k_trim_ready() == 0) {
		ret = 2;
		if (qcc74x_lpfw_diff_i32(iot2lp_para->rtc32k_error_ppm,
					 diff_ppm) < 1500) {
			qc7xx_lp_set_32k_trim_ready(1);
			ret = 3;
		}
	}

	if (QC7XX_GET_REG_BITS_VAL(QC7XX_RD_REG(HBN_BASE, HBN_GLB),
				   HBN_F32K_SEL) != 0U) {
		iot2lp_para->rtc32k_error_ppm =
			(diff_ppm + iot2lp_para->rtc32k_error_ppm * 7) / 8;
		goto update_tsf;
	}

	iot2lp_para->rtc32k_error_ppm =
		(diff_ppm * 2 + iot2lp_para->rtc32k_error_ppm * 6) / 8;

	if (iot2lp_para->rtc32k_error_ppm > 3000 ||
	    iot2lp_para->rtc32k_error_ppm < -3000) {
		uint32_t rc32k_reg = *((volatile uint32_t *)0x2000F200);
		uint32_t rc32k_code = rc32k_reg >> 22;
		uint8_t adjusted = 0U;

		if (iot2lp_para->rtc32k_error_ppm > 3000 &&
		    rc32k_code < 0x3FFU) {
			rc32k_code++;
			iot2lp_para->rtc32k_error_ppm -= 1800;
			adjusted = 1U;
		} else if (iot2lp_para->rtc32k_error_ppm < -3000 &&
			   rc32k_code > 0U) {
			rc32k_code--;
			iot2lp_para->rtc32k_error_ppm += 1800;
			adjusted = 1U;
		}

		if (adjusted != 0U) {
			rc32k_reg &= ~0xffc00000U;
			rc32k_reg |= rc32k_code << 22;
			*((volatile uint32_t *)0x2000F200) = rc32k_reg;
			iot2lp_para->rc32k_fr_ext = (int32_t)rc32k_code;
		}
	}

update_tsf:
	iot2lp_para->last_rc32trim_stamp_rtc_us = rtc_timestamp_now_us;
	iot2lp_para->last_rc32trim_stamp_beacon_us = beacon_timestamp_now_us;
	iot2lp_para->last_rc32trim_stamp_valid = (uint8_t)mode;
	return ret;
}

int32_t ATTR_TCM_SECTION
lpfw_calculate_beacon_delay(uint64_t beacon_timestamp_us,
			    uint64_t rtc_timestamp_us,
			    uint32_t mode)
{
	int32_t beacon_delay_us;
	uint32_t beacon_interval_tu = iot2lp_para->beacon_interval_tu;
	uint32_t beacon_interval_us;
	uint8_t size = qcc74x_lpfw_delay_win_size();

	iot2lp_para->continuous_loss_cnt = 0U;
	iot2lp_para->bcn_loss_level = 0;

	iot2lp_para->last_beacon_stamp_rtc_valid = mode;
	iot2lp_para->last_beacon_stamp_rtc_us = rtc_timestamp_us;
	iot2lp_para->last_beacon_stamp_beacon_us = beacon_timestamp_us;

	if (beacon_interval_tu == 0U) {
		beacon_interval_tu = 100U;
	}
	beacon_interval_us = beacon_interval_tu * 1024U;
	beacon_delay_us = (int32_t)qcc74x_lpfw_modulo64(beacon_timestamp_us,
						       beacon_interval_us);

	if (iot2lp_para->bcn_delay_sliding_win_buff == NULL ||
	    size == 0U) {
		iot2lp_para->last_beacon_delay_us = beacon_delay_us;
		iot2lp_para->bcn_delay_offset = 0;
		return beacon_delay_us;
	}

	if (iot2lp_para->bcn_delay_sliding_win_status < size) {
		iot2lp_para->last_beacon_delay_us = 0;
		iot2lp_para->bcn_delay_offset = 0;

		if (iot2lp_para->bcn_delay_sliding_win_status == size - 1U) {
			uint8_t indices[QCC74X_LPFW_DELAY_WIN_MAX];

			lpfw_beacon_delay_sliding_win_update(beacon_delay_us);

			if (size >= 3U) {
				qcc74x_lpfw_bubble_sort_i32(
					iot2lp_para->bcn_delay_sliding_win_buff,
					indices, size);
				iot2lp_para->bcn_delay_offset =
					iot2lp_para->bcn_delay_sliding_win_buff
						[indices[2]];
			} else {
				iot2lp_para->bcn_delay_offset =
					beacon_delay_us;
			}

			for (uint8_t i = 0U; i < size; i++) {
				if (iot2lp_para->bcn_delay_sliding_win_buff[i] >
				    iot2lp_para->bcn_delay_offset) {
					iot2lp_para->bcn_delay_sliding_win_buff[i] -=
						iot2lp_para->bcn_delay_offset;
				} else {
					iot2lp_para->bcn_delay_sliding_win_buff[i] = 0;
				}
			}

			if (beacon_delay_us > iot2lp_para->bcn_delay_offset) {
				beacon_delay_us -=
					iot2lp_para->bcn_delay_offset;
			} else {
				beacon_delay_us = 0;
			}
			iot2lp_para->last_beacon_delay_us = beacon_delay_us;
		} else {
			lpfw_beacon_delay_sliding_win_update(beacon_delay_us);
		}

		return beacon_delay_us;
	}

	if (beacon_delay_us < iot2lp_para->bcn_delay_offset) {
		if (iot2lp_para->bcn_delay_offset - beacon_delay_us < 5000) {
			uint32_t correction =
				(uint32_t)(iot2lp_para->bcn_delay_offset -
					   beacon_delay_us) /
				2U;

			if (iot2lp_para->bcn_delay_offset > (int32_t)correction) {
				iot2lp_para->bcn_delay_offset -=
					(int32_t)correction;
			} else {
				iot2lp_para->bcn_delay_offset = 0;
			}
		}
		beacon_delay_us = 0;
	} else {
		beacon_delay_us -= iot2lp_para->bcn_delay_offset;
		iot2lp_para->bcn_delay_offset +=
			(int32_t)(beacon_interval_tu * iot2lp_para->dtim_num /
				  100U);
	}

	lpfw_beacon_delay_sliding_win_update(beacon_delay_us);
	iot2lp_para->last_beacon_delay_us = beacon_delay_us;

	return beacon_delay_us;
}

int32_t ATTR_TCM_SECTION lpfw_beacon_delay_sliding_win_get_average(void)
{
	int32_t average = 0;
	uint8_t size = qcc74x_lpfw_delay_win_size();
	uint8_t data_num;

	if (iot2lp_para->bcn_delay_sliding_win_buff == NULL ||
	    size == 0U ||
	    iot2lp_para->bcn_delay_sliding_win_status < size / 3U) {
		return -2;
	}

	if (iot2lp_para->bcn_delay_sliding_win_status < size) {
		data_num = iot2lp_para->bcn_delay_sliding_win_status;
	} else {
		data_num = size;
	}

	if (data_num == 0U) {
		return -2;
	}

	for (uint8_t i = 0U; i < data_num; i++) {
		average += iot2lp_para->bcn_delay_sliding_win_buff[i];
	}

	return (int32_t)(average / data_num);
}

void lp_hook_pre_user(void *arg)
{
	ARG_UNUSED(arg);
}
void lp_hook_pre_sleep(iot2lp_para_t *arg)
{
	ARG_UNUSED(arg);
}

void lp_hook_post_sys(iot2lp_para_t *arg)
{
	int board_recovery_rc = 0;
	int wifi_rf_rc = 0;
	int shell_rc = 0;

	ARG_UNUSED(arg);

	qcc74x_restore_app_clocks_from_lp();

	board_recovery_rc = qcc74x_board_recovery_after_lp();

#if defined(CONFIG_WIFI)
	wifi_rf_rc = qc7xx_wifi_rf_resume_restore();
#endif

#if defined(CONFIG_QCC74X_PM_PDS15_BLE)
	/* Restore BLE controller registers after RF restore. Skip when
	 * bt_enable() hasn't completed — controller state isn't initialised
	 * yet so there is nothing to restore.
	 */
	if (bt_is_ready()) {
		btble_controller_sleep_restore();
		QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_resume: ble restored");
	}
#endif
	qcc74x_restore_kernel_timer_from_lp();

	shell_rc = qcc74x_restore_shell_uart_from_lp();

	/*  re-arm JTAG pins (GPIO0-3 -> JTAG function) after PDS15
	 * wake. PDS15 resets the GPIO pin mux, which is why JTAG/GDB is lost after
	 * the first sleep. Restoring GPIO0/1/2/3 to GPIO_FUN_JTAG lets the CH347
	 * JTAG adapter re-attach so GDB can inspect post-wake CPU state. Uses the
	 * romapi GLB_GPIO_Func_Init (already linked); no .c compile needed. */
	{
		uint8_t jtag_pins[4] = {GLB_GPIO_PIN_0, GLB_GPIO_PIN_1,
					GLB_GPIO_PIN_2, GLB_GPIO_PIN_3};
		GLB_GPIO_Func_Init(GPIO_FUN_JTAG, jtag_pins, 4);
	}

	QCC74X_PM_PDS15_LOG_DBG("qcc74x_pds15_resume: post_sys done board_rc=%d wifi_rf_rc=%d shell_rc=%d",
	       board_recovery_rc, wifi_rf_rc, shell_rc);
}

