/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_crypto

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/crypto/crypto.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/cache.h>

#include <zephyr/drivers/clock_control/qcc7xx_clk_sec_ll.h>
#include <qc7xx_sec_aes.h>
#include <qc7xx_sec_sha.h>
#include "crypto_qcc74x_priv.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(crypto_qcc74x, CONFIG_CRYPTO_LOG_LEVEL);

#define CRYPTO_CIPHER_CAPS (CAP_RAW_KEY | CAP_SEPARATE_IO_BUFS | CAP_SYNC_OPS | CAP_NO_IV_PREFIX)
#define CRYPTO_HASH_CAPS   (CAP_SEPARATE_IO_BUFS | CAP_SYNC_OPS)

#define BLOCK_LEN_BYTES 16
#define BLOCK_LEN_WORDS (BLOCK_LEN_BYTES / sizeof(uint32_t))

/** @brief SHA session structure for storing session data. */
__nocache_noinit struct sha_session sha_sessions[SHA_MAX_SESSIONS];

/** @brief Cipher session structure for storing session data. */
__nocache_noinit struct cipher_session cipher_sessions[CIPHER_MAX_SESSIONS];

/** @brief Opal device configuration structure for storing device data. */
static struct qc7xx_device_s sha_dev = {.name = QC7XX_NAME_SEC_SHA,
					.reg_base = 0x0,
					.irq_num = 0xFF,
					.idx = 0,
					.sub_idx = 0,
					.dev_type = QC7XX_DEVICE_TYPE_SEC_SHA,
					.user_data = NULL};

/** @brief Opal device configuration structure for storing device data. */
static struct qc7xx_device_s cipher_dev = {.name = QC7XX_NAME_SEC_AES,
					   .reg_base = 0x0,
					   .irq_num = 0xFF,
					   .idx = 0,
					   .sub_idx = 0,
					   .dev_type = QC7XX_DEVICE_TYPE_SEC_AES,
					   .user_data = NULL};

/**
 * @brief Clean the cache for a given address range.
 *
 * @param addr Pointer to the memory address to clean.
 * @param size Size of the memory region to clean in bytes.
 */
void crypto_cache_addr_clean(void *addr, uint32_t size)
{
	/* Cache aligned check */
	if ((qc7xx_check_cache_addr(addr)) &&
	    ((uintptr_t)addr % sys_cache_data_line_size_get() != 0)) {
		LOG_WRN("The address is not cache line aligned to clean: 0x%08x\n", (uint32_t)addr);
	}

	/* Cache clean */
	sys_cache_data_flush_range(addr, size);
}

/**
 * @brief Clean and invalidate the cache for a given address range.
 *
 * @param addr Pointer to the memory address to clean and invalidate.
 * @param size Size of the memory region to clean and invalidate in bytes.
 */
void crypto_cache_addr_clean_invalidate(void *addr, uint32_t size)
{
	/* Cache aligned check */
	if ((qc7xx_check_cache_addr(addr)) &&
	    ((uintptr_t)addr % sys_cache_data_line_size_get() != 0)) {
		LOG_WRN("The address is not cache line aligned to clean and invalidate: "
			"0x%08x\n",
			(uint32_t)addr);
	}

	/* Cache clean and invalidate */
	sys_cache_data_flush_and_invd_range(addr, size);
}

/**
 * @brief Get an unused cipher session index.
 *
 * @param dev Pointer to the device structure.
 * @return int Index of an available session, or -1 if none are available.
 */
static int qcc74x_cipher_get_unused_session_idx(const struct device *dev)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);

	k_sem_take(&data->cipher_sem.session_sem, K_FOREVER);

	for (int i = 0; i < CIPHER_MAX_SESSIONS; i++) {
		if (!cipher_sessions[i].in_use) {
			cipher_sessions[i].in_use = true;
			k_sem_give(&data->cipher_sem.session_sem);
			return i;
		}
	}

	k_sem_give(&data->cipher_sem.session_sem);

	return -1;
}

/**
 * @brief Get an unused SHA session index.
 *
 * @param dev Pointer to the device structure.
 * @return int Index of an available session, or -1 if none are available.
 */
static int qcc74x_sha_get_unused_session_idx(const struct device *dev)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);

	k_sem_take(&data->sha_sem.session_sem, K_FOREVER);

	for (int i = 0; i < SHA_MAX_SESSIONS; i++) {
		if (!sha_sessions[i].in_use) {
			sha_sessions[i].in_use = true;
			k_sem_give(&data->sha_sem.session_sem);
			return i;
		}
	}

	k_sem_give(&data->sha_sem.session_sem);

	return -1;
}

