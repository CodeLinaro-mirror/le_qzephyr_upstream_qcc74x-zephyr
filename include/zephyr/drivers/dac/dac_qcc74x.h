/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DAC_QCC74X_H_
#define ZEPHYR_INCLUDE_DRIVERS_DAC_QCC74X_H_

#include <zephyr/drivers/dac.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Type definition of DAC API function for setting a write DMA request.
 * See dac_write_dma() for argument descriptions.
 */
typedef int (*dac_qcc74x_api_write_dma)(const struct device *dev,
				    uint16_t len, uint32_t *source_address);

__subsystem struct dac_qcc74x_driver_api {
	struct dac_driver_api base;
	dac_qcc74x_api_write_dma write_dma;
};

/**
 * @brief Start DMA based data transfer to DAC.
 *
 * @param dev             Pointer to DAC device structure.
 * @param len             Length of data in bytes to be transferred.
 * @param source_address  Pointer to the sample buffer.
 */
__syscall int dac_write_dma(const struct device *dev, uint16_t len, uint32_t *source_address);

static inline int z_impl_dac_write_dma(const struct device *dev, uint16_t len, uint32_t *source_address)
{
	return DEVICE_API_GET(dac_qcc74x, dev)->write_dma(dev, len, source_address);
}

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/dac_qcc74x.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_DAC_QCC74X_H_ */
