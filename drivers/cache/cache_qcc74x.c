/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/cache.h>

#include <csi_core.h>
#include <qc7xx_core.h>
#include <qc7xx_l1c.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cache_qcc74x, CONFIG_CACHE_LOG_LEVEL);

void cache_data_enable(void)
{
	qc7xx_l1c_dcache_enable();
}

void cache_data_disable(void)
{
	qc7xx_l1c_dcache_disable();
}

void cache_instr_enable(void)
{
	qc7xx_l1c_icache_enable();
}

void cache_instr_disable(void)
{
	qc7xx_l1c_icache_disable();
}

int cache_data_flush_all(void)
{
	qc7xx_l1c_dcache_clean_all();
	return 0;
}

int cache_instr_flush_all(void)
{
	return -ENOTSUP;
}

int cache_data_invd_all(void)
{
	/* For invalid, call ASM directly instead of hal function, to avoid issues with call stack
	 */
	csi_dcache_invalid();
	return 0;
}

int cache_instr_invd_all(void)
{
	qc7xx_l1c_icache_invalid_all();
	return 0;
}

int cache_data_flush_and_invd_all(void)
{
	csi_dcache_clean_invalid();
	return 0;
}

int cache_instr_flush_and_invd_all(void)
{
	return -ENOTSUP;
}

int cache_data_flush_range(void *addr, size_t size)
{
	if (qc7xx_check_cache_addr(addr)) {
		qc7xx_l1c_dcache_clean_range(addr, size);
		return 0;
	}

	LOG_ERR("Addr 0x%lx is not a valid cache address", (uintptr_t)addr);
	return -EINVAL;
}

int cache_instr_flush_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

int cache_data_invd_range(void *addr, size_t size)
{
	if (qc7xx_check_cache_addr(addr)) {
		csi_dcache_invalid_range(addr, size);
		return 0;
	}

	LOG_ERR("Addr 0x%lx is not a valid cache address", (uintptr_t)addr);
	return -EINVAL;
}

int cache_instr_invd_range(void *addr, size_t size)
{
	if (qc7xx_check_cache_addr(addr)) {
		qc7xx_l1c_icache_invalid_range(addr, size);
		return 0;
	}

	LOG_ERR("Addr 0x%lx is not a valid cache address", (uintptr_t)addr);
	return -EINVAL;
}

int cache_data_flush_and_invd_range(void *addr, size_t size)
{
	if (qc7xx_check_cache_addr(addr)) {
		csi_dcache_clean_invalid_range(addr, size);
		return 0;
	}

	LOG_ERR("Addr 0x%lx is not a valid cache address", (uintptr_t)addr);
	return -EINVAL;
}

int cache_instr_flush_and_invd_range(void *addr, size_t size)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
	return -ENOTSUP;
}
