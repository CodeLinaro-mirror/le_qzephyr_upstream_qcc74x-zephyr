/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/toolchain.h>
#include <zephyr/arch/cache.h>
#include <zephyr/arch/common/init.h>
#include <zephyr/arch/common/xip.h>
#include <zephyr/platform/hooks.h>

#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
void soc_interrupt_init(void);
#endif

static void qc7xx_nocache_data_copy(void)
{
	uint32_t *psrc, *pdest;

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
	uint32_t *pdest;

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
	uint32_t *psrc, *pdest;

	extern uint32_t __psram_data_start__;
	extern uint32_t __psram_data_end__;
	extern uint32_t __psram_data_load_addr;

	psrc = &__psram_data_load_addr;
	pdest = &__psram_data_start__;

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
