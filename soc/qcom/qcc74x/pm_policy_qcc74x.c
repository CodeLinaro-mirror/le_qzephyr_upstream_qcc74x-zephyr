/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15.h>

#define QCC74X_PM_POLICY_PDS15_SUBSTATE_ID 15U

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

#ifdef CONFIG_PM_NEED_ALL_DEVICES_IDLE
	if (pm_device_is_any_busy()) {
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
			qcc74x_pm_pds15_record_auto_reject(ticks);
			continue;
		}

		if (qcc74x_pm_pds15_can_auto_enter(ticks)) {
			return state;
		}
	}

	/* Fall back to the best non-PDS15 state allowed by the DTS ordering. */
	for (uint8_t i = 0U; i < num_cpu_states; i++) {
		const struct pm_state_info *state = &cpu_states[i];

		if (qcc74x_pm_policy_is_pds15(state)) {
			continue;
		}

		if (!qcc74x_pm_policy_state_usable(state, ticks)) {
			continue;
		}

		out_state = state;
	}

	return out_state;
}
