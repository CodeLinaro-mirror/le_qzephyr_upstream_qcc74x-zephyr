# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0.

zephyr_sources(
  start.S
  start_load.c
  system_qcc74x.c
  hw_init.c
  fw_header.c
  soc.c
  soc_multi_heap.c
)

zephyr_include_directories(.)

if(CONFIG_BUILD_OUTPUT_BIN)
  message("Qualcomm Tool Path: ${QC7XX_SDK_PATH}")
  message("CMAKE_HOST_SYSTEM_NAME: ${CMAKE_HOST_SYSTEM_NAME}")

  if("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Generic")
    set(TOOL_SUFFIX "-ubuntu")
  elseif("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Linux")
    set(TOOL_SUFFIX "-ubuntu")
  elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
    set(TOOL_SUFFIX "-macos")
  elseif("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
    set(TOOL_SUFFIX ".exe")
  else()
    message(FATAL_ERROR "QCC74x Unsupported OS '${CMAKE_HOST_SYSTEM_NAME}', QConn_Secure not found.")
  endif()

  set(QCONN_SECURE ${QC7XX_SDK_PATH}/tools/qc7xx_tools/QConn_Secure/QConn_Secure${TOOL_SUFFIX})
  # Fallback: HAL module CMakeLists copies tools from blobs, but it runs after
  # this SoC CMakeLists. On a fresh workspace the tools/ copy may not exist yet
  # at configure time, so check the blob path directly.
  if(NOT EXISTS ${QCONN_SECURE})
    set(QCONN_SECURE ${QC7XX_SDK_PATH}/zephyr/blobs/qcc74x/tool/QConn_Secure${TOOL_SUFFIX})
  endif()

  # HAL v2.1.3 bsp uses a single qcc74xdk/qcc74x board config dir (with
  # partition_cfg_4M.toml). Both PSRAM and non-PSRAM builds share it.
  set(QCONN_SECURE_CONFIG
    --appkeys=shared
    --brdcfgdir=${QC7XX_SDK_PATH}/bsp/board/qcc74xdk/qcc74x
    --imgfile=${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
  )

  # QConn_Secure is a host binary shipped under tools/qc7xx_tools/, which is not
  # committed to the HAL repo (distributed separately). Only append the secure
  # image post-build step when the tool is actually present; otherwise skip it
  # so the firmware still builds. The secure image is only needed for flashing.
  if(EXISTS ${QCONN_SECURE})
    message("Qualcomm Tool Command: ${QCONN_SECURE}")
    message("Qualcomm Tool Para: ${QCONN_SECURE_CONFIG}")

    set_property(
      GLOBAL APPEND PROPERTY extra_post_build_commands
      COMMAND ${QCONN_SECURE} ${QCONN_SECURE_CONFIG}
    )
  else()
    message(WARNING
      "QConn_Secure not found at ${QCONN_SECURE}; skipping secure-image "
      "post-build step. ${CONFIG_KERNEL_BIN_NAME}.bin is built but not "
      "secured. Install the qc7xx_tools host tools to enable it for flashing.")
  endif()
endif()

# Use the unified common.ld (from HAL v2.1.3 / reference SoC). It handles both
# PSRAM and non-PSRAM via CONFIG_PSRAM internally and defines the wifi/psram
# heap symbols (_heap_wifi_start, __HeapBase/__HeapLimit, __psram_heap_base/
# __psram_limit) that soc_multi_heap.c requires.
set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/common.ld CACHE INTERNAL "")