static int qcc74x_crypto_query_caps(const struct device *dev)
{
	ARG_UNUSED(dev);
	return (CRYPTO_CIPHER_CAPS | CRYPTO_HASH_CAPS);
}

/**
 * @brief Copy key and IV to the cipher session context.
 *
 * @param ctx Pointer to the cipher context.
 * @param session Pointer to the cipher session.
 * @param iv Pointer to the initialization vector, or NULL if not used.
 * @param iv_len Length of the initialization vector in bytes.
 */
static void qcc74x_cipher_copy_key_and_iv_to_session(struct cipher_ctx *ctx,
						   struct cipher_session *session, uint8_t *iv,
						   int iv_len)
{
	for (int i = 0; i < (ctx->keylen / 4); i++) {
		memcpy((uint8_t *)&session->ctx_aes_link.aes_key0 + i * 4,
		       &ctx->key.bit_stream[i * 4], 4);
	}

	if (iv != NULL) {
		uint32_t iv_buf[BLOCK_LEN_WORDS] = {0};
		memcpy(iv_buf, iv, iv_len);
		for (int i = 0; i < BLOCK_LEN_WORDS; i++) {
			memcpy((uint8_t *)&session->ctx_aes_link.aes_iv0 + i * 4, &iv_buf[i],
			       sizeof(uint32_t));
		}
	}
}

/**
 * @brief Handle AES-ECB cipher operation.
 *
 * @param ctx Pointer to the cipher context.
 * @param pkt Pointer to the cipher packet containing input/output buffers.
 * @return int 0 on success, negative error code on failure.
 */
static int qcc74x_cipher_aes_ecb_handle(struct cipher_ctx *ctx, struct cipher_pkt *pkt)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(ctx->device);
	struct cipher_session *session = (struct cipher_session *)ctx->drv_sessn_state;

	qcc74x_cipher_copy_key_and_iv_to_session(ctx, session, NULL, 0);

	k_sem_take(&data->cipher_sem.device_sem, K_FOREVER);
	crypto_cache_addr_clean(pkt->in_buf, pkt->in_len);
	crypto_cache_addr_clean_invalidate(pkt->out_buf, pkt->in_len);

	if (qc7xx_aes_link_update(data->cipher_dev, (uint32_t)&session->ctx_aes_link, pkt->in_buf,
				  pkt->out_buf, pkt->in_len) == 0) {
		pkt->out_len = pkt->in_len;
	} else {
		LOG_ERR("qcc74x_aes_link_update failed\n");
		k_sem_give(&data->cipher_sem.device_sem);
		return -EINVAL;
	}

	k_sem_give(&data->cipher_sem.device_sem);

	return 0;
}

/**
 * @brief Handle AES-CBC cipher operation.
 *
 * @param ctx Pointer to the cipher context.
 * @param pkt Pointer to the cipher packet containing input/output buffers.
 * @param iv Pointer to the initialization vector.
 * @return int 0 on success, negative error code on failure.
 */
static int qcc74x_cipher_aes_cbc_handle(struct cipher_ctx *ctx, struct cipher_pkt *pkt, uint8_t *iv)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(ctx->device);
	struct cipher_session *session = (struct cipher_session *)ctx->drv_sessn_state;
	int offset = 0;

	qcc74x_cipher_copy_key_and_iv_to_session(ctx, session, iv, BLOCK_LEN_BYTES);

	k_sem_take(&data->cipher_sem.device_sem, K_FOREVER);
	crypto_cache_addr_clean(pkt->in_buf, pkt->in_len);
	if (session->ctx_aes_link.aes_dec_en == 1) {
		/* decode */
		if ((ctx->flags & CAP_NO_IV_PREFIX) == 0U) {
			offset = 16;
		}

		crypto_cache_addr_clean_invalidate(pkt->out_buf, pkt->in_len);

		if (qc7xx_aes_link_update(data->cipher_dev, (uint32_t)&session->ctx_aes_link,
					  pkt->in_buf + offset, pkt->out_buf, pkt->in_len) == 0) {
			pkt->out_len = pkt->in_len - offset;
		} else {
			LOG_ERR("qcc74x_aes_link_update failed\n");
			k_sem_give(&data->cipher_sem.device_sem);
			return -EINVAL;
		}
	} else {
		/* Encode */
		if ((ctx->flags & CAP_NO_IV_PREFIX) == 0U) {
			/* Prefix IV to ciphertext unless CAP_NO_IV_PREFIX is set. */
			memcpy(pkt->out_buf, iv, 16);
			offset = 16;
		}

		crypto_cache_addr_clean_invalidate(pkt->out_buf, pkt->in_len + offset);

		if (qc7xx_aes_link_update(data->cipher_dev, (uint32_t)&session->ctx_aes_link,
					  pkt->in_buf, pkt->out_buf + offset, pkt->in_len) == 0) {
			pkt->out_len = pkt->in_len + offset;
		} else {
			LOG_ERR("qcc74x_aes_link_update failed\n");
			k_sem_give(&data->cipher_sem.device_sem);
			return -EINVAL;
		}
	}
	k_sem_give(&data->cipher_sem.device_sem);

	return 0;
}

