/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <limits.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/soc/qualcomm/qcc74x_pm_pds15_timer.h>
#include <zephyr/sys_clock.h>

#define DT_DRV_COMPAT riscv_machine_timer

#define MTIME_REG    DT_INST_REG_ADDR_BY_NAME(0, mtime)
#define MTIMECMP_REG DT_INST_REG_ADDR_BY_NAME(0, mtimecmp)
#define TIMER_IRQN   DT_INST_IRQN(0)

#ifndef CONFIG_RISCV_MACHINE_TIMER_SYSTEM_CLOCK_DIVIDER
#define CONFIG_RISCV_MACHINE_TIMER_SYSTEM_CLOCK_DIVIDER 0
#endif

#define CYC_PER_TICK \
	(uint32_t)(sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)

#define cycle_diff_t   unsigned long
#define CYCLE_DIFF_MAX (~(cycle_diff_t)0)

#define CYCLES_MAX_1 ((uint64_t)INT32_MAX * (uint64_t)CYC_PER_TICK)
#define CYCLES_MAX_2 ((uint64_t)CYCLE_DIFF_MAX)
#define CYCLES_MAX_3 MIN(CYCLES_MAX_1, CYCLES_MAX_2)
#define CYCLES_MAX_4 (CYCLES_MAX_3 / 2 + CYCLES_MAX_3 / 4)
#define CYCLES_MAX   (CYCLES_MAX_4 + LSB_GET(CYCLES_MAX_4))

struct qcc74x_pm_pds15_timer_bookkeeping {
	uint64_t last_count;
	/* Includes fractional ticks captured before the counter is reset. */
	uint64_t pending_cycles;
};

static struct qcc74x_pm_pds15_timer_bookkeeping qcc74x_timer_bookkeeping;

#if defined(CONFIG_TEST)
const int32_t z_sys_timer_irq_for_test = TIMER_IRQN;
#endif

static uintptr_t get_hart_mtimecmp(void)
{
	return MTIMECMP_REG + (arch_proc_id() * 8);
}

static void set_mtimecmp(uint64_t time)
{
#ifdef CONFIG_64BIT
	*(volatile uint64_t *)get_hart_mtimecmp() = time;
#else
	volatile uint32_t *r = (uint32_t *)get_hart_mtimecmp();

	r[1] = 0xffffffff;
	r[0] = (uint32_t)time;
	r[1] = (uint32_t)(time >> 32);
#endif
}

static uint64_t mtime(void)
{
#ifdef CONFIG_64BIT
	return *(volatile uint64_t *)MTIME_REG;
#else
	volatile uint32_t *r = (uint32_t *)MTIME_REG;
	uint32_t lo;
	uint32_t hi;

	do {
		hi = r[1];
		lo = r[0];
	} while (r[1] != hi);

	return (((uint64_t)hi) << 32) | lo;
#endif
}

static uint64_t unannounced_cycles(uint64_t now)
{
	return qcc74x_timer_bookkeeping.pending_cycles +
	       now - qcc74x_timer_bookkeeping.last_count;
}

static void capture_elapsed(uint64_t now)
{
	qcc74x_timer_bookkeeping.pending_cycles = unannounced_cycles(now);
	qcc74x_timer_bookkeeping.last_count = now;
}

static void arm_pending(uint64_t now)
{
	uint64_t pending = unannounced_cycles(now);

	/* Delivery waits until the PM core unmasks interrupts. */
	set_mtimecmp(now + (pending >= CYC_PER_TICK ?
			   0U : CYC_PER_TICK - pending));
}

static void timer_isr(const void *arg)
{
	ARG_UNUSED(arg);

	k_spinlock_key_t key = sys_clock_lock();
	uint64_t now = mtime();
	capture_elapsed(now);
	uint64_t ticks = qcc74x_timer_bookkeeping.pending_cycles / CYC_PER_TICK;

	do {
		int32_t chunk = MIN(ticks, (uint64_t)INT32_MAX);

		qcc74x_timer_bookkeeping.pending_cycles -=
			(uint64_t)chunk * CYC_PER_TICK;
		if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
			arm_pending(mtime());
		}
		sys_clock_announce_locked(chunk, key);
		ticks -= chunk;
		if (ticks != 0U) {
			key = sys_clock_lock();
		}
	} while (ticks != 0U);
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(idle);

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return;
	}

	uint64_t now = mtime();
	uint64_t delay;

	/* RESUME-time timeout insertion must not postpone pending announces. */
	if (qcc74x_timer_bookkeeping.pending_cycles >= CYC_PER_TICK) {
		set_mtimecmp(now);
		return;
	}
	if (ticks == K_TICKS_FOREVER) {
		delay = CYCLES_MAX;
	} else if (ticks <= 0) {
		delay = 0U;
	} else {
		/* Compare uses live MTIMER time, not accumulated sleep time. */
		uint64_t fraction = unannounced_cycles(now) % CYC_PER_TICK;

		delay = MIN((uint64_t)ticks * CYC_PER_TICK - fraction,
			    CYCLES_MAX);
	}
	set_mtimecmp(now + delay);
}

uint32_t sys_clock_elapsed(void)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0U;
	}

	/* The kernel timeout queue uses a signed 32-bit elapsed value. */
	return MIN(unannounced_cycles(mtime()) / CYC_PER_TICK,
		   (uint64_t)INT32_MAX);
}

uint32_t sys_clock_cycle_get_32(void)
{
	return ((uint32_t)mtime()) <<
	       CONFIG_RISCV_MACHINE_TIMER_SYSTEM_CLOCK_DIVIDER;
}

uint64_t sys_clock_cycle_get_64(void)
{
	return mtime() << CONFIG_RISCV_MACHINE_TIMER_SYSTEM_CLOCK_DIVIDER;
}

void qcc74x_pm_pds15_timer_post_wake_resume(void)
{
	k_spinlock_key_t key = sys_clock_lock();

	/* The old counter epoch is invalid until deep_sleep_rebase(). */
	set_mtimecmp(UINT64_MAX);
	irq_enable(TIMER_IRQN);

	sys_clock_unlock(key);
}

void qcc74x_pm_pds15_timer_flush_elapsed(void)
{
	k_spinlock_key_t key = sys_clock_lock();
	uint64_t now = mtime();

	capture_elapsed(now);
	/* Also leaves a due interrupt on the HAL entry-rejection path. */
	arm_pending(now);
	sys_clock_unlock(key);
}

void qcc74x_pm_pds15_timer_deep_sleep_rebase(uint64_t elapsed_us)
{
	k_spinlock_key_t key = sys_clock_lock();
	uint64_t now = mtime();

	/* Preserve the old epoch's fraction captured by flush_elapsed(). */
	qcc74x_timer_bookkeeping.pending_cycles +=
		k_us_to_cyc_floor64(elapsed_us);
	qcc74x_timer_bookkeeping.last_count = now;
	arm_pending(now);
	irq_enable(TIMER_IRQN);
	sys_clock_unlock(key);
}

static int sys_clock_driver_init(void)
{
	IRQ_CONNECT(TIMER_IRQN, 0, timer_isr, NULL, 0);
	uint64_t now = mtime();

	qcc74x_timer_bookkeeping.last_count = now;
	qcc74x_timer_bookkeeping.pending_cycles = now % CYC_PER_TICK;
	arm_pending(now);
	irq_enable(TIMER_IRQN);

	return 0;
}

#ifdef CONFIG_SMP
void smp_timer_init(void)
{
	arm_pending(mtime());
	irq_enable(TIMER_IRQN);
}
#endif

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2,
	 CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
