# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0.

cmake_minimum_required(VERSION 3.20.0)

zephyr_syscall_include_directories(include)

zephyr_include_directories(include)

add_subdirectory(drivers)
