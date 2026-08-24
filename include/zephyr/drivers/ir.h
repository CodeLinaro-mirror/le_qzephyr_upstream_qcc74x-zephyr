/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IR_H_
#define ZEPHYR_INCLUDE_DRIVERS_IR_H_

#include <zephyr/types.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>

__subsystem struct ir_driver_api {
	int (*receive)(const struct device *dev, uint64_t *data);
	void (*enable)(const struct device *dev, const bool enable);
	int (*swm_receive)(const struct device *dev, uint16_t *data, uint8_t length);
	int (*get_rxfifo_cnt)(const struct device *dev);
	int (*get_rxint_status)(const struct device *dev);
	void (*rxfifo_clear)(const struct device *dev);
	void (*rxint_clear)(const struct device *dev);
};

/**
 * @brief Receive data in NEC/RC5 mode.
 *
 * @param dev IR device
 * @param data data to receive
 *
 * @return Bit count of data received
 * @retval -errno code Feilure
 */
__syscall int ir_receive(const struct device *dev, uint64_t *data);

/**
 * @brief Enable or disable IR receving.
 *
 * @param dev IR device
 * @param enable enable or disable
 */
__syscall void ir_enable(const struct device *dev, const bool enable);

/**
 * @brief Receive data in software mode.
 *
 * @param dev IR device
 * @param data data buffer to receive
 * @param length length of data buffer
 *
 * @return Length of data received
 * @retval -errno code Feilure
 */
__syscall int swm_receive(const struct device *dev, uint16_t *data, uint8_t length);

/**
 * @brief Get IR rx fifo available count.
 *
 * @param dev IR device
 *
 * @return Ir rx fifo available count
 */
__syscall int ir_get_rxfifo_cnt(const struct device *dev);

/**
 * @brief Get IR rx interrupt status.
 *
 * @param dev IR device
 *
 * @return IR rx interrupt status
 */
__syscall int ir_get_rxint_status(const struct device *dev);

/**
 * @brief Clear IR rx fifo.
 *
 * @param dev IR device
 */
__syscall void ir_rxfifo_clear(const struct device *dev);

/**
 * @brief Clear IR rx interrupt.
 *
 * @param dev IR device
 */
__syscall void ir_rxint_clear(const struct device *dev);

static inline int z_impl_ir_receive(const struct device *dev, uint64_t *data)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	return DEVICE_API_GET(ir, dev)->receive(dev, data);
}

static inline void z_impl_ir_enable(const struct device *dev, const bool enable)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	return DEVICE_API_GET(ir, dev)->enable(dev, enable);
}

static inline int z_impl_swm_receive(const struct device *dev, uint16_t *data, uint8_t length)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	return DEVICE_API_GET(ir, dev)->swm_receive(dev, data, length);
}

static inline int z_impl_ir_get_rxfifo_cnt(const struct device *dev)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	return DEVICE_API_GET(ir, dev)->get_rxfifo_cnt(dev);
}

static inline int z_impl_ir_get_rxint_status(const struct device *dev)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	return DEVICE_API_GET(ir, dev)->get_rxint_status(dev);
}

static inline void z_impl_ir_rxfifo_clear(const struct device *dev)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	DEVICE_API_GET(ir, dev)->rxfifo_clear(dev);
}

static inline void z_impl_ir_rxint_clear(const struct device *dev)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(ir, dev));
	DEVICE_API_GET(ir, dev)->rxint_clear(dev);
}

#include <syscalls/ir.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_IR_H_ */
