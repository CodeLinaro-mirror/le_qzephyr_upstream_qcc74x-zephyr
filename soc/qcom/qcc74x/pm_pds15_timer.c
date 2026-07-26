/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
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
	uint64_t last_ticks;
	uint64_t last_count;
	uint32_t last_elapsed;
	uint32_t reserved;
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

static void qcc74x_pm_pds15_timer_announce_locked(uint64_t ticks,
						  k_spinlock_key_t key)
{
	while (ticks > 0U) {
		int32_t chunk = (ticks > INT32_MAX) ? INT32_MAX : (int32_t)ticks;

		sys_clock_announce_locked(chunk, key);
		ticks -= chunk;
		if (ticks > 0U) {
			key = sys_clock_lock();
		}
	}
}

static void timer_isr(const void *arg)
{
	ARG_UNUSED(arg);

	k_spinlock_key_t key = sys_clock_lock();
	uint64_t now = mtime();
	uint64_t dcycles = now - qcc74x_timer_bookkeeping.last_count;
	uint32_t dticks = (cycle_diff_t)dcycles / CYC_PER_TICK;

	qcc74x_timer_bookkeeping.last_count +=
		(cycle_diff_t)dticks * CYC_PER_TICK;
	qcc74x_timer_bookkeeping.last_ticks += dticks;
	qcc74x_timer_bookkeeping.last_elapsed = 0U;

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		set_mtimecmp(qcc74x_timer_bookkeeping.last_count + CYC_PER_TICK);
	}

	sys_clock_announce_locked(dticks, key);
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(idle);

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return;
	}

	uint64_t cyc;

	if (ticks == K_TICKS_FOREVER) {
		cyc = qcc74x_timer_bookkeeping.last_count + CYCLES_MAX;
	} else {
		/*
		 * Why mtimecmp MUST be anchored on last_count (the live MTimer
		 * count) instead of last_ticks * CYC_PER_TICK.
		 *
		 * The driver keeps two bookkeeping values that upstream's
		 * riscv_machine_timer.c keeps locked together:
		 *   last_count = last MTimer counter value we aligned to
		 *   last_ticks = kernel tick counter (absolute uptime)
		 * Upstream they are always equal by CYC_PER_TICK because the
		 * MTimer never stops. Here PDS15 gates/resets the MTimer every
		 * deep sleep, so deep_sleep_rebase() must:
		 *   - advance last_ticks by the RTC-measured sleep interval
		 *     (absolute uptime must keep moving for k_uptime_get() /
		 *     k_work_schedule), and
		 *   - re-anchor last_count to the post-wake (reset) MTimer,
		 *     because every other function (sys_clock_elapsed, timer_isr,
		 *     flush_elapsed) computes time as (mtime() - last_count).
		 *
		 * That re-anchoring is physically forced: the MTimer cannot hold
		 * an "absolute since-boot" value across a reset. The two values
		 * therefore diverge permanently after the first PDS cycle, the
		 * gap being exactly the accumulated deep-sleep time (~tens of
		 * seconds). The upstream invariant last_count == last_ticks *
		 * CYC_PER_TICK no longer holds.
		 *
		 * Consequence of anchoring on last_ticks (the old code):
		 *   mtimecmp = (last_ticks + ...) * CYC   -> lives in the
		 *   "absolute uptime" domain, while the live mtime() lives in
		 *   the "reset" domain (a small value). The mtimer interrupt
		 *   only fires when mtime() >= mtimecmp, so the CPU waits for
		 *   mtime() to climb the whole domain gap = accumulated sleep
		 *   (~24s) before firing. A pure-tickless WFI that relied on
		 *   it (e.g. the BLE conn-param deferred_work) then fires tens
		 *   of seconds late.
		 *
		 * last_count is the only anchor that shares mtime()\'s domain
		 * (both reset together at each rebase), so
		 *   mtimecmp = last_count + (pending + ticks) * CYC
		 * makes (mtimecmp - mtime()) just the requested offset, with no
		 * drift regardless of how far last_ticks has diverged. When the
		 * invariant still holds (no PDS) this is bit-identical to the
		 * upstream formula.
		 *
		 * pending = sys_clock_elapsed() returns (mtime() - last_count) /
		 * CYC, i.e. the whole ticks already elapsed since last_count, so
		 * (pending + ticks) is the total offset from the live MTimer to
		 * the requested deadline.
		 */
		uint32_t pending = sys_clock_elapsed();

		cyc = qcc74x_timer_bookkeeping.last_count +
		      (uint64_t)(pending + (uint32_t)ticks) * CYC_PER_TICK;
		if ((cyc - qcc74x_timer_bookkeeping.last_count) > CYCLES_MAX) {
			cyc = qcc74x_timer_bookkeeping.last_count + CYCLES_MAX;
		}
	}
	set_mtimecmp(cyc);
}

