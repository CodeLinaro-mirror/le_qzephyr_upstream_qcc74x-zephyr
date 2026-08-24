/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_DRIVERS_CRYPTO_CRYPTO_QCC74X_PRIV_H_
#define ZEPHYR_DRIVERS_CRYPTO_CRYPTO_QCC74X_PRIV_H_

#define SHA_MAX_SESSIONS    CONFIG_CRYPTO_QCC74X_SHA_MAX_SESSION
#define CIPHER_MAX_SESSIONS CONFIG_CRYPTO_QCC74X_CIPHER_MAX_SESSION

/* Possible SHA states */
#define SHA_NOT_STARTED 0
#define SHA_IN_PROGRESS 1
#define SHA_COMPLETED   2

struct sha_session {
	struct qc7xx_sha256_link_ctx_s ctx_sha256;
	struct qc7xx_sha512_link_ctx_s ctx_sha512;
	/* Hash state: SHA_NOT_STARTED, SHA_IN_PROGRESS or SHA_COMPLETED */
	uint32_t sha_state;
	uint32_t algo;
	bool in_use;
};

struct cipher_session {
	struct qc7xx_aes_link_s ctx_aes_link;
	uint32_t algo;
	bool in_use;
};

struct crypto_qcc74x_config {
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const uint8_t clock_source;
	uint32_t reg_base;
};

struct crypto_sem {
	struct k_sem device_sem;
	struct k_sem session_sem;
};

struct crypto_qcc74x_data {
	struct qc7xx_device_s *sha_dev;
	struct crypto_sem sha_sem;
	struct qc7xx_device_s *cipher_dev;
	struct crypto_sem cipher_sem;
	uint8_t instance_id;
};

#define CRYPTO_QCC74X_CFG(dev) ((const struct crypto_qcc74x_config *const)(dev)->config)

#define CRYPTO_QCC74X_DATA(dev) ((struct crypto_qcc74x_data *const)(dev)->data)

#endif /* ZEPHYR_DRIVERS_CRYPTO_CRYPTO_QCC74X_PRIV_H_ */