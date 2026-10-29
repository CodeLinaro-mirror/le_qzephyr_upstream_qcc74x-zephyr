/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — AEAD one-shot implementation
 *
 * Implements AES-GCM using:
 *   - HW AES-ECB (via qcc74x_transparent_cipher_encrypt) for CTR keystream
 *   - Software GF(2^128) GHASH for authentication tag
 *
 * GCM encryption decomposition (independent steps):
 *   1. H = AES_ECB(K, 0^128)                    ← HW AES-ECB
 *   2. J0 = nonce || 0^31 || 1
 *   3. For each plaintext block Pi:
 *        Ci = Pi XOR AES_ECB(K, J0 + i)         ← HW AES-ECB (CTR mode)
 *   4. S = GHASH(H, AAD || C || len(AAD) || len(C))  ← SW GHASH
 *   5. Tag = S XOR AES_ECB(K, J0)               ← HW AES-ECB
 */

#include "qcc74x_psa_aead.h"
#include "qcc74x_psa_cipher.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <mbedtls/platform_util.h>

/* ================================================================
 * Constants
 * ================================================================ */
#define AES_BLOCK_SIZE 16
#define GCM_NONCE_SIZE 12  /* 96-bit nonce (standard GCM) */
#define GCM_TAG_SIZE   16  /* 128-bit auth tag */

/* ================================================================
 * GF(2^128) multiplication for GHASH
 *
 * Computes X * Y in GF(2^128) with the irreducible polynomial
 *   x^128 + x^7 + x^2 + x + 1
 *
 * Reference: NIST SP 800-38D, Algorithm 1
 * ================================================================ */
static void gcm_gf128_mult(const uint8_t *x, const uint8_t *y, uint8_t *out)
{
    uint64_t z[2] = {0, 0};  /* Z = 0 */
    uint64_t v[2];
    uint8_t yi;
    uint64_t r;

    /* Load y as v (big-endian) */
    v[0] = ((uint64_t)y[0] << 56) | ((uint64_t)y[1] << 48) |
           ((uint64_t)y[2] << 40) | ((uint64_t)y[3] << 32) |
           ((uint64_t)y[4] << 24) | ((uint64_t)y[5] << 16) |
           ((uint64_t)y[6] <<  8) | (uint64_t)y[7];
    v[1] = ((uint64_t)y[8] << 56) | ((uint64_t)y[9] << 48) |
           ((uint64_t)y[10]<< 40) | ((uint64_t)y[11]<< 32) |
           ((uint64_t)y[12]<< 24) | ((uint64_t)y[13]<< 16) |
           ((uint64_t)y[14]<<  8) | (uint64_t)y[15];

    for (int i = 0; i < 128; i++) {
        yi = (x[i / 8] >> (7 - (i % 8))) & 1;
        if (yi) {
            z[0] ^= v[0];
            z[1] ^= v[1];
        }
        r = v[1] & 1;
        v[1] = (v[0] << 63) | (v[1] >> 1);
        v[0] = v[0] >> 1;
        if (r) {
            v[0] ^= 0xE100000000000000ULL;  /* R = 0xE1 << 120 */
        }
    }

    /* Store z as big-endian */
    out[0]  = (uint8_t)(z[0] >> 56); out[1]  = (uint8_t)(z[0] >> 48);
    out[2]  = (uint8_t)(z[0] >> 40); out[3]  = (uint8_t)(z[0] >> 32);
    out[4]  = (uint8_t)(z[0] >> 24); out[5]  = (uint8_t)(z[0] >> 16);
    out[6]  = (uint8_t)(z[0] >>  8); out[7]  = (uint8_t)(z[0]);
    out[8]  = (uint8_t)(z[1] >> 56); out[9]  = (uint8_t)(z[1] >> 48);
    out[10] = (uint8_t)(z[1] >> 40); out[11] = (uint8_t)(z[1] >> 32);
    out[12] = (uint8_t)(z[1] >> 24); out[13] = (uint8_t)(z[1] >> 16);
    out[14] = (uint8_t)(z[1] >>  8); out[15] = (uint8_t)(z[1]);
}

/* ================================================================
 * GHASH computation
 *
 * GHASH(H, X) where X = AAD || C || len(AAD)_64 || len(C)_64
 * Each component is padded to 16-byte boundary.
 * ================================================================ */
