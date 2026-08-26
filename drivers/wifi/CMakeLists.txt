# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

# QCC74x source Wi-Fi driver (qc7xx). Kept in the module so the Zephyr tree stays
# clean; its own CMakeLists globally exposes src/ so the HAL socport glue can find
# qc7xx_wifi_drv_priv.h.
add_subdirectory_ifdef(CONFIG_WIFI_QCC74X qc7xx)
add_subdirectory_ifdef(CONFIG_WIFI_QCC75X qc7xx)
