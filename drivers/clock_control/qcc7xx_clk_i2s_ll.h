/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_I2S_LL_H_
#define QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_I2S_LL_H_

/* Structure defining clock properties needed for configuration */
struct i2s_clk_settings {
	uint8_t enable;
	uint8_t div;
	uint8_t in_ref;
	uint8_t out_ref;
};

#endif /* QCC74XZEPHYRPORTING_DRIVERS_CLOCK_CONTROL_QCC7XX_CLK_I2S_LL_H_ */
