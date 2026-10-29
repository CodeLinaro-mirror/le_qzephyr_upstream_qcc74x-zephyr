/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * OTP driver for QCC74x eFuse — wraps the LHAL qc7xx_ef_ctrl API.
 *
 * The existing efuse-qcc74x.c exposes the SYSCON API (used by hwinfo).
 * This driver exposes the Zephyr OTP API on the same hardware so that
 * otp_shell.c (CONFIG_OTP_SHELL) can read and optionally program eFuse.
 *
 * Device name: "otp0"
 * Usage (shell):
 *   otp read  otp0 <offset> <word_len>
 *   otp write otp0 <offset> <word0> [word1 ...]   (CONFIG_OTP_PROGRAM=y)
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/otp.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <qc7xx_ef_ctrl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(otp_qcc74x, CONFIG_OTP_LOG_LEVEL);

/* Total eFuse size in bytes — must match DTS efuse node `size` property. */
#define QCC74X_OTP_SIZE CONFIG_OTP_QCC74X_SIZE

static int otp_qcc74x_read(const struct device *dev, off_t offset,
			    void *buf, size_t len)
{
	ARG_UNUSED(dev);

	if (offset < 0 || len == 0) {
		return -EINVAL;
	}

	if ((size_t)offset >= QCC74X_OTP_SIZE ||
	    len > QCC74X_OTP_SIZE - (size_t)offset) {
		return -EINVAL;
	}

	/*
	 * qc7xx_ef_ctrl_read_direct works on 32-bit words.
	 * Handle potentially non-aligned offset / length by reading
	 * whole words into a temporary buffer then copying the bytes.
	 */
	size_t word_start = (size_t)offset / sizeof(uint32_t);
	size_t byte_off   = (size_t)offset % sizeof(uint32_t);
	size_t words      = DIV_ROUND_UP(byte_off + len, sizeof(uint32_t));
	uint32_t tmp[words];

	qc7xx_ef_ctrl_read_direct(NULL, word_start * sizeof(uint32_t),
				  tmp, words, 1);

	memcpy(buf, (uint8_t *)tmp + byte_off, len);
	return 0;
}

#ifdef CONFIG_OTP_PROGRAM
static int otp_qcc74x_program(const struct device *dev, off_t offset,
			       const void *buf, size_t len)
{
	ARG_UNUSED(dev);

	if (offset < 0 || len == 0) {
		return -EINVAL;
	}

	if ((size_t)offset >= QCC74X_OTP_SIZE ||
	    len > QCC74X_OTP_SIZE - (size_t)offset) {
		return -EINVAL;
	}

	/* write_direct requires 4-byte aligned offset and word-count length. */
	if (!IS_ALIGNED(offset, sizeof(uint32_t)) ||
	    !IS_ALIGNED(len, sizeof(uint32_t))) {
		return -EINVAL;
	}

	if (!IS_ALIGNED((uintptr_t)buf, sizeof(uint32_t))) {
		return -EINVAL;
	}

	/*
	 * program=1: actually burns the eFuse bits (irreversible).
	 * program=0: writes shadow RAM only (temporary, for testing).
	 */
	qc7xx_ef_ctrl_write_direct(NULL, (uint32_t)offset,
				   (uint32_t *)buf,
				   len / sizeof(uint32_t), 1);
	return 0;
}
#endif /* CONFIG_OTP_PROGRAM */

static DEVICE_API(otp, otp_qcc74x_api) = {
	.read    = otp_qcc74x_read,
#ifdef CONFIG_OTP_PROGRAM
	.program = otp_qcc74x_program,
#endif
};

/* Use DEVICE_DEFINE (no DTS node needed — hardware accessed via LHAL). */
DEVICE_DEFINE(otp_qcc74x, "otp0", NULL, NULL, NULL, NULL,
	      POST_KERNEL, CONFIG_OTP_INIT_PRIORITY, &otp_qcc74x_api);
