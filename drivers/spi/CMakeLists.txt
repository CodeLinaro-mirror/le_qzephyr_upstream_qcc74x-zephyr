# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_include_directories(${ZEPHYR_BASE}/drivers/spi)
zephyr_library_sources_ifdef(CONFIG_SPI_QCC74X	spi_qcc74x.c)
