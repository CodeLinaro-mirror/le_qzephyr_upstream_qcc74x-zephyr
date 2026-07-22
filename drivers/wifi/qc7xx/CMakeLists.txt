# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Wireless HAL include dirs (wifi_mgmr_ext.h, fhost_wpa.h, wireless_api.h) are
# added globally by the QCC74x HAL wireless/socport CMakeLists. Only the local
# src/ dir needs to be added here.
zephyr_include_directories(src)

if(CONFIG_QC7XX_SOCKET_OFFLOAD)
  zephyr_include_directories(${ZEPHYR_BASE}/subsys/net/lib/sockets)
  zephyr_library_sources(
    src/qc7xx_socket_offload.c
    src/qc7xx_ipc_socket.c
  )
else()
  zephyr_library_sources(
    src/qc7xx_wifi_drv.c
    src/qc7xx_wifi_shell.c
  )
endif()

# RPC service for NP core (network processor)
if(CONFIG_OPENAMP AND CONFIG_WIFI_QCC75X)
  zephyr_library_sources(
    src/qc7xx_rpc_service.c
  )
endif()

