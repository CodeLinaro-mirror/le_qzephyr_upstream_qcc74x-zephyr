/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>
#include <zephyr/soc/qualcomm/qcc74x_gpio_pds15.h>
#include <zephyr/sys/atomic.h>

#include "pm_policy_qcc74x.h"
#include "pm_uart.h"

#define QCC74X_PM_POLICY_PDS15_SUBSTATE_ID 15U

static atomic_t idle_after_reject;

void qcc74x_pm_policy_entry_rejected(void)
{
	atomic_set(&idle_after_reject, 1);
}

static uint32_t qcc74x_pm_policy_min_residency_ticks(
	const struct pm_state_info *state)
{
	uint64_t min_us = (uint64_t)state->min_residency_us +
			  (uint64_t)state->exit_latency_us;

	if (min_us == 0U) {
		return 0U;
	}

	return k_us_to_ticks_ceil32(min_us);
}

static bool qcc74x_pm_policy_is_pds15(const struct pm_state_info *state)
{
	return (state->state == PM_STATE_SUSPEND_TO_RAM) &&
	       (state->substate_id == QCC74X_PM_POLICY_PDS15_SUBSTATE_ID);
}

static bool qcc74x_pm_policy_state_usable(const struct pm_state_info *state,
					  int32_t ticks)
{
	uint32_t min_ticks = qcc74x_pm_policy_min_residency_ticks(state);

	if ((ticks != K_TICKS_FOREVER) &&
	    ((ticks < 0) || ((uint32_t)ticks < min_ticks))) {
		return false;
	}

	return pm_policy_state_is_available(state->state, state->substate_id);
}

const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
	const struct pm_state_info *cpu_states;
	const struct pm_state_info *out_state = NULL;
	uint8_t num_cpu_states;
	/* This policy runs again only after the previous manager resume completed. */
	bool force_idle = atomic_set(&idle_after_reject, 0) != 0;

#ifdef CONFIG_PM_NEED_ALL_DEVICES_IDLE
	if (pm_device_is_any_busy()) {
		qcc74x_pm_pds15_record_auto_reject(ticks);
		return NULL;
	}
#endif

	num_cpu_states = pm_state_cpu_get_all(cpu, &cpu_states);

	/* Prefer qcc74x PDS15 explicitly; do not depend on DTS state order. */
	for (uint8_t i = 0U; i < num_cpu_states; i++) {
		const struct pm_state_info *state = &cpu_states[i];

		if (!qcc74x_pm_policy_is_pds15(state)) {
			continue;
		}

		if (!qcc74x_pm_policy_state_usable(state, ticks)) {
			continue;
		}

		if (force_idle || qcc74x_pm_uart_preflight() != 0 ||
		    gpio_qc7xx_pds15_preflight() != 0) {
			break;
		}

		if (qcc74x_pm_pds15_can_auto_enter(ticks)) {
			return state;
		}
	}

	/* Discard any request left over from a previous policy decision. */
	qcc74x_pm_pds15_record_auto_reject(ticks);

	/* Rejection must reach idle without another device suspend/resume pass. */
	for (uint8_t i = 0U; i < num_cpu_states; i++) {
		const struct pm_state_info *state = &cpu_states[i];

		if (qcc74x_pm_policy_is_pds15(state)) {
			continue;
		}
		if (state->state != PM_STATE_SUSPEND_TO_IDLE || !state->pm_device_disabled) {
			continue;
		}

		if (!qcc74x_pm_policy_state_usable(state, ticks)) {
			continue;
		}

		out_state = state;
	}

	return out_state;
}
