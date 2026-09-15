/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_ANTI_ROLLBACK_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_ANTI_ROLLBACK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum application version supported by the QCC74x eFuse bitmap. */
#define QCC74X_ANTI_ROLLBACK_VERSION_MAX 128U

/** @brief Return the anti-rollback version embedded in this application. */
uint8_t qcc74x_anti_rollback_image_version_get(void);

/**
 * @brief Read the application anti-rollback version from eFuse.
 *
 * @param version Destination for the provisioned version.
 *
 * @retval 0 The version was read successfully.
 * @retval -EINVAL @p version is NULL.
 * @retval -EACCES Anti-rollback is not provisioned or the eFuse version
 *                 could not be read.
 * @retval -EIO The eFuse returned an invalid version.
 */
int qcc74x_anti_rollback_efuse_version_get(uint8_t *version);

/**
 * @brief Irreversibly commit this application's version to eFuse.
 *
 * Call this only after the application has completed its health checks and
 * the update has been confirmed. The image version must be in the range 1 to
 * 128. On an unprovisioned device, the first commit also enables the global
 * hardware anti-rollback bit. Both the enable bit and the exact application
 * version are read back before success is returned.
 *
 * @retval 0 Anti-rollback was provisioned, the eFuse already contains this
 *           version, or the version commit succeeded.
 * @retval -EINVAL The image version is zero or invalid.
 * @retval -EACCES The enabled eFuse version cannot be read.
 * @retval -EPERM The eFuse version is newer than this application.
 * @retval -EIO The enable/version write or read-back verification failed.
 */
int qcc74x_anti_rollback_version_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_ANTI_ROLLBACK_H_ */
