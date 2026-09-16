/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Operation context type definitions.
 *
 * This header provides the driver context types WITHOUT including any PSA
 * Crypto API headers. It uses only <stdint.h>/<stddef.h> so it can be safely
 * included by psa/crypto_driver_contexts_primitives.h (which is pulled in
 * early during psa/crypto.h expansion) without creating circular dependencies.
 *
 * The driver entry-point declarations (function prototypes) live in the
 * corresponding qcc74x_psa_cipher.h / qcc74x_psa_hash.h / etc. headers,
 * which DO include <psa/crypto.h> and therefore must NOT be included from
 * crypto_driver_contexts_primitives.h.
 */

#ifndef QCC74X_PSA_DRIVER_TYPES_H
#define QCC74X_PSA_DRIVER_TYPES_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Operation context for QCC74x transparent cipher streaming operations.
 *
 * Maintained as a global singleton in qcc74x_psa_cipher.c rather than embedded
 * inside psa_driver_cipher_context_t. This avoids patching the upstream
 * crypto_driver_contexts_primitives.h header. The PSA driver wrapper obtains
 * the context address via qcc74x_cipher_get_ctx().
 *
 * Holds all state needed to continue a multi-part AES operation across
 * multiple psa_cipher_update() calls.
 *
 * For streaming updates, the driver uses AES hardware DMA link mode:
 * - link_desc: pointer to qc7xx_aes_link_s allocated in PSRAM (0x2xxxxxxx)
 *   for the AES DMA engine to read the descriptor.
 * - link_first: 1 before any link-mode operation, 0 after. Controls whether
 *   aes_newkey_dis and aes_newiv_dis are set in the link descriptor.
 */
typedef struct {
    uint8_t  key[32];       /* AES key copy (zeroized on finish/abort) */
    uint16_t key_bits;      /* 128 / 192 / 256 */
    uint8_t  aes_mode;      /* AES_MODE_ECB / CBC / CTR */
    uint8_t  is_encrypt;    /* 1 = encrypt, 0 = decrypt */
    uint8_t  iv[16];        /* Running IV: CBC chain value / CTR counter */
    uint8_t  partial[16];   /* Partial block buffer */
    size_t   partial_len;   /* Valid bytes in partial[] */
    void    *link_desc;     /* PSRAM-allocated qc7xx_aes_link_s for DMA */
    uint8_t  link_first;    /* 1 = first link operation (load key/IV) */
} qcc74x_transparent_cipher_operation_t;

#endif /* QCC74X_PSA_DRIVER_TYPES_H */