static void gcm_ghash(const uint8_t *h, const uint8_t *aad, size_t aad_len,
                       const uint8_t *ciphertext, size_t ciphertext_len,
                       uint8_t *out)
{
    uint8_t y[AES_BLOCK_SIZE] = {0};
    uint8_t block[AES_BLOCK_SIZE];
    size_t i;
    size_t padded_aad_len = ((aad_len + 15) / 16) * 16;
    size_t padded_c_len  = ((ciphertext_len + 15) / 16) * 16;

    /* Process AAD blocks */
    for (i = 0; i < padded_aad_len; i += AES_BLOCK_SIZE) {
        size_t copy = (aad_len - i >= AES_BLOCK_SIZE) ? AES_BLOCK_SIZE : (aad_len - i);
        memset(block, 0, AES_BLOCK_SIZE);
        if (copy > 0) {
            memcpy(block, aad + i, copy);
        }
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            y[j] ^= block[j];
        }
        gcm_gf128_mult(y, h, y);
    }

    /* Process ciphertext blocks */
    for (i = 0; i < padded_c_len; i += AES_BLOCK_SIZE) {
        size_t copy = (ciphertext_len - i >= AES_BLOCK_SIZE) ? AES_BLOCK_SIZE : (ciphertext_len - i);
        memset(block, 0, AES_BLOCK_SIZE);
        if (copy > 0) {
            memcpy(block, ciphertext + i, copy);
        }
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            y[j] ^= block[j];
        }
        gcm_gf128_mult(y, h, y);
    }

    /* Process length block: len(AAD) || len(C) as 64-bit big-endian */
    memset(block, 0, AES_BLOCK_SIZE);
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t c_bits   = (uint64_t)ciphertext_len * 8;
    block[0]  = (uint8_t)(aad_bits >> 56);
    block[1]  = (uint8_t)(aad_bits >> 48);
    block[2]  = (uint8_t)(aad_bits >> 40);
    block[3]  = (uint8_t)(aad_bits >> 32);
    block[4]  = (uint8_t)(aad_bits >> 24);
    block[5]  = (uint8_t)(aad_bits >> 16);
    block[6]  = (uint8_t)(aad_bits >>  8);
    block[7]  = (uint8_t)(aad_bits);
    block[8]  = (uint8_t)(c_bits >> 56);
    block[9]  = (uint8_t)(c_bits >> 48);
    block[10] = (uint8_t)(c_bits >> 40);
    block[11] = (uint8_t)(c_bits >> 32);
    block[12] = (uint8_t)(c_bits >> 24);
    block[13] = (uint8_t)(c_bits >> 16);
    block[14] = (uint8_t)(c_bits >>  8);
    block[15] = (uint8_t)(c_bits);

    for (int j = 0; j < AES_BLOCK_SIZE; j++) {
        y[j] ^= block[j];
    }
    gcm_gf128_mult(y, h, y);

    memcpy(out, y, AES_BLOCK_SIZE);
}

/* ================================================================
 * AES-CTR single block keystream via HW AES-ECB
 *
 * Uses psa_cipher_encrypt with PSA_ALG_ECB_NO_PADDING to compute
 *   keystream = AES_ECB(K, counter_block)
 *
 * NOTE: For ECB mode, psa_cipher_encrypt treats the "iv" parameter as
 * the input plaintext. We pass the counter value as the input.
 * ================================================================ */
static psa_status_t aes_ecb_block(const psa_key_attributes_t *key_attr,
                                   const uint8_t *input, uint8_t *output)
{
    size_t out_len;
    return qcc74x_transparent_cipher_encrypt(
        key_attr, NULL, 0,
        PSA_ALG_ECB_NO_PADDING,
        NULL, 0,               /* ECB ignores iv */
        input, AES_BLOCK_SIZE,
        output, AES_BLOCK_SIZE, &out_len);
}

/* ================================================================
 * Increment GCM counter (big-endian, only last 32 bits)
 * ================================================================ */
static void gcm_inc32(uint8_t *counter)
{
    for (int i = 15; i >= 12; i--) {
        counter[i]++;
        if (counter[i] != 0) break;
    }
}

/* ================================================================
 * qcc74x_aead_encrypt — HW accelerated AES-GCM encrypt
 * ================================================================ */
