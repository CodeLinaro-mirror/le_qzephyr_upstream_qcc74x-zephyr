/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_SOC_QCOM_QCR373_PINCTRL_SOC_H_
#define ZEPHYR_SOC_QCOM_QCR373_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/types.h>
#include <zephyr/dt-bindings/pinctrl/qc7xx-pinctrl-common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type for QC7XX pin.
 */
typedef struct pinctrl_soc_pin {
	gpio_pin_t pinnum; ///< GPIO pin number
	uint32_t pinmux;   ///< Pinmux settings
	uint32_t pinsel;   ///< Peripheral function select
	uint8_t ir_flag;   ///< Select pin as IR (Infrared)
} pinctrl_soc_pin_t;

/**
 * @brief Utility macro to initialize a single pin's pinnum.
 *
 * @param node_id Node identifier.
 * @param prop Property name.
 * @param idx Property entry index.
 */
#define Z_PINCTRL_QC7XX_PINNUM_INIT(node_id, prop, idx)                                            \
	(((DT_PROP_BY_IDX(node_id, prop, idx) >> GPIO_PIN_POS) & GPIO_PIN_MSK))

/**
 * @brief Utility macro to initialize a single pin's pinmux.
 *
 * @param node_id Node identifier.
 * @param prop Property name.
 * @param idx Property entry index.
 */
#define Z_PINCTRL_QC7XX_PINMUX_INIT(node_id, prop, idx)                                            \
	((((DT_PROP_BY_IDX(node_id, prop, idx) >> GPIO_FUN_POS) & GPIO_FUN_MSK)                    \
	  << QC7XX_GPIO_0_FUNC_SEL_POS) |                                                          \
	 ((DT_PROP(node_id, bias_pull_up)) << QC7XX_GPIO_0_PU_POS) |                               \
	 ((DT_PROP(node_id, bias_pull_down)) << QC7XX_GPIO_0_PD_POS) |                             \
	 ((DT_PROP(node_id, input_enable)) << QC7XX_GPIO_0_IE_POS) |                               \
	 ((DT_PROP(node_id, input_schmitt_enable)) << QC7XX_GPIO_0_SMT_POS) |                      \
	 ((DT_PROP(node_id, output_enable) | DT_PROP(node_id, output_low) |                        \
	   DT_PROP(node_id, output_high))                                                          \
	  << QC7XX_GPIO_0_OE_POS) |                                                                \
	 ((DT_PROP(node_id, output_low)) << QC7XX_GPIO_0_CLR_POS) |                                \
	 ((DT_PROP(node_id, output_high)) << QC7XX_GPIO_0_SET_POS) |                               \
	 ((DT_PROP(node_id, drive_strength)) << QC7XX_GPIO_0_DRV_POS))

/**
 * @brief Utility macro to initialize a single pin's pinsel.
 *
 * @param node_id Node identifier.
 * @param prop Property name.
 * @param idx Property entry index.
 */
#define Z_PINCTRL_QC7XX_PINSEL_INIT(node_id, prop, idx)                                            \
	(((DT_PROP_BY_IDX(node_id, prop, idx) >> GPIO_INTFUN_POS) & GPIO_INTFUN_MSK))

/**
 * @brief Utility macro to initialize a single pin.
 *
 * @param node_id Node identifier.
 * @param prop Property name.
 * @param idx Property entry index.
 */
#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{.pinnum = Z_PINCTRL_QC7XX_PINNUM_INIT(node_id, prop, idx),                                \
	 .pinmux = Z_PINCTRL_QC7XX_PINMUX_INIT(node_id, prop, idx),                                \
	 .pinsel = Z_PINCTRL_QC7XX_PINSEL_INIT(node_id, prop, idx),                                \
	 .ir_flag = DT_PROP(node_id, ir_enable)},

/**
 * @brief Utility macro to initialize each configured pin.
 *
 * @param node_id Node identifier.
 * @param prop Property name.
 * @param idx Property entry index.
 */
#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{                                                                                          \
		DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop), DT_FOREACH_PROP_ELEM, pinmux,    \
				       Z_PINCTRL_STATE_PIN_INIT)                                   \
	}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_QCOM_QCR373_PINCTRL_SOC_H_ */
