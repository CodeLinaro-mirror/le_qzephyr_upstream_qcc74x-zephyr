/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <zephyr/toolchain.h>
#include <zephyr/arch/cache.h>
#include <zephyr/arch/common/init.h>
#include <zephyr/arch/common/xip.h>
#include <zephyr/platform/hooks.h>
#include "soc.h"

#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
void soc_interrupt_init(void);
#endif

#ifdef CONFIG_PSRAM_COPY_CODE
/* The RAM copy helpers run before relocated libc is available. */
void arch_early_memcpy(void *dst, const void *src, size_t n)
{
	volatile uint8_t *d = dst;
	const volatile uint8_t *s = src;

	while (n--) {
		*d++ = *s++;
	}
}

void arch_early_memset(void *dst, int c, size_t n)
{
	volatile uint8_t *d = dst;

	while (n--) {
		*d++ = (uint8_t)c;
	}
}
#endif

static void qc7xx_nocache_data_copy(void)
{
	const volatile uint32_t *psrc;
	volatile uint32_t *pdest;

	extern uint32_t __nocache_ram_data_start__;
	extern uint32_t __nocache_ram_data_end__;
	extern uint32_t __nocache_ram_data_load_addr;

	psrc = &__nocache_ram_data_load_addr;
	pdest = &__nocache_ram_data_start__;

	for (; pdest < &__nocache_ram_data_end__;) {
		*pdest++ = *psrc++;
	}
}

static void qc7xx_wifi_bss_zero(void)
{
	volatile uint32_t *pdest;

	extern uint32_t __nocache_wifiram_bss_start__;
	extern uint32_t __nocache_wifiram_bss_end__;

	pdest = &__nocache_wifiram_bss_start__;

	for (; pdest < &__nocache_wifiram_bss_end__;) {
		*pdest++ = 0;
	}
}

#ifdef CONFIG_PSRAM
static void qc7xx_psram_data_copy(void)
{
	const volatile uint32_t *psrc;
	volatile uint32_t *pdest;

	extern uint32_t __psram_data_start__;
	extern uint32_t __psram_data_end__;
	extern uint32_t __psram_data_load_addr;

	psrc = &__psram_data_load_addr;
	pdest = &__psram_data_start__;

#ifdef CONFIG_QCC74X_PM_PDS1
	int rc = qcc74x_psram_check_config();

	/* An empty section can wait for the existing late controller init. */
	if (rc != 0 && !(rc == -EAGAIN && pdest == &__psram_data_end__)) {
		/* No console yet. Never copy into a mismatched region. */
		__asm__ volatile("csrci mstatus, 8" ::: "memory");
		for (;;) {
			__asm__ volatile("wfi");
		}
	}
#endif

	for (; pdest < &__psram_data_end__;) {
		*pdest++ = *psrc++;
	}
}
#endif

/* do just as arch/riscv/core/prep_c.c */
void start_load(void)
{
	soc_prep_hook();

	arch_bss_zero();
	arch_data_copy();

	qc7xx_nocache_data_copy();
	qc7xx_wifi_bss_zero();
#ifdef CONFIG_PSRAM
	qc7xx_psram_data_copy();
#endif
#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
	soc_interrupt_init();
#endif
#if CONFIG_ARCH_CACHE
	arch_cache_init();
#endif
}
