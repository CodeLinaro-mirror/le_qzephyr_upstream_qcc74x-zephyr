/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>

#include <qc7xx_efuse.h>
#include <../sys/qc7xx_sys.h>
#include <../sys/qc7xx_lp.h>

#define SIZE_OF_DEVICE_ID    (8)
#define RST_REASON_CHK_CLEAR (0xBF1BA55A)

/**
 * @brief Implements z_impl_hwinfo_get_device_id() that is declared in
 * zephyr\include\zephyr\drivers\hwinfo.h.
 */
ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	uint8_t device_id[SIZE_OF_DEVICE_ID] = {0};

	qc7xx_efuse_get_chipid(device_id);

	if (length > sizeof(device_id)) {
		length = sizeof(device_id);
	}

	arch_memcpy(buffer, device_id, length);

	return length;
}

/**
 * @brief Implements z_impl_hwinfo_get_reset_cause() that is declared in
 * zephyr\include\zephyr\drivers\hwinfo.h.
 */
int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	QC7XX_RST_REASON_E reset_reason = HBN_SYS_RESET_REASON;

	switch (reset_reason) {
#if defined(QCC74X)
	case QC7XX_RST_POWER_OFF:
		*cause = RESET_POR;
		break;
	case QC7XX_RST_HARDWARE_WATCHDOG:
		*cause = RESET_WATCHDOG;
		break;
	case QC7XX_RST_FATAL_EXCEPTION:
		*cause = RESET_USER;
		break;
	case QC7XX_RST_SOFTWARE_WATCHDOG:
		*cause = RESET_WATCHDOG;
		break;
	case QC7XX_RST_SOFTWARE:
		*cause = RESET_SOFTWARE;
		break;
	case QC7XX_RST_HBN:
		*cause = RESET_LOW_POWER_WAKE;
		break;
	case QC7XX_RST_BOD:
		*cause = RESET_PIN;
		break;
#elif defined(QCC75X)
	case QC7XX_RST_POWER_ON:
		*cause = RESET_POR;
		break;
	case QC7XX_RST_WATCHDOG:
		*cause = RESET_WATCHDOG;
		break;
	case QC7XX_RST_PDS_WAKEUP:
		*cause = RESET_PIN;
		break;
	case QC7XX_RST_SOFTWARE:
		*cause = RESET_SOFTWARE;
		break;
	case QC7XX_RST_HBN_WAKEUP:
		*cause = RESET_LOW_POWER_WAKE;
		break;
#endif
	default:
		*cause = 0;
	}

	return 0;
}

/**
 * @brief Implements z_impl_hwinfo_get_supported_reset_cause() that is declared in
 * zephyr\include\zephyr\drivers\hwinfo.h.
 */
int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = (RESET_POR | RESET_PIN | RESET_SOFTWARE | RESET_WATCHDOG | RESET_LOW_POWER_WAKE
#if defined(QCC74X)
		      | RESET_USER
#endif
	);

	return 0;
}

/**
 * @brief Implements z_impl_hwinfo_clear_reset_cause() that is declared in
 * zephyr\include\zephyr\drivers\hwinfo.h.
 */
int z_impl_hwinfo_clear_reset_cause(void)
{
	QC7XX_RST_REASON_E reset_clear = 0;

	HBN_SYS_RESET_REASON = (uint32_t)reset_clear;
	HBN_SYS_RESET_REASON_CHK = (uint32_t)reset_clear ^ RST_REASON_CHK_CLEAR;

	return 0;
}
