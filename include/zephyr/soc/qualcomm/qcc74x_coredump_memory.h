/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_COREDUMP_MEMORY_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_COREDUMP_MEMORY_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *qcc74x_psram_malloc(size_t size);
void qcc74x_psram_free(void *ptr);
bool qcc74x_coredump_psram_ledger_overflowed(void);
size_t qcc74x_coredump_psram_ledger_active_count(void);

struct qcc74x_coredump_flash_budget {
	size_t region_data_bytes;
	size_t region_header_bytes;
	size_t fixed_payload_bytes;
	size_t planned_payload_bytes;
	size_t flash_header_bytes;
	size_t flash_write_padding_bytes;
	size_t required_flash_bytes;
	size_t slot_bytes;
	size_t remaining_bytes;
	size_t region_count;
	bool psram_ledger_overflow;
};

/**
 * @brief Calculate the Flash space required by the current QCC74x coredump.
 *
 * This checks the same QCC74x memory-region list used by the linker-RAM
 * coredump profile. A zero return means the complete planned dump fits in the
 * selected coredump Flash partition. The calculation is deliberately
 * fail-closed when the PSRAM ledger is incomplete or when another coredump
 * memory mode adds data that cannot be accounted for here.
 *
 * @param include_arch_record Reserve the RISC-V register record when true.
 * @param budget Filled with the calculated Flash-space breakdown.
 *
 * @retval 0 The complete dump fits in the Flash slot.
 * @retval -ENOSPC The complete dump exceeds the Flash slot.
 * @retval -EOVERFLOW The PSRAM allocation ledger is incomplete.
 * @retval -ENOTSUP Another enabled coredump memory mode cannot be budgeted.
 */
int qcc74x_coredump_flash_budget_check(
	bool include_arch_record, struct qcc74x_coredump_flash_budget *budget);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_COREDUMP_MEMORY_H_ */
