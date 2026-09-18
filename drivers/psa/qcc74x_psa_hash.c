/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * PSA Transparent Hash Driver - Direct LHAL implementation
 * Bypasses Zephyr Crypto API, calls qc7xx_sec_sha.c LHAL functions directly.
 * Supports: SHA-1, SHA-224, SHA-256, SHA-384, SHA-512, SHA-512/224, SHA-512/256
 *
 * Cache coherency strategy:
 *   - LHAL context is embedded in the PSA operation struct (PSA-managed memory).
 *   - qc7xx_get_no_cache_addr() provides a non-cached alias so all hardware
 *     reads/writes bypass CPU cache and go directly to physical RAM.
 *   - Input buffers are flushed to physical RAM before hardware access via
 *     sys_cache_data_flush_range().
 *   - No session pool or .nocache_noinit_ram needed.
 */

#include "qcc74x_psa_hash.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_sec_ll.h>
#include <zephyr/logging/log.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <qc7xx_sec_sha.h>

LOG_MODULE_REGISTER(qcc74x_psa_hash, CONFIG_CRYPTO_LOG_LEVEL);

/* ========================================================================
 * Static SHA device structure for LHAL access
 * ======================================================================== */

static struct qc7xx_device_s qcc74x_sha_dev = {
	.name = "sec_sha",
	.reg_base = DT_REG_ADDR(DT_NODELABEL(crypto)),
	.irq_num = 0xFF,
	.idx = 0,
	.sub_idx = 0,
	.dev_type = 0,
	.user_data = NULL,
};

/* Static semaphore for hardware access serialization */
static K_SEM_DEFINE(qcc74x_hash_sem, 1, 1);

/* SHA hardware initialization flag — group0_request + link_init only needed ONCE after boot */
static bool hw_initialized = false;

/* ========================================================================
 * Algorithm helpers
 * ======================================================================== */

static qcc74x_hash_family_t get_hash_family(psa_algorithm_t alg)
{
	switch (alg) {
	case PSA_ALG_SHA_1:
		return QCC74X_HASH_FAMILY_SHA1;
	case PSA_ALG_SHA_224:
	case PSA_ALG_SHA_256:
		return QCC74X_HASH_FAMILY_SHA256;
	case PSA_ALG_SHA_384:
	case PSA_ALG_SHA_512:
	case PSA_ALG_SHA_512_224:
	case PSA_ALG_SHA_512_256:
		return QCC74X_HASH_FAMILY_SHA512;
	default:
		return QCC74X_HASH_FAMILY_UNSUPPORTED;
	}
}

static size_t get_hash_output_length(psa_algorithm_t alg)
{
	switch (alg) {
	case PSA_ALG_SHA_1:       return 20;
	case PSA_ALG_SHA_224:     return 28;
	case PSA_ALG_SHA_256:     return 32;
	case PSA_ALG_SHA_384:     return 48;
	case PSA_ALG_SHA_512:     return 64;
	case PSA_ALG_SHA_512_224: return 28;
	case PSA_ALG_SHA_512_256: return 32;
	default:                  return 0;
	}
}

/* ========================================================================
 * Hardware access helpers
 * ======================================================================== */

static inline void hw_lock(void)
{
	k_sem_take(&qcc74x_hash_sem, K_FOREVER);
}

static inline void hw_unlock(void)
{
	k_sem_give(&qcc74x_hash_sem);
}

static inline struct qc7xx_device_s *get_sha_dev(void)
{
	return &qcc74x_sha_dev;
}

/* Ensure SHA hardware is initialized for link mode (once after boot). */
static inline void hw_ensure_initialized(struct qc7xx_device_s *sha_dev)
{
	if (!hw_initialized) {
		qc7xx_group0_request_sha_access(sha_dev);
		qc7xx_sha_link_init(sha_dev);
		hw_initialized = true;
	}
}

/* ========================================================================
 * Cache coherency helper
 *
 * Returns the non-cached alias of addr so LHAL reads/writes bypass CPU cache.
 * Falls back to the original address if qc7xx_get_no_cache_addr() returns NULL
 * (addr is already in a non-cacheable region).
 * ======================================================================== */

static inline void *nc_addr(void *addr)
{
	void *nc = qc7xx_get_no_cache_addr(addr);

	return nc ? nc : addr;
}

