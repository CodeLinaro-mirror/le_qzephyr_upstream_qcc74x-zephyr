/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_SOC_QCOM_QCC74X_PINCTRL_SOC_H_
#define ZEPHYR_SOC_QCOM_QCC74X_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>

#include <zephyr/dt-bindings/pinctrl/qc7xx-pinctrl-common.h>
#include <zephyr/dt-bindings/pinctrl/qc7xx-qcc74x-pinctrl.h>

/** Type for QC7XX pin. */
typedef struct pinctrl_soc_pin {
	/** Pinmux settings. */
	uint32_t pinmux;
	/** Pincfg settings. */
	uint32_t pincfg;
} pinctrl_soc_pin_t;

#define Z_PINCTRL_QC7XX_PINMUX_INIT(node_id, prop, idx) \
	DT_PROP_BY_IDX(node_id, prop, idx)

#define Z_PINCTRL_QC7XX_PINCFG_INIT(node_id)                                                       \
	(((QC7XX_NO_PULL * DT_PROP(node_id, bias_disable)) << QC7XX_PIN_BIAS_SHIFT) |              \
	 ((QC7XX_PULL_UP * DT_PROP(node_id, bias_pull_up)) << QC7XX_PIN_BIAS_SHIFT) |              \
	 ((QC7XX_PULL_DOWN * DT_PROP(node_id, bias_pull_down)) << QC7XX_PIN_BIAS_SHIFT) |          \
	 ((QC7XX_PUSH_PULL * DT_PROP(node_id, drive_push_pull)) << QC7XX_PIN_DRV_SHIFT) |          \
	 ((QC7XX_OPEN_DRAIN * DT_PROP(node_id, drive_open_drain)) << QC7XX_PIN_DRV_SHIFT) |        \
	 ((QC7XX_PIN_OUT_HIGH * DT_PROP(node_id, output_high)) << QC7XX_PIN_OUT_SHIFT) |           \
	 ((QC7XX_PIN_OUT_LOW * DT_PROP(node_id, output_low)) << QC7XX_PIN_OUT_SHIFT) |             \
	 ((QC7XX_PIN_OUT_EN * DT_PROP(node_id, output_enable)) << QC7XX_PIN_OUT_EN_SHIFT) |        \
	 ((QC7XX_PIN_IN_EN * DT_PROP(node_id, input_enable)) << QC7XX_PIN_IN_EN_SHIFT))

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{ .pinmux = Z_PINCTRL_QC7XX_PINMUX_INIT(node_id, prop, idx),                                \
	  .pincfg = Z_PINCTRL_QC7XX_PINCFG_INIT(node_id) },

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                    \
	{ DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop), DT_FOREACH_PROP_ELEM, pinmux,           \
				       Z_PINCTRL_STATE_PIN_INIT) }

#endif /* ZEPHYR_SOC_QCOM_QCC74X_PINCTRL_SOC_H_ */
