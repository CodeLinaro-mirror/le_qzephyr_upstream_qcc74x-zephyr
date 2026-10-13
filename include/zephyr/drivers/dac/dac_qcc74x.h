/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DAC_QCC74X_H_
#define ZEPHYR_INCLUDE_DRIVERS_DAC_QCC74X_H_

#include <zephyr/drivers/dac.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Type definition of DAC API function for setting a write DMA request.
 * See dac_write_dma() for argument descriptions.
 */
typedef int (*dac_qcc74x_api_write_dma)(const struct device *dev,
				    uint16_t len, uint32_t *source_address);

typedef int (*dac_qcc74x_api_write_dma_sync)(const struct device *dev,
					     uint16_t len, uint32_t *source_address,
					     k_timeout_t timeout);
/** Start a non-blocking single-value DMA transfer. */
int dac_qcc74x_write_dma_value(const struct device *dev, uint8_t channel, uint32_t value);

/** Start a single-value DMA transfer and wait for completion. */
int dac_qcc74x_write_dma_value_sync(const struct device *dev, uint8_t channel,
					    uint32_t value, k_timeout_t timeout);

__subsystem struct dac_qcc74x_driver_api {
	struct dac_driver_api base;
	dac_qcc74x_api_write_dma write_dma;
	dac_qcc74x_api_write_dma_sync write_dma_sync;
};

/**
 * @brief Start DMA based data transfer to DAC (non-blocking).
 *
 * @param dev             Pointer to DAC device structure.
 * @param len             Length of data in bytes to be transferred.
 * @param source_address  Pointer to the sample buffer.
 *
 * Only one DMA transfer may be outstanding for a DAC instance. The caller
 * must keep source_address valid and unchanged until the DMA callback runs.
 *
 * @retval 0        DMA transfer started successfully.
 * @retval -EBUSY   Another DMA transfer is in progress.
 * @retval <0       DMA configuration or start error.
 */
__syscall int dac_write_dma(const struct device *dev, uint16_t len, uint32_t *source_address);

static inline int z_impl_dac_write_dma(const struct device *dev, uint16_t len, uint32_t *source_address)
{
	return DEVICE_API_GET(dac_qcc74x, dev)->write_dma(dev, len, source_address);
}

/**
 * @brief Start DMA based data transfer to DAC and wait for completion.
 *
 * @param dev             Pointer to DAC device structure.
 * @param len             Length of data in bytes to be transferred.
 * @param source_address  Pointer to the sample buffer.
 * @param timeout         Maximum time to wait for DMA completion.
 *
 * The caller must keep source_address valid and unchanged until this function
 * returns. The timeout covers both waiting for the DMA channel and waiting
 * for this transfer to complete.
 *
 * @retval 0        Transfer completed successfully.
 * @retval -EAGAIN  Timed out waiting for the channel or completion.
 * @retval <0       DMA configuration or start error.
 */
__syscall int dac_write_dma_sync(const struct device *dev, uint16_t len,
				 uint32_t *source_address, k_timeout_t timeout);

static inline int z_impl_dac_write_dma_sync(const struct device *dev, uint16_t len,
					     uint32_t *source_address, k_timeout_t timeout)
{
	return DEVICE_API_GET(dac_qcc74x, dev)->write_dma_sync(dev, len, source_address, timeout);
}

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/dac_qcc74x.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_DAC_QCC74X_H_ */
