/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_QCC74X_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_QCC74X_H_

#include <zephyr/drivers/dma.h>
#include <qc7xx_dma.h>

/**
 * @brief Invalidate DMA destination address cache.
 *
 * @param dev     Pointer to dma device structure.
 * @param channel DMA channel number.
 */
void dma_qcc74x_dest_addr_cache_invalidate(const struct device *dev, const uint32_t channel);

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_QCC74X_H_ */
