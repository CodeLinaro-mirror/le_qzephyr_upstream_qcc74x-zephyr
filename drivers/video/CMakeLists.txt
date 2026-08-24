# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_include_directories(${ZEPHYR_BASE}/drivers/video)
zephyr_library_sources_ifdef(CONFIG_VIDEO_QCC74X video_qcc74x.c)
if(CONFIG_VIDEO_QCC74X_JPEG)
    zephyr_library_sources(video_qcc74x_jpeg.c jpeg/jpeg_head.c)
endif()