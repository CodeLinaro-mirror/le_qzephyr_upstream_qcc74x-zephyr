# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_sources(
  start.S
  start_load.c
  system_qcc74x.c
  hw_init.c
  fw_header.c
  soc_irq.S
  soc.c
  soc_multi_heap.c
)

if(CONFIG_PM)
  zephyr_sources(power.c)
endif()

zephyr_sources_ifdef(CONFIG_POWEROFF poweroff.c)
zephyr_sources_ifdef(CONFIG_QCC74X_HBN_SHELL hbn_shell.c)

if(CONFIG_POWEROFF OR CONFIG_QCC74X_PM_PDS15)
  zephyr_sources(
    ${QC7XX_SDK_PATH}/drivers/sys/qcc74x/qc7xx_hbn_system.c
    ${QC7XX_SDK_PATH}/drivers/lhal/src/qc7xx_acomp.c
  )
endif()

zephyr_sources_ifdef(CONFIG_QCC74X_PM_PDS15_POLICY
  pm_policy_qcc74x.c
)

zephyr_sources_ifdef(CONFIG_QCC74X_PM_PDS15_SYSTEM_TIMER
  pm_pds15_timer.c
)

zephyr_sources_ifdef(CONFIG_QCC74X_PM_PDS15
  pm_pds15_glue.c
  pm_pds15.c
  ${QC7XX_SDK_PATH}/drivers/sys/qcc74x/qcc74x_lp.c
  ${QC7XX_SDK_PATH}/drivers/soc/qcc74x/std/startup/interrupt.c
)

set(LPFW_BIN
  ${QC7XX_SDK_PATH}/zephyr/blobs/qcc74x/bin/qcc74x_lp_fw.bin)
set(LPFW_PATCH
  ${QC7XX_SDK_PATH}/tools/lpfw/patch_lpfw.py)

if(CONFIG_QCC74X_PM_PDS15)
  if(NOT EXISTS "${LPFW_BIN}")
    message(FATAL_ERROR
      "Missing qcc74x LPFW blob: ${LPFW_BIN}. "
      "Fetch it with 'west blobs fetch hal_qualcomm' or ensure "
      "modules/hal/qcom/qcc74x/zephyr/blobs/qcc74x/bin/qcc74x_lp_fw.bin exists.")
  endif()
  if(NOT EXISTS "${LPFW_PATCH}")
    message(FATAL_ERROR "Missing LPFW patch tool: ${LPFW_PATCH}")
  endif()
  message(STATUS "QCC74X LPFW blob: ${LPFW_BIN}")
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
    COMMAND ${Python3_EXECUTABLE} ${LPFW_PATCH}
      ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
      ${LPFW_BIN})
endif()

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
  # tools/qc7xx_tools/ is populated by the HAL module, and modules are processed
  # after soc/, so on a virgin tree the tool is missing here and the partition.bin
  # post-build step below would be silently skipped. Copy it ourselves instead of
  # only probing, to the same path the HAL uses (its later copy becomes a no-op).
  # Copying is required because the drops ship the tool non-executable (0640).
  if(NOT EXISTS ${QCONN_SECURE})
    foreach(_qconn_src ${QC7XX_SDK_PATH}/zephyr/blobs/qcc74x/tool
                       ${QC7XX_SDK_PATH}/../../../../prop/qcc74x-bin/tool)
      if(EXISTS ${_qconn_src}/QConn_Secure${TOOL_SUFFIX})
        file(COPY ${_qconn_src}/QConn_Secure${TOOL_SUFFIX}
             DESTINATION ${QC7XX_SDK_PATH}/tools/qc7xx_tools/QConn_Secure
             FILE_PERMISSIONS
               OWNER_READ OWNER_WRITE OWNER_EXECUTE
               GROUP_READ GROUP_EXECUTE
               WORLD_READ WORLD_EXECUTE)
        break()
      endif()
    endforeach()
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