/**
 * @brief Handle AES-CTR cipher operation.
 *
 * @param ctx Pointer to the cipher context.
 * @param pkt Pointer to the cipher packet containing input/output buffers.
 * @param iv Pointer to the initialization vector.
 * @return int 0 on success, negative error code on failure.
 */
static int qcc74x_cipher_aes_ctr_handle(struct cipher_ctx *ctx, struct cipher_pkt *pkt, uint8_t *iv)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(ctx->device);
	struct cipher_session *session = (struct cipher_session *)ctx->drv_sessn_state;

	int iv_len = BLOCK_LEN_BYTES - (ctx->mode_params.ctr_info.ctr_len >> 3);

	if ((iv_len > BLOCK_LEN_BYTES) || (iv_len % 4 != 0)) {
		LOG_ERR("Invalid IV length");
		return -EINVAL;
	}

	qcc74x_cipher_copy_key_and_iv_to_session(ctx, session, iv, iv_len);

	k_sem_take(&data->cipher_sem.device_sem, K_FOREVER);
	crypto_cache_addr_clean(pkt->in_buf, pkt->in_len);
	crypto_cache_addr_clean_invalidate(pkt->out_buf, pkt->in_len);
	if (qc7xx_aes_link_update(data->cipher_dev, (uint32_t)&session->ctx_aes_link, pkt->in_buf,
				  pkt->out_buf, pkt->in_len) == 0) {
		pkt->out_len = pkt->in_len;
	} else {
		LOG_ERR("qcc74x_aes_link_update failed\n");
		k_sem_give(&data->cipher_sem.device_sem);
		return -EINVAL;
	}
	k_sem_give(&data->cipher_sem.device_sem);

	return 0;
}

