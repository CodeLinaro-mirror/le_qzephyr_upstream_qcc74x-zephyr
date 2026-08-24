# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

# Upstream zephyr/drivers/mdio/CMakeLists.txt does not call zephyr_library(),
# so there is no drivers__mdio library to amend. Create it here instead.
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_MDIO_QCC74X	mdio_qcc74x.c)
