# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/ipm.h)
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_IPM_QCC74X ipm_qcc74x.c)