static int qcc74x_cipher_session_setup(const struct device *dev, struct cipher_ctx *ctx,
				     enum cipher_algo algo, enum cipher_mode mode,
				     enum cipher_op op_type)
{
	int ctx_idx = 0;
	struct cipher_session *session;
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);

	if (data->cipher_dev == NULL) {
		LOG_ERR("No cipher device");
		return -ENODEV;
	}

	if (ctx->flags & ~(CRYPTO_CIPHER_CAPS)) {
		LOG_ERR("Unsupported flag");
		return -EINVAL;
	}

	if (algo != CRYPTO_CIPHER_ALGO_AES) {
		LOG_ERR("Unsupported algo");
		return -EINVAL;
	}

	if ((mode != CRYPTO_CIPHER_MODE_ECB) && (mode != CRYPTO_CIPHER_MODE_CBC) &&
	    (mode != CRYPTO_CIPHER_MODE_CTR)) {
		LOG_ERR("Unsupported mode");
		return -EINVAL;
	}

	/* Supports key sizes of 128, 192 and 256 bits. */
	if ((ctx->keylen != 16U) && (ctx->keylen != 24U) && (ctx->keylen != 32U)) {
		LOG_ERR("%u key size is not supported", ctx->keylen);
		return -EINVAL;
	}

	ctx_idx = qcc74x_cipher_get_unused_session_idx(dev);
	if (ctx_idx < 0) {
		LOG_ERR("No free session for now");
		return -ENOSPC;
	}
	session = &cipher_sessions[ctx_idx];
	memset(&session->ctx_aes_link, 0, sizeof(session->ctx_aes_link));

	k_sem_take(&data->cipher_sem.device_sem, K_FOREVER);
	qc7xx_group0_request_aes_access(data->cipher_dev);
	qc7xx_aes_link_init(data->cipher_dev);
	k_sem_give(&data->cipher_sem.device_sem);

	session->algo = algo;
	session->ctx_aes_link.aes_msglen = 1;

	switch (mode) {
	case CRYPTO_CIPHER_MODE_ECB:
		session->ctx_aes_link.aes_mode = AES_MODE_ECB;
		ctx->ops.block_crypt_hndlr = qcc74x_cipher_aes_ecb_handle;
		break;
	case CRYPTO_CIPHER_MODE_CBC:
		session->ctx_aes_link.aes_mode = AES_MODE_CBC;
		ctx->ops.cbc_crypt_hndlr = qcc74x_cipher_aes_cbc_handle;
		break;
	case CRYPTO_CIPHER_MODE_CTR:
		session->ctx_aes_link.aes_mode = AES_MODE_CTR;
		ctx->ops.ctr_crypt_hndlr = qcc74x_cipher_aes_ctr_handle;
		break;
	default:
		LOG_ERR("Unsupported mode");
		return -ENOTSUP;
	}

	switch (op_type) {
	case CRYPTO_CIPHER_OP_ENCRYPT:
		session->ctx_aes_link.aes_dec_en = 0;
		break;
	case CRYPTO_CIPHER_OP_DECRYPT:
		session->ctx_aes_link.aes_dec_en = 1;
		break;
	default:
		LOG_ERR("Unsupported operation");
		return -ENOTSUP;
	}

	switch (ctx->keylen) {
	case 16U:
		session->ctx_aes_link.aes_key = AES_LINK_KEY_128BITS;
		break;
	case 24U:
		session->ctx_aes_link.aes_key = AES_LINK_KEY_192BITS;
		break;
	case 32U:
		session->ctx_aes_link.aes_key = AES_LINK_KEY_256BITS;
		break;
	default:
		LOG_ERR("Unsupported key size");
		return -ENOTSUP;
	}

	ctx->drv_sessn_state = session;
	ctx->device = dev;

	return 0;
}

static int qcc74x_cipher_session_free(const struct device *dev, struct cipher_ctx *ctx)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);
	struct cipher_session *session = (struct cipher_session *)ctx->drv_sessn_state;

	k_sem_take(&data->cipher_sem.session_sem, K_FOREVER);

	memset(&session->ctx_aes_link, 0, sizeof(session->ctx_aes_link));
	session->in_use = false;
	session->algo = 0;

	/* Disable peripheral only if there are no more active sessions. */
	for (int i = 0; i < CIPHER_MAX_SESSIONS; i++) {
		if (cipher_sessions[i].in_use) {
			k_sem_give(&data->cipher_sem.session_sem);
			return 0;
		}
	}

	k_sem_give(&data->cipher_sem.session_sem);

	return 0;
}

/**
 * @brief Compute a SHA hash.
 *
 * @param ctx Pointer to the hash context.
 * @param pkt Pointer to the hash packet containing input/output buffers.
 * @param finish Flag indicating whether to finalize the hash computation.
 * @return int 0 on success, negative error code on failure.
 */
