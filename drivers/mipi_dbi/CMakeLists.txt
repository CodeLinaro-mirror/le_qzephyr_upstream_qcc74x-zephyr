# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_sources_ifdef(CONFIG_MIPI_DBI_QCC74X	mipi_dbi_qcc74x.c)