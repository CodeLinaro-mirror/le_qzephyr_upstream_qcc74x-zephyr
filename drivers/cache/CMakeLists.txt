# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_CACHE_QCC74X	cache_qcc74x.c)