psa_status_t qcc74x_aead_encrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *ciphertext, size_t ciphertext_size,
    size_t *ciphertext_length)
{
#if !defined(CONFIG_QCC74X_AEAD_HW_ACCEL)
    (void)attributes; (void)key_buffer; (void)key_buffer_size;
    (void)nonce; (void)nonce_length;
    (void)additional_data; (void)additional_data_length;
    (void)plaintext; (void)plaintext_length;
    (void)ciphertext; (void)ciphertext_size; (void)ciphertext_length;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_key_attributes_t key_attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id;
    psa_status_t status;
    uint8_t h[AES_BLOCK_SIZE];
    uint8_t j0[AES_BLOCK_SIZE];
    uint8_t ctr[AES_BLOCK_SIZE];
    uint8_t ecb_out[AES_BLOCK_SIZE];
    uint8_t ghash_out[AES_BLOCK_SIZE];
    uint8_t tag[AES_BLOCK_SIZE];
    size_t out_len;
    size_t offset;

    /* Only support AES-GCM */
    if (alg != PSA_ALG_GCM) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    /* Validate nonce length (standard GCM uses 12 bytes) */
    if (nonce_length != GCM_NONCE_SIZE) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    /* Check minimum output size: ciphertext = plaintext + tag */
    if (ciphertext_size < plaintext_length + GCM_TAG_SIZE) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    /* Validate key type */
    if (psa_get_key_type(attributes) != PSA_KEY_TYPE_AES) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

#ifdef CONFIG_QCC74X_AES_HWKEY
    /* HW key path: skip psa_import_key, pass NULL to cipher for OTP key */
    psa_set_key_type(&key_attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&key_attr, psa_get_key_bits(attributes));
    psa_set_key_algorithm(&key_attr, PSA_ALG_ECB_NO_PADDING);
    psa_set_key_usage_flags(&key_attr, PSA_KEY_USAGE_ENCRYPT);

    /* ---- Step 1: H = AES_ECB(K, 0^128) ---- */
    {
        uint8_t zero_block[AES_BLOCK_SIZE] = {0};
        status = qcc74x_transparent_cipher_encrypt(
            &key_attr, NULL, 0, PSA_ALG_ECB_NO_PADDING,
            NULL, 0, zero_block, AES_BLOCK_SIZE,
            h, AES_BLOCK_SIZE, &out_len);
        if (status == PSA_ERROR_HARDWARE_FAILURE ||
            status == PSA_ERROR_NOT_SUPPORTED) {
            goto fallback_sw_enc;
        }
        if (status != PSA_SUCCESS) goto cleanup;
    }

    /* ---- Step 2: J0 = nonce || 0^31 || 1 ---- */
    memset(j0, 0, AES_BLOCK_SIZE);
    memcpy(j0, nonce, nonce_length);
    j0[AES_BLOCK_SIZE - 1] = 0x01;

    /* ---- Step 3: AES-CTR encrypt plaintext ---- */
    memcpy(ctr, j0, AES_BLOCK_SIZE);
    gcm_inc32(ctr);

    for (offset = 0; offset < plaintext_length; offset += AES_BLOCK_SIZE) {
        size_t chunk = plaintext_length - offset;
        if (chunk > AES_BLOCK_SIZE) chunk = AES_BLOCK_SIZE;

        status = qcc74x_transparent_cipher_encrypt(
            &key_attr, NULL, 0, PSA_ALG_ECB_NO_PADDING,
            NULL, 0, ctr, AES_BLOCK_SIZE,
            ecb_out, AES_BLOCK_SIZE, &out_len);
        if (status != PSA_SUCCESS) goto cleanup;

        for (size_t i = 0; i < chunk; i++) {
            ciphertext[offset + i] = plaintext[offset + i] ^ ecb_out[i];
        }
        gcm_inc32(ctr);
    }
    *ciphertext_length = plaintext_length;

    /* ---- Step 4: GHASH(H, AAD || C || len(AAD) || len(C)) ---- */
    gcm_ghash(h, additional_data, additional_data_length,
              ciphertext, plaintext_length, ghash_out);

    /* ---- Step 5: Tag = GHASH_out XOR AES_ECB(K, J0) ---- */
    status = qcc74x_transparent_cipher_encrypt(
        &key_attr, NULL, 0, PSA_ALG_ECB_NO_PADDING,
        NULL, 0, j0, AES_BLOCK_SIZE,
        ecb_out, AES_BLOCK_SIZE, &out_len);
    if (status != PSA_SUCCESS) goto cleanup;

    for (int i = 0; i < GCM_TAG_SIZE; i++) {
        tag[i] = ghash_out[i] ^ ecb_out[i];
    }

    memcpy(ciphertext + plaintext_length, tag, GCM_TAG_SIZE);
    *ciphertext_length = plaintext_length + GCM_TAG_SIZE;

    goto cleanup;

fallback_sw_enc:
    /* HW key path failed (e.g. OTP key not fused). Fall back to SW key. */
    mbedtls_platform_zeroize(h, sizeof(h));
#endif /* CONFIG_QCC74X_AES_HWKEY */

    /* SW key path: import key then use AES-ECB */
    psa_set_key_type(&key_attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&key_attr, psa_get_key_bits(attributes));
    psa_set_key_algorithm(&key_attr, PSA_ALG_ECB_NO_PADDING);
    psa_set_key_usage_flags(&key_attr, PSA_KEY_USAGE_ENCRYPT);

    status = psa_import_key(&key_attr, key_buffer, key_buffer_size, &key_id);
    if (status != PSA_SUCCESS) {
        return status;
    }
    psa_set_key_id(&key_attr, key_id);

    /* ---- Step 1: H = AES_ECB(K, 0^128) ---- */
    {
        uint8_t zero_block[AES_BLOCK_SIZE] = {0};
        status = aes_ecb_block(&key_attr, zero_block, h);
        if (status != PSA_SUCCESS) goto cleanup_sw;
    }

    /* ---- Step 2: J0 = nonce || 0^31 || 1 ---- */
    memset(j0, 0, AES_BLOCK_SIZE);
    memcpy(j0, nonce, nonce_length);
    j0[AES_BLOCK_SIZE - 1] = 0x01;

    /* ---- Step 3: AES-CTR encrypt plaintext ---- */
    memcpy(ctr, j0, AES_BLOCK_SIZE);
    gcm_inc32(ctr);

    for (offset = 0; offset < plaintext_length; offset += AES_BLOCK_SIZE) {
        size_t chunk = plaintext_length - offset;
        if (chunk > AES_BLOCK_SIZE) chunk = AES_BLOCK_SIZE;

        status = aes_ecb_block(&key_attr, ctr, ecb_out);
        if (status != PSA_SUCCESS) goto cleanup_sw;

        for (size_t i = 0; i < chunk; i++) {
            ciphertext[offset + i] = plaintext[offset + i] ^ ecb_out[i];
        }
        gcm_inc32(ctr);
    }
    *ciphertext_length = plaintext_length;

    /* ---- Step 4: GHASH(H, AAD || C || len(AAD) || len(C)) ---- */
    gcm_ghash(h, additional_data, additional_data_length,
              ciphertext, plaintext_length, ghash_out);

    /* ---- Step 5: Tag = GHASH_out XOR AES_ECB(K, J0) ---- */
    status = aes_ecb_block(&key_attr, j0, ecb_out);
    if (status != PSA_SUCCESS) goto cleanup_sw;

    for (int i = 0; i < GCM_TAG_SIZE; i++) {
        tag[i] = ghash_out[i] ^ ecb_out[i];
    }

    memcpy(ciphertext + plaintext_length, tag, GCM_TAG_SIZE);
    *ciphertext_length = plaintext_length + GCM_TAG_SIZE;

cleanup_sw:
    psa_destroy_key(key_id);
    /* fall through to cleanup */

cleanup:
    mbedtls_platform_zeroize(h, sizeof(h));
    mbedtls_platform_zeroize(j0, sizeof(j0));
    mbedtls_platform_zeroize(ctr, sizeof(ctr));
    mbedtls_platform_zeroize(ecb_out, sizeof(ecb_out));
    mbedtls_platform_zeroize(ghash_out, sizeof(ghash_out));
    mbedtls_platform_zeroize(tag, sizeof(tag));
    return status;
#endif /* CONFIG_QCC74X_AEAD_HW_ACCEL */
}

