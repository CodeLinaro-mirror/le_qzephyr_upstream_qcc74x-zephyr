/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SDIO_DEVICE_H_
#define ZEPHYR_INCLUDE_DRIVERS_SDIO_DEVICE_H_

#include <zephyr/types.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>

/** @defgroup SDIO_CMD sdio feature_control commands.
 * @{
 */
#define SDIO_CMD_GET_BUS_WIDTH            (1)
#define SDIO_CMD_GET_FUNC_HOST_READY      (2)
#define SDIO_CMD_GET_FUNC_BLK_SIZE        (3)
#define SDIO_CMD_GET_DNLD_MAX_SIZE        (4)
#define SDIO_CMD_GET_UPLD_MAX_SIZE        (5)
#define SDIO_CMD_GET_DNLD_QUEUE_WAIT_NUM  (6)
#define SDIO_CMD_GET_DNLD_QUEUE_AVAILABLE (7)
#define SDIO_CMD_GET_UPLD_QUEUE_WAIT_NUM  (8)
#define SDIO_CMD_GET_UPLD_QUEUE_AVAILABLE (9)
/**
 * @}
 */

/**
 * @brief Structure working as a descriptor for single SDIO transmission.
 */
typedef struct {
	uint16_t buff_len;
	uint16_t data_len;
	void *buff;
	void *user_arg;
} sdio_transfer_desc_t;

typedef void (*sdio_download_callback_t)(const struct device *dev,
					 sdio_transfer_desc_t *trans_desc);
typedef void (*sdio_upload_callback_t)(const struct device *dev, sdio_transfer_desc_t *trans_desc);
typedef void (*sdio_reset_callback_t)(const struct device *dev);

__subsystem struct sdio_driver_api {
	int (*dnld_port_push)(const struct device *dev, sdio_transfer_desc_t *trans_desc);
	int (*upld_port_push)(const struct device *dev, sdio_transfer_desc_t *trans_desc);
	int (*dnld_port_pop)(const struct device *dev, sdio_transfer_desc_t *trans_desc);
	int (*upld_port_pop)(const struct device *dev, sdio_transfer_desc_t *trans_desc);
	int (*feature_control)(const struct device *dev, int cmd, uintptr_t arg);
	void (*set_download_callback)(const struct device *dev, sdio_download_callback_t callback);
	void (*set_upload_callback)(const struct device *dev, sdio_upload_callback_t callback);
	void (*set_reset_callback)(const struct device *dev, sdio_reset_callback_t callback);
	void (*re_initialize)(const struct device *dev);
};

/**
 * @brief Add to download buffer.
 *
 * @param dev        pointer to sdio device
 * @param trans_desc pointer to transmission descriptor.
 *
 * @retval 0 if successful.
 * @retval -1 Ring queue is full
 * @retval -2 Potential overflow
 */
__syscall int sdio_dnld_port_push(const struct device *dev, sdio_transfer_desc_t *trans_desc);

/**
 * @brief Add to upload buffer.
 *
 * @param dev        pointer to sdio device
 * @param trans_desc pointer to transmission descriptor.
 *
 * @retval 0 if successful.
 * @retval -1 Ring queue is full
 * @retval -2 Potential overflow
 */
__syscall int sdio_upld_port_push(const struct device *dev, sdio_transfer_desc_t *trans_desc);

/**
 * @brief Remove from download buffer. Use only after reset or before re_initialize.
 *
 * @param dev        pointer to sdio device
 * @param trans_desc pointer to transmission descriptor.
 *
 * @retval 0 if successful.
 */
__syscall int sdio_dnld_port_pop(const struct device *dev, sdio_transfer_desc_t *trans_desc);

/**
 * @brief Remove from upload buffer. Use only after reset or before re_initialize.
 *
 * @param dev        pointer to sdio device
 * @param trans_desc pointer to transmission descriptor.
 *
 * @retval 0 if successful.
 */
__syscall int sdio_upld_port_pop(const struct device *dev, sdio_transfer_desc_t *trans_desc);

/**
 * @brief Feature control.
 *
 * @param dev pointer to sdio device
 * @param cmd command number, use @ref SDIO_CMD
 * @param arg command argument
 *
 * @retval 0 if successful.
 */
__syscall int sdio_feature_control(const struct device *dev, int cmd, uintptr_t arg);

/**
 * @brief Assign callback function, called when download is completed.
 *
 * @param dev      pointer to sdio device
 * @param callback callback func
 *
 */
__syscall void sdio_set_download_callback(const struct device *dev,
					  sdio_download_callback_t callback);

/**
 * @brief Assign callback function, called when upload is completed.
 *
 * @param dev      pointer to sdio device
 * @param callback callback func
 *
 */
__syscall void sdio_set_upload_callback(const struct device *dev, sdio_upload_callback_t callback);

/**
 * @brief Assign callback function, called when reset is requested.
 *
 * @param dev      pointer to sdio device
 * @param callback callback func
 *
 */
__syscall void sdio_set_reset_callback(const struct device *dev, sdio_reset_callback_t callback);

/**
 * @brief De-initialize and initialize back sdio device.
 *
 * @param dev      pointer to sdio device
 */
__syscall void sdio_re_initialize(const struct device *dev);

static inline int z_impl_sdio_dnld_port_push(const struct device *dev,
					     sdio_transfer_desc_t *trans_desc)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	return DEVICE_API_GET(sdio, dev)->dnld_port_push(dev, trans_desc);
}

static inline int z_impl_sdio_upld_port_push(const struct device *dev,
					     sdio_transfer_desc_t *trans_desc)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	return DEVICE_API_GET(sdio, dev)->upld_port_push(dev, trans_desc);
}

static inline int z_impl_sdio_dnld_port_pop(const struct device *dev,
					    sdio_transfer_desc_t *trans_desc)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	return DEVICE_API_GET(sdio, dev)->dnld_port_pop(dev, trans_desc);
}

static inline int z_impl_sdio_upld_port_pop(const struct device *dev,
					    sdio_transfer_desc_t *trans_desc)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	return DEVICE_API_GET(sdio, dev)->upld_port_pop(dev, trans_desc);
}

static inline int z_impl_sdio_feature_control(const struct device *dev, int cmd, uintptr_t arg)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	return DEVICE_API_GET(sdio, dev)->feature_control(dev, cmd, arg);
}

static inline void z_impl_sdio_set_download_callback(const struct device *dev,
						     sdio_download_callback_t callback)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	DEVICE_API_GET(sdio, dev)->set_download_callback(dev, callback);
}

static inline void z_impl_sdio_set_upload_callback(const struct device *dev,
						   sdio_upload_callback_t callback)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	DEVICE_API_GET(sdio, dev)->set_upload_callback(dev, callback);
}

static inline void z_impl_sdio_set_reset_callback(const struct device *dev,
						  sdio_reset_callback_t callback)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	DEVICE_API_GET(sdio, dev)->set_reset_callback(dev, callback);
}

static inline void z_impl_sdio_re_initialize(const struct device *dev)
{
	__ASSERT_NO_MSG(DEVICE_API_IS(sdio, dev));
	DEVICE_API_GET(sdio, dev)->re_initialize(dev);
}

#include <syscalls/sdio_device.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_SDIO_DEVICE_H_ */
