/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
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

static void qcr373_nocache_data_copy(void)
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

static void qcr373_wifi_data_copy(void)
{
	uint32_t *psrc, *pdest;

	extern uint32_t __nocache_wifiram_data_start__;
	extern uint32_t __nocache_wifiram_data_end__;
	extern uint32_t __nocache_wifiram_data_load_addr;

	psrc = &__nocache_wifiram_data_load_addr;
	pdest = &__nocache_wifiram_data_start__;

	for (; pdest < &__nocache_wifiram_data_end__;) {
		*pdest++ = *psrc++;
	}
}

static void qcr373_tcm_data_copy(void)
{
	uint32_t *psrc, *pdest;

	extern uint32_t __tcm_start;
	extern uint32_t __tcm_end;
	extern uint32_t __tcm_load_start;

	psrc = &__tcm_load_start;
	pdest = &__tcm_start;

	for (; pdest < &__tcm_end;) {
		*pdest++ = *psrc++;
	}
}

#ifdef CONFIG_PSRAM
static void qcr373_psram_data_copy(void)
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
#if defined(CONFIG_SOC_PREP_HOOK)
	soc_prep_hook();
#endif

	arch_bss_zero();
	arch_data_copy();

	qcr373_tcm_data_copy();
	qcr373_nocache_data_copy();
	qcr373_wifi_data_copy();
#ifdef CONFIG_PSRAM
	qcr373_psram_data_copy();
#endif
#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
	soc_interrupt_init();
#endif
#if CONFIG_ARCH_CACHE
	arch_cache_init();
#endif
}
