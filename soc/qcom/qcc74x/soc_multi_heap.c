/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/soc/qualcomm/qcc74x_coredump_memory.h>
#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/multi_heap.h>
#include <zephyr/sys/sys_heap.h>
#if defined(CONFIG_MBEDTLS)
#include <mbedtls/platform.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#if defined(CONFIG_COMMON_LIBC_MALLOC)
#include <sys_malloc.h>
#endif
#include "soc_mem.h"
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
#include "coredump_memory_regions.h"
#endif

#define SOC_MULTI_HEAP_MAX_REGIONS 3U
#define SOC_MULTI_HEAP_MIN_SIZE 128U

struct soc_heap_region {
  const char *name;
  struct sys_heap *heap;
  uintptr_t start;
  size_t size;
};

struct soc_multi_heap_region_stats {
  const char *name;
  uintptr_t start;
  size_t size;
  struct sys_memory_stats stats;
  int err;
};

static struct sys_multi_heap soc_multi_heap;
static struct k_spinlock soc_multi_heap_lock;

extern uint32_t __HeapBase;
extern uint32_t __HeapLimit;
extern uint32_t _heap_wifi_start;
extern uint32_t _heap_wifi_size;
#ifdef CONFIG_PSRAM
extern uint32_t __psram_heap_base;
extern uint32_t __psram_limit;
#endif

static struct sys_heap ram_heap;
static struct sys_heap wram_heap;
#ifdef CONFIG_PSRAM
static struct sys_heap psram_heap;
#endif
static struct soc_heap_region soc_heap_regions[SOC_MULTI_HEAP_MAX_REGIONS];
static size_t soc_heap_region_count;
static bool soc_multi_heap_ready;
#ifdef CONFIG_PSRAM
static bool psram_heap_registered;
#endif

#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
struct qcc74x_coredump_psram_ledger qcc74x_coredump_psram_ledger;

#define QCC74X_COREDUMP_PSRAM_LEDGER_MAGIC 0x51434d4cu
#define QCC74X_COREDUMP_PSRAM_LEDGER_VERSION 1u

static bool psram_range_contains(uintptr_t address) {
  return psram_heap_registered && address >= (uintptr_t)&__psram_heap_base &&
         address < (uintptr_t)&__psram_limit;
}

static void psram_allocations_publish(void) {
  qcc74x_coredump_memory_regions_update(
      qcc74x_coredump_psram_ledger.entries,
      ARRAY_SIZE(qcc74x_coredump_psram_ledger.entries));
}

static void psram_allocation_record(void *ptr, size_t size) {
  if (!psram_range_contains((uintptr_t)ptr)) {
    return;
  }

  for (size_t index = 0U;
       index < ARRAY_SIZE(qcc74x_coredump_psram_ledger.entries); index++) {
    struct qcc74x_coredump_psram_allocation *allocation =
        &qcc74x_coredump_psram_ledger.entries[index];

    if (!allocation->active) {
      allocation->address = (uintptr_t)ptr;
      allocation->size = size;
      allocation->generation = ++qcc74x_coredump_psram_ledger.generation;
      allocation->active = true;
      qcc74x_coredump_psram_ledger.active_count++;
      psram_allocations_publish();
      return;
    }
  }

  qcc74x_coredump_psram_ledger.overflow = 1U;
  psram_allocations_publish();
}

static void psram_allocation_release(void *ptr) {
  if (!psram_range_contains((uintptr_t)ptr)) {
    return;
  }

  for (size_t index = 0U;
       index < ARRAY_SIZE(qcc74x_coredump_psram_ledger.entries); index++) {
    struct qcc74x_coredump_psram_allocation *allocation =
        &qcc74x_coredump_psram_ledger.entries[index];

    if (allocation->active && allocation->address == (uintptr_t)ptr) {
      allocation->active = false;
      qcc74x_coredump_psram_ledger.active_count--;
      psram_allocations_publish();
      return;
    }
  }

  qcc74x_coredump_psram_ledger.overflow = 1U;
  psram_allocations_publish();
}
#endif

