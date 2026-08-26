/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_PDS15_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_PDS15_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

struct qcc74x_pm_pds15_status {
	bool initialized;
	bool init_in_progress;
	bool auto_enabled;
	bool policy_lock_active;
	bool auto_entry_pending;
	bool auto_deadline_active;
	int init_rc;
	uint64_t timeout_us;
	uint64_t auto_timeout_us;
	uint64_t auto_deadline_us;
	uint32_t cycle_count;
};

#ifdef CONFIG_QCC74X_PM_PDS15
int qcc74x_pm_pds15_init(void);
int qcc74x_pm_pds15_policy_lock_set(bool lock);
bool qcc74x_pm_pds15_policy_lock_is_active(void);
int qcc74x_pm_pds15_auto_enable(bool enable);
int qcc74x_pm_pds15_auto_enable_for(uint64_t duration_us);
int qcc74x_pm_pds15_auto_arm_for(uint64_t duration_us, bool *out_release_pending);
void qcc74x_pm_pds15_auto_release_pending_lock(void);
bool qcc74x_pm_pds15_auto_is_enabled(void);
int qcc74x_pm_pds15_enter(uint8_t substate_id);
void qcc74x_pm_pds15_resume_complete(uint8_t substate_id);
bool qcc74x_pm_pds15_is_initialized(void);
void qcc74x_pm_pds15_get_status(struct qcc74x_pm_pds15_status *status);
bool qcc74x_pm_pds15_can_auto_enter(int32_t ticks);
void qcc74x_pm_pds15_record_auto_reject(int32_t ticks);
#else
static inline int qcc74x_pm_pds15_init(void)
{
	return -ENOTSUP;
}

static inline int qcc74x_pm_pds15_policy_lock_set(bool lock)
{
	ARG_UNUSED(lock);
	return -ENOTSUP;
}

static inline bool qcc74x_pm_pds15_policy_lock_is_active(void)
{
	return false;
}

static inline int qcc74x_pm_pds15_auto_enable(bool enable)
{
	ARG_UNUSED(enable);
	return -ENOTSUP;
}

static inline int qcc74x_pm_pds15_auto_enable_for(uint64_t duration_us)
{
	ARG_UNUSED(duration_us);
	return -ENOTSUP;
}

static inline int qcc74x_pm_pds15_auto_arm_for(uint64_t duration_us,
						bool *out_release_pending)
{
	ARG_UNUSED(duration_us);
	if (out_release_pending != NULL) {
		*out_release_pending = false;
	}
	return -ENOTSUP;
}

static inline void qcc74x_pm_pds15_auto_release_pending_lock(void)
{
}

static inline bool qcc74x_pm_pds15_auto_is_enabled(void)
{
	return false;
}

static inline int qcc74x_pm_pds15_enter(uint8_t substate_id)
{
	ARG_UNUSED(substate_id);
	return -ENOTSUP;
}

static inline void qcc74x_pm_pds15_resume_complete(uint8_t substate_id)
{
	ARG_UNUSED(substate_id);
}

static inline bool qcc74x_pm_pds15_is_initialized(void)
{
	return false;
}

static inline void qcc74x_pm_pds15_get_status(
	struct qcc74x_pm_pds15_status *status)
{
	if (status != NULL) {
		*status = (struct qcc74x_pm_pds15_status){ 0 };
	}
}

static inline bool qcc74x_pm_pds15_can_auto_enter(int32_t ticks)
{
	ARG_UNUSED(ticks);
	return false;
}

static inline void qcc74x_pm_pds15_record_auto_reject(int32_t ticks)
{
	ARG_UNUSED(ticks);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_PM_PDS15_H_ */
