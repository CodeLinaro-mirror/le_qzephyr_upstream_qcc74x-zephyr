/* Uncomment to enable verbose trace for debugging */
/* #define CIPHER_TRACE */

/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * QCC74x PSA Driver — Cipher one-shot + streaming implementation
 *
 * One-shot (psa_cipher_encrypt):
 *   Uses the register-based qc7xx_aes_encrypt() for simple single-block or
 *   multi-block encrypt/decrypt where the caller supplies all data at once.
 *
 * Streaming (setup/update/finish):
 *   Uses AES hardware DMA link mode (qc7xx_aes_link_init/update/deinit) so
 *   that the key and mode are programmed only once during setup. Each update
 *   call programs a qc7xx_aes_link_s descriptor in PSRAM (0x2xxxxxxx region)
 *   with the block count, source/destination addresses, and IV, then triggers
 *   the hardware DMA to process the data directly. This avoids the overhead
 *   of re-initializing the AES engine on every update call.
 *
 *   For CBC and CTR modes the running IV is maintained by the hardware when
 *   aes_newiv_dis = 1 (reuse IV from previous operation). Partial blocks are
 *   buffered in software and flushed when a full 16-byte block is available.
 */

#include "../include/qcc74x_psa_cipher.h"
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include "mbedtls/platform_util.h"
#include "qc7xx_core.h"
#include "qc7xx_sec_aes.h"

/* Extern: AES mutex defined in qcc74x_psa_init.c */
extern struct k_mutex qcc74x_psa_aes_mutex;

/* Synchronize DMA buffers; caller must not access them until completion. */
static int aes_cache_prepare(const uint8_t *input, uint8_t *output, size_t len)
{
    int ret;
    if (qc7xx_check_cache_addr(input)) {
        ret = sys_cache_data_flush_range((void *)input, len);
        if (ret != 0)
            return ret;
    }
    if (qc7xx_check_cache_addr(output)) {
        ret = sys_cache_data_flush_and_invd_range(output, len);
        if (ret != 0)
            return ret;
    }
    return 0;
}

static int aes_cache_complete(uint8_t *output, size_t len, int ret)
{
    if (qc7xx_check_cache_addr(output)) {
        int cache_ret = sys_cache_data_invd_range(output, len);
        if (cache_ret != 0)
            return cache_ret;
    }
    return ret;
}

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

static psa_status_t map_alg_to_aes_mode(psa_algorithm_t alg, uint8_t *aes_mode)
{
    switch (alg) {
    case PSA_ALG_ECB_NO_PADDING:
        *aes_mode = AES_MODE_ECB;
        return PSA_SUCCESS;
    case PSA_ALG_CBC_NO_PADDING:
        *aes_mode = AES_MODE_CBC;
        return PSA_SUCCESS;
    case PSA_ALG_CTR:
        *aes_mode = AES_MODE_CTR;
        return PSA_SUCCESS;
    default:
        return PSA_ERROR_NOT_SUPPORTED;
    }
}

static void ctr_increment(uint8_t ctr[16], uint32_t n)
{
    uint32_t carry = n;
    for (int i = 15; i >= 0 && carry != 0; i--) {
        uint32_t sum = (uint32_t)ctr[i] + (carry & 0xFFu);
        ctr[i] = (uint8_t)(sum & 0xFFu);
        carry  = (carry >> 8) + (sum >> 8);
    }
}

/**
 * @brief Map AES mode to link descriptor keybits field.
 */
static uint8_t keybits_to_link_key(uint16_t key_bits)
{
    switch (key_bits) {
    case 128: return AES_LINK_KEY_128BITS;
    case 192: return AES_LINK_KEY_192BITS;
    case 256: return AES_LINK_KEY_256BITS;
    default:  return AES_LINK_KEY_128BITS;
    }
}

/**
 * @brief Build a qc7xx_aes_link_s descriptor and call link_update.
 *
 * The link descriptor must reside in the PSRAM region (0x2xxxxxxx) for the
 * AES DMA engine to access it. We use a descriptor allocated during setup
 * and stored in the operation context.
 *
 * Reload the operation's key and IV on every submission; another operation
 * or a register-mode partial block may have changed the shared engine.
 *
 * @param dev      AES device handle
 * @param op       Operation context with link descriptor and state
 * @param input    Input data (must be DMA-accessible)
 * @param output   Output buffer (must be DMA-accessible)
 * @param len      Data length in bytes, MUST be multiple of 16
 * @return 0 on success, negated errno on failure.
 */