int qcc74x_coredump_flash_budget_check(
    bool include_arch_record, struct qcc74x_coredump_flash_budget *budget) {
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
  return qcc74x_coredump_flash_budget_check_internal(include_arch_record,
                                                      budget);
#else
  ARG_UNUSED(include_arch_record);
  ARG_UNUSED(budget);
  return -ENOTSUP;
#endif
}

bool qcc74x_coredump_psram_ledger_overflowed(void) {
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
  return qcc74x_coredump_psram_ledger.overflow != 0U;
#else
  return false;
#endif
}

size_t qcc74x_coredump_psram_ledger_active_count(void) {
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
  return qcc74x_coredump_psram_ledger.active_count;
#else
  return 0U;
#endif
}

static size_t
soc_multi_heap_region_stats_get(struct soc_multi_heap_region_stats *regions,
                                size_t max_regions);

static void *heap_choice_fn(struct sys_multi_heap *mheap, void *cfg,
                            size_t align, size_t size) {
  ARG_UNUSED(mheap);
  ARG_UNUSED(cfg);

  for (size_t i = 0; i < soc_heap_region_count; i++) {
    void *ptr = sys_heap_aligned_alloc(soc_heap_regions[i].heap, align, size);

    if (ptr) {
      return ptr;
    }
  }

  return NULL;
}

static bool register_heap(const char *name, struct sys_heap *heap,
                          uintptr_t start, size_t size) {
  if (size < SOC_MULTI_HEAP_MIN_SIZE) {
    return false;
  }

  if (soc_heap_region_count >= ARRAY_SIZE(soc_heap_regions)) {
    return false;
  }

  sys_heap_init(heap, (void *)start, size);
  sys_multi_heap_add_heap(&soc_multi_heap, heap, NULL);

  soc_heap_regions[soc_heap_region_count++] = (struct soc_heap_region){
      .name = name,
      .heap = heap,
      .start = start,
      .size = size,
  };

  return true;
}

static int soc_multi_heap_init(void) {
  uintptr_t ram_start = (uintptr_t)&__HeapBase;
  uintptr_t ram_end = (uintptr_t)&__HeapLimit;
  uintptr_t wram_start = (uintptr_t)&_heap_wifi_start;
  size_t wram_size = (size_t)&_heap_wifi_size;

  sys_multi_heap_init(&soc_multi_heap, heap_choice_fn);
  soc_heap_region_count = 0U;
  soc_multi_heap_ready = false;
#ifdef CONFIG_PSRAM
  psram_heap_registered = false;
#endif

  if (ram_end > ram_start) {
    register_heap("ocram", &ram_heap, ram_start, ram_end - ram_start);
  }

  register_heap("wram", &wram_heap, wram_start, wram_size);

  soc_multi_heap_ready = soc_heap_region_count > 0U;

  return 0;
}

#ifdef CONFIG_PSRAM
int soc_multi_heap_psram_add(void) {
  k_spinlock_key_t key;
  uintptr_t psram_start = (uintptr_t)&__psram_heap_base;
  uintptr_t psram_end = (uintptr_t)&__psram_limit;

  if (psram_end > psram_start) {
    key = k_spin_lock(&soc_multi_heap_lock);
    if (!psram_heap_registered) {
      psram_heap_registered = register_heap("psram", &psram_heap, psram_start,
                                            psram_end - psram_start);
      soc_multi_heap_ready = soc_heap_region_count > 0U;
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
      if (psram_heap_registered) {
        memset(&qcc74x_coredump_psram_ledger, 0,
               sizeof(qcc74x_coredump_psram_ledger));
        qcc74x_coredump_psram_ledger.magic = QCC74X_COREDUMP_PSRAM_LEDGER_MAGIC;
        qcc74x_coredump_psram_ledger.version =
            QCC74X_COREDUMP_PSRAM_LEDGER_VERSION;
        qcc74x_coredump_psram_ledger.entry_count =
            ARRAY_SIZE(qcc74x_coredump_psram_ledger.entries);
        psram_allocations_publish();
      }
#endif
    }
    k_spin_unlock(&soc_multi_heap_lock, key);
  }

  return 0;
}
#else
int soc_multi_heap_psram_add(void) { return 0; }
#endif

