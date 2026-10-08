/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * PSA Transparent Hash Driver - Direct LHAL implementation
 * Bypasses Zephyr Crypto API, calls LHAL functions directly.
 * Supports: SHA-1, SHA-224, SHA-256, SHA-384, SHA-512, SHA-512/224, SHA-512/256
 */

#ifndef QCC74X_PSA_HASH_H_
#define QCC74X_PSA_HASH_H_

#include <psa/crypto_types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Suppress -Wunused-parameter warnings from LHAL headers when included
 * in compilation units with strict warning flags (e.g. mbedtls). */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <qc7xx_sec_sha.h>
#pragma GCC diagnostic pop

/* Hash family enum — cached in operation to avoid repeated get_hash_family() calls */
typedef enum {
	QCC74X_HASH_FAMILY_SHA1        = 0,
	QCC74X_HASH_FAMILY_SHA256      = 1,
	QCC74X_HASH_FAMILY_SHA512      = 2,
	QCC74X_HASH_FAMILY_UNSUPPORTED = 3,
} qcc74x_hash_family_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PSA Transparent Hash Driver operation context (Direct LHAL).
 *
 * The LHAL context (sha1/sha256/sha512) is embedded directly here and managed
 * by the PSA layer. No separate session pool is needed.
 *
 * Cache coherency is handled by the driver:
 *   - Before passing ctx to LHAL, qc7xx_get_no_cache_addr() converts the
 *     address to a non-cached alias so hardware reads/writes bypass CPU cache.
 *   - Input data is flushed to physical RAM before hardware access.
 *
 * NOTE: link_addr inside sha1/sha256/sha512 is a self-referencing pointer
 * (set by link_start to point to its own link_cfg). After memcpy (clone),
 * it must be fixed to the new non-cached alias address.
 */
typedef struct {
	union {
		struct qc7xx_sha1_link_ctx_s   sha1;
		struct qc7xx_sha256_link_ctx_s sha256;
		struct qc7xx_sha512_link_ctx_s sha512;
	} ctx;                       /* LHAL context, managed by PSA layer */
	psa_algorithm_t alg;
	qcc74x_hash_family_t family; /* Cached at setup to avoid repeated switch */
	bool is_setup;               /* per session, to indicate if qcc74x_transparent_hash_setup has been called correctly */
	bool is_link_started;        /* per session, to indicate if link_start has been correctly done during qcc74x_transparent_hash_update/qcc74x_transparent_hash_finish */
} qcc74x_transparent_hash_operation_t;

psa_status_t qcc74x_transparent_hash_compute(
	psa_algorithm_t alg,
	const uint8_t *input,
	size_t input_length,
	uint8_t *hash,
	size_t buffer_size,
	size_t *hash_length);

psa_status_t qcc74x_transparent_hash_setup(
	qcc74x_transparent_hash_operation_t *operation,
	psa_algorithm_t alg);

psa_status_t qcc74x_transparent_hash_update(
	qcc74x_transparent_hash_operation_t *operation,
	const uint8_t *input,
	size_t input_length);

psa_status_t qcc74x_transparent_hash_finish(
	qcc74x_transparent_hash_operation_t *operation,
	uint8_t *hash,
	size_t buffer_size,
	size_t *hash_length);

psa_status_t qcc74x_transparent_hash_abort(
	qcc74x_transparent_hash_operation_t *operation);

psa_status_t qcc74x_transparent_hash_clone(
	const qcc74x_transparent_hash_operation_t *source_operation,
	qcc74x_transparent_hash_operation_t *target_operation);

#ifdef __cplusplus
}
#endif

#endif /* QCC74X_PSA_HASH_H_ */