static int aes_link_process(struct qc7xx_device_s *dev,
                            qcc74x_transparent_cipher_operation_t *op,
                            const uint8_t *input, uint8_t *output,
                            uint32_t len)
{
    struct qc7xx_aes_link_s *link = op->link_desc;
    int cache_ret = aes_cache_prepare(input, output, len);
    if (cache_ret != 0)
        return cache_ret;
    /* HAL updates descriptor fields immediately before triggering DMA.
     * Like hash, use the noncached alias for CPU/HAL descriptor accesses.
     */
    if (qc7xx_check_cache_addr(link)) {
        cache_ret = sys_cache_data_flush_and_invd_range(link, sizeof(*link));
        if (cache_ret != 0)
            return cache_ret;
        link = qc7xx_get_no_cache_addr(link);
        if (!link)
            return -EIO;
    }
    uint32_t num_blocks = len / 16;

    /* Build the link descriptor */
    memset(link, 0, sizeof(*link));

    /* [4:3] key size */
    link->aes_key = keybits_to_link_key(op->key_bits);
    /* [5] encrypt=0 / decrypt=1 */
    link->aes_dec_en = (op->is_encrypt) ? 0 : 1;
    /* [6] 0 = use new key from descriptor, 1 = reuse previous key */
    link->aes_newkey_dis = 0;
    /* [14] 0 = use new IV from descriptor, 1 = reuse previous IV */
    link->aes_newiv_dis  = 0;
    /* [7] hardware key disabled */
    link->aes_hwkey_en = 0;
    /* [13:12] mode */
    link->aes_mode = op->aes_mode;
    /* [15] XTS off */
    link->aes_xts = 0;
    /* [31:16] number of 128-bit blocks */
    link->aes_msglen = num_blocks & 0xFFFF;

    /* Source and destination addresses (physical / bus addresses) */
    link->aes_srcaddr = (uint32_t)(uintptr_t)input;
    link->aes_dstaddr = (uint32_t)(uintptr_t)output;

    /* Match the byte layout used by crypto_qcc74x.c. */
    if (op->aes_mode != AES_MODE_ECB)
        memcpy(&link->aes_iv0, op->iv, 16);
    memcpy(&link->aes_key0, op->key, op->key_bits / 8);

    op->link_first = 0;

    int ret = qc7xx_aes_link_update(dev, (uint32_t)(uintptr_t)link,
                                    input, output, len);
    ret = aes_cache_complete(output, len, ret);
    mbedtls_platform_zeroize(&link->aes_key0, sizeof(op->key));
    return ret;
}

/**
 * @brief Process a single 16-byte block using the register-based API.
 *
 * Used for the partial-block flush step where we operate on the
 * operation->partial buffer (which may not be in PSRAM).
 */
static int aes_process_one_block(struct qc7xx_device_s *dev,
                                 qcc74x_transparent_cipher_operation_t *op,
                                 const uint8_t *in, uint8_t *out)
{
    int ret = aes_cache_prepare(in, out, 16);
    if (ret != 0)
        return ret;
    if (op->is_encrypt)
        ret = qc7xx_aes_encrypt(dev, in,
                                (op->aes_mode != AES_MODE_ECB) ? op->iv : NULL,
                                out, 16);
    else
        ret = qc7xx_aes_decrypt(dev, in,
                                (op->aes_mode != AES_MODE_ECB) ? op->iv : NULL,
                                out, 16);
    return aes_cache_complete(out, 16, ret);
}

/* =========================================================================
 * Cipher — one-shot encrypt
 * ======================================================================= */

