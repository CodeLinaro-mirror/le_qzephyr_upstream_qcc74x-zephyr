# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/i2s.h)
zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_I2S_QCC74X	i2s_qcc74x.c)
