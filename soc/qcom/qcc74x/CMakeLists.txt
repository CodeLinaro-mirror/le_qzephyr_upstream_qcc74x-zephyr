# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
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
  if(CONFIG_QCC74X_PM_LPFW)
    # Keep kernel stacks in internal SRAM, independently of PSRAM code copying.
    dt_chosen(main_ram PROPERTY "zephyr,sram")
    dt_nodelabel(internal_ram NODELABEL "sram0" REQUIRED)
    dt_node_has_status(internal_ram_enabled PATH "${internal_ram}" STATUS "okay")
    if(NOT main_ram STREQUAL internal_ram OR NOT internal_ram_enabled)
      message(FATAL_ERROR "QCC74x LPFW requires zephyr,sram to select enabled "
        "internal sram0, not ${main_ram}.")
    endif()
    dt_nodelabel(psram_path NODELABEL "psram" REQUIRED)
    dt_node_has_status(psram_enabled PATH "${psram_path}" STATUS "okay")
    if(CONFIG_QCC74X_PM_PDS1)
      if(NOT CONFIG_PSRAM OR NOT psram_enabled)
        message(FATAL_ERROR "QCC74x PDS1 requires enabled PSRAM.")
      endif()
      dt_reg_size(psram_size PATH "${psram_path}")
      dt_reg_addr(psram_base PATH "${psram_path}")
      if(NOT psram_size EQUAL CONFIG_PSRAM_LENGTH OR
         NOT (psram_size EQUAL 0x400000 OR psram_size EQUAL 0x800000 OR
              psram_size EQUAL 0x1000000) OR NOT psram_base EQUAL 0xa8000000)
        message(FATAL_ERROR "QCC74x PDS1 requires PSRAM at 0xa8000000 with "
          "4, 8 or 16 MiB matching CONFIG_PSRAM_LENGTH; ${psram_path} has "
          "base=${psram_base}, size=${psram_size}.")
      endif()
    elseif(CONFIG_PSRAM OR psram_enabled OR CONFIG_PSRAM_COPY_CODE)
      message(FATAL_ERROR "QCC74x PDS15 requires PSRAM disabled in both Kconfig "
        "and devicetree, without PSRAM code copying.")
    endif()
  endif()
endif()

if(CONFIG_PM)
  zephyr_sources(power.c)
endif()

zephyr_sources_ifdef(CONFIG_POWEROFF poweroff.c)
zephyr_sources_ifdef(CONFIG_ANTI_ROLLBACK anti_rollback.c)
zephyr_sources_ifdef(CONFIG_QCC74X_COREDUMP_FULL_MEMORY coredump_memory_regions.c)

if(CONFIG_POWEROFF OR CONFIG_QCC74X_PM_LPFW)
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

zephyr_sources_ifdef(CONFIG_QCC74X_PM_LPFW
  pm_uart.c
  pm_pds15_glue.c
  pm_pds15.c
  ${QC7XX_SDK_PATH}/drivers/sys/qcc74x/qcc74x_lp.c
  ${QC7XX_SDK_PATH}/drivers/soc/qcc74x/std/startup/interrupt.c
)

set(LPFW_BIN
  ${QC7XX_SDK_PATH}/zephyr/blobs/qcc74x/bin/qcc74x_lp_fw.bin)
set(LPFW_PATCH
  ${QC7XX_SDK_PATH}/tools/lpfw/patch_lpfw.py)

if(CONFIG_QCC74X_PM_LPFW)
  if(NOT EXISTS "${LPFW_BIN}")
    message(FATAL_ERROR
      "Missing qcc74x LPFW blob: ${LPFW_BIN}. "
      "Run qapp/qcc74x/prop_blobs_fetch.py from the SDK root; ensure "
      "modules/hal/qcom/qcc74x/zephyr/blobs/qcc74x/bin/qcc74x_lp_fw.bin exists.")
  endif()
  if(NOT EXISTS "${LPFW_PATCH}")
    message(FATAL_ERROR "Missing LPFW patch tool: ${LPFW_PATCH}")
  endif()
  message(STATUS "QCC74X LPFW blob: ${LPFW_BIN}")
  zephyr_blobs_verify(FILES ${LPFW_BIN} REQUIRED)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LPFW_BIN})
  execute_process(COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_lpfw.py ${LPFW_BIN}
    COMMAND_ERROR_IS_FATAL ANY)
  if(CONFIG_BUILD_OUTPUT_BIN)
    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
      COMMAND ${Python3_EXECUTABLE} ${LPFW_PATCH}
        ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
        ${LPFW_BIN})
  endif()
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
