/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCC74X_COREDUMP_MEMORY_REGIONS_H_
#define QCC74X_COREDUMP_MEMORY_REGIONS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/soc/qualcomm/qcc74x_coredump_memory.h>

struct qcc74x_coredump_psram_allocation {
  uintptr_t address;
  size_t size;
  uint32_t generation;
  bool active;
};

struct qcc74x_coredump_psram_ledger {
  uint32_t magic;
  uint16_t version;
  uint16_t entry_count;
  uint32_t generation;
  uint32_t overflow;
  uint32_t active_count;
  struct qcc74x_coredump_psram_allocation
      entries[CONFIG_QCC74X_COREDUMP_PSRAM_LEDGER_MAX_ENTRIES];
};

extern struct qcc74x_coredump_psram_ledger qcc74x_coredump_psram_ledger;

void qcc74x_coredump_memory_regions_update(
    const struct qcc74x_coredump_psram_allocation *allocations,
    size_t allocation_count);

int qcc74x_coredump_flash_budget_check_internal(
    bool include_arch_record, struct qcc74x_coredump_flash_budget *budget);

#endif /* QCC74X_COREDUMP_MEMORY_REGIONS_H_ */