static int qcc74x_sha_compute(struct hash_ctx *ctx, struct hash_pkt *pkt, bool finish)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(ctx->device);
	struct sha_session *session = (struct sha_session *)ctx->drv_sessn_state;

	/* Hash start */
	k_sem_take(&data->sha_sem.device_sem, K_FOREVER);
	if (session->sha_state != SHA_IN_PROGRESS) {
		switch (session->algo) {
		case CRYPTO_HASH_ALGO_SHA224:
			qc7xx_sha256_link_start(data->sha_dev, &session->ctx_sha256, 1);
			break;
		case CRYPTO_HASH_ALGO_SHA256:
			qc7xx_sha256_link_start(data->sha_dev, &session->ctx_sha256, 0);
			break;
		case CRYPTO_HASH_ALGO_SHA384:
			qc7xx_sha512_link_start(data->sha_dev, &session->ctx_sha512, 1);
			break;
		case CRYPTO_HASH_ALGO_SHA512:
			qc7xx_sha512_link_start(data->sha_dev, &session->ctx_sha512, 0);
			break;
		default:
			LOG_ERR("Unsupported algorithm %d to start", session->algo);
			k_sem_give(&data->sha_sem.device_sem);
			return -ENOTSUP;
		}
		session->sha_state = SHA_IN_PROGRESS;
	}

	crypto_cache_addr_clean(pkt->in_buf, pkt->in_len);

	if (finish) {
		/* Hash finish */
		switch (session->algo) {
		case CRYPTO_HASH_ALGO_SHA224:
		case CRYPTO_HASH_ALGO_SHA256:
			qc7xx_sha256_link_update(data->sha_dev, &session->ctx_sha256, pkt->in_buf,
						 pkt->in_len);
			qc7xx_sha256_link_finish(data->sha_dev, &session->ctx_sha256, pkt->out_buf);
			session->sha_state = SHA_COMPLETED;
			break;
		case CRYPTO_HASH_ALGO_SHA384:
		case CRYPTO_HASH_ALGO_SHA512:
			qc7xx_sha512_link_update(data->sha_dev, &session->ctx_sha512, pkt->in_buf,
						 pkt->in_len);
			qc7xx_sha512_link_finish(data->sha_dev, &session->ctx_sha512, pkt->out_buf);
			session->sha_state = SHA_COMPLETED;
			break;
		default:
			LOG_ERR("Unsupported algorithm");
			k_sem_give(&data->sha_sem.device_sem);
			return -ENOTSUP;
		}
	} else {
		/* Hash update */
		switch (session->algo) {
		case CRYPTO_HASH_ALGO_SHA224:
		case CRYPTO_HASH_ALGO_SHA256:
			qc7xx_sha256_link_update(data->sha_dev, &session->ctx_sha256, pkt->in_buf,
						 pkt->in_len);
			break;
		case CRYPTO_HASH_ALGO_SHA384:
		case CRYPTO_HASH_ALGO_SHA512:
			qc7xx_sha512_link_update(data->sha_dev, &session->ctx_sha512, pkt->in_buf,
						 pkt->in_len);
			break;
		default:
			LOG_ERR("Unsupported algorithm");
			k_sem_give(&data->sha_sem.device_sem);
			return -ENOTSUP;
		}
	}
	k_sem_give(&data->sha_sem.device_sem);

	return 0;
}

/**
 * @brief Query hardware capabilities for SHA operations.
 *
 * @param dev Pointer to the device structure.
 * @return int Bitmask of supported capabilities.
 */
static inline int qcc74x_query_hw_caps(const struct device *dev)
{
	return (CAP_SEPARATE_IO_BUFS | CAP_SYNC_OPS);
}

static int qcc74x_sha_device_set_hash_type(const struct device *dev, struct hash_ctx *ctx,
					 enum hash_algo algo)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);
	int ctx_idx = 0;

	if (data->sha_dev == NULL) {
		LOG_ERR("SHA Device is NULL\n");
		return -ENODEV;
	}

	/* Check sessions */
	ctx_idx = qcc74x_sha_get_unused_session_idx(dev);
	if (ctx_idx < 0) {
		LOG_ERR("All sessions in use!");
		return -ENOSPC;
	}

	if (ctx->flags & ~(CRYPTO_HASH_CAPS)) {
		LOG_ERR("Unsupported flag");
		return -EINVAL;
	}

	/* Sha init */
	k_sem_take(&data->sha_sem.device_sem, K_FOREVER);
	qc7xx_group0_request_sha_access(data->sha_dev);
	qc7xx_sha_link_init(data->sha_dev);
	k_sem_give(&data->sha_sem.device_sem);

	ctx->drv_sessn_state = &sha_sessions[ctx_idx];
	sha_sessions[ctx_idx].algo = algo;
	sha_sessions[ctx_idx].in_use = true;
	sha_sessions[ctx_idx].sha_state = SHA_NOT_STARTED;

	ctx->hash_hndlr = qcc74x_sha_compute;

	return 0;
}

static int qcc74x_sha_device_free(const struct device *dev, struct hash_ctx *ctx)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);
	struct sha_session *session = (struct sha_session *)ctx->drv_sessn_state;

	k_sem_take(&data->sha_sem.session_sem, K_FOREVER);

	memset(&session->ctx_sha256, 0, sizeof(struct qc7xx_sha256_link_ctx_s));
	memset(&session->ctx_sha512, 0, sizeof(struct qc7xx_sha512_link_ctx_s));
	session->sha_state = SHA_NOT_STARTED;
	session->in_use = false;
	session->algo = 0;

	/* Disable peripheral only if there are no more active sessions. */
	for (int i = 0; i < SHA_MAX_SESSIONS; i++) {
		if (sha_sessions[i].in_use) {
			k_sem_give(&data->sha_sem.session_sem);
			return 0;
		}
	}

	k_sem_give(&data->sha_sem.session_sem);

	return 0;
}