psa_status_t qcc74x_transparent_cipher_encrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *iv, size_t iv_length,
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_size, size_t *output_length)
{
#if !defined(PSA_CRYPTO_DRIVER_QCC74X_AES)
    (void)attributes; (void)key_buffer; (void)key_buffer_size;
    (void)alg; (void)iv; (void)iv_length;
    (void)input; (void)input_length;
    (void)output; (void)output_size; (void)output_length;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    struct qc7xx_device_s *dev;
    uint8_t aes_mode;
    psa_key_type_t key_type = psa_get_key_type(attributes);
    size_t key_bits = psa_get_key_bits(attributes);
    psa_status_t status;
    int ret;

    if (key_type != PSA_KEY_TYPE_AES)
        return PSA_ERROR_NOT_SUPPORTED;
    if (key_bits != 128 && key_bits != 192 && key_bits != 256)
        return PSA_ERROR_NOT_SUPPORTED;

    status = map_alg_to_aes_mode(alg, &aes_mode);
    if (status != PSA_SUCCESS)
        return status;

    if (input_length == 0 || (input_length % 16) != 0)
        return PSA_ERROR_INVALID_ARGUMENT;
    if (output_size < input_length)
        return PSA_ERROR_BUFFER_TOO_SMALL;

    ret = aes_cache_prepare(input, output, input_length);
    if (ret != 0)
        return PSA_ERROR_HARDWARE_FAILURE;
    dev = qc7xx_device_get_by_name(QC7XX_NAME_SEC_AES);
    if (!dev)
        return PSA_ERROR_NOT_SUPPORTED;

    k_mutex_lock(&qcc74x_psa_aes_mutex, K_FOREVER);
    qc7xx_group0_request_aes_access(dev);
    qc7xx_aes_link_deinit(dev);
    qc7xx_aes_init(dev);
    qc7xx_aes_set_mode(dev, aes_mode);

#ifdef CONFIG_QCC74X_AES_HWKEY
    if (key_buffer == NULL) {
        qc7xx_aes_select_hwkey(dev, CONFIG_QCC74X_AES_HWKEY_KEY_SEL0,
                                     CONFIG_QCC74X_AES_HWKEY_KEY_SEL1);
        qc7xx_aes_set_hwkey_source(dev, CONFIG_QCC74X_AES_HWKEY_KEY_SOURCE);
        qc7xx_aes_setkey(dev, NULL, key_bits);  /* HW key path */
    } else
#endif
    {
        qc7xx_aes_setkey(dev, key_buffer, key_bits);  /* SW key path */
    }

    const uint8_t *hw_iv = (iv_length > 0) ? iv : NULL;

    ret = qc7xx_aes_encrypt(dev, input, hw_iv, output, (uint32_t)input_length);
    ret = aes_cache_complete(output, input_length, ret);
    qc7xx_aes_deinit(dev);

    k_mutex_unlock(&qcc74x_psa_aes_mutex);

    if (ret != 0)
        return PSA_ERROR_HARDWARE_FAILURE;

    *output_length = input_length;
    return PSA_SUCCESS;
#endif
}

/* =========================================================================
 * Cipher — streaming: internal setup helper
 * ======================================================================= */

static psa_status_t cipher_setup(
    qcc74x_transparent_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg, uint8_t is_encrypt)
{
#if !defined(PSA_CRYPTO_DRIVER_QCC74X_AES)
    (void)operation; (void)attributes; (void)key_buffer;
    (void)key_buffer_size; (void)alg; (void)is_encrypt;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_key_type_t key_type = psa_get_key_type(attributes);
    size_t key_bits = psa_get_key_bits(attributes);
    uint8_t aes_mode;
    psa_status_t status;

    (void)key_buffer_size;

    if (key_type != PSA_KEY_TYPE_AES)
        return PSA_ERROR_NOT_SUPPORTED;
    if (key_bits != 128 && key_bits != 192 && key_bits != 256)
        return PSA_ERROR_NOT_SUPPORTED;

    status = map_alg_to_aes_mode(alg, &aes_mode);
    if (status != PSA_SUCCESS)
        return status;

    memset(operation, 0, sizeof(*operation));
    if (key_buffer != NULL) {
        memcpy(operation->key, key_buffer, key_bits / 8);
    }
    operation->key_bits   = (uint16_t)key_bits;
    operation->aes_mode   = aes_mode;
    operation->is_encrypt = is_encrypt;
    operation->link_first = 1;
    operation->link_desc  = NULL;

    return PSA_SUCCESS;
#endif
}

/* =========================================================================
 * Cipher — streaming encrypt/decrypt setup
 * ======================================================================= */

psa_status_t qcc74x_transparent_cipher_encrypt_setup(
    qcc74x_transparent_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg)
{
    psa_status_t s = cipher_setup(operation, attributes, key_buffer,
                                  key_buffer_size, alg, 1);

    return s;
}

psa_status_t qcc74x_transparent_cipher_decrypt_setup(
    qcc74x_transparent_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg)
{
    return cipher_setup(operation, attributes, key_buffer,
                        key_buffer_size, alg, 0);
}

