/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_QC7XX_PINCTRL_COMMON_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_QC7XX_PINCTRL_COMMON_H_

/**
 * @name Qualcomm QC7XX GPIO pin list.
 * @{
 */
#define GPIO_0  (0U)
#define GPIO_1  (1U)
#define GPIO_2  (2U)
#define GPIO_3  (3U)
#define GPIO_4  (4U)
#define GPIO_5  (5U)
#define GPIO_6  (6U)
#define GPIO_7  (7U)
#define GPIO_8  (8U)
#define GPIO_9  (9U)
#define GPIO_10 (10U)
#define GPIO_11 (11U)
#define GPIO_12 (12U)
#define GPIO_13 (13U)
#define GPIO_14 (14U)
#define GPIO_15 (15U)
#define GPIO_16 (16U)
#define GPIO_17 (17U)
#define GPIO_18 (18U)
#define GPIO_19 (19U)
#define GPIO_20 (20U)
#define GPIO_21 (21U)
#define GPIO_22 (22U)
#define GPIO_23 (23U)
#define GPIO_24 (24U)
#define GPIO_25 (25U)
#define GPIO_26 (26U)
#define GPIO_27 (27U)
#define GPIO_28 (28U)
#define GPIO_29 (29U)
#define GPIO_30 (30U)
#define GPIO_31 (31U)
#define GPIO_32 (32U)
#define GPIO_33 (33U)
#define GPIO_34 (34U)
#define GPIO_35 (35U)
#define GPIO_36 (36U)
#define GPIO_37 (37U)
#define GPIO_38 (38U)
#define GPIO_39 (39U)
#define GPIO_40 (40U)
#define GPIO_41 (41U)
#define GPIO_42 (42U)
#define GPIO_43 (43U)
#define GPIO_44 (44U)
#define GPIO_45 (45U)
#define GPIO_46 (46U)
#define GPIO_47 (47U)
#define GPIO_48 (48U)
#define GPIO_49 (49U)
#define GPIO_50 (50U)
#define GPIO_51 (51U)
#define GPIO_52 (52U)
#define GPIO_53 (53U)
#define GPIO_54 (54U)
#define GPIO_55 (55U)
#define GPIO_56 (56U)
#define GPIO_57 (57U)
#define GPIO_58 (58U)
#define GPIO_59 (59U)
#define GPIO_60 (60U)
#define GPIO_61 (61U)
#define GPIO_62 (62U)
#define GPIO_63 (63U)
/** @} */

#define GPIO_PIN_MSK (0x3FU)
#define GPIO_PIN_POS (0U)

#define GPIO_FUN_MSK (0x1FU)
#define GPIO_FUN_POS (GPIO_PIN_POS + 6U)

#define GPIO_INTFUN_MSK (0xFU)
#define GPIO_INTFUN_POS (GPIO_FUN_POS + 5U)

/** Get pin function from the pinmux value */
#define GET_PIN_FUN(pinmux) ((pinmux & GLB_REG_GPIO_0_FUNC_SEL_MSK) >> GLB_REG_GPIO_0_FUNC_SEL_POS)

/* Zephyr 4.4 QC7XX pinmux encoding. */
#define QC7XX_PIN_FUNC_SHIFT           0U
#define QC7XX_PIN_FUNC_MASK            0x1FU
#define QC7XX_PIN_GET_FUNC(pinmux)     (((pinmux) >> QC7XX_PIN_FUNC_SHIFT) & QC7XX_PIN_FUNC_MASK)

#define QC7XX_PIN_FUNC_CFG_SHIFT       0U
#define QC7XX_PIN_FUNC_CFG_MASK        0xFFFFU
#define QC7XX_PIN_GET_FUNC_CFG(pinmux) (((pinmux) >> QC7XX_PIN_FUNC_CFG_SHIFT) & QC7XX_PIN_FUNC_CFG_MASK)

#define QC7XX_PIN_NUM_SHIFT            16U
#define QC7XX_PIN_NUM_MASK             0xFFU
#define QC7XX_PIN_GET_NUM(pinmux)      (((pinmux) >> QC7XX_PIN_NUM_SHIFT) & QC7XX_PIN_NUM_MASK)

#define QC7XX_PIN_UART_SIG_SHIFT       24U
#define QC7XX_PIN_UART_SIG_MASK        0x1FU
#define QC7XX_PIN_GET_UART_SIG(pinmux) (((pinmux) >> QC7XX_PIN_UART_SIG_SHIFT) & QC7XX_PIN_UART_SIG_MASK)

#define QC7XX_PIN_IR_FLAG_SHIFT        24U
#define QC7XX_PIN_IR_FLAG_MASK         0x1FU
#define QC7XX_PIN_GET_IR_FLAG(pinmux)  (((pinmux) >> QC7XX_PIN_IR_FLAG_SHIFT) & QC7XX_PIN_IR_FLAG_MASK)

