/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/arch/riscv/arch.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_diag.h>

#ifndef CONFIG_QCC74X_PM_PDS15_LOG_LEVEL
#define CONFIG_QCC74X_PM_PDS15_LOG_LEVEL LOG_LEVEL_NONE
#endif

LOG_MODULE_REGISTER(qcc74x_pm_power, CONFIG_QCC74X_PM_PDS15_LOG_LEVEL);

#define QCC74X_PM_PDS15_LOG_DBG(...)                                      \
	do {                                                                  \
		if (IS_ENABLED(CONFIG_QCC74X_PM_PDS15_DEBUG_LOG)) {           \
			LOG_DBG(__VA_ARGS__);                                \
		}                                                             \
	} while (0)

#ifdef CONFIG_QCC74X_PM_PDS15
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>
#endif

#ifdef CONFIG_QCC74X_PM_DIAGNOSTICS
static struct k_spinlock diag_lock;
static struct qcc74x_pm_diag_stats diag_stats;

void qcc74x_pm_diag_get(struct qcc74x_pm_diag_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	*stats = diag_stats;

	k_spin_unlock(&diag_lock, key);
}

void qcc74x_pm_diag_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag_stats = (struct qcc74x_pm_diag_stats){ 0 };

	k_spin_unlock(&diag_lock, key);
}

static void qcc74x_pm_diag_record_entry(enum pm_state state, uint8_t substate_id)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag_stats.state_set_calls++;
	diag_stats.last_state = (uint32_t)state;
	diag_stats.last_substate = substate_id;
	diag_stats.last_entry_cycle = sys_clock_cycle_get_64();
	if (state == PM_STATE_SUSPEND_TO_IDLE) {
		diag_stats.shallow_idle_calls++;
	} else if (state == PM_STATE_SUSPEND_TO_RAM) {
		diag_stats.deep_sleep_calls++;
	} else {
		diag_stats.unsupported_state_calls++;
	}

	k_spin_unlock(&diag_lock, key);
}

static void qcc74x_pm_diag_record_exit(enum pm_state state, uint8_t substate_id)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag_stats.exit_post_ops_calls++;
	diag_stats.last_state = (uint32_t)state;
	diag_stats.last_substate = substate_id;
	diag_stats.last_exit_cycle = sys_clock_cycle_get_64();

	k_spin_unlock(&diag_lock, key);
}

void qcc74x_pm_diag_record_deep_prepare(uint64_t timeout_us)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag_stats.deep_prepare_calls++;
	diag_stats.last_deep_requested_us = timeout_us;

	k_spin_unlock(&diag_lock, key);
}

void qcc74x_pm_diag_record_deep_skip(
	enum qcc74x_pm_deep_skip_reason reason)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	if (reason >= QCC74X_PM_DEEP_SKIP_REASON_COUNT) {
		reason = QCC74X_PM_DEEP_SKIP_REASON_NONE;
	}

	diag_stats.deep_skip_calls++;
	diag_stats.last_deep_skip_reason = (uint32_t)reason;
	diag_stats.deep_skip_reason_counts[reason]++;

	k_spin_unlock(&diag_lock, key);
}

void qcc74x_pm_diag_record_deep_result(int rc, uint64_t elapsed_us,
					 uint32_t wake_reason,
					 uint64_t gpio_wake_bits,
					 uint64_t gpio_wake_edge_bits)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag_stats.last_deep_return_code = rc;
	diag_stats.last_deep_elapsed_us = elapsed_us;
	diag_stats.last_wake_reason = wake_reason;
	diag_stats.last_gpio_wake_bits = gpio_wake_bits;
	diag_stats.last_gpio_wake_edge_bits = gpio_wake_edge_bits;
	if (gpio_wake_bits != 0U) {
		diag_stats.gpio_wake_count++;
	}
	if (rc == 0) {
		diag_stats.deep_resume_success++;
	} else {
		diag_stats.deep_resume_failures++;
	}

	k_spin_unlock(&diag_lock, key);
}
#endif /* CONFIG_QCC74X_PM_DIAGNOSTICS */

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
#ifdef CONFIG_QCC74X_PM_DIAGNOSTICS
	qcc74x_pm_diag_record_entry(state, substate_id);
#endif
	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		__asm__ volatile("fence iorw, iorw" ::: "memory");
		arch_cpu_atomic_idle(MSTATUS_IEN);
		break;
	case PM_STATE_SUSPEND_TO_RAM:
#ifdef CONFIG_QCC74X_PM_PDS15
		(void)qcc74x_pm_pds15_enter(substate_id);
		/* Return to the manager for UART RESUME before unmasking IRQs. */
		break;
#endif
		__asm__ volatile("fence iorw, iorw" ::: "memory");
		arch_cpu_atomic_idle(MSTATUS_IEN);
		break;
	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
#ifdef CONFIG_QCC74X_PM_DIAGNOSTICS
	qcc74x_pm_diag_record_exit(state, substate_id);
#endif

#ifdef CONFIG_QCC74X_PM_PDS15
	if (state == PM_STATE_SUSPEND_TO_RAM) {
		qcc74x_pm_pds15_resume_complete(substate_id);
	}
#endif

	if (state == PM_STATE_SUSPEND_TO_RAM) {
		irq_unlock(MSTATUS_IEN);
		return;
	}

	irq_unlock(MSTATUS_IEN);
}
