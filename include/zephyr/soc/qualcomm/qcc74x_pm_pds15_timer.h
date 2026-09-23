/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_SOC_QUALCOMM_QCC74X_PM_PDS15_TIMER_H_
#define ZEPHYR_INCLUDE_ZEPHYR_SOC_QUALCOMM_QCC74X_PM_PDS15_TIMER_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_QCC74X_PM_PDS15_SYSTEM_TIMER)
void qcc74x_pm_pds15_timer_post_wake_resume(void);
/* IRQ-locked PM helpers only account time; the timer ISR dispatches it. */
void qcc74x_pm_pds15_timer_flush_elapsed(void);
void qcc74x_pm_pds15_timer_deep_sleep_rebase(uint64_t elapsed_us);
#else
static inline void qcc74x_pm_pds15_timer_post_wake_resume(void)
{
}

static inline void qcc74x_pm_pds15_timer_flush_elapsed(void)
{
}

static inline void qcc74x_pm_pds15_timer_deep_sleep_rebase(uint64_t elapsed_us)
{
	ARG_UNUSED(elapsed_us);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_SOC_QUALCOMM_QCC74X_PM_PDS15_TIMER_H_ */