/* ================================================================
 * qcc74x_aead_decrypt — HW accelerated AES-GCM decrypt
 * ================================================================ */
psa_status_t qcc74x_aead_decrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *ciphertext, size_t ciphertext_length,
    uint8_t *plaintext, size_t plaintext_size,
    size_t *plaintext_length)
{
#if !defined(CONFIG_QCC74X_AEAD_HW_ACCEL)
    (void)attributes; (void)key_buffer; (void)key_buffer_size;
    (void)nonce; (void)nonce_length;
    (void)additional_data; (void)additional_data_length;
    (void)ciphertext; (void)ciphertext_length;
    (void)plaintext; (void)plaintext_size; (void)plaintext_length;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_key_attributes_t key_attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id;
    psa_status_t status;
    uint8_t h[AES_BLOCK_SIZE];
    uint8_t j0[AES_BLOCK_SIZE];
    uint8_t ctr[AES_BLOCK_SIZE];
    uint8_t ecb_out[AES_BLOCK_SIZE];
    uint8_t ghash_out[AES_BLOCK_SIZE];
    uint8_t expected_tag[GCM_TAG_SIZE];
    size_t out_len, offset;
    size_t ct_len, tag_offset;
    volatile int tag_check;

    /* Only support AES-GCM */
    if (alg != PSA_ALG_GCM) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (nonce_length != GCM_NONCE_SIZE) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    /* Ciphertext must contain at least the tag */
    if (ciphertext_length <= GCM_TAG_SIZE) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    ct_len = ciphertext_length - GCM_TAG_SIZE;
    tag_offset = ct_len;

    if (plaintext_size < ct_len) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    if (psa_get_key_type(attributes) != PSA_KEY_TYPE_AES) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

#ifdef CONFIG_QCC74X_AES_HWKEY
    /* HW key path: skip psa_import_key, pass NULL to cipher for OTP key */
    psa_set_key_type(&key_attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&key_attr, psa_get_key_bits(attributes));
    psa_set_key_algorithm(&key_attr, PSA_ALG_ECB_NO_PADDING);
    psa_set_key_usage_flags(&key_attr, PSA_KEY_USAGE_DECRYPT);

    /* Step 1: H = AES_ECB(K, 0^128) with HW key */
    {
        uint8_t zero_block[AES_BLOCK_SIZE] = {0};
        status = qcc74x_transparent_cipher_encrypt(
            &key_attr, NULL, 0, PSA_ALG_ECB_NO_PADDING,
            NULL, 0, zero_block, AES_BLOCK_SIZE,
            h, AES_BLOCK_SIZE, &out_len);
        if (status == PSA_ERROR_HARDWARE_FAILURE ||
            status == PSA_ERROR_NOT_SUPPORTED) {
            goto fallback_sw_dec;
        }
        if (status != PSA_SUCCESS) goto cleanup_dec;
    }

    /* Step 2: J0 = nonce || 0^31 || 1 */
    memset(j0, 0, AES_BLOCK_SIZE);
    memcpy(j0, nonce, nonce_length);
    j0[AES_BLOCK_SIZE - 1] = 0x01;

    /* Step 3: Verify GHASH first */
    gcm_ghash(h, additional_data, additional_data_length,
              ciphertext, ct_len, ghash_out);

    status = qcc74x_transparent_cipher_encrypt(
        &key_attr, NULL, 0, PSA_ALG_ECB_NO_PADDING,
        NULL, 0, j0, AES_BLOCK_SIZE,
        ecb_out, AES_BLOCK_SIZE, &out_len);
    if (status != PSA_SUCCESS) goto cleanup_dec;

    for (int i = 0; i < GCM_TAG_SIZE; i++) {
        expected_tag[i] = ghash_out[i] ^ ecb_out[i];
    }

    /* Constant-time tag comparison */
    tag_check = 0;
    for (int i = 0; i < GCM_TAG_SIZE; i++) {
        tag_check |= (expected_tag[i] ^ ciphertext[tag_offset + i]);
    }
    if (tag_check != 0) {
        return PSA_ERROR_INVALID_SIGNATURE;
    }

    /* Step 4: AES-CTR decrypt (same as encrypt) */
    memcpy(ctr, j0, AES_BLOCK_SIZE);
    gcm_inc32(ctr);

    for (offset = 0; offset < ct_len; offset += AES_BLOCK_SIZE) {
        size_t chunk = ct_len - offset;
        if (chunk > AES_BLOCK_SIZE) chunk = AES_BLOCK_SIZE;

        status = qcc74x_transparent_cipher_encrypt(
            &key_attr, NULL, 0, PSA_ALG_ECB_NO_PADDING,
            NULL, 0, ctr, AES_BLOCK_SIZE,
            ecb_out, AES_BLOCK_SIZE, &out_len);
        if (status != PSA_SUCCESS) goto cleanup_dec;

        for (size_t i = 0; i < chunk; i++) {
            plaintext[offset + i] = ciphertext[offset + i] ^ ecb_out[i];
        }
        gcm_inc32(ctr);
    }
    *plaintext_length = ct_len;

    goto cleanup_dec;

fallback_sw_dec:
    mbedtls_platform_zeroize(h, sizeof(h));
#endif /* CONFIG_QCC74X_AES_HWKEY */

    /* SW key path: import key then use AES-ECB */
    psa_set_key_type(&key_attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&key_attr, psa_get_key_bits(attributes));
    psa_set_key_algorithm(&key_attr, PSA_ALG_ECB_NO_PADDING);
    psa_set_key_usage_flags(&key_attr, PSA_KEY_USAGE_DECRYPT);

    status = psa_import_key(&key_attr, key_buffer, key_buffer_size, &key_id);
    if (status != PSA_SUCCESS) return status;
    psa_set_key_id(&key_attr, key_id);

    /* Step 1: H = AES_ECB(K, 0^128) */
    {
        uint8_t zero_block[AES_BLOCK_SIZE] = {0};
        status = aes_ecb_block(&key_attr, zero_block, h);
        if (status != PSA_SUCCESS) goto cleanup_sw_dec;
    }

    /* Step 2: J0 = nonce || 0^31 || 1 */
    memset(j0, 0, AES_BLOCK_SIZE);
    memcpy(j0, nonce, nonce_length);
    j0[AES_BLOCK_SIZE - 1] = 0x01;

    /* Step 3: Verify GHASH first */
    gcm_ghash(h, additional_data, additional_data_length,
              ciphertext, ct_len, ghash_out);

    status = aes_ecb_block(&key_attr, j0, ecb_out);
    if (status != PSA_SUCCESS) goto cleanup_sw_dec;

    for (int i = 0; i < GCM_TAG_SIZE; i++) {
        expected_tag[i] = ghash_out[i] ^ ecb_out[i];
    }

    /* Constant-time tag comparison */
    tag_check = 0;
    for (int i = 0; i < GCM_TAG_SIZE; i++) {
        tag_check |= (expected_tag[i] ^ ciphertext[tag_offset + i]);
    }
    if (tag_check != 0) {
        psa_destroy_key(key_id);
        return PSA_ERROR_INVALID_SIGNATURE;
    }

    /* Step 4: AES-CTR decrypt (same as encrypt) */
    memcpy(ctr, j0, AES_BLOCK_SIZE);
    gcm_inc32(ctr);

    for (offset = 0; offset < ct_len; offset += AES_BLOCK_SIZE) {
        size_t chunk = ct_len - offset;
        if (chunk > AES_BLOCK_SIZE) chunk = AES_BLOCK_SIZE;

        status = aes_ecb_block(&key_attr, ctr, ecb_out);
        if (status != PSA_SUCCESS) goto cleanup_sw_dec;

        for (size_t i = 0; i < chunk; i++) {
            plaintext[offset + i] = ciphertext[offset + i] ^ ecb_out[i];
        }
        gcm_inc32(ctr);
    }
    *plaintext_length = ct_len;

cleanup_sw_dec:
    psa_destroy_key(key_id);
    /* fall through to cleanup */

cleanup_dec:
    mbedtls_platform_zeroize(h, sizeof(h));
    mbedtls_platform_zeroize(j0, sizeof(j0));
    mbedtls_platform_zeroize(ctr, sizeof(ctr));
    mbedtls_platform_zeroize(ecb_out, sizeof(ecb_out));
    mbedtls_platform_zeroize(ghash_out, sizeof(ghash_out));
    mbedtls_platform_zeroize(expected_tag, sizeof(expected_tag));
    return status;
#endif /* CONFIG_QCC74X_AEAD_HW_ACCEL */
}