/**
 * @brief Initialize the crypto device.
 *
 * @param dev Pointer to the device structure.
 * @return int 0 on success, negative error code on failure.
 */
static int crypto_qcc74x_init(const struct device *dev)
{
	struct crypto_qcc74x_data *data = CRYPTO_QCC74X_DATA(dev);
	const struct crypto_qcc74x_config *cfg = CRYPTO_QCC74X_CFG(dev);
	uint8_t instance = data->instance_id;
	int ret = 0;

	if (instance > 1) {
		LOG_ERR("Crypto is not support instance %d\n", instance);
		return -ENODEV;
	}

	cipher_dev.reg_base = cfg->reg_base;
	data->cipher_dev = &cipher_dev;
	if (NULL == data->cipher_dev) {
		LOG_ERR("Cipher Device Get Fail\n");
		return -ENODEV;
	}

	sha_dev.reg_base = cfg->reg_base;
	data->sha_dev = &sha_dev;
	if (NULL == data->sha_dev) {
		LOG_ERR("Sha Device Get Fail\n");
		return -ENODEV;
	}

	LOG_DBG("Reg base : 0x%x 0x%x", data->cipher_dev->reg_base, data->sha_dev->reg_base);

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("Clock control device not ready");
		return -ENODEV;
	}

	/* Select clock source */
	ret = clock_control_configure(cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)cfg->clock_id,
				      (void *)&cfg->clock_source);
	if (ret != 0) {
		LOG_ERR("Failed to configure SEC clock with source: %u err: %d", cfg->clock_source,
			ret);
		return ret;
	}

	/* Enable clock */
	ret = clock_control_on(cfg->clock_dev, (clock_control_subsys_t)(uintptr_t)cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for SEC driver is already enabled");
		} else {
			LOG_ERR("Failed to enable SEC clock: %d", ret);
			return ret;
		}
	}

	for (int i = 0; i < CIPHER_MAX_SESSIONS; i++) {
		memset(&cipher_sessions[i].ctx_aes_link, 0, sizeof(struct qc7xx_aes_link_s));
		cipher_sessions[i].in_use = false;
		cipher_sessions[i].algo = 0;
	}

	for (int i = 0; i < SHA_MAX_SESSIONS; i++) {
		memset(&sha_sessions[i].ctx_sha256, 0, sizeof(struct qc7xx_sha256_link_ctx_s));
		memset(&sha_sessions[i].ctx_sha512, 0, sizeof(struct qc7xx_sha512_link_ctx_s));
		sha_sessions[i].sha_state = SHA_NOT_STARTED;
		sha_sessions[i].in_use = false;
		sha_sessions[i].algo = 0;
	}

	k_sem_init(&data->cipher_sem.device_sem, 1, 1);
	k_sem_init(&data->cipher_sem.session_sem, 1, 1);
	k_sem_init(&data->sha_sem.device_sem, 1, 1);
	k_sem_init(&data->sha_sem.session_sem, 1, 1);

	return 0;
}

/**
 * @brief Crypto device API structure.
 */
static DEVICE_API(crypto, crypto_enc_funcs) = {
	.cipher_begin_session = qcc74x_cipher_session_setup,
	.cipher_free_session = qcc74x_cipher_session_free,
	.query_hw_caps = qcc74x_crypto_query_caps,
	.cipher_async_callback_set = NULL,
	.hash_begin_session = qcc74x_sha_device_set_hash_type,
	.hash_free_session = qcc74x_sha_device_free,
	.hash_async_callback_set = NULL,
};

#define QCC74X_SHA_DEVICE_INIT(idx)                                                                  \
	static struct crypto_qcc74x_data crypto_qcc74x_dev_data_##idx = {                              \
		.sha_dev = NULL,                                                                   \
		.cipher_dev = NULL,                                                                \
		.instance_id = idx,                                                                \
	};                                                                                         \
	static const struct crypto_qcc74x_config crypto_qcc74x_dev_config_##idx = {                    \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                              \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(idx, id),                  \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(idx, source),                         \
		.reg_base = DT_INST_REG_ADDR(idx),                                                 \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, crypto_qcc74x_init, NULL, &crypto_qcc74x_dev_data_##idx,            \
			      &crypto_qcc74x_dev_config_##idx, POST_KERNEL,                          \
			      CONFIG_CRYPTO_INIT_PRIORITY, (void *)&crypto_enc_funcs);

DT_INST_FOREACH_STATUS_OKAY(QCC74X_SHA_DEVICE_INIT)