/* ========================================================================
 * link_start helper (called on first update/finish)
 *
 * All LHAL calls receive the non-cached alias (nc_xxx) so that link_start
 * initialises link_addr = &nc_ctx->link_cfg (a non-cached address).
 * The SHA hardware then reads the link descriptor from physical RAM directly.
 * ======================================================================== */

static void do_link_start(qcc74x_transparent_hash_operation_t *operation,
			  struct qc7xx_device_s *sha_dev)
{
	qcc74x_hash_family_t family = operation->family;

	switch (family) {
	case QCC74X_HASH_FAMILY_SHA1: {
		struct qc7xx_sha1_link_ctx_s *nc = (struct qc7xx_sha1_link_ctx_s *)nc_addr(&operation->ctx.sha1);
		qc7xx_sha1_link_start(sha_dev, nc);
		operation->is_link_started = true;
		break;
	}
	case QCC74X_HASH_FAMILY_SHA256: {
		struct qc7xx_sha256_link_ctx_s *nc = (struct qc7xx_sha256_link_ctx_s *)nc_addr(&operation->ctx.sha256);
		qc7xx_sha256_link_start(sha_dev, nc,
					(operation->alg == PSA_ALG_SHA_224) ? 1 : 0);
		operation->is_link_started = true;
		break;
	}
	case QCC74X_HASH_FAMILY_SHA512: {
		struct qc7xx_sha512_link_ctx_s *nc = (struct qc7xx_sha512_link_ctx_s *)nc_addr(&operation->ctx.sha512);
		qc7xx_sha512_link_start(sha_dev, nc,
					(operation->alg == PSA_ALG_SHA_384) ? 1 : 0);
		if (operation->alg == PSA_ALG_SHA_512_224) {
			nc->link_cfg.sha_mode = SHA_MODE_SHA512T224;
		}
		if (operation->alg == PSA_ALG_SHA_512_256) {
			nc->link_cfg.sha_mode = SHA_MODE_SHA512T256;
		}
		operation->is_link_started = true;
		break;
	}
	default:
		/* unreachable: family validated at setup */
		LOG_ERR("do_link_start: unsupported family %d", family);
		__ASSERT(false, "do_link_start: unsupported family");
		break;
	}
}

/* ========================================================================
 * PSA Transparent Driver Entry Points
 * ======================================================================== */

psa_status_t qcc74x_transparent_hash_setup(
	qcc74x_transparent_hash_operation_t *operation,
	psa_algorithm_t alg)
{
	qcc74x_hash_family_t family = get_hash_family(alg);

	if (family == QCC74X_HASH_FAMILY_UNSUPPORTED) {
		return PSA_ERROR_NOT_SUPPORTED;
	}

	/* Hardware init is deferred to the first update/finish call. */
	operation->alg = alg;
	operation->family = family;
	operation->is_setup = true;
	operation->is_link_started = false;

	return PSA_SUCCESS;
}

