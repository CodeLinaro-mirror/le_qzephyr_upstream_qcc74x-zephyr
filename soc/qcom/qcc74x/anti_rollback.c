/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/soc/qualcomm/qcc74x_anti_rollback.h>
#include <zephyr/toolchain.h>

#include <qc7xx_common.h>

struct qcc74x_anti_rollback_info {
	uint8_t anti_rollback;
	uint8_t x;
	uint8_t y;
	uint8_t z;
	uint32_t name;
	uint32_t build_time;
	uint32_t commit_id;
	uint32_t reserved0;
	uint32_t reserved1;
};

BUILD_ASSERT(sizeof(struct qcc74x_anti_rollback_info) == 24,
	     "Bootloader requires a 24-byte QCC74x version structure");
BUILD_ASSERT(offsetof(struct qcc74x_anti_rollback_info, anti_rollback) == 0,
	     "Bootloader requires the anti-rollback version at byte zero");
BUILD_ASSERT(CONFIG_APP_ANTI_ROLLBACK_VER >= 0 &&
	     CONFIG_APP_ANTI_ROLLBACK_VER <= QCC74X_ANTI_ROLLBACK_VERSION_MAX,
	     "QCC74x anti-rollback version must be in the range 0..128");

static const struct qcc74x_anti_rollback_info image_info
	Z_GENERIC_SECTION(.qcc74xverinf) __used = {
		.anti_rollback = CONFIG_APP_ANTI_ROLLBACK_VER,
	};

K_MUTEX_DEFINE(anti_rollback_lock);

uint8_t qcc74x_anti_rollback_image_version_get(void)
{
	return image_info.anti_rollback;
}

int qcc74x_anti_rollback_efuse_version_get(uint8_t *version)
{
	if (version == NULL) {
		return -EINVAL;
	}

	/* The HAL reports failure when the global anti-rollback bit is clear. */
	if (qc7xx_get_app_version_from_efuse(version) != 0) {
		return -EACCES;
	}

	if (*version > QCC74X_ANTI_ROLLBACK_VERSION_MAX) {
		return -EIO;
	}

	return 0;
}

int qcc74x_anti_rollback_version_commit(void)
{
	const uint8_t image_version = qcc74x_anti_rollback_image_version_get();
	uint8_t enabled;
	uint8_t efuse_version;
	int ret;

	/* HAL eFuse bitmap programming is defined only for versions 1..128. */
	if (image_version == 0U ||
	    image_version > QCC74X_ANTI_ROLLBACK_VERSION_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&anti_rollback_lock, K_FOREVER);

	if (qc7xx_get_anti_rollback_enable(&enabled) != 0) {
		ret = -EIO;
		goto out;
	}

	if (enabled == 0U) {
		/* The HAL setter provisions enable before the App version. */
		if (qc7xx_set_app_version_to_efuse(image_version) != 0) {
			ret = -EIO;
			goto out;
		}

		if (qc7xx_get_anti_rollback_enable(&enabled) != 0 || enabled == 0U) {
			ret = -EIO;
			goto out;
		}

		ret = qcc74x_anti_rollback_efuse_version_get(&efuse_version);
		if (ret != 0 || efuse_version != image_version) {
			ret = -EIO;
			goto out;
		}

		ret = 0;
		goto out;
	}

	ret = qcc74x_anti_rollback_efuse_version_get(&efuse_version);
	if (ret != 0) {
		goto out;
	}

	if (efuse_version > image_version) {
		ret = -EPERM;
		goto out;
	}

	if (efuse_version == image_version) {
		ret = 0;
		goto out;
	}

	if (qc7xx_set_app_version_to_efuse(image_version) != 0) {
		ret = -EIO;
		goto out;
	}

	ret = qcc74x_anti_rollback_efuse_version_get(&efuse_version);
	if (ret != 0 || efuse_version != image_version) {
		ret = -EIO;
	}

out:
	k_mutex_unlock(&anti_rollback_lock);
	return ret;
}
