# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_SYSCON_QCC74X_EFUSE efuse-qcc74x.c)