#define QC7XX_PIN_BIAS_SHIFT           4U
#define QC7XX_PIN_BIAS_MASK            0x3U
#define QC7XX_PIN_DRV_SHIFT            0U
#define QC7XX_PIN_DRV_MASK             0x3U
#define QC7XX_PIN_OUT_SHIFT            24U
#define QC7XX_PIN_IN_EN_SHIFT          0U
#define QC7XX_PIN_OUT_EN_SHIFT         6U

#define QC7XX_NO_PULL                  0x0
#define QC7XX_PULL_UP                  0x1
#define QC7XX_PULL_DOWN                0x2

#define QC7XX_PUSH_PULL                0x0
#define QC7XX_OPEN_DRAIN               0x0

#define QC7XX_PIN_OUT_HIGH             0x1
#define QC7XX_PIN_OUT_LOW              0x0
#define QC7XX_PIN_OUT_EN               1
#define QC7XX_PIN_IN_EN                1

#define GPIO_MODE_SHIFT                5
#define GPIO_MODE_MASK                 (0xf << GPIO_MODE_SHIFT)
#define GPIO_INPUT                     (1 << 5)
#define GPIO_OUTPUT                    (1 << 6)
#define GPIO_ANALOG                    (1 << 7)
#define GPIO_ALTERNATE                 (1 << 8)

#define GPIO_PUPD_SHIFT                9
#define GPIO_PUPD_MASK                 (3 << GPIO_PUPD_SHIFT)
#define GPIO_FLOAT                     (0 << GPIO_PUPD_SHIFT)
#define GPIO_PULLUP                    (1 << GPIO_PUPD_SHIFT)
#define GPIO_PULLDOWN                  (2 << GPIO_PUPD_SHIFT)

#define GPIO_SMT_SHIFT                 11
#define GPIO_SMT_MASK                  (1 << GPIO_SMT_SHIFT)
#define GPIO_SMT_DIS                   (0 << GPIO_SMT_SHIFT)
#define GPIO_SMT_EN                    (1 << GPIO_SMT_SHIFT)

#define GPIO_DRV_SHIFT                 12
#define GPIO_DRV_MASK                  (3 << GPIO_DRV_SHIFT)
#define GPIO_DRV_0                     (0 << GPIO_DRV_SHIFT)
#define GPIO_DRV_1                     (1 << GPIO_DRV_SHIFT)
#define GPIO_DRV_2                     (2 << GPIO_DRV_SHIFT)
#define GPIO_DRV_3                     (3 << GPIO_DRV_SHIFT)

/*
 * Definitions used to extract pin properties:
 * bias, drive and initial pin level.
 */
#define QC7XX_GPIO_0_FUNC_SEL_POS (8U)
#define QC7XX_GPIO_0_PU_POS       (4U)
#define QC7XX_GPIO_0_PD_POS       (5U)
#define QC7XX_GPIO_0_IE_POS       (0U)
#define QC7XX_GPIO_0_SMT_POS      (1U)
#define QC7XX_GPIO_0_OE_POS       (6U)
#define QC7XX_GPIO_0_CLR_POS      (26U)
#define QC7XX_GPIO_0_SET_POS      (25U)
#define QC7XX_GPIO_0_DRV_POS      (2U)

/**
 * @brief Qualcomm QC7XX pinmux bit field.
 * @anchor QC7XX_PINMUX
 *
 * Fields:
 *    0:5 - GPIO PIN
 *   6:10 - GPIO Function
 *  11:15 - GPIO Internal Function Select
 *
 * @param pin        Pin  (0..63)
 * @param func_sel   Function select (SDH, SPI0, FLASH, ...)
 * @param periph_sel Peripheral function select (UART0_RTS, UART0_CTS, ...)
 */
#define QC7XX_PINMUX_LEGACY(pin, func_sel, periph_sel)                                             \
	((((GPIO_##pin) & GPIO_PIN_MSK) << GPIO_PIN_POS) |                                         \
	 (((GPIO_FUN_##func_sel) & GPIO_FUN_MSK) << GPIO_FUN_POS) |                                \
	 (((GPIO_INTFUN_##periph_sel) & GPIO_INTFUN_MSK) << GPIO_INTFUN_POS))

#define QC7XX_PINMUX_V44(pin, pinmux) (((pin) << QC7XX_PIN_NUM_SHIFT) | (pinmux))
#define QC7XX_PINMUX_SELECT(_1, _2, _3, NAME, ...) NAME
#define QC7XX_PINMUX(...)                                                                          \
	QC7XX_PINMUX_SELECT(__VA_ARGS__, QC7XX_PINMUX_LEGACY, QC7XX_PINMUX_V44)(__VA_ARGS__)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_QC7XX_COMMON_H_ */
