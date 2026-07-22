/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_JPEG_QCC74X_H_
#define ZEPHYR_INCLUDE_DRIVERS_JPEG_QCC74X_H_

#include <zephyr/drivers/video.h>

#ifndef VIDEO_PIX_FMT_NV12
#define VIDEO_PIX_FMT_NV12 VIDEO_FOURCC('N','V','1','2')
#endif

#ifndef VIDEO_PIX_FMT_NV21
#define VIDEO_PIX_FMT_NV21 VIDEO_FOURCC('N','V','2','1')
#endif

#ifndef VIDEO_PIX_FMT_NV16
#define VIDEO_PIX_FMT_NV16 VIDEO_FOURCC('N','V','1','6')
#endif

#ifndef VIDEO_PIX_FMT_NV61
#define VIDEO_PIX_FMT_NV61 VIDEO_FOURCC('N','V','6','1')
#endif

#ifndef VIDEO_PIX_FMT_UYVY
#define VIDEO_PIX_FMT_UYVY VIDEO_FOURCC('U','Y','V','Y')
#endif

#ifndef VIDEO_PIX_FMT_YVYU
#define VIDEO_PIX_FMT_YVYU VIDEO_FOURCC('Y','V','Y','U')
#endif

#ifndef VIDEO_PIX_FMT_VYUY
#define VIDEO_PIX_FMT_VYUY VIDEO_FOURCC('V','Y','U','Y')
#endif

#ifndef VIDEO_PIX_FMT_GREY
#define VIDEO_PIX_FMT_GREY VIDEO_FOURCC('G','R','E','Y')
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_JPEG_QCC74X_H_ */