/* =========================================================================
 * Cipher — streaming: set_iv
 * ======================================================================= */

psa_status_t qcc74x_transparent_cipher_set_iv(
    qcc74x_transparent_cipher_operation_t *operation,
    const uint8_t *iv, size_t iv_length)
{
#if !defined(PSA_CRYPTO_DRIVER_QCC74X_AES)
    (void)operation; (void)iv; (void)iv_length;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    printk("SET_IV: mode=%u ivlen=%zu\n", operation->aes_mode, iv_length);
 
    if (operation->aes_mode == AES_MODE_ECB) {
        if (iv_length != 0)
            return PSA_ERROR_INVALID_ARGUMENT;
        return PSA_SUCCESS;
    }
    if (iv_length != 16)
        return PSA_ERROR_INVALID_ARGUMENT;

    memcpy(operation->iv, iv, 16);
    return PSA_SUCCESS;
#endif
}

/* =========================================================================
 * Cipher — streaming: update (link-mode accelerated)
 * ======================================================================= */

psa_status_t qcc74x_transparent_cipher_update(
    qcc74x_transparent_cipher_operation_t *operation,
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_size, size_t *output_length)
{
#if !defined(PSA_CRYPTO_DRIVER_QCC74X_AES)
    (void)operation; (void)input; (void)input_length;
    (void)output; (void)output_size; (void)output_length;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    struct qc7xx_device_s *dev;
    size_t total        = operation->partial_len + input_length;
    size_t out_blocks   = total / 16;
    size_t out_needed   = out_blocks * 16;
    size_t out_pos      = 0;
    const uint8_t *in_ptr = input;
    size_t in_remaining   = input_length;
    int ret = 0;
    int link_initialized = 0;
    int reg_initialized  = 0;
    uint8_t *input_copy = NULL;

    *output_length = 0;

  

    /* No complete block available — just buffer */
    if (out_blocks == 0) {
        printk("UPDATE: buffer only\n");
        memcpy(operation->partial + operation->partial_len, input, input_length);
        operation->partial_len += input_length;
        return PSA_SUCCESS;
    }

    if (output_size < out_needed) {
        printk("UPDATE: output too small: need %zu have %zu\n", out_needed, output_size);
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    dev = qc7xx_device_get_by_name(QC7XX_NAME_SEC_AES);
    if (!dev) {
        printk("UPDATE: no AES device\n");
        return PSA_ERROR_NOT_SUPPORTED;
    }

    /* Buffered output can overwrite input bytes needed by the next block. */
    uintptr_t src = (uintptr_t)input, dst = (uintptr_t)output;
    if (operation->partial_len &&
        (src <= dst ? dst - src < input_length : src - dst < out_needed)) {
        input_copy = k_malloc(input_length);
        if (!input_copy)
            return PSA_ERROR_INSUFFICIENT_MEMORY;
        memcpy(input_copy, input, input_length);
        in_ptr = input_copy;
    }

    k_mutex_lock(&qcc74x_psa_aes_mutex, K_FOREVER);
    qc7xx_group0_request_aes_access(dev);

    /*
     * Step A: Flush the partial buffer using register-based single-block ops.
     * The partial buffer lives in the operation struct (on stack/heap, not
     * necessarily in PSRAM), so we cannot use link-mode DMA for it.
     */

    if (operation->partial_len > 0) {
        size_t need = 16 - operation->partial_len;
        memcpy(operation->partial + operation->partial_len, in_ptr, need);
        in_ptr       += need;
        in_remaining -= need;

        /* saved_ct is only used on CBC decrypt path to save ciphertext
         * before it gets overwritten by the next block's decryption.
         * It is explicitly zeroed after use.
         */
        uint8_t saved_ct[16];
        memset(saved_ct, 0, sizeof(saved_ct));
        if (operation->aes_mode == AES_MODE_CBC && !operation->is_encrypt)
            memcpy(saved_ct, operation->partial, 16);

        qc7xx_aes_link_deinit(dev);
        qc7xx_aes_init(dev);
        reg_initialized = 1;
        qc7xx_aes_set_mode(dev, operation->aes_mode);
        qc7xx_aes_setkey(dev, operation->key, operation->key_bits);

        ret = aes_process_one_block(dev, operation,
                                    operation->partial, output + out_pos);
        if (ret != 0) {
            qc7xx_aes_deinit(dev);
            reg_initialized = 0;
            mbedtls_platform_zeroize(saved_ct, sizeof(saved_ct));
            goto hw_error;
        }

        if (operation->aes_mode == AES_MODE_CBC) {
            if (operation->is_encrypt)
                memcpy(operation->iv, output + out_pos, 16);
            else
                memcpy(operation->iv, saved_ct, 16);
        } else if (operation->aes_mode == AES_MODE_CTR) {
            ctr_increment(operation->iv, 1);
        }
        out_pos += 16;
        operation->partial_len = 0;

        qc7xx_aes_deinit(dev);
        reg_initialized = 0;
        mbedtls_platform_zeroize(saved_ct, sizeof(saved_ct));
    }

    /*
     * Step B: Process remaining complete blocks via link-mode DMA.
     * The input and output buffers for the bulk data are on the caller's
     * stack/heap, which may not be in PSRAM. However, the link descriptor
     * itself (qc7xx_aes_link_s) must be in PSRAM for the AES DMA engine
     * to read it. We allocate it on first use and keep it in the operation
     * context. The src/dst addresses in the descriptor are physical
     * addresses of the input/output buffers (the AES DMA engine accesses
     * system memory directly via the bus).
     */
    {
        size_t complete_blocks = in_remaining / 16;
        if (complete_blocks > 0) {
            size_t process_len = complete_blocks * 16;

            /* Allocate link descriptor on first DMA use */
            if (operation->link_desc == NULL) {
                operation->link_desc =
                    (struct qc7xx_aes_link_s *)k_malloc(sizeof(struct qc7xx_aes_link_s));
                if (operation->link_desc == NULL) {
                    /* Mutex is still held; goto hw_error which will unlock it.
                     * link_initialized remains 0 so link_deinit is skipped. */
                    goto hw_error;
                }
            }

            /* Restore link mode after other operations or partial blocks. */
            qc7xx_aes_link_init(dev);
            link_initialized = 1;

            /* CBC decrypt must preserve ciphertext before in-place DMA. */
            uint8_t saved_ct[16] = {0};
            if (operation->aes_mode == AES_MODE_CBC && !operation->is_encrypt)
                memcpy(saved_ct, in_ptr + process_len - 16, 16);

            ret = aes_link_process(dev, operation,
                                   in_ptr, output + out_pos,
                                   (uint32_t)process_len);
            if (ret != 0) {
                mbedtls_platform_zeroize(saved_ct, sizeof(saved_ct));
                goto hw_error;
            }

            /* After link update, the hardware has updated the IV internally.
             * We need to retrieve the final IV from the hardware for the
             * next operation. However, the link mode does not expose the
             * updated IV via the descriptor. For CBC, the hardware chain
             * maintains IV automatically via aes_newiv_dis=1.
             *
             * For CTR, the hardware also increments the counter internally.
             * We must keep our software copy in sync by computing the
             * expected counter value.
             */
            if (operation->aes_mode == AES_MODE_CTR) {
                ctr_increment(operation->iv, (uint32_t)complete_blocks);
            } else if (operation->aes_mode == AES_MODE_CBC) {
                /* For CBC, the output ciphertext of the last block is the
                 * IV for the next operation. Since link mode chains this
                 * automatically via aes_newiv_dis=1, we only need it for
                 * the partial block flush above. Copy it from output.
                 */
                memcpy(operation->iv, operation->is_encrypt ?
                       output + out_pos + process_len - 16 : saved_ct, 16);
            }
            mbedtls_platform_zeroize(saved_ct, sizeof(saved_ct));

            out_pos      += process_len;
            in_ptr       += process_len;
            in_remaining -= process_len;
        }
    }

    if (link_initialized)
        qc7xx_aes_link_deinit(dev);
    k_mutex_unlock(&qcc74x_psa_aes_mutex);

    /* Step C: Buffer remaining bytes (< 16) */
    if (in_remaining > 0) {
        memcpy(operation->partial, in_ptr, in_remaining);
        operation->partial_len = in_remaining;
    }

    if (input_copy) {
        mbedtls_platform_zeroize(input_copy, input_length);
        k_free(input_copy);
    }
    *output_length = out_pos;
    return PSA_SUCCESS;

hw_error:
    if (operation->link_desc) {
        k_free(operation->link_desc);
        operation->link_desc = NULL;
    }
    /* Only deinit link mode if it was actually initialized */
    if (link_initialized) {
        qc7xx_aes_link_deinit(dev);
    }
    /* Only deinit register mode if it was initialized */
    if (reg_initialized) {
        qc7xx_aes_deinit(dev);
    }
    k_mutex_unlock(&qcc74x_psa_aes_mutex);
    if (input_copy) {
        mbedtls_platform_zeroize(input_copy, input_length);
        k_free(input_copy);
    }
    return PSA_ERROR_HARDWARE_FAILURE;
#endif
}

/* =========================================================================
 * Cipher — streaming: finish
 * ======================================================================= */

psa_status_t qcc74x_transparent_cipher_finish(
    qcc74x_transparent_cipher_operation_t *operation,
    uint8_t *output, size_t output_size, size_t *output_length)
{
#if !defined(PSA_CRYPTO_DRIVER_QCC74X_AES)
    (void)operation; (void)output;
    (void)output_size; (void)output_length;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    struct qc7xx_device_s *dev = NULL;
    int ret = 0;
    psa_status_t status = PSA_SUCCESS;

    *output_length = 0;

    /* If link mode was used, deinit it */
    if (operation->link_desc != NULL) {
        dev = qc7xx_device_get_by_name(QC7XX_NAME_SEC_AES);
        if (dev)
            qc7xx_aes_link_deinit(dev);
        k_free(operation->link_desc);
        operation->link_desc = NULL;
    }

    if (operation->aes_mode == AES_MODE_CTR && operation->partial_len > 0) {
        /* CTR allows partial last block: pad with zeros, encrypt, truncate */
        if (output_size < operation->partial_len) {
            status = PSA_ERROR_BUFFER_TOO_SMALL;
            goto cleanup;
        }

        uint8_t padded[16];
        uint8_t tmp[16];

        memset(padded, 0, sizeof(padded));
        memcpy(padded, operation->partial, operation->partial_len);

        if (!dev)
            dev = qc7xx_device_get_by_name(QC7XX_NAME_SEC_AES);
        if (!dev) {
            mbedtls_platform_zeroize(padded, sizeof(padded));
            status = PSA_ERROR_NOT_SUPPORTED;
            goto cleanup;
        }

        k_mutex_lock(&qcc74x_psa_aes_mutex, K_FOREVER);
        qc7xx_aes_init(dev);
        qc7xx_aes_set_mode(dev, AES_MODE_CTR);
        qc7xx_aes_setkey(dev, operation->key, operation->key_bits);
        qc7xx_group0_request_aes_access(dev);
        ret = aes_cache_prepare(padded, tmp, 16);
        if (ret == 0) {
            ret = qc7xx_aes_encrypt(dev, padded, operation->iv, tmp, 16);
            ret = aes_cache_complete(tmp, 16, ret);
        }
        qc7xx_aes_deinit(dev);
        k_mutex_unlock(&qcc74x_psa_aes_mutex);

        if (ret != 0) {
            mbedtls_platform_zeroize(padded, sizeof(padded));
            mbedtls_platform_zeroize(tmp, sizeof(tmp));
            status = PSA_ERROR_HARDWARE_FAILURE;
            goto cleanup;
        }

        memcpy(output, tmp, operation->partial_len);
        *output_length = operation->partial_len;

        /* Wipe sensitive stack buffers */
        mbedtls_platform_zeroize(padded, sizeof(padded));
        mbedtls_platform_zeroize(tmp, sizeof(tmp));

    } else if (operation->partial_len != 0) {
        /* CBC/ECB: partial block is an error */
        status = PSA_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

cleanup:
    mbedtls_platform_zeroize(operation, sizeof(*operation));
    return status;
#endif
}

/* =========================================================================
 * Cipher — streaming: abort
 * ======================================================================= */

psa_status_t qcc74x_transparent_cipher_abort(
    qcc74x_transparent_cipher_operation_t *operation)
{
#if !defined(PSA_CRYPTO_DRIVER_QCC74X_AES)
    (void)operation;
    return PSA_ERROR_NOT_SUPPORTED;
#else
    if (operation->link_desc != NULL) {
        struct qc7xx_device_s *dev = qc7xx_device_get_by_name(QC7XX_NAME_SEC_AES);
        if (dev)
            qc7xx_aes_link_deinit(dev);
        k_free(operation->link_desc);
        operation->link_desc = NULL;
    }
    mbedtls_platform_zeroize(operation, sizeof(*operation));
    return PSA_SUCCESS;
#endif
}