psa_status_t qcc74x_transparent_hash_update(
	qcc74x_transparent_hash_operation_t *operation,
	const uint8_t *input,
	size_t input_length)
{
	struct qc7xx_device_s *sha_dev;
	qcc74x_hash_family_t family;
	int ret = 0;
	int cache_ret;


	if (!operation->is_setup) {
		return PSA_ERROR_BAD_STATE;
	}

	if (input_length == 0) {
		return PSA_SUCCESS;
	}

	sha_dev = get_sha_dev();
	family = operation->family;


	/* Synchronize the operation context and input before hardware access. */
	if (qc7xx_check_cache_addr(&operation->ctx)) {
		cache_ret = sys_cache_data_flush_and_invd_range(&operation->ctx, sizeof(operation->ctx));
		if (cache_ret != 0) {
			LOG_ERR("failed to flush/invalidate hash context cache: %p", operation);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}

	if (qc7xx_check_cache_addr((void *)input)) {
		cache_ret = sys_cache_data_flush_range((void *)input, input_length);
		if (cache_ret != 0) {
			LOG_ERR("failed to flush hash input cache: %p", input);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}

	hw_lock();
	/* On first invocation: link_init + link_start in one lock hold */
	if (!operation->is_link_started) {
		hw_ensure_initialized(sha_dev);
		do_link_start(operation, sha_dev);
	}

	switch (family) {
	case QCC74X_HASH_FAMILY_SHA1:
		ret = qc7xx_sha1_link_update(sha_dev,
			(struct qc7xx_sha1_link_ctx_s *)nc_addr(&operation->ctx.sha1),
			input, input_length);
		break;
	case QCC74X_HASH_FAMILY_SHA256:
		ret = qc7xx_sha256_link_update(sha_dev,
			(struct qc7xx_sha256_link_ctx_s *)nc_addr(&operation->ctx.sha256),
			input, input_length);
		break;
	case QCC74X_HASH_FAMILY_SHA512:
		ret = qc7xx_sha512_link_update(sha_dev,
			(struct qc7xx_sha512_link_ctx_s *)nc_addr(&operation->ctx.sha512),
			input, (uint64_t)input_length);
		break;
	default:
		/* unreachable */
		LOG_ERR("hash_update: unexpected family %d", family);
		__ASSERT(false, "hash_update: unexpected family");
		hw_unlock();
		return PSA_ERROR_BAD_STATE;
	}

	hw_unlock();

	/* NO need to invalidate the cachable operation->ctx at the end of this function,
	 * because it will get invalidated at the following update or finish.
	 * The normal callflow is -- setup + update ... + finish, or setup + finish */

	if (ret != 0) {
		LOG_ERR("hash link_update failed: %d", ret);
		qcc74x_transparent_hash_abort(operation);
		return PSA_ERROR_HARDWARE_FAILURE;
	}

	return PSA_SUCCESS;
}

psa_status_t qcc74x_transparent_hash_finish(
	qcc74x_transparent_hash_operation_t *operation,
	uint8_t *hash,
	size_t buffer_size,
	size_t *hash_length)
{
	struct qc7xx_device_s *sha_dev;
	qcc74x_hash_family_t family;
	size_t output_len;
	int cache_ret;


	if (!operation->is_setup) {
		return PSA_ERROR_BAD_STATE;
	}

	family = operation->family;
	output_len = get_hash_output_length(operation->alg);
	if (buffer_size < output_len) {
		return PSA_ERROR_BUFFER_TOO_SMALL;
	}

	sha_dev = get_sha_dev();

	if (qc7xx_check_cache_addr(&operation->ctx)) {
		cache_ret = sys_cache_data_flush_and_invd_range(&operation->ctx, sizeof(operation->ctx));
		if (cache_ret != 0) {
			LOG_ERR("failed to flush/invalidate hash context cache: %p",
				operation);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}

	hw_lock();

	/* If no update was called (empty message hash), do deferred init here */
	if (!operation->is_link_started) {
		hw_ensure_initialized(sha_dev);
		do_link_start(operation, sha_dev);
	}

	/* All LHAL calls use non-cached alias; link_finish reads result from
	 * physical RAM via nc pointer and copies to hash via CPU -- no invalidate
	 * needed on the hash output buffer. */
	switch (family) {
	case QCC74X_HASH_FAMILY_SHA1:
		qc7xx_sha1_link_finish(sha_dev,
			(struct qc7xx_sha1_link_ctx_s *)nc_addr(&operation->ctx.sha1),
			hash);
		break;
	case QCC74X_HASH_FAMILY_SHA256:
		qc7xx_sha256_link_finish(sha_dev,
			(struct qc7xx_sha256_link_ctx_s *)nc_addr(&operation->ctx.sha256),
			hash);
		break;
	case QCC74X_HASH_FAMILY_SHA512:
		qc7xx_sha512_link_finish(sha_dev,
			(struct qc7xx_sha512_link_ctx_s *)nc_addr(&operation->ctx.sha512),
			hash);
		break;
	default:
		/* unreachable */
		LOG_ERR("hash_finish: unexpected family %d", family);
		__ASSERT(false, "hash_finish: unexpected family");
		hw_unlock();
		return PSA_ERROR_BAD_STATE;
	}

	hw_unlock();

	/* NO need to invalidate the output hash at the end of this function,
	 * because the callflow is HW -> non-cachable RAM -> cache(output hash).it51xxx_cycle_types
	 * It's not HW -> cachable RAM */

	operation->is_setup = false;
	operation->is_link_started = false;
	*hash_length = output_len;

	return PSA_SUCCESS;
}

psa_status_t qcc74x_transparent_hash_abort(
	qcc74x_transparent_hash_operation_t *operation)
{
	if (operation->is_setup) {
		/* Clear LHAL state in physical RAM via non-cached alias */
		void *nc = qc7xx_get_no_cache_addr(&operation->ctx);
		if (nc) {
			mbedtls_platform_zeroize(nc, sizeof(operation->ctx));
		}
	}
	mbedtls_platform_zeroize(operation, sizeof(*operation));
	return PSA_SUCCESS;
}

psa_status_t qcc74x_transparent_hash_clone(
	const qcc74x_transparent_hash_operation_t *source_operation,
	qcc74x_transparent_hash_operation_t *target_operation)
{
	if (!source_operation->is_setup) {
		return PSA_ERROR_BAD_STATE;
	}

	int cache_ret;

	if (qc7xx_check_cache_addr((void *)&source_operation->ctx)) {
		cache_ret = sys_cache_data_flush_and_invd_range((void *)&source_operation->ctx, sizeof(source_operation->ctx));
		if (cache_ret != 0) {
			LOG_ERR("failed to flush/invalidate source hash context: %p", source_operation);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}

	if (qc7xx_check_cache_addr((void *)&target_operation->ctx)) {
		cache_ret = sys_cache_data_invd_range((void *)&target_operation->ctx, sizeof(target_operation->ctx));
		if (cache_ret != 0) {
			LOG_ERR("failed to flush/invalidate target hash context: %p", target_operation);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}

	/* Copy scalar fields */
	target_operation->alg             = source_operation->alg;
	target_operation->family          = source_operation->family;
	target_operation->is_setup        = true;
	target_operation->is_link_started = source_operation->is_link_started;

	if (!source_operation->is_link_started) {
		/* link_start not yet called -- no LHAL state to copy */
		return PSA_SUCCESS;
	}

	/* Copy LHAL context via non-cached aliases:
	 *   - Read from nc_src: bypasses cache, gets latest hardware-written
	 *     data from physical RAM.
	 *   - Write to nc_tgt: bypasses cache, goes directly to physical RAM.
	 * Then fix the self-referencing link_addr to point to nc_tgt's link_cfg. */
	switch (source_operation->family) {
	case QCC74X_HASH_FAMILY_SHA1: {
		struct qc7xx_sha1_link_ctx_s *nc_src = (struct qc7xx_sha1_link_ctx_s *) nc_addr((void *)&source_operation->ctx.sha1);
		struct qc7xx_sha1_link_ctx_s *nc_tgt = (struct qc7xx_sha1_link_ctx_s *) nc_addr(&target_operation->ctx.sha1);
		memcpy(nc_tgt, nc_src, sizeof(*nc_tgt));
		nc_tgt->link_addr = (uint32_t)(uintptr_t)&nc_tgt->link_cfg;
		break;
	}
	case QCC74X_HASH_FAMILY_SHA256: {
		struct qc7xx_sha256_link_ctx_s *nc_src = (struct qc7xx_sha256_link_ctx_s *) nc_addr((void *)&source_operation->ctx.sha256);
		struct qc7xx_sha256_link_ctx_s *nc_tgt = (struct qc7xx_sha256_link_ctx_s *) nc_addr(&target_operation->ctx.sha256);
		memcpy(nc_tgt, nc_src, sizeof(*nc_tgt));
		nc_tgt->link_addr = (uint32_t)(uintptr_t)&nc_tgt->link_cfg;
		break;
	}
	case QCC74X_HASH_FAMILY_SHA512: {
		struct qc7xx_sha512_link_ctx_s *nc_src = (struct qc7xx_sha512_link_ctx_s *) nc_addr((void *)&source_operation->ctx.sha512);
		struct qc7xx_sha512_link_ctx_s *nc_tgt = (struct qc7xx_sha512_link_ctx_s *) nc_addr(&target_operation->ctx.sha512);
		memcpy(nc_tgt, nc_src, sizeof(*nc_tgt));
		nc_tgt->link_addr = (uint32_t)(uintptr_t)&nc_tgt->link_cfg;
		break;
	}
	default:
		mbedtls_platform_zeroize(target_operation, sizeof(*target_operation));
		return PSA_ERROR_BAD_STATE;
	}

	/* NO need to invalidate the cachable tgt->ctx at the end of this function,
	 * because it will get invalidated at the following update or finish.
	 * The normal callflow is -- setup + update ... + finish, or setup + finish */

	return PSA_SUCCESS;
}

/* ========================================================================
 * One-shot hash compute.
 *
 * Uses a local context on the stack. Non-cached alias obtained via
 * qc7xx_get_no_cache_addr() so hardware accesses bypass CPU cache.
 * hw_lock is held for the entire operation to serialize against concurrent
 * multi-part operations.
 * ======================================================================== */

psa_status_t qcc74x_transparent_hash_compute(
	psa_algorithm_t alg,
	const uint8_t *input,
	size_t input_length,
	uint8_t *hash,
	size_t buffer_size,
	size_t *hash_length)
{
	struct qc7xx_device_s *sha_dev;
	qcc74x_hash_family_t family;
	size_t output_len;
	int ret = 0;
	union {
		struct qc7xx_sha1_link_ctx_s   sha1;
		struct qc7xx_sha256_link_ctx_s sha256;
		struct qc7xx_sha512_link_ctx_s sha512;
	} ctx = { 0 };
	int cache_ret;


	family = get_hash_family(alg);
	if (family == QCC74X_HASH_FAMILY_UNSUPPORTED) {
		return PSA_ERROR_NOT_SUPPORTED;
	}

	output_len = get_hash_output_length(alg);
	if (buffer_size < output_len) {
		return PSA_ERROR_BUFFER_TOO_SMALL;
	}

	sha_dev = get_sha_dev();

	/* Synchronize the local context and input before hardware access. */
	if (qc7xx_check_cache_addr(&ctx)) {
		cache_ret = sys_cache_data_flush_and_invd_range(&ctx, sizeof(ctx));
		if (cache_ret != 0) {
			LOG_ERR("failed to flush/invalidate one-shot hash context: %p", &ctx);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}

	if (input_length > 0 && qc7xx_check_cache_addr((void *)input)) {
		cache_ret = sys_cache_data_flush_range((void *)input, input_length);
		if (cache_ret != 0) {
			LOG_ERR("failed to flush one-shot hash input cache: %p", input);
			return PSA_ERROR_HARDWARE_FAILURE;
		}
	}
  
	hw_lock();
	hw_ensure_initialized(sha_dev);

	switch (family) {
	case QCC74X_HASH_FAMILY_SHA1: {
		struct qc7xx_sha1_link_ctx_s *nc =
			(struct qc7xx_sha1_link_ctx_s *)nc_addr(&ctx.sha1);
		qc7xx_sha1_link_start(sha_dev, nc);
		ret = qc7xx_sha1_link_update(sha_dev, nc, input, input_length);
		if (ret == 0) {
			qc7xx_sha1_link_finish(sha_dev, nc, hash);
		}
		break;
	}
	case QCC74X_HASH_FAMILY_SHA256: {
		struct qc7xx_sha256_link_ctx_s *nc =
			(struct qc7xx_sha256_link_ctx_s *)nc_addr(&ctx.sha256);
		qc7xx_sha256_link_start(sha_dev, nc, (alg == PSA_ALG_SHA_224) ? 1 : 0);
		ret = qc7xx_sha256_link_update(sha_dev, nc, input, input_length);
		if (ret == 0) {
			qc7xx_sha256_link_finish(sha_dev, nc, hash);
		}
		break;
	}
	case QCC74X_HASH_FAMILY_SHA512: {
		struct qc7xx_sha512_link_ctx_s *nc =
			(struct qc7xx_sha512_link_ctx_s *)nc_addr(&ctx.sha512);
		qc7xx_sha512_link_start(sha_dev, nc, (alg == PSA_ALG_SHA_384) ? 1 : 0);
		if (alg == PSA_ALG_SHA_512_224) {
			nc->link_cfg.sha_mode = SHA_MODE_SHA512T224;
		}
		if (alg == PSA_ALG_SHA_512_256) {
			nc->link_cfg.sha_mode = SHA_MODE_SHA512T256;
		}
		ret = qc7xx_sha512_link_update(sha_dev, nc, input, (uint64_t)input_length);
		if (ret == 0) {
			qc7xx_sha512_link_finish(sha_dev, nc, hash);
		}
		break;
	}
	default:
		/* unreachable */
		LOG_ERR("hash_compute: unexpected family %d", family);
		__ASSERT(false, "hash_compute: unexpected family");
		hw_unlock();
		return PSA_ERROR_BAD_STATE;
	}

	hw_unlock();

	/* NO need to invalidate the output hash at the end of this function,
	 * because the callflow is HW -> non-cachable RAM -> cache(output hash).it51xxx_cycle_types
	 * It's not HW -> cachable RAM */

	if (ret != 0) {
		LOG_ERR("hash_compute: link_update failed: %d", ret);
		return PSA_ERROR_HARDWARE_FAILURE;
	}

	*hash_length = output_len;
	return PSA_SUCCESS;
}