static void *soc_multi_aligned_alloc(size_t align, size_t size) {
  k_spinlock_key_t key;
  void *p;

  if (!soc_multi_heap_ready) {
    return NULL;
  }

  key = k_spin_lock(&soc_multi_heap_lock);
  p = sys_multi_heap_aligned_alloc(&soc_multi_heap, NULL, align, size);
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
  if (p) {
    psram_allocation_record(p, size);
  }
#endif
  k_spin_unlock(&soc_multi_heap_lock, key);

  return p;
}

void *soc_multi_malloc(size_t size) {
  void *p = soc_multi_aligned_alloc(64, size);

  if (!p) {
    while (1) {
      p = soc_multi_aligned_alloc(64, size);
      if (p) {
        break;
      }
      printf("%s mem err\r\n", __func__);
      k_msleep(1000);
    }
  }

  return p;
}

static void *soc_multi_calloc(size_t nmemb, size_t size) {
  void *p;

  if (size_mul_overflow(nmemb, size, &size)) {
    return NULL;
  }

  p = soc_multi_aligned_alloc(64, size);
  if (p) {
    (void)memset(p, 0, size);
  }

  return p;
}

#if defined(CONFIG_MBEDTLS)
static int soc_mbedtls_mem_init(void) {
#if defined(MBEDTLS_PLATFORM_MEMORY)
  (void)mbedtls_platform_set_calloc_free(soc_multi_calloc, soc_multi_free);
#endif

  return 0;
}
#endif

void soc_multi_free(void *ptr) {
  k_spinlock_key_t key;

  if (ptr && soc_multi_heap_ready) {
    key = k_spin_lock(&soc_multi_heap_lock);
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
    psram_allocation_release(ptr);
#endif
    sys_multi_heap_free(&soc_multi_heap, ptr);
    k_spin_unlock(&soc_multi_heap_lock, key);
  }
}

void *qcc74x_psram_malloc(size_t size) {
#ifdef CONFIG_QCC74X_COREDUMP_FULL_MEMORY
  k_spinlock_key_t key;
  void *ptr;

  if (!psram_heap_registered) {
    return NULL;
  }

  key = k_spin_lock(&soc_multi_heap_lock);
  ptr = sys_heap_aligned_alloc(&psram_heap, 64U, size);
  if (ptr) {
    psram_allocation_record(ptr, size);
  }
  k_spin_unlock(&soc_multi_heap_lock, key);
  return ptr;
#else
  ARG_UNUSED(size);
  return NULL;
#endif
}

void qcc74x_psram_free(void *ptr) { soc_multi_free(ptr); }

static void soc_multi_heap_stats(struct sys_memory_stats *stats) {
  k_spinlock_key_t key;

  if (!stats) {
    return;
  }

  stats->free_bytes = 0U;
  stats->allocated_bytes = 0U;
  stats->max_allocated_bytes = 0U;

  key = k_spin_lock(&soc_multi_heap_lock);
  for (unsigned int i = 0; i < soc_multi_heap.nheaps; i++) {
    struct sys_memory_stats heap_stats;

    if (sys_heap_runtime_stats_get(soc_multi_heap.heaps[i].heap, &heap_stats) !=
        0) {
      continue;
    }

    stats->free_bytes += heap_stats.free_bytes;
    stats->allocated_bytes += heap_stats.allocated_bytes;
    stats->max_allocated_bytes += heap_stats.max_allocated_bytes;
  }
  k_spin_unlock(&soc_multi_heap_lock, key);
}