uint32_t sys_clock_elapsed(void)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0U;
	}

	uint64_t now = mtime();
	uint64_t dcycles = now - qcc74x_timer_bookkeeping.last_count;
	uint32_t dticks = (cycle_diff_t)dcycles / CYC_PER_TICK;

	qcc74x_timer_bookkeeping.last_elapsed = dticks;

	return dticks;
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

	set_mtimecmp(qcc74x_timer_bookkeeping.last_count + CYC_PER_TICK);
	irq_enable(TIMER_IRQN);

	sys_clock_unlock(key);
}

void qcc74x_pm_pds15_timer_flush_elapsed(void)
{
	uint64_t dcycles;
	uint32_t dticks;

	k_spinlock_key_t key = sys_clock_lock();

	dcycles = mtime() - qcc74x_timer_bookkeeping.last_count;
	dticks = (cycle_diff_t)dcycles / CYC_PER_TICK;
	if (dticks != 0U) {
		qcc74x_timer_bookkeeping.last_count +=
			(cycle_diff_t)dticks * CYC_PER_TICK;
		qcc74x_timer_bookkeeping.last_ticks += dticks;
		qcc74x_timer_bookkeeping.last_elapsed = 0U;
		set_mtimecmp(qcc74x_timer_bookkeeping.last_count +
			     CYC_PER_TICK);
	}

	if (dticks == 0U) {
		sys_clock_unlock(key);
		return;
	}

	sys_clock_announce_locked(dticks, key);
}

void qcc74x_pm_pds15_timer_deep_sleep_rebase(uint64_t elapsed_us)
{
	uint64_t elapsed_ticks = k_us_to_ticks_floor64(elapsed_us);
	uint64_t elapsed_cycles = k_us_to_cyc_floor64(elapsed_us);
	uint64_t rem_cycles = elapsed_cycles - (elapsed_ticks * CYC_PER_TICK);
	uint64_t now;

	k_spinlock_key_t key = sys_clock_lock();

	now = mtime();
	qcc74x_timer_bookkeeping.last_ticks += elapsed_ticks;
	if (now >= rem_cycles) {
		qcc74x_timer_bookkeeping.last_count = now - rem_cycles;
	} else {
		qcc74x_timer_bookkeeping.last_count = now;
	}
	qcc74x_timer_bookkeeping.last_elapsed = 0U;
	set_mtimecmp(qcc74x_timer_bookkeeping.last_count + CYC_PER_TICK);
	irq_enable(TIMER_IRQN);

	if (elapsed_ticks == 0U) {
		sys_clock_unlock(key);
		return;
	}

	qcc74x_pm_pds15_timer_announce_locked(elapsed_ticks, key);
}

static int sys_clock_driver_init(void)
{
	IRQ_CONNECT(TIMER_IRQN, 0, timer_isr, NULL, 0);
	qcc74x_timer_bookkeeping.last_ticks = mtime() / CYC_PER_TICK;
	qcc74x_timer_bookkeeping.last_count =
		qcc74x_timer_bookkeeping.last_ticks * CYC_PER_TICK;
	set_mtimecmp(qcc74x_timer_bookkeeping.last_count + CYC_PER_TICK);
	irq_enable(TIMER_IRQN);

	return 0;
}

#ifdef CONFIG_SMP
void smp_timer_init(void)
{
	set_mtimecmp(qcc74x_timer_bookkeeping.last_count + CYC_PER_TICK);
	irq_enable(TIMER_IRQN);
}
#endif

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2,
	 CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
