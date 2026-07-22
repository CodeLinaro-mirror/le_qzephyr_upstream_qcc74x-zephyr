# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0.

if (CONFIG_BUILD_ONLY_NO_BLOBS)
  message(WARNING "
  ---------------------------------------------------------------------------
  Building only the Bluetooth driver without binary blobs and patches.
  This is only for building (CI) purposes and will not work on a real device.
  ---------------------------------------------------------------------------
  ")
else()


endif() # CONFIG_BUILD_ONLY_NO_BLOBS

zephyr_library_sources_ifdef(CONFIG_BT_QCC74X	bt_hci_qcc74x.c)
