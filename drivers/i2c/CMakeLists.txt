# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_include_directories(${ZEPHYR_BASE}/drivers/i2c)
zephyr_library_sources_ifdef(CONFIG_I2C_QCC74X	i2c_qcc74x.c)
