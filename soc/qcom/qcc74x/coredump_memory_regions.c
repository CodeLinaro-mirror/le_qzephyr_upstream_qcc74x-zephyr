/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "coredump_memory_regions.h"

#define QCC74X_COREDUMP_INTERNAL_REGION_COUNT 4U
#define QCC74X_COREDUMP_REGION_COUNT                                           \
  (QCC74X_COREDUMP_INTERNAL_REGION_COUNT +                                     \
   CONFIG_QCC74X_COREDUMP_PSRAM_LEDGER_MAX_ENTRIES + 1U)

#define QCC74X_COREDUMP_RISCV_ARCH_BLOCK_BYTES (33U * sizeof(uint32_t))
#define QCC74X_COREDUMP_RISCV_ARCH_RECORD_BYTES                                 \
  (sizeof(struct coredump_arch_hdr_t) + QCC74X_COREDUMP_RISCV_ARCH_BLOCK_BYTES)

#if defined(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION) || \
    defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH)
#define QCC74X_COREDUMP_FLASH_CONTROLLER                                       \
  DT_PARENT(DT_PARENT(DT_NODELABEL(coredump_partition)))
#define QCC74X_COREDUMP_FLASH_WRITE_BYTES                                      \
  DT_PROP(QCC74X_COREDUMP_FLASH_CONTROLLER, write_block_size)
#endif

struct z_coredump_memory_region_t {
  uintptr_t start;
  uintptr_t end;
};

extern uint32_t __psram_heap_base;

struct z_coredump_memory_region_t
    z_coredump_memory_regions[QCC74X_COREDUMP_REGION_COUNT];

static void add_region(size_t *index, uintptr_t start, uintptr_t end) {
  if (end > start) {
    z_coredump_memory_regions[*index].start = start;
    z_coredump_memory_regions[*index].end = end;
    (*index)++;
  }
}

static bool budget_add(size_t *total, size_t amount) {
  if (amount > SIZE_MAX - *total) {
    return false;
  }

  *total += amount;
  return true;
}

void qcc74x_coredump_memory_regions_update(
    const struct qcc74x_coredump_psram_allocation *allocations,
    size_t allocation_count) {
  size_t index = 0U;

  add_region(&index, DT_REG_ADDR(DT_NODELABEL(sram0)),
             DT_REG_ADDR(DT_NODELABEL(sram0)) +
                 DT_REG_SIZE(DT_NODELABEL(sram0)));
  add_region(&index, DT_REG_ADDR(DT_NODELABEL(wifiram)),
             DT_REG_ADDR(DT_NODELABEL(wifiram)) +
                 DT_REG_SIZE(DT_NODELABEL(wifiram)));
  add_region(&index, DT_REG_ADDR(DT_NODELABEL(hbnram)),
             DT_REG_ADDR(DT_NODELABEL(hbnram)) +
                 DT_REG_SIZE(DT_NODELABEL(hbnram)));
#if defined(CONFIG_QCC74X_COREDUMP_SERIAL_FULL_PSRAM)
  add_region(&index, DT_REG_ADDR(DT_NODELABEL(psram)),
             DT_REG_ADDR(DT_NODELABEL(psram)) +
                 DT_REG_SIZE(DT_NODELABEL(psram)));
#else
  add_region(&index, DT_REG_ADDR(DT_NODELABEL(psram)),
             (uintptr_t)&__psram_heap_base);

  for (size_t allocation_index = 0U; allocation_index < allocation_count &&
                                     index < QCC74X_COREDUMP_REGION_COUNT - 1U;
       allocation_index++) {
    const struct qcc74x_coredump_psram_allocation *allocation =
        &allocations[allocation_index];

    if (allocation->active) {
      add_region(&index, allocation->address,
                 allocation->address + allocation->size);
    }
  }
#endif

  z_coredump_memory_regions[index] = (struct z_coredump_memory_region_t){0};
}

int qcc74x_coredump_flash_budget_check_internal(
    bool include_arch_record, struct qcc74x_coredump_flash_budget *budget) {
  size_t region_data_bytes = 0U;
  size_t region_count = 0U;
  size_t fixed_payload_bytes;
  size_t planned_payload_bytes;
  size_t flash_write_padding_bytes;
  size_t required_flash_bytes;

  if (budget == NULL) {
    return -EINVAL;
  }

  *budget = (struct qcc74x_coredump_flash_budget){0};

#if !defined(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION) && \
    !defined(CONFIG_QCC74X_COREDUMP_OTHER_SAFE_FLASH)
  return -ENOTSUP;
#elif defined(CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_MIN) ||                         \
    defined(CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_THREADS) ||                       \
    defined(CONFIG_DEBUG_COREDUMP_THREADS_METADATA) ||                          \
    defined(CONFIG_COREDUMP_DEVICE)
  return -ENOTSUP;
#else
  for (size_t index = 0U; index < QCC74X_COREDUMP_REGION_COUNT; index++) {
    const struct z_coredump_memory_region_t *region =
        &z_coredump_memory_regions[index];
    size_t region_size;

    if (region->end == 0U) {
      break;
    }

    if (region->end <= region->start) {
      return -EOVERFLOW;
    }

    region_size = region->end - region->start;
    if (!budget_add(&region_data_bytes, region_size)) {
      return -EOVERFLOW;
    }

    region_count++;
  }

  if (qcc74x_coredump_psram_ledger.overflow != 0U) {
    budget->psram_ledger_overflow = true;
    return -EOVERFLOW;
  }

  fixed_payload_bytes = sizeof(struct coredump_hdr_t);
  if (include_arch_record &&
      !budget_add(&fixed_payload_bytes,
                  QCC74X_COREDUMP_RISCV_ARCH_RECORD_BYTES)) {
    return -EOVERFLOW;
  }

  planned_payload_bytes = fixed_payload_bytes;
  if (!budget_add(&planned_payload_bytes,
                  region_count * sizeof(struct coredump_mem_hdr_t)) ||
      !budget_add(&planned_payload_bytes, region_data_bytes)) {
    return -EOVERFLOW;
  }

  flash_write_padding_bytes =
      ROUND_UP(planned_payload_bytes, QCC74X_COREDUMP_FLASH_WRITE_BYTES) -
      planned_payload_bytes;
  required_flash_bytes = QCC74X_COREDUMP_FLASH_WRITE_BYTES;
  if (!budget_add(&required_flash_bytes, planned_payload_bytes) ||
      !budget_add(&required_flash_bytes, flash_write_padding_bytes)) {
    return -EOVERFLOW;
  }

  budget->region_data_bytes = region_data_bytes;
  budget->region_header_bytes =
      region_count * sizeof(struct coredump_mem_hdr_t);
  budget->fixed_payload_bytes = fixed_payload_bytes;
  budget->planned_payload_bytes = planned_payload_bytes;
  budget->flash_header_bytes = QCC74X_COREDUMP_FLASH_WRITE_BYTES;
  budget->flash_write_padding_bytes = flash_write_padding_bytes;
  budget->required_flash_bytes = required_flash_bytes;
  budget->slot_bytes = DT_REG_SIZE(DT_NODELABEL(coredump_partition));
  budget->region_count = region_count;
  budget->remaining_bytes =
      budget->slot_bytes > required_flash_bytes
          ? budget->slot_bytes - required_flash_bytes
          : 0U;

  return required_flash_bytes <= budget->slot_bytes ? 0 : -ENOSPC;
#endif
}
