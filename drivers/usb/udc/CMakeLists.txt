# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_include_directories(${ZEPHYR_BASE}/drivers/usb/udc)
zephyr_library_sources_ifdef(CONFIG_UDC_QCC74X	udc_qcc74x.c)