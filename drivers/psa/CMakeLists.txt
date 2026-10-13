# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause
#
# QCC74x PSA Crypto Transparent Driver — Build rules
#
# This file is included from qcc74x-zephyr/drivers/CMakeLists.txt
# when CONFIG_PSA_CRYPTO_DRIVER_QCC74X is enabled.

# 1. Apply patches to upstream MbedTLS PSA Crypto sources
#    The patches are applied via git apply to the tf-psa-crypto module.
#    ZEPHYR_QCC74X_ES_SDK_MODULES_DIR is set by the Zephyr build system
#    to point to the es_sdk modules directory.
#    If this variable is not available, fall back to a relative path from
#    the Zephyr base (which is qcc74x-zephyr).
#
#    Patch 0001: Add QCC74X dispatch table entries to psa_crypto_driver_wrappers.h
#    Patch 0002: Add QCC74X context member to psa_driver_cipher_context_t union
#    Patch 0003: Convert local arrays to static globals in tf-psa-crypto/core/psa_crypto.c
#               to prevent stack overflow when ITS uses AES-GCM transform.
#
#    Patches are only applied once; repeated builds will see "already applied"
#    from git apply --check and skip without error.

# Try to locate the tf-psa-crypto module directory
if(DEFINED ZEPHYR_QCC74X_ES_SDK_MODULES_DIR)
    set(TF_PSA_CRYPTO_DIR "${ZEPHYR_QCC74X_ES_SDK_MODULES_DIR}/crypto/tf-psa-crypto")
else()
    # Fallback: assume es_sdk layout relative to ZEPHYR_BASE
    get_filename_component(ZEPHYR_PARENT "${ZEPHYR_BASE}" DIRECTORY)
    set(TF_PSA_CRYPTO_DIR "${ZEPHYR_PARENT}/modules/crypto/tf-psa-crypto")
endif()

set(QCC74X_PSA_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches")

# Helper macro: try to apply a patch, skip if already applied
macro(qcc74x_apply_patch patch_file)
    set(full_patch "${QCC74X_PSA_PATCH_DIR}/${patch_file}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E chdir "${TF_PSA_CRYPTO_DIR}"
                git apply --check "${full_patch}"
        RESULT_VARIABLE _check_result
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_check_result EQUAL 0)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E chdir "${TF_PSA_CRYPTO_DIR}"
                    git apply "${full_patch}"
            RESULT_VARIABLE _apply_result
            OUTPUT_QUIET
        )
        if(_apply_result EQUAL 0)
            message(STATUS "Applied ${patch_file} to tf-psa-crypto")
        else()
            message(FATAL_ERROR "Failed to apply ${patch_file} to tf-psa-crypto")
        endif()
    else()
        message(STATUS "Patch ${patch_file} already applied (or cannot check: skipping)")
    endif()
endmacro()

qcc74x_apply_patch(0001-add-qcc74x-to-psa-crypto-driver-wrappers.patch)
qcc74x_apply_patch(0002-add-qcc74x-context-to-crypto-driver-contexts-primitives.patch)
qcc74x_apply_patch(0003-secure-storage-convert-local-arrays-to-static.patch)

# 2. Compile all source modules
zephyr_sources(src/qcc74x_psa_init.c)
zephyr_sources(src/qcc74x_psa_cipher.c)
zephyr_sources(src/qcc74x_psa_entropy.c)
zephyr_sources_ifdef(CONFIG_QCC74X_AEAD_HW_ACCEL src/qcc74x_psa_aead.c)

zephyr_library()
zephyr_library_sources_ifdef(
    CONFIG_PSA_CRYPTO_DRIVER_QCC74X_HASH
    qcc74x_psa_hash.c
)

# 3. Expose include/ and . (for master header) to the build
zephyr_include_directories(.)
zephyr_include_directories(include)

# 4. Activate the driver in the PSA dispatch layer.
zephyr_compile_definitions(PSA_CRYPTO_DRIVER_QCC74X)

# 5. Sub-feature flags
zephyr_compile_definitions_ifdef(CONFIG_PSA_CRYPTO_DRIVER_QCC74X_AES
                                 PSA_CRYPTO_DRIVER_QCC74X_AES)
zephyr_compile_definitions_ifdef(CONFIG_PSA_CRYPTO_DRIVER_QCC74X_HASH
                                 PSA_CRYPTO_DRIVER_QCC74X_HASH)
zephyr_compile_definitions_ifdef(CONFIG_QCC74X_AEAD_HW_ACCEL
                                 PSA_CRYPTO_DRIVER_QCC74X_AEAD)
