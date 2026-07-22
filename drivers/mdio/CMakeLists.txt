# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0

# Upstream zephyr/drivers/mdio/CMakeLists.txt does not call zephyr_library(),
# so there is no drivers__mdio library to amend. Create it here instead.
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_MDIO_QCC74X	mdio_qcc74x.c)
