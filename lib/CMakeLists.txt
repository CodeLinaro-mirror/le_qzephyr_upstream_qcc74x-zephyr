# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

if(CONFIG_QCC74X_VIK_MEMCPY)
  zephyr_library_sources(lib_vikmemcpy.c)
  zephyr_library_compile_options(
    -fno-builtin-memcpy
    -fno-tree-loop-distribute-patterns
  )
endif()
