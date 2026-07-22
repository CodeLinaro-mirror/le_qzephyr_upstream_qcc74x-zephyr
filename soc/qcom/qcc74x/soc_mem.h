/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_QUALCOMM_QCC74X_SOC_MEM_H_
#define ZEPHYR_SOC_QUALCOMM_QCC74X_SOC_MEM_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Add PSRAM to the SoC heap after the board has initialized the controller. */
int soc_multi_heap_psram_add(void);

/*
 * Allocate from the QCC74x SoC heap.
 *
 * Allocation priority follows heap registration order: ocram, then wram, then
 * psram after soc_multi_heap_psram_add() registers it.
 * The implementation currently uses 64-byte alignment for DMA/cache users.
 */
void *soc_multi_malloc(size_t size);
void soc_multi_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_QUALCOMM_QCC74X_SOC_MEM_H_ */
