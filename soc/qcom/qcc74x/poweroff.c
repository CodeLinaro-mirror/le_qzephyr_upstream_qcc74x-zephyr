// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/platform/hooks.h>
#include <zephyr/soc/qualcomm/qcc74x_hbn.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/util.h>

#include <qc7xx_hbn_system.h>
#include <qc7xx_lp.h>
#include <qcc74x_common.h>
#include <qcc74x_hbn.h>
#include <qcc74x_pm.h>
#include <hardware/hbn_reg.h>

#define QCC74X_HBN_RTC_FREQUENCY_HZ 32768ULL
#define QCC74X_HBN_RTC_COUNTER_TOP  BIT64(40)
#define QCC74X_HBN_PATTERN          0x55AAAA55U

BUILD_ASSERT(IOT2BOOT2_PARA_ADDR == 0x20010300U);
BUILD_ASSERT(sizeof(iot2boot2_para_t) == 56U);
BUILD_ASSERT(offsetof(iot2boot2_para_t, hbn_sleep_period) == 8U);
BUILD_ASSERT(offsetof(iot2boot2_para_t, hbn_level) == 16U);
BUILD_ASSERT(offsetof(iot2boot2_para_t, wkup_rtc_cnt) == 32U);
BUILD_ASSERT(offsetof(iot2boot2_para_t, wdt_pattern) == 40U);
BUILD_ASSERT(IOT2BOOT2_PARA_ADDR + sizeof(iot2boot2_para_t) <= 0x20010400U);

struct qcc74x_hbn_state {
	uint64_t sleep_cycles;
	struct qc7xx_hbn_acomp_config acomp;
	bool acomp_enabled;
	bool configured;
};

static struct qcc74x_hbn_state hbn_state;
static struct qcc74x_hbn_wake_info hbn_wake_info;
static struct k_spinlock hbn_lock;

int qcc74x_hbn_configure(const struct qcc74x_hbn_config *config)
{
	struct qc7xx_hbn_acomp_config hal_acomp = {0};
	uint64_t sleep_cycles;
	k_spinlock_key_t key;

	if (config == NULL) {
		return -EINVAL;
	}

	if (config->acomp.enabled) {
		hal_acomp.input_pin = config->acomp.input_pin;
		hal_acomp.trigger = config->acomp.trigger;
		if ((qc7xx_hbn_acomp_select_id(hal_acomp.input_pin, &hal_acomp.id) != 0) ||
		    (qc7xx_hbn_acomp_validate(&hal_acomp) != 0)) {
			return -EINVAL;
		}
	}

	if ((config->timeout_ms == 0U) && !config->acomp.enabled) {
		return -EINVAL;
	}

	if (config->timeout_ms > UINT64_MAX / QCC74X_HBN_RTC_FREQUENCY_HZ) {
		return -ERANGE;
	}

	sleep_cycles =
		(config->timeout_ms * QCC74X_HBN_RTC_FREQUENCY_HZ) / 1000ULL;
	if (sleep_cycles >= QCC74X_HBN_RTC_COUNTER_TOP) {
		return -ERANGE;
	}

	key = k_spin_lock(&hbn_lock);
	hbn_state.sleep_cycles = sleep_cycles;
	hbn_state.acomp = hal_acomp;
	hbn_state.acomp_enabled = config->acomp.enabled;
	hbn_state.configured = true;
	k_spin_unlock(&hbn_lock, key);

	return 0;
}

static void qcc74x_hbn_wake_snapshot(void)
{
	const iot2boot2_para_t *boot2 = iot2boot2_para;
	struct qcc74x_hbn_wake_info info = {0};

	info.raw_hbn_status = HBN_Get_Status_Flag();
	info.valid = boot2->hbn_pattern == QCC74X_HBN_PATTERN;
	/* Boot2 publishes this flag after the supported HBN0 cold-boot wake. */
	info.woke_from_hbn = info.raw_hbn_status == HBN_STATUS_WAKEUP_FLAG;

	if (info.valid) {
		info.boot2_return = boot2->hal_boot2_lp_ret;
		info.wakeup_reason = boot2->wakeup_reason;
		info.requested_sleep_cycles = boot2->hbn_sleep_period;
		info.rtc_wakeup_count = boot2->wkup_rtc_cnt;
		info.wakeup_io_bits = boot2->wkup_io_bits;
		info.wakeup_io_edge_bits = boot2->wkup_io_edge_bits;
		info.wakeup_acomp_bits = boot2->wkup_acomp_bits;
		info.wakeup_acomp_edge_bits = boot2->wkup_acomp_edge_bits;
	}

	hbn_wake_info = info;

	/*
	 * Boot2 has already consumed this one-shot marker for the current boot.
	 * Clear it before normal application execution so a later software or
	 * watchdog reset cannot be misclassified as another HBN wake.
	 */
	HBN_Set_Status_Flag(0U);
}

void soc_early_init_hook(void)
{
	qcc74x_hbn_wake_snapshot();
}

int qcc74x_hbn_wake_info_get(struct qcc74x_hbn_wake_info *info)
{
	if (info == NULL) {
		return -EINVAL;
	}

	*info = hbn_wake_info;
	return 0;
}

void qcc74x_hbn_wake_info_clear(void)
{
	memset((void *)IOT2BOOT2_PARA_ADDR, 0, sizeof(iot2boot2_para_t));
	HBN_Set_Status_Flag(0U);
	hbn_wake_info = (struct qcc74x_hbn_wake_info){0};
}

FUNC_NORETURN void z_sys_poweroff(void)
{
	struct qc7xx_hbn_acomp_config hal_acomp;
	struct qcc74x_hbn_state state;
	k_spinlock_key_t key;

	key = k_spin_lock(&hbn_lock);
	state = hbn_state;
	hbn_state.configured = false;
	k_spin_unlock(&hbn_lock, key);

	if (!state.configured) {
		k_panic();
		CODE_UNREACHABLE;
	}

	qc7xx_hbn_boot2_init(0U, 0U, 0U, 0U);
	qc7xx_hbn_entry_begin();
	qc7xx_hbn_acomp_disable_all();
	if (state.acomp_enabled) {
		hal_acomp = state.acomp;
		if (qc7xx_hbn_acomp_apply(&hal_acomp) != 0) {
			k_panic();
			CODE_UNREACHABLE;
		}
	}
	qc7xx_hbn_system_enter(PM_HBN_LEVEL_0, state.sleep_cycles);

	CODE_UNREACHABLE;
}
