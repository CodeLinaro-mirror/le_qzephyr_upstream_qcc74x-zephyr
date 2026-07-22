# Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_CAN_QCC74X	can_qcc74x.c)

# Add headers
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib/include)

# Link static library
zephyr_library_link_libraries(${CMAKE_CURRENT_SOURCE_DIR}/lib/libiso11898.a)