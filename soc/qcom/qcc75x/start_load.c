/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

extern void z_bss_zero(void);
extern void z_data_copy(void);

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

static void qc7xx_wifi_data_copy(void)
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
#if defined(CONFIG_SOC_PREP_HOOK)
	soc_prep_hook();
#endif

	z_bss_zero();
	z_data_copy();

	qc7xx_nocache_data_copy();
	qc7xx_wifi_data_copy();
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