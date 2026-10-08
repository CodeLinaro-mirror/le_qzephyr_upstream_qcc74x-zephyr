/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_FLASH_QCC74X_EMERGENCY_H_
#define ZEPHYR_INCLUDE_DRIVERS_FLASH_QCC74X_EMERGENCY_H_

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

struct flash_qcc74x_emergency_region {
	off_t offset;
	size_t size;
};

struct flash_qcc74x_emergency_session {
	const struct device *dev;
	struct flash_qcc74x_emergency_region region;
	bool locked;
};

int flash_qcc74x_emergency_try_lock(
	const struct device *dev,
	const struct flash_qcc74x_emergency_region *region,
	struct flash_qcc74x_emergency_session *session);
int flash_qcc74x_emergency_erase(struct flash_qcc74x_emergency_session *session);
int flash_qcc74x_emergency_write(struct flash_qcc74x_emergency_session *session,
				  off_t offset, const void *buffer, size_t length);
int flash_qcc74x_emergency_read(struct flash_qcc74x_emergency_session *session,
				 off_t offset, void *buffer, size_t length);
void flash_qcc74x_emergency_unlock(struct flash_qcc74x_emergency_session *session);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_FLASH_QCC74X_EMERGENCY_H_ */
