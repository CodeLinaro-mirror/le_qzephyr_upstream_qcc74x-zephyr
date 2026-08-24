/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_QCC74X_DAC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_QCC74X_DAC_H_

#define DAC_CHANNEL_0 0
#define DAC_CHANNEL_1 1

#define DAC_CLK_DIV_16 0
#define DAC_CLK_DIV_32 1
#define DAC_CLK_DIV_64 3
#define DAC_CLK_DIV_1  4

/* DAC output voltage range in mV*/
#define DAC_VOLTAGE_RANGE_200_1200 0
#define DAC_VOLTAGE_RANGE_225_1425 1
#define DAC_VOLTAGE_RANGE_200_1800 3

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_QCC74X_DAC_H_ */
