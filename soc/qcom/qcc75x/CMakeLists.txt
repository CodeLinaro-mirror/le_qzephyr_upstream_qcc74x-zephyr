# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(
  start.S
  start_load.c
  system_qcc75x.c
  hw_init.c
  fw_header.c
  soc.c
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

  if(CONFIG_PSRAM)
    set(QCONN_SECURE_CONFIG
      --chipname=qcc75x
      --appkeys=shared
      --brdcfgdir=${QC7XX_SDK_PATH}/bsp/board/qcc75xdk/config
      --ptfile=${QC7XX_SDK_PATH}/bsp/board/qcc75xdk/partition_cfg_8M_psram.toml
      --imgfile=${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
    )
  else()
    set(QCONN_SECURE_CONFIG
      --chipname=qcc75x
      --appkeys=shared
      --brdcfgdir=${QC7XX_SDK_PATH}/bsp/board/qcc75xdk/config
      --imgfile=${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
    )
  endif()

  message("Qualcomm Tool Command: ${QCONN_SECURE}")
  message("Qualcomm Tool Para: ${QCONN_SECURE_CONFIG}")

  # QConn_Secure is a host binary shipped under tools/qc7xx_tools/, which is not
  # committed to the HAL repo (distributed separately). Only append the secure
  # image post-build step when the tool is actually present; otherwise skip it
  # so the firmware still builds. The secure image is only needed for flashing.
  if(EXISTS ${QCONN_SECURE})
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

if(CONFIG_PSRAM)
  if(CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU1)
    set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/default_psram_cpu1.ld CACHE INTERNAL "")
  elseif(CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU0)
    set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/default_psram_cpu0.ld CACHE INTERNAL "")
  else()
    set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/default_psram.ld CACHE INTERNAL "")
  endif()
else()
  if(CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU1)
    set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/default_cpu1.ld CACHE INTERNAL "")
  elseif(CONFIG_BOARD_QCC75X_EVK_QCC75X_CPU0)
    set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/default_cpu0.ld CACHE INTERNAL "")
  else()
    set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/default.ld CACHE INTERNAL "")
  endif()
endif()
