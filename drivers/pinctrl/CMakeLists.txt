# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0.

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_PINCTRL_QCC74X	pinctrl_qcc74x.c)
zephyr_library_sources_ifdef(CONFIG_PINCTRL_QC7XX	pinctrl_qc7xx.c)
