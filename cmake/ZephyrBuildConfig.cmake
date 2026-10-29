# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

# Keeps `west build -b qcc748m_evk` working for all qcc74x apps, for
# compatibility with the old project layout where the board was named
# qcc748m_evk instead of qcc748m3_evk.
#
# Zephyr's own board_aliases/BOARD_ALIAS mechanisms either fail schema
# validation (board.yml) or mis-handle single-SoC boards with no qualifier
# (ZEPHYR_BOARD_ALIASES), so the BOARD cache entry is remapped directly here
# instead. This file is loaded automatically for every app in the workspace
# via the ZephyrBuildConfiguration CMake package mechanism, before boards are
# resolved.
if(BOARD STREQUAL "qcc748m_evk")
  set(BOARD qcc748m3_evk CACHE STRING "Board" FORCE)
endif()
