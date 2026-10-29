# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_sources(
  start.S
  start_load.c
  system_qcr373.c
  hw_init.c
  soc.c
)

zephyr_sources_ifdef(CONFIG_WIFI_QCR373 soc_multi_heap.c)

if(CONFIG_BOOTLOADER_MCUBOOT)
  zephyr_sources(mcuboot_header.c)
endif()


zephyr_include_directories(.)

if(CONFIG_BUILD_OUTPUT_BIN AND CONFIG_QCR373_BUILD_POST_PROCESS)
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
    foreach(_qconn_src ${QC7XX_SDK_PATH}/zephyr/blobs/qcr373/tool
                       ${QC7XX_SDK_PATH}/../../../../prop/qcc74x-bin/qcr373/tool)
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

  if(CONFIG_PSRAM)
    set(QCONN_SECURE_CONFIG
      --chipname=qcr373
      --appkeys=shared
      --brdcfgdir=${QC7XX_SDK_PATH}/bsp/board/qcr373dk/config
      --ptfile=${QC7XX_SDK_PATH}/bsp/board/qcr373dk/partition_cfg_8M_psram.toml
      --imgfile=${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
    )
  else()
    set(QCONN_SECURE_CONFIG
      --chipname=qcr373
      --appkeys=shared
      --brdcfgdir=${QC7XX_SDK_PATH}/bsp/board/qcr373dk/config
      --imgfile=${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
    )
  endif()

  message("Qualcomm Tool Command: ${QCONN_SECURE}")
  message("Qualcomm Tool Para: ${QCONN_SECURE_CONFIG}")

  if(CONFIG_QCR373_MULTI_IMAGE)
    if("${CONFIG_QCR373_REMOTE_IMAGE}" STREQUAL "")
      message(FATAL_ERROR
        "CONFIG_QCR373_REMOTE_IMAGE must be supplied by sysbuild")
    endif()

    set(QCR373_MULTI_IMAGE_TOOL
      ${CMAKE_CURRENT_SOURCE_DIR}/qcr373_multi_image.py)

    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/soc.c
      DIRECTORY ${ZEPHYR_BASE} APPEND PROPERTY
      OBJECT_DEPENDS ${CONFIG_QCR373_REMOTE_IMAGE})

    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
      COMMAND ${Python3_EXECUTABLE} ${QCR373_MULTI_IMAGE_TOOL}
        --ap ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
        --np ${CONFIG_QCR373_REMOTE_IMAGE}
        --output ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
    )
  endif()

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

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/qcr3xx_common.ld.in CACHE INTERNAL "")
