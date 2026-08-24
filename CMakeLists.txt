# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20.0)

zephyr_syscall_include_directories(include)

zephyr_include_directories(include)

add_subdirectory(drivers)
