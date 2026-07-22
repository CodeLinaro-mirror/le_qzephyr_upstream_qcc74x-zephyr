/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#ifndef __JPEG_HEAD_H__
#define __JPEG_HEAD_H__

#include "stdint.h"

#define YUV_MODE_400				1
#define YUV_MODE_420				2
#define YUV_MODE_422				3

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create JPEG header
 *
 * @param type  YUV subsampling mode
 *              - 1 = YUV_MODE_400 (grayscale / 4:0:0)
 *              - 2 = YUV_MODE_420 (4:2:0, most common)
 *              - 3 = YUV_MODE_422 (4:2:2)
 * @param q     Quality factor (1–100)
 * @param width Image width
 * @param height Image height
 * @param out   Output buffer
 * @return Number of bytes written to @p out, or 0 on error
 */
uint32_t JpegHeadCreate(uint8_t type, uint8_t q, int width, int height, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __JPEG_HEAD_H__ */