static void print_stats_line(const char *prefix,
                             const struct sys_memory_stats *stats) {
  printf("%s free:           %zu\r\n", prefix, stats->free_bytes);
  printf("%s allocated:      %zu\r\n", prefix, stats->allocated_bytes);
  printf("%s max. allocated: %zu\r\n", prefix, stats->max_allocated_bytes);
}

static void soc_heap_print_info(void) {
  struct sys_memory_stats stats;
  int err;

#if defined(K_HEAP_MEM_POOL_SIZE) && (K_HEAP_MEM_POOL_SIZE > 0)
  extern struct k_heap _system_heap;
  k_spinlock_key_t key;

  key = k_spin_lock(&_system_heap.lock);
  err = sys_heap_runtime_stats_get(&_system_heap.heap, &stats);
  k_spin_unlock(&_system_heap.lock, key);
  if (err) {
    printf("Heap kernel stats err: %d\r\n", err);
  } else {
    print_stats_line("Heap kernel", &stats);
  }
#else
  printf("Heap kernel unavailable\r\n");
#endif

#if defined(CONFIG_COMMON_LIBC_MALLOC) &&                                      \
    (CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE != 0) &&                             \
    defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
  err = malloc_runtime_stats_get(&stats);
  if (err) {
    printf("Heap libc stats err: %d\r\n", err);
  } else {
    print_stats_line("Heap libc", &stats);
  }
#else
  printf("Heap libc unavailable\r\n");
#endif

  soc_multi_heap_stats(&stats);
  print_stats_line("Heap total", &stats);

  struct soc_multi_heap_region_stats regions[SOC_MULTI_HEAP_MAX_REGIONS];
  size_t count = soc_multi_heap_region_stats_get(regions, ARRAY_SIZE(regions));

  for (size_t i = 0; i < count; i++) {
    uintptr_t end = regions[i].start + regions[i].size;
    char prefix[32];

    if (regions[i].err) {
      printf("Heap %-8s stats err: %d\r\n", regions[i].name, regions[i].err);
      continue;
    }

    printf("Heap %-8s range: 0x%08lx - 0x%08lx, size: %zu\r\n", regions[i].name,
           (unsigned long)regions[i].start, (unsigned long)end,
           regions[i].size);
    snprintk(prefix, sizeof(prefix), "Heap %-8s", regions[i].name);
    print_stats_line(prefix, &regions[i].stats);
  }
}

#ifdef CONFIG_SHELL
static int soc_heap_info_shell_cmd(const struct shell *shell, size_t argc,
                                   char **argv) {
  ARG_UNUSED(shell);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  soc_heap_print_info();
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(soc_heap_subcmds,
                               SHELL_CMD_ARG(info, NULL,
                                             "Show QCC74x heap statistics",
                                             soc_heap_info_shell_cmd, 1, 0),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_ARG_REGISTER(soc_heap, &soc_heap_subcmds, "QCC74x heap commands",
                       soc_heap_info_shell_cmd, 1, 1);
#endif

static size_t
soc_multi_heap_region_stats_get(struct soc_multi_heap_region_stats *regions,
                                size_t max_regions) {
  k_spinlock_key_t key;
  size_t count = MIN(soc_heap_region_count, max_regions);

  if (!regions) {
    return soc_heap_region_count;
  }

  key = k_spin_lock(&soc_multi_heap_lock);
  for (size_t i = 0; i < count; i++) {
    regions[i].name = soc_heap_regions[i].name;
    regions[i].start = soc_heap_regions[i].start;
    regions[i].size = soc_heap_regions[i].size;
    regions[i].err =
        sys_heap_runtime_stats_get(soc_heap_regions[i].heap, &regions[i].stats);
  }
  k_spin_unlock(&soc_multi_heap_lock, key);

  return count;
}

/* Automatically initialize at system startup */
SYS_INIT(soc_multi_heap_init, PRE_KERNEL_1, 0);
#if defined(CONFIG_MBEDTLS)
SYS_INIT(soc_mbedtls_mem_init, POST_KERNEL, 0);
#endif
