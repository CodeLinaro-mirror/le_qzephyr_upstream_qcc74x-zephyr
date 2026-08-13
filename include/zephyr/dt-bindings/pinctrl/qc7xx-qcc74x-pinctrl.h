/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_QCC74X_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_QCC74X_PINCTRL_H_

/* clang-format off */
/**
 * @name Qualcomm QCC74x pin function list.
 * @{
 */
#define GPIO_FUN_SDH    (0U)
#define GPIO_FUN_SPI    (1U)
#define GPIO_FUN_FLASH  (2U)
#define GPIO_FUN_I2S    (3U)
#define GPIO_FUN_PDM    (4U)
#define GPIO_FUN_I2C0   (5U)
#define GPIO_FUN_I2C1   (6U)
#define GPIO_FUN_UART   (7U)
#define GPIO_FUN_EMAC   (8U)
#define GPIO_FUN_CAM    (9U)
#define GPIO_FUN_ANALOG (10U)
#define GPIO_FUN_GPIO   (11U)
#define GPIO_FUN_SDIO   (12U)
#define GPIO_FUN_PWM0   (16U)
#define GPIO_FUN_DBI_B  (22U)
#define GPIO_FUN_DBI_C  (23U)
#define GPIO_FUN_QSPI   (24U)
#define GPIO_FUN_AUDAC  (25U)
#define GPIO_FUN_JTAG   (26U)
#define GPIO_FUN_CLKOUT (31U)
/** @} */

/**
 * @name Qualcomm QC7XX peripheral pin function list.
 * @{
 */
#define GPIO_INTFUN_UART0_RTS    (0U)
#define GPIO_INTFUN_UART0_CTS    (1U)
#define GPIO_INTFUN_UART0_TXD    (2U)
#define GPIO_INTFUN_UART0_RXD    (3U)
#define GPIO_INTFUN_UART1_RTS    (4U)
#define GPIO_INTFUN_UART1_CTS    (5U)
#define GPIO_INTFUN_UART1_TXD    (6U)
#define GPIO_INTFUN_UART1_RXD    (7U)
#define GPIO_INTFUN_ISO11898_TXD (10U)
#define GPIO_INTFUN_ISO11898_RXD (11U)
#define GPIO_INTFUN_PWM_SEL0     (0U)
#define GPIO_INTFUN_PWM_SEL1     (1U)
#define GPIO_INTFUN_none         (12U)
/** @} */

#define GPIO_UART_FUNC_UART0_RTS 0
#define GPIO_UART_FUNC_UART0_CTS 1
#define GPIO_UART_FUNC_UART0_TXD 2
#define GPIO_UART_FUNC_UART0_RXD 3
#define GPIO_UART_FUNC_UART1_RTS 4
#define GPIO_UART_FUNC_UART1_CTS 5
#define GPIO_UART_FUNC_UART1_TXD 6
#define GPIO_UART_FUNC_UART1_RXD 7

#define GPIO_FUNC_SHIFT          0
#define GPIO_FUNC_UART           (7 << GPIO_FUNC_SHIFT)

#define UART0_TXD_GPIO21                                                                        \
	QC7XX_PINMUX(21 | (GPIO_UART_FUNC_UART0_TXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART0_RXD_GPIO22                                                                        \
	QC7XX_PINMUX(22 | (GPIO_UART_FUNC_UART0_RXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART1_RTS_GPIO27                                                                        \
	QC7XX_PINMUX(27 | (GPIO_UART_FUNC_UART1_RTS << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART1_CTS_GPIO28                                                                        \
	QC7XX_PINMUX(28 | (GPIO_UART_FUNC_UART1_CTS << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART1_TXD_GPIO29                                                                        \
	QC7XX_PINMUX(29 | (GPIO_UART_FUNC_UART1_TXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART1_RXD_GPIO30                                                                        \
	QC7XX_PINMUX(30 | (GPIO_UART_FUNC_UART1_RXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART1_RXD_GPIO29                                                                        \
	QC7XX_PINMUX(29 | (GPIO_UART_FUNC_UART1_RXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define UART1_TXD_GPIO28                                                                        \
	QC7XX_PINMUX(28 | (GPIO_UART_FUNC_UART1_TXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for ISO11898 (CAN) - signal routed through UART mux */
#define GPIO_UART_FUNC_ISO11898_TXD  10
#define GPIO_UART_FUNC_ISO11898_RXD  11
#define ISO11898_TXD_GPIO20                                                                     \
	QC7XX_PINMUX(20 | (GPIO_UART_FUNC_ISO11898_TXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define ISO11898_RXD_GPIO16                                                                     \
	QC7XX_PINMUX(16 | (GPIO_UART_FUNC_ISO11898_RXD << (QC7XX_PIN_UART_SIG_SHIFT - QC7XX_PIN_NUM_SHIFT)), \
		       GPIO_FUNC_UART | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for I2C1 */
#define I2C1_SCL_GPIO18                                                                         \
	QC7XX_PINMUX(18, GPIO_FUN_I2C1 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define I2C1_SDA_GPIO19                                                                         \
	QC7XX_PINMUX(19, GPIO_FUN_I2C1 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for PWM0 */
#define PWM0_CH0P_GPIO4                                                                         \
	QC7XX_PINMUX(4, GPIO_FUN_PWM0 | GPIO_ALTERNATE | GPIO_DRV_1)
#define PWM0_CH0P_GPIO20                                                                        \
	QC7XX_PINMUX(20, GPIO_FUN_PWM0 | GPIO_ALTERNATE | GPIO_DRV_1)

/* V44-format macros for I2S0 */
#define I2S0_BCLK_GPIO16                                                                        \
	QC7XX_PINMUX(16, GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)
#define I2S0_FS_GPIO17                                                                          \
	QC7XX_PINMUX(17, GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)
#define I2S0_DO_GPIO31                                                                          \
	QC7XX_PINMUX(31, GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)
#define I2S0_DI_GPIO34                                                                          \
	QC7XX_PINMUX(34, GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for SPI0 */
#define SPI0_SCLK_GPIO13                                                                        \
	QC7XX_PINMUX(13, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MISO_GPIO10                                                                        \
	QC7XX_PINMUX(10, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MOSI_GPIO11                                                                        \
	QC7XX_PINMUX(11, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_DRV_1)
#define SPI0_SS_GPIO12                                                                          \
	QC7XX_PINMUX(12, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_SCLK_GPIO25                                                                        \
	QC7XX_PINMUX(25, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MISO_GPIO26                                                                        \
	QC7XX_PINMUX(26, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MOSI_GPIO23                                                                        \
	QC7XX_PINMUX(23, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_DRV_1)
#define SPI0_SS_GPIO24                                                                          \
	QC7XX_PINMUX(24, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_SCLK_GPIO29                                                                        \
	QC7XX_PINMUX(29, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MISO_GPIO30                                                                        \
	QC7XX_PINMUX(30, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MOSI_GPIO27                                                                        \
	QC7XX_PINMUX(27, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_DRV_1)
#define SPI0_SS_GPIO28                                                                          \
	QC7XX_PINMUX(28, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MISO_GPIO34                                                                        \
	QC7XX_PINMUX(34, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_MOSI_GPIO31                                                                        \
	QC7XX_PINMUX(31, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_DRV_1)
#define SPI0_SCLK_GPIO33                                                                        \
	QC7XX_PINMUX(33, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SPI0_SS_GPIO32                                                                          \
	QC7XX_PINMUX(32, GPIO_FUN_SPI | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for CLKOUT */
#define CLKOUT_GPIO0                                                                            \
	QC7XX_PINMUX(0, GPIO_FUN_CLKOUT | GPIO_ALTERNATE | GPIO_DRV_1)
#define CLKOUT_GPIO23                                                                           \
	QC7XX_PINMUX(23, GPIO_FUN_CLKOUT | GPIO_ALTERNATE | GPIO_DRV_1)

/* V44-format macros for PWM0 additional channels */
#define PWM0_CH1P_GPIO1                                                                         \
	QC7XX_PINMUX(1, GPIO_FUN_PWM0 | GPIO_ALTERNATE | GPIO_DRV_1)
#define PWM0_CH0N_GPIO1                                                                         \
	QC7XX_PINMUX(1, GPIO_FUN_PWM0 | GPIO_ALTERNATE | GPIO_DRV_1)

/* V44-format macros for IR RX - ir_flag=1 encoded in bits[28:24] (same field as uart_sig).
 * The pin argument encodes both pin number (bits[7:0]) and ir_flag (bit[8]):
 *   pin | (1U << (QC7XX_PIN_IR_FLAG_SHIFT - QC7XX_PIN_NUM_SHIFT))
 * The V44 driver reads QC7XX_PIN_GET_IR_FLAG(pinmux) and calls GLB_IR_RX_GPIO_Sel().
 */
#define IR_RX_GPIO9                                                                             \
	QC7XX_PINMUX(9U  | (1U << (QC7XX_PIN_IR_FLAG_SHIFT - QC7XX_PIN_NUM_SHIFT)),            \
		       GPIO_FUN_GPIO | GPIO_INPUT | GPIO_SMT_EN | GPIO_DRV_1)
#define IR_RX_GPIO10                                                                            \
	QC7XX_PINMUX(10U | (1U << (QC7XX_PIN_IR_FLAG_SHIFT - QC7XX_PIN_NUM_SHIFT)),            \
		       GPIO_FUN_GPIO | GPIO_INPUT | GPIO_SMT_EN | GPIO_DRV_1)
#define IR_RX_GPIO20                                                                            \
	QC7XX_PINMUX(20U | (1U << (QC7XX_PIN_IR_FLAG_SHIFT - QC7XX_PIN_NUM_SHIFT)),            \
		       GPIO_FUN_GPIO | GPIO_INPUT | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for I2C0 */
#define I2C0_SCL_GPIO18                                                                         \
	QC7XX_PINMUX(18, GPIO_FUN_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define I2C0_SDA_GPIO19                                                                         \
	QC7XX_PINMUX(19, GPIO_FUN_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define I2C0_SCL_GPIO30                                                                         \
	QC7XX_PINMUX(30, GPIO_FUN_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define I2C0_SDA_GPIO29                                                                         \
	QC7XX_PINMUX(29, GPIO_FUN_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for SDH */
#define SDH_DAT1_GPIO10                                                                         \
	QC7XX_PINMUX(10, GPIO_FUN_SDH | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDH_DAT0_GPIO11                                                                         \
	QC7XX_PINMUX(11, GPIO_FUN_SDH | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDH_CLK_GPIO12                                                                          \
	QC7XX_PINMUX(12, GPIO_FUN_SDH | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDH_CMD_GPIO13                                                                          \
	QC7XX_PINMUX(13, GPIO_FUN_SDH | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDH_DAT3_GPIO14                                                                         \
	QC7XX_PINMUX(14, GPIO_FUN_SDH | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDH_DAT2_GPIO15                                                                         \
	QC7XX_PINMUX(15, GPIO_FUN_SDH | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for SDIO */
#define SDIO_DAT0_GPIO14                                                                        \
	QC7XX_PINMUX(14, GPIO_FUN_SDIO | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDIO_DAT1_GPIO15                                                                        \
	QC7XX_PINMUX(15, GPIO_FUN_SDIO | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDIO_DAT2_GPIO10                                                                        \
	QC7XX_PINMUX(10, GPIO_FUN_SDIO | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDIO_DAT3_GPIO11                                                                        \
	QC7XX_PINMUX(11, GPIO_FUN_SDIO | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDIO_CMD_GPIO12                                                                         \
	QC7XX_PINMUX(12, GPIO_FUN_SDIO | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define SDIO_CLK_GPIO13                                                                         \
	QC7XX_PINMUX(13, GPIO_FUN_SDIO | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for I2S0 additional pin combinations */
#define I2S0_BCLK_GPIO8                                                                         \
	QC7XX_PINMUX(8,  GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)
#define I2S0_FS_GPIO5                                                                           \
	QC7XX_PINMUX(5,  GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)
#define I2S0_DI_GPIO6                                                                           \
	QC7XX_PINMUX(6,  GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define I2S0_DO_GPIO7                                                                           \
	QC7XX_PINMUX(7,  GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)
#define I2S0_DI_GPIO30                                                                          \
	QC7XX_PINMUX(30, GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define I2S0_DO_GPIO27                                                                          \
	QC7XX_PINMUX(27, GPIO_FUN_I2S | GPIO_ALTERNATE | GPIO_DRV_1)

/* V44-format macros for EMAC (RMII) */
#define EMAC_REF_CLK_GPIO25                                                                     \
	QC7XX_PINMUX(25, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define EMAC_TXD0_GPIO26                                                                        \
	QC7XX_PINMUX(26, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define EMAC_TXD1_GPIO27                                                                        \
	QC7XX_PINMUX(27, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define EMAC_RXD0_GPIO28                                                                        \
	QC7XX_PINMUX(28, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define EMAC_RXD1_GPIO29                                                                        \
	QC7XX_PINMUX(29, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define EMAC_RXERR_GPIO30                                                                       \
	QC7XX_PINMUX(30, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define EMAC_TX_EN_GPIO31                                                                       \
	QC7XX_PINMUX(31, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define EMAC_RX_DV_GPIO32                                                                       \
	QC7XX_PINMUX(32, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define EMAC_MDC_GPIO33                                                                         \
	QC7XX_PINMUX(33, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define EMAC_MDIO_GPIO34                                                                        \
	QC7XX_PINMUX(34, GPIO_FUN_EMAC | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for AUDAC */
#define AUDAC_PWM_P_GPIO14                                                                      \
	QC7XX_PINMUX(14, GPIO_FUN_AUDAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define AUDAC_PWM_N_GPIO15                                                                      \
	QC7XX_PINMUX(15, GPIO_FUN_AUDAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define AUDAC_PWM_P_GPIO22                                                                      \
	QC7XX_PINMUX(22, GPIO_FUN_AUDAC | GPIO_ALTERNATE | GPIO_DRV_1)
#define AUDAC_PWM_N_GPIO23                                                                      \
	QC7XX_PINMUX(23, GPIO_FUN_AUDAC | GPIO_ALTERNATE | GPIO_DRV_1)

/* V44-format macros for DBI-C */
#define DBI_C_SCL_GPIO12                                                                        \
	QC7XX_PINMUX(12, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_DRV_1)
#define DBI_C_CSn_GPIO13                                                                        \
	QC7XX_PINMUX(13, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_DRV_1)
#define DBI_C_SDA0_GPIO14                                                                       \
	QC7XX_PINMUX(14, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_DRV_1)
#define DBI_C_DCn_GPIO15                                                                        \
	QC7XX_PINMUX(15, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_DRV_1)
/* DBI-C with drive-strength=2, pull-up, SMT (stronger drive for display) */
#define DBI_C_SCL_GPIO12_DS2                                                                    \
	QC7XX_PINMUX(12, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_2)
#define DBI_C_CSn_GPIO13_DS2                                                                    \
	QC7XX_PINMUX(13, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_2)
#define DBI_C_SDA0_GPIO14_DS2                                                                   \
	QC7XX_PINMUX(14, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_2)
#define DBI_C_DCn_GPIO15_DS2                                                                    \
	QC7XX_PINMUX(15, GPIO_FUN_DBI_C | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_2)

/* V44-format macros for CAM0 (DVP camera interface) */
#define CAM0_DAT0_GPIO24                                                                        \
	QC7XX_PINMUX(24, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT1_GPIO25                                                                        \
	QC7XX_PINMUX(25, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT2_GPIO26                                                                        \
	QC7XX_PINMUX(26, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT3_GPIO27                                                                        \
	QC7XX_PINMUX(27, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_HSYNC_GPIO28                                                                       \
	QC7XX_PINMUX(28, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_VSYNC_GPIO29                                                                       \
	QC7XX_PINMUX(29, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_CLK_GPIO30                                                                         \
	QC7XX_PINMUX(30, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT4_GPIO31                                                                        \
	QC7XX_PINMUX(31, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT5_GPIO32                                                                        \
	QC7XX_PINMUX(32, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT6_GPIO33                                                                        \
	QC7XX_PINMUX(33, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)
#define CAM0_DAT7_GPIO34                                                                        \
	QC7XX_PINMUX(34, GPIO_FUN_CAM | GPIO_ALTERNATE | GPIO_SMT_EN | GPIO_DRV_1)

/* V44-format macros for analog (ADC/DAC/AUADC) - no digital buffer, no pull */
#define ADC_CH8_GPIO1                                                                           \
	QC7XX_PINMUX(1,  GPIO_FUN_ANALOG)
#define ADC_CH2_GPIO2                                                                           \
	QC7XX_PINMUX(2,  GPIO_FUN_ANALOG)
#define ADC_CH3_GPIO3                                                                           \
	QC7XX_PINMUX(3,  GPIO_FUN_ANALOG)
#define ADC_CH0_GPIO20                                                                          \
	QC7XX_PINMUX(20, GPIO_FUN_ANALOG)
#define ADC_CH10_GPIO27                                                                         \
	QC7XX_PINMUX(27, GPIO_FUN_ANALOG)
#define AUADC_CH4_GPIO27                                                                        \
	QC7XX_PINMUX(27, GPIO_FUN_ANALOG)
#define AUADC_CH7_GPIO30                                                                        \
	QC7XX_PINMUX(30, GPIO_FUN_ANALOG)

#define GPIO0_SPI_SS          QC7XX_PINMUX(0, SPI, none)
#define GPIO0_I2S_BCLK        QC7XX_PINMUX(0, I2S, none)
#define GPIO0_I2C0_SCL        QC7XX_PINMUX(0, I2C0, none)
#define GPIO0_I2C1_SCL        QC7XX_PINMUX(0, I2C1, none)
#define GPIO0_UART0_RTS       QC7XX_PINMUX(0, UART, UART0_RTS)
#define GPIO0_UART0_CTS       QC7XX_PINMUX(0, UART, UART0_CTS)
#define GPIO0_UART0_TXD       QC7XX_PINMUX(0, UART, UART0_TXD)
#define GPIO0_UART0_RXD       QC7XX_PINMUX(0, UART, UART0_RXD)
#define GPIO0_UART1_RTS       QC7XX_PINMUX(0, UART, UART1_RTS)
#define GPIO0_UART1_CTS       QC7XX_PINMUX(0, UART, UART1_CTS)
#define GPIO0_UART1_TXD       QC7XX_PINMUX(0, UART, UART1_TXD)
#define GPIO0_UART1_RXD       QC7XX_PINMUX(0, UART, UART1_RXD)
#define GPIO0_ISO11898_TXD    QC7XX_PINMUX(0, UART, ISO11898_TXD)
#define GPIO0_ISO11898_RXD    QC7XX_PINMUX(0, UART, ISO11898_RXD)
#define GPIO0_CAM1_VSYNC      QC7XX_PINMUX(0, CAM, none)
#define GPIO0_ADC_CH9         QC7XX_PINMUX(0, ANALOG, none)
#define GPIO0_SWGPIO0         QC7XX_PINMUX(0, GPIO, none)
#define GPIO0_PWM0_CH0P       QC7XX_PINMUX(0, PWM0, PWM_SEL0)
#define GPIO0_M0_JTAG_TMS     QC7XX_PINMUX(0, JTAG, none)

#define GPIO1_SPI_SCLK        QC7XX_PINMUX(1, SPI, none)
#define GPIO1_I2S_FS          QC7XX_PINMUX(1, I2S, none)
#define GPIO1_I2C0_SDA        QC7XX_PINMUX(1, I2C0, none)
#define GPIO1_I2C1_SDA        QC7XX_PINMUX(1, I2C1, none)
#define GPIO1_UART0_RTS       QC7XX_PINMUX(1, UART, UART0_RTS)
#define GPIO1_UART0_CTS       QC7XX_PINMUX(1, UART, UART0_CTS)
#define GPIO1_UART0_TXD       QC7XX_PINMUX(1, UART, UART0_TXD)
#define GPIO1_UART0_RXD       QC7XX_PINMUX(1, UART, UART0_RXD)
#define GPIO1_UART1_RTS       QC7XX_PINMUX(1, UART, UART1_RTS)
#define GPIO1_UART1_CTS       QC7XX_PINMUX(1, UART, UART1_CTS)
#define GPIO1_UART1_TXD       QC7XX_PINMUX(1, UART, UART1_TXD)
#define GPIO1_UART1_RXD       QC7XX_PINMUX(1, UART, UART1_RXD)
#define GPIO1_ISO11898_TXD    QC7XX_PINMUX(1, UART, ISO11898_TXD)
#define GPIO1_ISO11898_RXD    QC7XX_PINMUX(1, UART, ISO11898_RXD)
#define GPIO1_CAM1_HSYNC      QC7XX_PINMUX(1, CAM, none)
#define GPIO1_ADC_CH8         QC7XX_PINMUX(1, ANALOG, none)
#define GPIO1_SWGPIO1         QC7XX_PINMUX(1, GPIO, none)
#define GPIO1_PWM0_CH1P       QC7XX_PINMUX(1, PWM0, PWM_SEL0)
#define GPIO1_PWM0_CH0N       QC7XX_PINMUX(1, PWM0, PWM_SEL1)
#define GPIO1_M0_JTAG_TCK     QC7XX_PINMUX(1, JTAG, none)

#define GPIO2_SPI_MISO        QC7XX_PINMUX(2, SPI, none)
#define GPIO2_I2S_DI          QC7XX_PINMUX(2, I2S, none)
#define GPIO2_I2S_RCLK_O      QC7XX_PINMUX(2, I2S, none)
#define GPIO2_I2C0_SCL        QC7XX_PINMUX(2, I2C0, none)
#define GPIO2_I2C1_SCL        QC7XX_PINMUX(2, I2C1, none)
#define GPIO2_UART0_RTS       QC7XX_PINMUX(2, UART, UART0_RTS)
#define GPIO2_UART0_CTS       QC7XX_PINMUX(2, UART, UART0_CTS)
#define GPIO2_UART0_TXD       QC7XX_PINMUX(2, UART, UART0_TXD)
#define GPIO2_UART0_RXD       QC7XX_PINMUX(2, UART, UART0_RXD)
#define GPIO2_UART1_RTS       QC7XX_PINMUX(2, UART, UART1_RTS)
#define GPIO2_UART1_CTS       QC7XX_PINMUX(2, UART, UART1_CTS)
#define GPIO2_UART1_TXD       QC7XX_PINMUX(2, UART, UART1_TXD)
#define GPIO2_UART1_RXD       QC7XX_PINMUX(2, UART, UART1_RXD)
#define GPIO2_ISO11898_TXD    QC7XX_PINMUX(2, UART, ISO11898_TXD)
#define GPIO2_ISO11898_RXD    QC7XX_PINMUX(2, UART, ISO11898_RXD)
#define GPIO2_ADC_CH2         QC7XX_PINMUX(2, ANALOG, none)
#define GPIO2_SWGPIO2         QC7XX_PINMUX(2, GPIO, none)
#define GPIO2_PWM0_CH2P       QC7XX_PINMUX(2, PWM0, PWM_SEL0)
#define GPIO2_PWM0_CH1P       QC7XX_PINMUX(2, PWM0, PWM_SEL1)
#define GPIO2_M0_JTAG_TDO     QC7XX_PINMUX(2, JTAG, none)

#define GPIO3_SPI_MOSI        QC7XX_PINMUX(3, SPI, none)
#define GPIO3_I2S_DO          QC7XX_PINMUX(3, I2S, none)
#define GPIO3_I2S_RCLK_O      QC7XX_PINMUX(3, I2S, none)
#define GPIO3_I2C0_SDA        QC7XX_PINMUX(3, I2C0, none)
#define GPIO3_I2C1_SDA        QC7XX_PINMUX(3, I2C1, none)
#define GPIO3_UART0_RTS       QC7XX_PINMUX(3, UART, UART0_RTS)
#define GPIO3_UART0_CTS       QC7XX_PINMUX(3, UART, UART0_CTS)
#define GPIO3_UART0_TXD       QC7XX_PINMUX(3, UART, UART0_TXD)
#define GPIO3_UART0_RXD       QC7XX_PINMUX(3, UART, UART0_RXD)
#define GPIO3_UART1_RTS       QC7XX_PINMUX(3, UART, UART1_RTS)
#define GPIO3_UART1_CTS       QC7XX_PINMUX(3, UART, UART1_CTS)
#define GPIO3_UART1_TXD       QC7XX_PINMUX(3, UART, UART1_TXD)
#define GPIO3_UART1_RXD       QC7XX_PINMUX(3, UART, UART1_RXD)
#define GPIO3_ISO11898_TXD    QC7XX_PINMUX(3, UART, ISO11898_TXD)
#define GPIO3_ISO11898_RXD    QC7XX_PINMUX(3, UART, ISO11898_RXD)
#define GPIO3_CAM1_DAT0       QC7XX_PINMUX(3, CAM, none)
#define GPIO3_ADC_CH3         QC7XX_PINMUX(3, ANALOG, none)
#define GPIO3_SWGPIO3         QC7XX_PINMUX(3, GPIO, none)
#define GPIO3_PWM0_CH3P       QC7XX_PINMUX(3, PWM0, PWM_SEL0)
#define GPIO3_PWM0_CH1N       QC7XX_PINMUX(3, PWM0, PWM_SEL1)
#define GPIO3_M0_JTAG_TDI     QC7XX_PINMUX(3, JTAG, none)

#define GPIO4_SPI_SS          QC7XX_PINMUX(4, SPI, none)
#define GPIO4_SF2_CS          QC7XX_PINMUX(4, FLASH, none)
#define GPIO4_I2S_BCLK        QC7XX_PINMUX(4, I2S, none)
#define GPIO4_I2C0_SCL        QC7XX_PINMUX(4, I2C0, none)
#define GPIO4_I2C1_SCL        QC7XX_PINMUX(4, I2C1, none)
#define GPIO4_UART0_RTS       QC7XX_PINMUX(4, UART, UART0_RTS)
#define GPIO4_UART0_CTS       QC7XX_PINMUX(4, UART, UART0_CTS)
#define GPIO4_UART0_TXD       QC7XX_PINMUX(4, UART, UART0_TXD)
#define GPIO4_UART0_RXD       QC7XX_PINMUX(4, UART, UART0_RXD)
#define GPIO4_UART1_RTS       QC7XX_PINMUX(4, UART, UART1_RTS)
#define GPIO4_UART1_CTS       QC7XX_PINMUX(4, UART, UART1_CTS)
#define GPIO4_UART1_TXD       QC7XX_PINMUX(4, UART, UART1_TXD)
#define GPIO4_UART1_RXD       QC7XX_PINMUX(4, UART, UART1_RXD)
#define GPIO4_ISO11898_TXD    QC7XX_PINMUX(4, UART, ISO11898_TXD)
#define GPIO4_ISO11898_RXD    QC7XX_PINMUX(4, UART, ISO11898_RXD)
#define GPIO4_SWGPIO4         QC7XX_PINMUX(4, GPIO, none)
#define GPIO4_PWM0_CH0P       QC7XX_PINMUX(4, PWM0, PWM_SEL0)
#define GPIO4_PWM0_CH2P       QC7XX_PINMUX(4, PWM0, PWM_SEL1)
#define GPIO4_DBI_TypeB_WRn   QC7XX_PINMUX(4, DBI_B, none)
#define GPIO4_DBI_TypeC_SCL   QC7XX_PINMUX(4, DBI_C, none)
#define GPIO4_DISP_QSPI_SCL   QC7XX_PINMUX(4, QSPI, none)
#define GPIO4_M0_JTAG_TMS     QC7XX_PINMUX(4, JTAG, none)

#define GPIO5_SPI_SCLK        QC7XX_PINMUX(5, SPI, none)
#define GPIO5_SF2_D1          QC7XX_PINMUX(5, FLASH, none)
#define GPIO5_I2S_FS          QC7XX_PINMUX(5, I2S, none)
#define GPIO5_I2C0_SDA        QC7XX_PINMUX(5, I2C0, none)
#define GPIO5_I2C1_SDA        QC7XX_PINMUX(5, I2C1, none)
#define GPIO5_UART0_RTS       QC7XX_PINMUX(5, UART, UART0_RTS)
#define GPIO5_UART0_CTS       QC7XX_PINMUX(5, UART, UART0_CTS)
#define GPIO5_UART0_TXD       QC7XX_PINMUX(5, UART, UART0_TXD)
#define GPIO5_UART0_RXD       QC7XX_PINMUX(5, UART, UART0_RXD)
#define GPIO5_UART1_RTS       QC7XX_PINMUX(5, UART, UART1_RTS)
#define GPIO5_UART1_CTS       QC7XX_PINMUX(5, UART, UART1_CTS)
#define GPIO5_UART1_TXD       QC7XX_PINMUX(5, UART, UART1_TXD)
#define GPIO5_UART1_RXD       QC7XX_PINMUX(5, UART, UART1_RXD)
#define GPIO5_ISO11898_TXD    QC7XX_PINMUX(5, UART, ISO11898_TXD)
#define GPIO5_ISO11898_RXD    QC7XX_PINMUX(5, UART, ISO11898_RXD)
#define GPIO5_SWGPIO5         QC7XX_PINMUX(5, GPIO, none)
#define GPIO5_PWM0_CH1P       QC7XX_PINMUX(5, PWM0, PWM_SEL0)
#define GPIO5_PWM0_CH2N       QC7XX_PINMUX(5, PWM0, PWM_SEL1)
#define GPIO5_DBI_TypeB_CSn   QC7XX_PINMUX(5, DBI_B, none)
#define GPIO5_DBI_TypeC_CSn   QC7XX_PINMUX(5, DBI_C, none)
#define GPIO5_DISP_QSPI_CSn   QC7XX_PINMUX(5, QSPI, none)
#define GPIO5_M0_JTAG_TCK     QC7XX_PINMUX(5, JTAG, none)

#define GPIO6_SPI_MISO        QC7XX_PINMUX(6, SPI, none)
#define GPIO6_SF2_D2          QC7XX_PINMUX(6, FLASH, none)
#define GPIO6_I2S_DI          QC7XX_PINMUX(6, I2S, none)
#define GPIO6_I2S_RCLK_O      QC7XX_PINMUX(6, I2S, none)
#define GPIO6_I2C0_SCL        QC7XX_PINMUX(6, I2C0, none)
#define GPIO6_I2C1_SCL        QC7XX_PINMUX(6, I2C1, none)
#define GPIO6_UART0_RTS       QC7XX_PINMUX(6, UART, UART0_RTS)
#define GPIO6_UART0_CTS       QC7XX_PINMUX(6, UART, UART0_CTS)
#define GPIO6_UART0_TXD       QC7XX_PINMUX(6, UART, UART0_TXD)
#define GPIO6_UART0_RXD       QC7XX_PINMUX(6, UART, UART0_RXD)
#define GPIO6_UART1_RTS       QC7XX_PINMUX(6, UART, UART1_RTS)
#define GPIO6_UART1_CTS       QC7XX_PINMUX(6, UART, UART1_CTS)
#define GPIO6_UART1_TXD       QC7XX_PINMUX(6, UART, UART1_TXD)
#define GPIO6_UART1_RXD       QC7XX_PINMUX(6, UART, UART1_RXD)
#define GPIO6_ISO11898_TXD    QC7XX_PINMUX(6, UART, ISO11898_TXD)
#define GPIO6_ISO11898_RXD    QC7XX_PINMUX(6, UART, ISO11898_RXD)
#define GPIO6_SWGPIO6         QC7XX_PINMUX(6, GPIO, none)
#define GPIO6_PWM0_CH2P       QC7XX_PINMUX(6, PWM0, PWM_SEL0)
#define GPIO6_PWM0_CH3P       QC7XX_PINMUX(6, PWM0, PWM_SEL1)
#define GPIO6_DBI_TypeB_RDn   QC7XX_PINMUX(6, DBI_B, none)
#define GPIO6_DBI_TypeC_SDA0  QC7XX_PINMUX(6, DBI_C, none)
#define GPIO6_DISP_QSPI_SDA0  QC7XX_PINMUX(6, QSPI, none)
#define GPIO6_M0_JTAG_TDO     QC7XX_PINMUX(6, JTAG, none)

#define GPIO7_SPI_MOSI        QC7XX_PINMUX(7, SPI, none)
#define GPIO7_SF2_D0          QC7XX_PINMUX(7, FLASH, none)
#define GPIO7_I2S_DO          QC7XX_PINMUX(7, I2S, none)
#define GPIO7_I2S_RCLK_O      QC7XX_PINMUX(7, I2S, none)
#define GPIO7_I2C0_SDA        QC7XX_PINMUX(7, I2C0, none)
#define GPIO7_I2C1_SDA        QC7XX_PINMUX(7, I2C1, none)
#define GPIO7_UART0_RTS       QC7XX_PINMUX(7, UART, UART0_RTS)
#define GPIO7_UART0_CTS       QC7XX_PINMUX(7, UART, UART0_CTS)
#define GPIO7_UART0_TXD       QC7XX_PINMUX(7, UART, UART0_TXD)
#define GPIO7_UART0_RXD       QC7XX_PINMUX(7, UART, UART0_RXD)
#define GPIO7_UART1_RTS       QC7XX_PINMUX(7, UART, UART1_RTS)
#define GPIO7_UART1_CTS       QC7XX_PINMUX(7, UART, UART1_CTS)
#define GPIO7_UART1_TXD       QC7XX_PINMUX(7, UART, UART1_TXD)
#define GPIO7_UART1_RXD       QC7XX_PINMUX(7, UART, UART1_RXD)
#define GPIO7_ISO11898_TXD    QC7XX_PINMUX(7, UART, ISO11898_TXD)
#define GPIO7_ISO11898_RXD    QC7XX_PINMUX(7, UART, ISO11898_RXD)
#define GPIO7_SWGPIO7         QC7XX_PINMUX(7, GPIO, none)
#define GPIO7_PWM0_CH3P       QC7XX_PINMUX(7, PWM0, PWM_SEL0)
#define GPIO7_PWM0_CH3N       QC7XX_PINMUX(7, PWM0, PWM_SEL1)
#define GPIO7_DBI_TypeB_DCn   QC7XX_PINMUX(7, DBI_B, none)
#define GPIO7_DBI_TypeC_DCn   QC7XX_PINMUX(7, DBI_C, none)
#define GPIO7_DISP_QSPI_SDA1  QC7XX_PINMUX(7, QSPI, none)
#define GPIO7_M0_JTAG_TDI     QC7XX_PINMUX(7, JTAG, none)

#define GPIO8_SPI_SS          QC7XX_PINMUX(8, SPI, none)
#define GPIO8_SF2_CLK         QC7XX_PINMUX(8, FLASH, none)
#define GPIO8_I2S_BCLK        QC7XX_PINMUX(8, I2S, none)
#define GPIO8_I2C0_SCL        QC7XX_PINMUX(8, I2C0, none)
#define GPIO8_I2C1_SCL        QC7XX_PINMUX(8, I2C1, none)
#define GPIO8_UART0_RTS       QC7XX_PINMUX(8, UART, UART0_RTS)
#define GPIO8_UART0_CTS       QC7XX_PINMUX(8, UART, UART0_CTS)
#define GPIO8_UART0_TXD       QC7XX_PINMUX(8, UART, UART0_TXD)
#define GPIO8_UART0_RXD       QC7XX_PINMUX(8, UART, UART0_RXD)
#define GPIO8_UART1_RTS       QC7XX_PINMUX(8, UART, UART1_RTS)
#define GPIO8_UART1_CTS       QC7XX_PINMUX(8, UART, UART1_CTS)
#define GPIO8_UART1_TXD       QC7XX_PINMUX(8, UART, UART1_TXD)
#define GPIO8_UART1_RXD       QC7XX_PINMUX(8, UART, UART1_RXD)
#define GPIO8_ISO11898_TXD    QC7XX_PINMUX(8, UART, ISO11898_TXD)
#define GPIO8_ISO11898_RXD    QC7XX_PINMUX(8, UART, ISO11898_RXD)
#define GPIO8_SWGPIO8         QC7XX_PINMUX(8, GPIO, none)
#define GPIO8_PWM0_CH0P       QC7XX_PINMUX(8, PWM0, PWM_SEL0)
#define GPIO8_DBI_TypeB_DB0   QC7XX_PINMUX(8, DBI_B, none)
#define GPIO8_DBI_TypeC_SCL   QC7XX_PINMUX(8, DBI_C, none)
#define GPIO8_DISP_QSPI_SDA2  QC7XX_PINMUX(8, QSPI, none)
#define GPIO8_M0_JTAG_TMS     QC7XX_PINMUX(8, JTAG, none)

#define GPIO9_SPI_SCLK        QC7XX_PINMUX(9, SPI, none)
#define GPIO9_SF2_D3          QC7XX_PINMUX(9, FLASH, none)
#define GPIO9_I2S_FS          QC7XX_PINMUX(9, I2S, none)
#define GPIO9_I2C0_SDA        QC7XX_PINMUX(9, I2C0, none)
#define GPIO9_I2C1_SDA        QC7XX_PINMUX(9, I2C1, none)
#define GPIO9_UART0_RTS       QC7XX_PINMUX(9, UART, UART0_RTS)
#define GPIO9_UART0_CTS       QC7XX_PINMUX(9, UART, UART0_CTS)
#define GPIO9_UART0_TXD       QC7XX_PINMUX(9, UART, UART0_TXD)
#define GPIO9_UART0_RXD       QC7XX_PINMUX(9, UART, UART0_RXD)
#define GPIO9_UART1_RTS       QC7XX_PINMUX(9, UART, UART1_RTS)
#define GPIO9_UART1_CTS       QC7XX_PINMUX(9, UART, UART1_CTS)
#define GPIO9_UART1_TXD       QC7XX_PINMUX(9, UART, UART1_TXD)
#define GPIO9_UART1_RXD       QC7XX_PINMUX(9, UART, UART1_RXD)
#define GPIO9_ISO11898_TXD    QC7XX_PINMUX(9, UART, ISO11898_TXD)
#define GPIO9_ISO11898_RXD    QC7XX_PINMUX(9, UART, ISO11898_RXD)
#define GPIO9_SWGPIO9         QC7XX_PINMUX(9, GPIO, none)
#define GPIO9_PWM0_CH1P       QC7XX_PINMUX(9, PWM0, PWM_SEL0)
#define GPIO9_PWM0_CH0N       QC7XX_PINMUX(9, PWM0, PWM_SEL1)
#define GPIO9_DBI_TypeB_DB1   QC7XX_PINMUX(9, DBI_B, none)
#define GPIO9_DBI_TypeC_CSn   QC7XX_PINMUX(9, DBI_C, none)
#define GPIO9_DISP_QSPI_SDA3  QC7XX_PINMUX(9, QSPI, none)
#define GPIO9_M0_JTAG_TCK     QC7XX_PINMUX(9, JTAG, none)

#define GPIO10_SDH_DAT1       QC7XX_PINMUX(10, SDH, none)
#define GPIO10_SPI_MISO       QC7XX_PINMUX(10, SPI, none)
#define GPIO10_SF3_D3         QC7XX_PINMUX(10, FLASH, none)
#define GPIO10_I2S_DI         QC7XX_PINMUX(10, I2S, none)
#define GPIO10_I2S_RCLK_O     QC7XX_PINMUX(10, I2S, none)
#define GPIO10_I2C0_SCL       QC7XX_PINMUX(10, I2C0, none)
#define GPIO10_I2C1_SCL       QC7XX_PINMUX(10, I2C1, none)
#define GPIO10_UART0_RTS      QC7XX_PINMUX(10, UART, UART0_RTS)
#define GPIO10_UART0_CTS      QC7XX_PINMUX(10, UART, UART0_CTS)
#define GPIO10_UART0_TXD      QC7XX_PINMUX(10, UART, UART0_TXD)
#define GPIO10_UART0_RXD      QC7XX_PINMUX(10, UART, UART0_RXD)
#define GPIO10_UART1_RTS      QC7XX_PINMUX(10, UART, UART1_RTS)
#define GPIO10_UART1_CTS      QC7XX_PINMUX(10, UART, UART1_CTS)
#define GPIO10_UART1_TXD      QC7XX_PINMUX(10, UART, UART1_TXD)
#define GPIO10_UART1_RXD      QC7XX_PINMUX(10, UART, UART1_RXD)
#define GPIO10_ISO11898_TXD   QC7XX_PINMUX(10, UART, ISO11898_TXD)
#define GPIO10_ISO11898_RXD   QC7XX_PINMUX(10, UART, ISO11898_RXD)
#define GPIO10_CAM1_DAT1      QC7XX_PINMUX(10, CAM, none)
#define GPIO10_ADC_CH7        QC7XX_PINMUX(10, ANALOG, none)
#define GPIO10_SWGPIO10       QC7XX_PINMUX(10, GPIO, none)
#define GPIO10_SDIO_DAT2      QC7XX_PINMUX(10, SDIO, none)
#define GPIO10_PWM0_CH2P      QC7XX_PINMUX(10, PWM0, PWM_SEL0)
#define GPIO10_PWM0_CH1P      QC7XX_PINMUX(10, PWM0, PWM_SEL1)
#define GPIO10_DBI_TypeB_DB2  QC7XX_PINMUX(10, DBI_B, none)
#define GPIO10_DBI_TypeC_SDA0 QC7XX_PINMUX(10, DBI_C, none)
#define GPIO10_DISP_QSPI_SCL  QC7XX_PINMUX(10, QSPI, none)
#define GPIO10_M0_JTAG_TDO    QC7XX_PINMUX(10, JTAG, none)

#define GPIO11_SDH_DAT0       QC7XX_PINMUX(11, SDH, none)
#define GPIO11_SPI_MOSI       QC7XX_PINMUX(11, SPI, none)
#define GPIO11_SF3_CLK        QC7XX_PINMUX(11, FLASH, none)
#define GPIO11_I2S_DO         QC7XX_PINMUX(11, I2S, none)
#define GPIO11_I2S_RCLK_O     QC7XX_PINMUX(11, I2S, none)
#define GPIO11_I2C0_SDA       QC7XX_PINMUX(11, I2C0, none)
#define GPIO11_I2C1_SDA       QC7XX_PINMUX(11, I2C1, none)
#define GPIO11_UART0_RTS      QC7XX_PINMUX(11, UART, UART0_RTS)
#define GPIO11_UART0_CTS      QC7XX_PINMUX(11, UART, UART0_CTS)
#define GPIO11_UART0_TXD      QC7XX_PINMUX(11, UART, UART0_TXD)
#define GPIO11_UART0_RXD      QC7XX_PINMUX(11, UART, UART0_RXD)
#define GPIO11_UART1_RTS      QC7XX_PINMUX(11, UART, UART1_RTS)
#define GPIO11_UART1_CTS      QC7XX_PINMUX(11, UART, UART1_CTS)
#define GPIO11_UART1_TXD      QC7XX_PINMUX(11, UART, UART1_TXD)
#define GPIO11_UART1_RXD      QC7XX_PINMUX(11, UART, UART1_RXD)
#define GPIO11_ISO11898_TXD   QC7XX_PINMUX(11, UART, ISO11898_TXD)
#define GPIO11_ISO11898_RXD   QC7XX_PINMUX(11, UART, ISO11898_RXD)
#define GPIO11_CAM1_DAT2      QC7XX_PINMUX(11, CAM, none)
#define GPIO11_SWGPIO11       QC7XX_PINMUX(11, GPIO, none)
#define GPIO11_SDIO_DAT3      QC7XX_PINMUX(11, SDIO, none)
#define GPIO11_PWM0_CH3P      QC7XX_PINMUX(11, PWM0, PWM_SEL0)
#define GPIO11_PWM0_CH1N      QC7XX_PINMUX(11, PWM0, PWM_SEL1)
#define GPIO11_DBI_TypeB_DB3  QC7XX_PINMUX(11, DBI_B, none)
#define GPIO11_DBI_TypeC_DCn  QC7XX_PINMUX(11, DBI_C, none)
#define GPIO11_DISP_QSPI_CSn  QC7XX_PINMUX(11, QSPI, none)
#define GPIO11_M0_JTAG_TDI    QC7XX_PINMUX(11, JTAG, none)

#define GPIO12_SDH_CLK        QC7XX_PINMUX(12, SDH, none)
#define GPIO12_SPI_SS         QC7XX_PINMUX(12, SPI, none)
#define GPIO12_SF3_D0         QC7XX_PINMUX(12, FLASH, none)
#define GPIO12_I2S_BCLK       QC7XX_PINMUX(12, I2S, none)
#define GPIO12_I2C0_SCL       QC7XX_PINMUX(12, I2C0, none)
#define GPIO12_I2C1_SCL       QC7XX_PINMUX(12, I2C1, none)
#define GPIO12_UART0_RTS      QC7XX_PINMUX(12, UART, UART0_RTS)
#define GPIO12_UART0_CTS      QC7XX_PINMUX(12, UART, UART0_CTS)
#define GPIO12_UART0_TXD      QC7XX_PINMUX(12, UART, UART0_TXD)
#define GPIO12_UART0_RXD      QC7XX_PINMUX(12, UART, UART0_RXD)
#define GPIO12_UART1_RTS      QC7XX_PINMUX(12, UART, UART1_RTS)
#define GPIO12_UART1_CTS      QC7XX_PINMUX(12, UART, UART1_CTS)
#define GPIO12_UART1_TXD      QC7XX_PINMUX(12, UART, UART1_TXD)
#define GPIO12_UART1_RXD      QC7XX_PINMUX(12, UART, UART1_RXD)
#define GPIO12_ISO11898_TXD   QC7XX_PINMUX(12, UART, ISO11898_TXD)
#define GPIO12_ISO11898_RXD   QC7XX_PINMUX(12, UART, ISO11898_RXD)
#define GPIO12_CAM1_DAT3      QC7XX_PINMUX(12, CAM, none)
#define GPIO12_ADC_CH6        QC7XX_PINMUX(12, ANALOG, none)
#define GPIO12_SWGPIO12       QC7XX_PINMUX(12, GPIO, none)
#define GPIO12_SDIO_CMD       QC7XX_PINMUX(12, SDIO, none)
#define GPIO12_PWM0_CH0P      QC7XX_PINMUX(12, PWM0, PWM_SEL0)
#define GPIO12_PWM0_CH2P      QC7XX_PINMUX(12, PWM0, PWM_SEL1)
#define GPIO12_DBI_TypeB_DB4  QC7XX_PINMUX(12, DBI_B, none)
#define GPIO12_DBI_TypeC_SCL  QC7XX_PINMUX(12, DBI_C, none)
#define GPIO12_DISP_QSPI_SDA0 QC7XX_PINMUX(12, QSPI, none)
#define GPIO12_M0_JTAG_TMS    QC7XX_PINMUX(12, JTAG, none)

#define GPIO13_SDH_CMD        QC7XX_PINMUX(13, SDH, none)
#define GPIO13_SPI_SCLK       QC7XX_PINMUX(13, SPI, none)
#define GPIO13_SF3_D2         QC7XX_PINMUX(13, FLASH, none)
#define GPIO13_I2S_FS         QC7XX_PINMUX(13, I2S, none)
#define GPIO13_I2C0_SDA       QC7XX_PINMUX(13, I2C0, none)
#define GPIO13_I2C1_SDA       QC7XX_PINMUX(13, I2C1, none)
#define GPIO13_UART0_RTS      QC7XX_PINMUX(13, UART, UART0_RTS)
#define GPIO13_UART0_CTS      QC7XX_PINMUX(13, UART, UART0_CTS)
#define GPIO13_UART0_TXD      QC7XX_PINMUX(13, UART, UART0_TXD)
#define GPIO13_UART0_RXD      QC7XX_PINMUX(13, UART, UART0_RXD)
#define GPIO13_UART1_RTS      QC7XX_PINMUX(13, UART, UART1_RTS)
#define GPIO13_UART1_CTS      QC7XX_PINMUX(13, UART, UART1_CTS)
#define GPIO13_UART1_TXD      QC7XX_PINMUX(13, UART, UART1_TXD)
#define GPIO13_UART1_RXD      QC7XX_PINMUX(13, UART, UART1_RXD)
#define GPIO13_ISO11898_TXD   QC7XX_PINMUX(13, UART, ISO11898_TXD)
#define GPIO13_ISO11898_RXD   QC7XX_PINMUX(13, UART, ISO11898_RXD)
#define GPIO13_CAM1_CLK       QC7XX_PINMUX(13, CAM, none)
#define GPIO13_ADC_CH5        QC7XX_PINMUX(13, ANALOG, none)
#define GPIO13_SWGPIO13       QC7XX_PINMUX(13, GPIO, none)
#define GPIO13_SDIO_CLK       QC7XX_PINMUX(13, SDIO, none)
#define GPIO13_PWM0_CH1P      QC7XX_PINMUX(13, PWM0, PWM_SEL0)
#define GPIO13_PWM0_CH2N      QC7XX_PINMUX(13, PWM0, PWM_SEL1)
#define GPIO13_DBI_TypeB_DB5  QC7XX_PINMUX(13, DBI_B, none)
#define GPIO13_DBI_TypeC_CSn  QC7XX_PINMUX(13, DBI_C, none)
#define GPIO13_DISP_QSPI_SDA1 QC7XX_PINMUX(13, QSPI, none)
#define GPIO13_M0_JTAG_TCK    QC7XX_PINMUX(13, JTAG, none)

#define GPIO14_SDH_DAT3       QC7XX_PINMUX(14, SDH, none)
#define GPIO14_SPI_MISO       QC7XX_PINMUX(14, SPI, none)
#define GPIO14_SF3_D1         QC7XX_PINMUX(14, FLASH, none)
#define GPIO14_I2S_DI         QC7XX_PINMUX(14, I2S, none)
#define GPIO14_I2S_RCLK_O     QC7XX_PINMUX(14, I2S, none)
#define GPIO14_I2C0_SCL       QC7XX_PINMUX(14, I2C0, none)
#define GPIO14_I2C1_SCL       QC7XX_PINMUX(14, I2C1, none)
#define GPIO14_UART0_RTS      QC7XX_PINMUX(14, UART, UART0_RTS)
#define GPIO14_UART0_CTS      QC7XX_PINMUX(14, UART, UART0_CTS)
#define GPIO14_UART0_TXD      QC7XX_PINMUX(14, UART, UART0_TXD)
#define GPIO14_UART0_RXD      QC7XX_PINMUX(14, UART, UART0_RXD)
#define GPIO14_UART1_RTS      QC7XX_PINMUX(14, UART, UART1_RTS)
#define GPIO14_UART1_CTS      QC7XX_PINMUX(14, UART, UART1_CTS)
#define GPIO14_UART1_TXD      QC7XX_PINMUX(14, UART, UART1_TXD)
#define GPIO14_UART1_RXD      QC7XX_PINMUX(14, UART, UART1_RXD)
#define GPIO14_ISO11898_TXD   QC7XX_PINMUX(14, UART, ISO11898_TXD)
#define GPIO14_ISO11898_RXD   QC7XX_PINMUX(14, UART, ISO11898_RXD)
#define GPIO14_CAM1_DAT4      QC7XX_PINMUX(14, CAM, none)
#define GPIO14_ADC_CH4        QC7XX_PINMUX(14, ANALOG, none)
#define GPIO14_SWGPIO14       QC7XX_PINMUX(14, GPIO, none)
#define GPIO14_SDIO_DAT0      QC7XX_PINMUX(14, SDIO, none)
#define GPIO14_PWM0_CH2P      QC7XX_PINMUX(14, PWM0, PWM_SEL0)
#define GPIO14_PWM0_CH3P      QC7XX_PINMUX(14, PWM0, PWM_SEL1)
#define GPIO14_DBI_TypeB_DB6  QC7XX_PINMUX(14, DBI_B, none)
#define GPIO14_DBI_TypeC_SDA0 QC7XX_PINMUX(14, DBI_C, none)
#define GPIO14_DISP_QSPI_SDA2 QC7XX_PINMUX(14, QSPI, none)
#define GPIO14_AUDAC_PWM_P    QC7XX_PINMUX(14, AUDAC, none)
#define GPIO14_M0_JTAG_TDO    QC7XX_PINMUX(14, JTAG, none)

#define GPIO15_SDH_DAT2       QC7XX_PINMUX(15, SDH, none)
#define GPIO15_SPI_MOSI       QC7XX_PINMUX(15, SPI, none)
#define GPIO15_SF3_CS         QC7XX_PINMUX(15, FLASH, none)
#define GPIO15_I2S_DO         QC7XX_PINMUX(15, I2S, none)
#define GPIO15_I2S_RCLK_O     QC7XX_PINMUX(15, I2S, none)
#define GPIO15_I2C0_SDA       QC7XX_PINMUX(15, I2C0, none)
#define GPIO15_I2C1_SDA       QC7XX_PINMUX(15, I2C1, none)
#define GPIO15_UART0_RTS      QC7XX_PINMUX(15, UART, UART0_RTS)
#define GPIO15_UART0_CTS      QC7XX_PINMUX(15, UART, UART0_CTS)
#define GPIO15_UART0_TXD      QC7XX_PINMUX(15, UART, UART0_TXD)
#define GPIO15_UART0_RXD      QC7XX_PINMUX(15, UART, UART0_RXD)
#define GPIO15_UART1_RTS      QC7XX_PINMUX(15, UART, UART1_RTS)
#define GPIO15_UART1_CTS      QC7XX_PINMUX(15, UART, UART1_CTS)
#define GPIO15_UART1_TXD      QC7XX_PINMUX(15, UART, UART1_TXD)
#define GPIO15_UART1_RXD      QC7XX_PINMUX(15, UART, UART1_RXD)
#define GPIO15_ISO11898_TXD   QC7XX_PINMUX(15, UART, ISO11898_TXD)
#define GPIO15_ISO11898_RXD   QC7XX_PINMUX(15, UART, ISO11898_RXD)
#define GPIO15_CAM1_DAT5      QC7XX_PINMUX(15, CAM, none)
#define GPIO15_SWGPIO15       QC7XX_PINMUX(15, GPIO, none)
#define GPIO15_SDIO_DAT1      QC7XX_PINMUX(15, SDIO, none)
#define GPIO15_PWM0_CH3P      QC7XX_PINMUX(15, PWM0, PWM_SEL0)
#define GPIO15_PWM0_CH3N      QC7XX_PINMUX(15, PWM0, PWM_SEL1)
#define GPIO15_DBI_TypeB_DB7  QC7XX_PINMUX(15, DBI_B, none)
#define GPIO15_DBI_TypeC_DCn  QC7XX_PINMUX(15, DBI_C, none)
#define GPIO15_DISP_QSPI_SDA3 QC7XX_PINMUX(15, QSPI, none)
#define GPIO15_AUDAC_PWM_N    QC7XX_PINMUX(15, AUDAC, none)
#define GPIO15_M0_JTAG_TDI    QC7XX_PINMUX(15, JTAG, none)

#define GPIO16_SPI_SS         QC7XX_PINMUX(16, SPI, none)
#define GPIO16_I2S_BCLK       QC7XX_PINMUX(16, I2S, none)
#define GPIO16_I2C0_SCL       QC7XX_PINMUX(16, I2C0, none)
#define GPIO16_I2C1_SCL       QC7XX_PINMUX(16, I2C1, none)
#define GPIO16_UART0_RTS      QC7XX_PINMUX(16, UART, UART0_RTS)
#define GPIO16_UART0_CTS      QC7XX_PINMUX(16, UART, UART0_CTS)
#define GPIO16_UART0_TXD      QC7XX_PINMUX(16, UART, UART0_TXD)
#define GPIO16_UART0_RXD      QC7XX_PINMUX(16, UART, UART0_RXD)
#define GPIO16_UART1_RTS      QC7XX_PINMUX(16, UART, UART1_RTS)
#define GPIO16_UART1_CTS      QC7XX_PINMUX(16, UART, UART1_CTS)
#define GPIO16_UART1_TXD      QC7XX_PINMUX(16, UART, UART1_TXD)
#define GPIO16_UART1_RXD      QC7XX_PINMUX(16, UART, UART1_RXD)
#define GPIO16_ISO11898_TXD   QC7XX_PINMUX(16, UART, ISO11898_TXD)
#define GPIO16_ISO11898_RXD   QC7XX_PINMUX(16, UART, ISO11898_RXD)
#define GPIO16_CAM1_DAT6      QC7XX_PINMUX(16, CAM, none)
#define GPIO16_SWGPIO16       QC7XX_PINMUX(16, GPIO, none)
#define GPIO16_PWM0_CH0P      QC7XX_PINMUX(16, PWM0, PWM_SEL0)
#define GPIO16_M0_JTAG_TMS    QC7XX_PINMUX(16, JTAG, none)

#define GPIO17_SPI_SCLK       QC7XX_PINMUX(17, SPI, none)
#define GPIO17_I2S_FS         QC7XX_PINMUX(17, I2S, none)
#define GPIO17_I2C0_SDA       QC7XX_PINMUX(17, I2C0, none)
#define GPIO17_I2C1_SDA       QC7XX_PINMUX(17, I2C1, none)
#define GPIO17_UART0_RTS      QC7XX_PINMUX(17, UART, UART0_RTS)
#define GPIO17_UART0_CTS      QC7XX_PINMUX(17, UART, UART0_CTS)
#define GPIO17_UART0_TXD      QC7XX_PINMUX(17, UART, UART0_TXD)
#define GPIO17_UART0_RXD      QC7XX_PINMUX(17, UART, UART0_RXD)
#define GPIO17_UART1_RTS      QC7XX_PINMUX(17, UART, UART1_RTS)
#define GPIO17_UART1_CTS      QC7XX_PINMUX(17, UART, UART1_CTS)
#define GPIO17_UART1_TXD      QC7XX_PINMUX(17, UART, UART1_TXD)
#define GPIO17_UART1_RXD      QC7XX_PINMUX(17, UART, UART1_RXD)
#define GPIO17_ISO11898_TXD   QC7XX_PINMUX(17, UART, ISO11898_TXD)
#define GPIO17_ISO11898_RXD   QC7XX_PINMUX(17, UART, ISO11898_RXD)
#define GPIO17_CAM1_DAT7      QC7XX_PINMUX(17, CAM, none)
#define GPIO17_SWGPIO17       QC7XX_PINMUX(17, GPIO, none)
#define GPIO17_PWM0_CH1P      QC7XX_PINMUX(17, PWM0, PWM_SEL0)
#define GPIO17_PWM0_CH0N      QC7XX_PINMUX(17, PWM0, PWM_SEL1)
#define GPIO17_M0_JTAG_TCK    QC7XX_PINMUX(17, JTAG, none)

#define GPIO18_SPI_MISO       QC7XX_PINMUX(18, SPI, none)
#define GPIO18_I2S_DI         QC7XX_PINMUX(18, I2S, none)
#define GPIO18_I2S_RCLK_O     QC7XX_PINMUX(18, I2S, none)
#define GPIO18_I2C0_SCL       QC7XX_PINMUX(18, I2C0, none)
#define GPIO18_I2C1_SCL       QC7XX_PINMUX(18, I2C1, none)
#define GPIO18_UART0_RTS      QC7XX_PINMUX(18, UART, UART0_RTS)
#define GPIO18_UART0_CTS      QC7XX_PINMUX(18, UART, UART0_CTS)
#define GPIO18_UART0_TXD      QC7XX_PINMUX(18, UART, UART0_TXD)
#define GPIO18_UART0_RXD      QC7XX_PINMUX(18, UART, UART0_RXD)
#define GPIO18_UART1_RTS      QC7XX_PINMUX(18, UART, UART1_RTS)
#define GPIO18_UART1_CTS      QC7XX_PINMUX(18, UART, UART1_CTS)
#define GPIO18_UART1_TXD      QC7XX_PINMUX(18, UART, UART1_TXD)
#define GPIO18_UART1_RXD      QC7XX_PINMUX(18, UART, UART1_RXD)
#define GPIO18_ISO11898_TXD   QC7XX_PINMUX(18, UART, ISO11898_TXD)
#define GPIO18_ISO11898_RXD   QC7XX_PINMUX(18, UART, ISO11898_RXD)
#define GPIO18_SWGPIO18       QC7XX_PINMUX(18, GPIO, none)
#define GPIO18_PWM0_CH2P      QC7XX_PINMUX(18, PWM0, PWM_SEL0)
#define GPIO18_PWM0_CH1P      QC7XX_PINMUX(18, PWM0, PWM_SEL1)
#define GPIO18_M0_JTAG_TDO    QC7XX_PINMUX(18, JTAG, none)

#define GPIO19_SPI_MOSI       QC7XX_PINMUX(19, SPI, none)
#define GPIO19_I2S_DO         QC7XX_PINMUX(19, I2S, none)
#define GPIO19_I2S_RCLK_O     QC7XX_PINMUX(19, I2S, none)
#define GPIO19_I2C0_SDA       QC7XX_PINMUX(19, I2C0, none)
#define GPIO19_I2C1_SDA       QC7XX_PINMUX(19, I2C1, none)
#define GPIO19_UART0_RTS      QC7XX_PINMUX(19, UART, UART0_RTS)
#define GPIO19_UART0_CTS      QC7XX_PINMUX(19, UART, UART0_CTS)
#define GPIO19_UART0_TXD      QC7XX_PINMUX(19, UART, UART0_TXD)
#define GPIO19_UART0_RXD      QC7XX_PINMUX(19, UART, UART0_RXD)
#define GPIO19_UART1_RTS      QC7XX_PINMUX(19, UART, UART1_RTS)
#define GPIO19_UART1_CTS      QC7XX_PINMUX(19, UART, UART1_CTS)
#define GPIO19_UART1_TXD      QC7XX_PINMUX(19, UART, UART1_TXD)
#define GPIO19_UART1_RXD      QC7XX_PINMUX(19, UART, UART1_RXD)
#define GPIO19_ISO11898_TXD   QC7XX_PINMUX(19, UART, ISO11898_TXD)
#define GPIO19_ISO11898_RXD   QC7XX_PINMUX(19, UART, ISO11898_RXD)
#define GPIO19_ADC_CH1        QC7XX_PINMUX(19, ANALOG, none)
#define GPIO19_SWGPIO19       QC7XX_PINMUX(19, GPIO, none)
#define GPIO19_PWM0_CH3P      QC7XX_PINMUX(19, PWM0, PWM_SEL0)
#define GPIO19_PWM0_CH1N      QC7XX_PINMUX(19, PWM0, PWM_SEL1)
#define GPIO19_M0_JTAG_TDI    QC7XX_PINMUX(19, JTAG, none)

#define GPIO20_SPI_SS         QC7XX_PINMUX(20, SPI, none)
#define GPIO20_I2S_BCLK       QC7XX_PINMUX(20, I2S, none)
#define GPIO20_PDM_CLK_O      QC7XX_PINMUX(20, PDM, none)
#define GPIO20_I2C0_SCL       QC7XX_PINMUX(20, I2C0, none)
#define GPIO20_I2C1_SCL       QC7XX_PINMUX(20, I2C1, none)
#define GPIO20_UART0_RTS      QC7XX_PINMUX(20, UART, UART0_RTS)
#define GPIO20_UART0_CTS      QC7XX_PINMUX(20, UART, UART0_CTS)
#define GPIO20_UART0_TXD      QC7XX_PINMUX(20, UART, UART0_TXD)
#define GPIO20_UART0_RXD      QC7XX_PINMUX(20, UART, UART0_RXD)
#define GPIO20_UART1_RTS      QC7XX_PINMUX(20, UART, UART1_RTS)
#define GPIO20_UART1_CTS      QC7XX_PINMUX(20, UART, UART1_CTS)
#define GPIO20_UART1_TXD      QC7XX_PINMUX(20, UART, UART1_TXD)
#define GPIO20_UART1_RXD      QC7XX_PINMUX(20, UART, UART1_RXD)
#define GPIO20_ISO11898_TXD   QC7XX_PINMUX(20, UART, ISO11898_TXD)
#define GPIO20_ISO11898_RXD   QC7XX_PINMUX(20, UART, ISO11898_RXD)
#define GPIO20_ADC_CH0        QC7XX_PINMUX(20, ANALOG, none)
#define GPIO20_AUADC_CH0      QC7XX_PINMUX(20, ANALOG, none)
#define GPIO20_SWGPIO20       QC7XX_PINMUX(20, GPIO, none)
#define GPIO20_PWM0_CH0P      QC7XX_PINMUX(20, PWM0, PWM_SEL0)
#define GPIO20_PWM0_CH2P      QC7XX_PINMUX(20, PWM0, PWM_SEL1)
#define GPIO20_M0_JTAG_TMS    QC7XX_PINMUX(20, JTAG, none)

#define GPIO21_SPI_SCLK       QC7XX_PINMUX(21, SPI, none)
#define GPIO21_I2S_FS         QC7XX_PINMUX(21, I2S, none)
#define GPIO21_PDM_0_IN       QC7XX_PINMUX(21, PDM, none)
#define GPIO21_I2C0_SDA       QC7XX_PINMUX(21, I2C0, none)
#define GPIO21_I2C1_SDA       QC7XX_PINMUX(21, I2C1, none)
#define GPIO21_UART0_RTS      QC7XX_PINMUX(21, UART, UART0_RTS)
#define GPIO21_UART0_CTS      QC7XX_PINMUX(21, UART, UART0_CTS)
#define GPIO21_UART0_TXD      QC7XX_PINMUX(21, UART, UART0_TXD)
#define GPIO21_UART0_RXD      QC7XX_PINMUX(21, UART, UART0_RXD)
#define GPIO21_UART1_RTS      QC7XX_PINMUX(21, UART, UART1_RTS)
#define GPIO21_UART1_CTS      QC7XX_PINMUX(21, UART, UART1_CTS)
#define GPIO21_UART1_TXD      QC7XX_PINMUX(21, UART, UART1_TXD)
#define GPIO21_UART1_RXD      QC7XX_PINMUX(21, UART, UART1_RXD)
#define GPIO21_ISO11898_TXD   QC7XX_PINMUX(21, UART, ISO11898_TXD)
#define GPIO21_ISO11898_RXD   QC7XX_PINMUX(21, UART, ISO11898_RXD)
#define GPIO21_SWGPIO21       QC7XX_PINMUX(21, GPIO, none)
#define GPIO21_PWM0_CH1P      QC7XX_PINMUX(21, PWM0, PWM_SEL0)
#define GPIO21_PWM0_CH2N      QC7XX_PINMUX(21, PWM0, PWM_SEL1)
#define GPIO21_M0_JTAG_TCK    QC7XX_PINMUX(21, JTAG, none)

#define GPIO22_SPI_MISO       QC7XX_PINMUX(22, SPI, none)
#define GPIO22_I2S_DI         QC7XX_PINMUX(22, I2S, none)
#define GPIO22_I2S_RCLK_O     QC7XX_PINMUX(22, I2S, none)
#define GPIO22_I2C0_SCL       QC7XX_PINMUX(22, I2C0, none)
#define GPIO22_I2C1_SCL       QC7XX_PINMUX(22, I2C1, none)
#define GPIO22_UART0_RTS      QC7XX_PINMUX(22, UART, UART0_RTS)
#define GPIO22_UART0_CTS      QC7XX_PINMUX(22, UART, UART0_CTS)
#define GPIO22_UART0_TXD      QC7XX_PINMUX(22, UART, UART0_TXD)
#define GPIO22_UART0_RXD      QC7XX_PINMUX(22, UART, UART0_RXD)
#define GPIO22_UART1_RTS      QC7XX_PINMUX(22, UART, UART1_RTS)
#define GPIO22_UART1_CTS      QC7XX_PINMUX(22, UART, UART1_CTS)
#define GPIO22_UART1_TXD      QC7XX_PINMUX(22, UART, UART1_TXD)
#define GPIO22_UART1_RXD      QC7XX_PINMUX(22, UART, UART1_RXD)
#define GPIO22_ISO11898_TXD   QC7XX_PINMUX(22, UART, ISO11898_TXD)
#define GPIO22_ISO11898_RXD   QC7XX_PINMUX(22, UART, ISO11898_RXD)
#define GPIO22_SWGPIO22       QC7XX_PINMUX(22, GPIO, none)
#define GPIO22_PWM0_CH2P      QC7XX_PINMUX(22, PWM0, PWM_SEL0)
#define GPIO22_PWM0_CH3P      QC7XX_PINMUX(22, PWM0, PWM_SEL1)
#define GPIO22_AUDAC_PWM_P    QC7XX_PINMUX(22, AUDAC, none)
#define GPIO22_M0_JTAG_TDO    QC7XX_PINMUX(22, JTAG, none)

#define GPIO23_SPI_MOSI       QC7XX_PINMUX(23, SPI, none)
#define GPIO23_I2S_DO         QC7XX_PINMUX(23, I2S, none)
#define GPIO23_I2S_RCLK_O     QC7XX_PINMUX(23, I2S, none)
#define GPIO23_I2C0_SDA       QC7XX_PINMUX(23, I2C0, none)
#define GPIO23_I2C1_SDA       QC7XX_PINMUX(23, I2C1, none)
#define GPIO23_UART0_RTS      QC7XX_PINMUX(23, UART, UART0_RTS)
#define GPIO23_UART0_CTS      QC7XX_PINMUX(23, UART, UART0_CTS)
#define GPIO23_UART0_TXD      QC7XX_PINMUX(23, UART, UART0_TXD)
#define GPIO23_UART0_RXD      QC7XX_PINMUX(23, UART, UART0_RXD)
#define GPIO23_UART1_RTS      QC7XX_PINMUX(23, UART, UART1_RTS)
#define GPIO23_UART1_CTS      QC7XX_PINMUX(23, UART, UART1_CTS)
#define GPIO23_UART1_TXD      QC7XX_PINMUX(23, UART, UART1_TXD)
#define GPIO23_UART1_RXD      QC7XX_PINMUX(23, UART, UART1_RXD)
#define GPIO23_ISO11898_TXD   QC7XX_PINMUX(23, UART, ISO11898_TXD)
#define GPIO23_ISO11898_RXD   QC7XX_PINMUX(23, UART, ISO11898_RXD)
#define GPIO23_AUADC_CH3      QC7XX_PINMUX(23, ANALOG, none)
#define GPIO23_SWGPIO23       QC7XX_PINMUX(23, GPIO, none)
#define GPIO23_PWM0_CH3P      QC7XX_PINMUX(23, PWM0, PWM_SEL0)
#define GPIO23_PWM0_CH3N      QC7XX_PINMUX(23, PWM0, PWM_SEL1)
#define GPIO23_AUDAC_PWM_N    QC7XX_PINMUX(23, AUDAC, none)
#define GPIO23_M0_JTAG_TDI    QC7XX_PINMUX(23, JTAG, none)

#define GPIO24_SPI_SS         QC7XX_PINMUX(24, SPI, none)
#define GPIO24_I2S_BCLK       QC7XX_PINMUX(24, I2S, none)
#define GPIO24_I2C0_SCL       QC7XX_PINMUX(24, I2C0, none)
#define GPIO24_I2C1_SCL       QC7XX_PINMUX(24, I2C1, none)
#define GPIO24_UART0_RTS      QC7XX_PINMUX(24, UART, UART0_RTS)
#define GPIO24_UART0_CTS      QC7XX_PINMUX(24, UART, UART0_CTS)
#define GPIO24_UART0_TXD      QC7XX_PINMUX(24, UART, UART0_TXD)
#define GPIO24_UART0_RXD      QC7XX_PINMUX(24, UART, UART0_RXD)
#define GPIO24_UART1_RTS      QC7XX_PINMUX(24, UART, UART1_RTS)
#define GPIO24_UART1_CTS      QC7XX_PINMUX(24, UART, UART1_CTS)
#define GPIO24_UART1_TXD      QC7XX_PINMUX(24, UART, UART1_TXD)
#define GPIO24_UART1_RXD      QC7XX_PINMUX(24, UART, UART1_RXD)
#define GPIO24_ISO11898_TXD   QC7XX_PINMUX(24, UART, ISO11898_TXD)
#define GPIO24_ISO11898_RXD   QC7XX_PINMUX(24, UART, ISO11898_RXD)
#define GPIO24_CAM0_DAT0      QC7XX_PINMUX(24, CAM, none)
#define GPIO24_SWGPIO24       QC7XX_PINMUX(24, GPIO, none)
#define GPIO24_PWM0_CH0P      QC7XX_PINMUX(24, PWM0, PWM_SEL0)
#define GPIO24_M0_JTAG_TMS    QC7XX_PINMUX(24, JTAG, none)

#define GPIO25_SPI_SCLK       QC7XX_PINMUX(25, SPI, none)
#define GPIO25_I2S_FS         QC7XX_PINMUX(25, I2S, none)
#define GPIO25_PDM_0_IN       QC7XX_PINMUX(25, PDM, none)
#define GPIO25_I2C0_SDA       QC7XX_PINMUX(25, I2C0, none)
#define GPIO25_I2C1_SDA       QC7XX_PINMUX(25, I2C1, none)
#define GPIO25_UART0_RTS      QC7XX_PINMUX(25, UART, UART0_RTS)
#define GPIO25_UART0_CTS      QC7XX_PINMUX(25, UART, UART0_CTS)
#define GPIO25_UART0_TXD      QC7XX_PINMUX(25, UART, UART0_TXD)
#define GPIO25_UART0_RXD      QC7XX_PINMUX(25, UART, UART0_RXD)
#define GPIO25_UART1_RTS      QC7XX_PINMUX(25, UART, UART1_RTS)
#define GPIO25_UART1_CTS      QC7XX_PINMUX(25, UART, UART1_CTS)
#define GPIO25_UART1_TXD      QC7XX_PINMUX(25, UART, UART1_TXD)
#define GPIO25_UART1_RXD      QC7XX_PINMUX(25, UART, UART1_RXD)
#define GPIO25_ISO11898_TXD   QC7XX_PINMUX(25, UART, ISO11898_TXD)
#define GPIO25_ISO11898_RXD   QC7XX_PINMUX(25, UART, ISO11898_RXD)
#define GPIO25_RMII_REF_CLK   QC7XX_PINMUX(25, EMAC, none)
#define GPIO25_CAM0_DAT1      QC7XX_PINMUX(25, CAM, none)
#define GPIO25_SWGPIO25       QC7XX_PINMUX(25, GPIO, none)
#define GPIO25_PWM0_CH1P      QC7XX_PINMUX(25, PWM0, PWM_SEL0)
#define GPIO25_PWM0_CH0N      QC7XX_PINMUX(25, PWM0, PWM_SEL1)
#define GPIO25_M0_JTAG_TCK    QC7XX_PINMUX(25, JTAG, none)

#define GPIO26_SPI_MISO       QC7XX_PINMUX(26, SPI, none)
#define GPIO26_I2S_DI         QC7XX_PINMUX(26, I2S, none)
#define GPIO26_I2S_RCLK_O     QC7XX_PINMUX(26, I2S, none)
#define GPIO26_PDM_CLK_O      QC7XX_PINMUX(26, PDM, none)
#define GPIO26_I2C0_SCL       QC7XX_PINMUX(26, I2C0, none)
#define GPIO26_I2C1_SCL       QC7XX_PINMUX(26, I2C1, none)
#define GPIO26_UART0_RTS      QC7XX_PINMUX(26, UART, UART0_RTS)
#define GPIO26_UART0_CTS      QC7XX_PINMUX(26, UART, UART0_CTS)
#define GPIO26_UART0_TXD      QC7XX_PINMUX(26, UART, UART0_TXD)
#define GPIO26_UART0_RXD      QC7XX_PINMUX(26, UART, UART0_RXD)
#define GPIO26_UART1_RTS      QC7XX_PINMUX(26, UART, UART1_RTS)
#define GPIO26_UART1_CTS      QC7XX_PINMUX(26, UART, UART1_CTS)
#define GPIO26_UART1_TXD      QC7XX_PINMUX(26, UART, UART1_TXD)
#define GPIO26_UART1_RXD      QC7XX_PINMUX(26, UART, UART1_RXD)
#define GPIO26_ISO11898_TXD   QC7XX_PINMUX(26, UART, ISO11898_TXD)
#define GPIO26_ISO11898_RXD   QC7XX_PINMUX(26, UART, ISO11898_RXD)
#define GPIO26_RMII_TXD0      QC7XX_PINMUX(26, EMAC, none)
#define GPIO26_CAM0_DAT2      QC7XX_PINMUX(26, CAM, none)
#define GPIO26_SWGPIO26       QC7XX_PINMUX(26, GPIO, none)
#define GPIO26_PWM0_CH2P      QC7XX_PINMUX(26, PWM0, PWM_SEL0)
#define GPIO26_PWM0_CH1P      QC7XX_PINMUX(26, PWM0, PWM_SEL1)
#define GPIO26_M0_JTAG_TDO    QC7XX_PINMUX(26, JTAG, none)

#define GPIO27_SPI_MOSI       QC7XX_PINMUX(27, SPI, none)
#define GPIO27_I2S_DO         QC7XX_PINMUX(27, I2S, none)
#define GPIO27_I2S_RCLK_O     QC7XX_PINMUX(27, I2S, none)
#define GPIO27_I2C0_SDA       QC7XX_PINMUX(27, I2C0, none)
#define GPIO27_I2C1_SDA       QC7XX_PINMUX(27, I2C1, none)
#define GPIO27_UART0_RTS      QC7XX_PINMUX(27, UART, UART0_RTS)
#define GPIO27_UART0_CTS      QC7XX_PINMUX(27, UART, UART0_CTS)
#define GPIO27_UART0_TXD      QC7XX_PINMUX(27, UART, UART0_TXD)
#define GPIO27_UART0_RXD      QC7XX_PINMUX(27, UART, UART0_RXD)
#define GPIO27_UART1_RTS      QC7XX_PINMUX(27, UART, UART1_RTS)
#define GPIO27_UART1_CTS      QC7XX_PINMUX(27, UART, UART1_CTS)
#define GPIO27_UART1_TXD      QC7XX_PINMUX(27, UART, UART1_TXD)
#define GPIO27_UART1_RXD      QC7XX_PINMUX(27, UART, UART1_RXD)
#define GPIO27_ISO11898_TXD   QC7XX_PINMUX(27, UART, ISO11898_TXD)
#define GPIO27_ISO11898_RXD   QC7XX_PINMUX(27, UART, ISO11898_RXD)
#define GPIO27_RMII_TXD1      QC7XX_PINMUX(27, EMAC, none)
#define GPIO27_CAM0_DAT3      QC7XX_PINMUX(27, CAM, none)
#define GPIO27_ADC_CH10       QC7XX_PINMUX(27, ANALOG, none)
#define GPIO27_AUADC_CH4      QC7XX_PINMUX(27, ANALOG, none)
#define GPIO27_SWGPIO27       QC7XX_PINMUX(27, GPIO, none)
#define GPIO27_PWM0_CH3P      QC7XX_PINMUX(27, PWM0, PWM_SEL0)
#define GPIO27_PWM0_CH1N      QC7XX_PINMUX(27, PWM0, PWM_SEL1)
#define GPIO27_AUDAC_PWM_N    QC7XX_PINMUX(27, AUDAC, none)
#define GPIO27_M0_JTAG_TDI    QC7XX_PINMUX(27, JTAG, none)

#define GPIO28_SPI_SS         QC7XX_PINMUX(28, SPI, none)
#define GPIO28_I2S_BCLK       QC7XX_PINMUX(28, I2S, none)
#define GPIO28_I2C0_SCL       QC7XX_PINMUX(28, I2C0, none)
#define GPIO28_I2C1_SCL       QC7XX_PINMUX(28, I2C1, none)
#define GPIO28_UART0_RTS      QC7XX_PINMUX(28, UART, UART0_RTS)
#define GPIO28_UART0_CTS      QC7XX_PINMUX(28, UART, UART0_CTS)
#define GPIO28_UART0_TXD      QC7XX_PINMUX(28, UART, UART0_TXD)
#define GPIO28_UART0_RXD      QC7XX_PINMUX(28, UART, UART0_RXD)
#define GPIO28_UART1_RTS      QC7XX_PINMUX(28, UART, UART1_RTS)
#define GPIO28_UART1_CTS      QC7XX_PINMUX(28, UART, UART1_CTS)
#define GPIO28_UART1_TXD      QC7XX_PINMUX(28, UART, UART1_TXD)
#define GPIO28_UART1_RXD      QC7XX_PINMUX(28, UART, UART1_RXD)
#define GPIO28_ISO11898_TXD   QC7XX_PINMUX(28, UART, ISO11898_TXD)
#define GPIO28_ISO11898_RXD   QC7XX_PINMUX(28, UART, ISO11898_RXD)
#define GPIO28_RMII_RXD0      QC7XX_PINMUX(28, EMAC, none)
#define GPIO28_CAM0_HSYNC     QC7XX_PINMUX(28, CAM, none)
#define GPIO28_SWGPIO28       QC7XX_PINMUX(28, GPIO, none)
#define GPIO28_PWM0_CH0P      QC7XX_PINMUX(28, PWM0, PWM_SEL0)
#define GPIO28_PWM0_CH2P      QC7XX_PINMUX(28, PWM0, PWM_SEL1)
#define GPIO28_AUDAC_PWM_P    QC7XX_PINMUX(28, AUDAC, none)
#define GPIO28_M0_JTAG_TMS    QC7XX_PINMUX(28, JTAG, none)

#define GPIO29_SPI_SCLK       QC7XX_PINMUX(29, SPI, none)
#define GPIO29_I2S_FS         QC7XX_PINMUX(29, I2S, none)
#define GPIO29_I2C0_SDA       QC7XX_PINMUX(29, I2C0, none)
#define GPIO29_I2C1_SDA       QC7XX_PINMUX(29, I2C1, none)
#define GPIO29_UART0_RTS      QC7XX_PINMUX(29, UART, UART0_RTS)
#define GPIO29_UART0_CTS      QC7XX_PINMUX(29, UART, UART0_CTS)
#define GPIO29_UART0_TXD      QC7XX_PINMUX(29, UART, UART0_TXD)
#define GPIO29_UART0_RXD      QC7XX_PINMUX(29, UART, UART0_RXD)
#define GPIO29_UART1_RTS      QC7XX_PINMUX(29, UART, UART1_RTS)
#define GPIO29_UART1_CTS      QC7XX_PINMUX(29, UART, UART1_CTS)
#define GPIO29_UART1_TXD      QC7XX_PINMUX(29, UART, UART1_TXD)
#define GPIO29_UART1_RXD      QC7XX_PINMUX(29, UART, UART1_RXD)
#define GPIO29_ISO11898_TXD   QC7XX_PINMUX(29, UART, ISO11898_TXD)
#define GPIO29_ISO11898_RXD   QC7XX_PINMUX(29, UART, ISO11898_RXD)
#define GPIO29_RMII_RXD1      QC7XX_PINMUX(29, EMAC, none)
#define GPIO29_CAM0_VSYNC     QC7XX_PINMUX(29, CAM, none)
#define GPIO29_SWGPIO29       QC7XX_PINMUX(29, GPIO, none)
#define GPIO29_PWM0_CH1P      QC7XX_PINMUX(29, PWM0, PWM_SEL0)
#define GPIO29_PWM0_CH2N      QC7XX_PINMUX(29, PWM0, PWM_SEL1)
#define GPIO29_M0_JTAG_TCK    QC7XX_PINMUX(29, JTAG, none)

#define GPIO30_SPI_MISO       QC7XX_PINMUX(30, SPI, none)
#define GPIO30_I2S_DI         QC7XX_PINMUX(30, I2S, none)
#define GPIO30_I2S_RCLK_O     QC7XX_PINMUX(30, I2S, none)
#define GPIO30_I2C0_SCL       QC7XX_PINMUX(30, I2C0, none)
#define GPIO30_I2C1_SCL       QC7XX_PINMUX(30, I2C1, none)
#define GPIO30_UART0_RTS      QC7XX_PINMUX(30, UART, UART0_RTS)
#define GPIO30_UART0_CTS      QC7XX_PINMUX(30, UART, UART0_CTS)
#define GPIO30_UART0_TXD      QC7XX_PINMUX(30, UART, UART0_TXD)
#define GPIO30_UART0_RXD      QC7XX_PINMUX(30, UART, UART0_RXD)
#define GPIO30_UART1_RTS      QC7XX_PINMUX(30, UART, UART1_RTS)
#define GPIO30_UART1_CTS      QC7XX_PINMUX(30, UART, UART1_CTS)
#define GPIO30_UART1_TXD      QC7XX_PINMUX(30, UART, UART1_TXD)
#define GPIO30_UART1_RXD      QC7XX_PINMUX(30, UART, UART1_RXD)
#define GPIO30_ISO11898_TXD   QC7XX_PINMUX(30, UART, ISO11898_TXD)
#define GPIO30_ISO11898_RXD   QC7XX_PINMUX(30, UART, ISO11898_RXD)
#define GPIO30_RMII_RXERR     QC7XX_PINMUX(30, EMAC, none)
#define GPIO30_CAM0_CLK       QC7XX_PINMUX(30, CAM, none)
#define GPIO30_AUADC_CH7      QC7XX_PINMUX(30, ANALOG, none)
#define GPIO30_SWGPIO30       QC7XX_PINMUX(30, GPIO, none)
#define GPIO30_PWM0_CH2P      QC7XX_PINMUX(30, PWM0, PWM_SEL0)
#define GPIO30_PWM0_CH3P      QC7XX_PINMUX(30, PWM0, PWM_SEL1)
#define GPIO30_M0_JTAG_TDO    QC7XX_PINMUX(30, JTAG, none)

#define GPIO31_SPI_MOSI       QC7XX_PINMUX(31, SPI, none)
#define GPIO31_I2S_DO         QC7XX_PINMUX(31, I2S, none)
#define GPIO31_I2S_RCLK_O     QC7XX_PINMUX(31, I2S, none)
#define GPIO31_I2C0_SDA       QC7XX_PINMUX(31, I2C0, none)
#define GPIO31_I2C1_SDA       QC7XX_PINMUX(31, I2C1, none)
#define GPIO31_UART0_RTS      QC7XX_PINMUX(31, UART, UART0_RTS)
#define GPIO31_UART0_CTS      QC7XX_PINMUX(31, UART, UART0_CTS)
#define GPIO31_UART0_TXD      QC7XX_PINMUX(31, UART, UART0_TXD)
#define GPIO31_UART0_RXD      QC7XX_PINMUX(31, UART, UART0_RXD)
#define GPIO31_UART1_RTS      QC7XX_PINMUX(31, UART, UART1_RTS)
#define GPIO31_UART1_CTS      QC7XX_PINMUX(31, UART, UART1_CTS)
#define GPIO31_UART1_TXD      QC7XX_PINMUX(31, UART, UART1_TXD)
#define GPIO31_UART1_RXD      QC7XX_PINMUX(31, UART, UART1_RXD)
#define GPIO31_ISO11898_TXD   QC7XX_PINMUX(31, UART, ISO11898_TXD)
#define GPIO31_ISO11898_RXD   QC7XX_PINMUX(31, UART, ISO11898_RXD)
#define GPIO31_RMII_TX_EN     QC7XX_PINMUX(31, EMAC, none)
#define GPIO31_CAM0_DAT4      QC7XX_PINMUX(31, CAM, none)
#define GPIO31_SWGPIO31       QC7XX_PINMUX(31, GPIO, none)
#define GPIO31_PWM0_CH3P      QC7XX_PINMUX(31, PWM0, PWM_SEL0)
#define GPIO31_PWM0_CH3N      QC7XX_PINMUX(31, PWM0, PWM_SEL1)
#define GPIO31_M0_JTAG_TDI    QC7XX_PINMUX(31, JTAG, none)

#define GPIO32_SPI_SS         QC7XX_PINMUX(32, SPI, none)
#define GPIO32_I2S_BCLK       QC7XX_PINMUX(32, I2S, none)
#define GPIO32_I2C0_SCL       QC7XX_PINMUX(32, I2C0, none)
#define GPIO32_I2C1_SCL       QC7XX_PINMUX(32, I2C1, none)
#define GPIO32_UART0_RTS      QC7XX_PINMUX(32, UART, UART0_RTS)
#define GPIO32_UART0_CTS      QC7XX_PINMUX(32, UART, UART0_CTS)
#define GPIO32_UART0_TXD      QC7XX_PINMUX(32, UART, UART0_TXD)
#define GPIO32_UART0_RXD      QC7XX_PINMUX(32, UART, UART0_RXD)
#define GPIO32_UART1_RTS      QC7XX_PINMUX(32, UART, UART1_RTS)
#define GPIO32_UART1_CTS      QC7XX_PINMUX(32, UART, UART1_CTS)
#define GPIO32_UART1_TXD      QC7XX_PINMUX(32, UART, UART1_TXD)
#define GPIO32_UART1_RXD      QC7XX_PINMUX(32, UART, UART1_RXD)
#define GPIO32_ISO11898_TXD   QC7XX_PINMUX(32, UART, ISO11898_TXD)
#define GPIO32_ISO11898_RXD   QC7XX_PINMUX(32, UART, ISO11898_RXD)
#define GPIO32_RMII_RX_DV     QC7XX_PINMUX(32, EMAC, none)
#define GPIO32_CAM0_DAT5      QC7XX_PINMUX(32, CAM, none)
#define GPIO32_SWGPIO32       QC7XX_PINMUX(32, GPIO, none)
#define GPIO32_PWM0_CH0P      QC7XX_PINMUX(32, PWM0, PWM_SEL0)
#define GPIO32_M0_JTAG_TMS    QC7XX_PINMUX(32, JTAG, none)

#define GPIO33_SPI_SCLK       QC7XX_PINMUX(33, SPI, none)
#define GPIO33_I2S_FS         QC7XX_PINMUX(33, I2S, none)
#define GPIO33_I2C0_SDA       QC7XX_PINMUX(33, I2C0, none)
#define GPIO33_I2C1_SDA       QC7XX_PINMUX(33, I2C1, none)
#define GPIO33_UART0_RTS      QC7XX_PINMUX(33, UART, UART0_RTS)
#define GPIO33_UART0_CTS      QC7XX_PINMUX(33, UART, UART0_CTS)
#define GPIO33_UART0_TXD      QC7XX_PINMUX(33, UART, UART0_TXD)
#define GPIO33_UART0_RXD      QC7XX_PINMUX(33, UART, UART0_RXD)
#define GPIO33_UART1_RTS      QC7XX_PINMUX(33, UART, UART1_RTS)
#define GPIO33_UART1_CTS      QC7XX_PINMUX(33, UART, UART1_CTS)
#define GPIO33_UART1_TXD      QC7XX_PINMUX(33, UART, UART1_TXD)
#define GPIO33_UART1_RXD      QC7XX_PINMUX(33, UART, UART1_RXD)
#define GPIO33_ISO11898_TXD   QC7XX_PINMUX(33, UART, ISO11898_TXD)
#define GPIO33_ISO11898_RXD   QC7XX_PINMUX(33, UART, ISO11898_RXD)
#define GPIO33_RMII_MDC       QC7XX_PINMUX(33, EMAC, none)
#define GPIO33_CAM0_DAT6      QC7XX_PINMUX(33, CAM, none)
#define GPIO33_SWGPIO33       QC7XX_PINMUX(33, GPIO, none)
#define GPIO33_PWM0_CH1P      QC7XX_PINMUX(33, PWM0, PWM_SEL0)
#define GPIO33_PWM0_CH0N      QC7XX_PINMUX(33, PWM0, PWM_SEL1)
#define GPIO33_M0_JTAG_TCK    QC7XX_PINMUX(33, JTAG, none)

#define GPIO34_SPI_MISO       QC7XX_PINMUX(34, SPI, none)
#define GPIO34_I2S_DI         QC7XX_PINMUX(34, I2S, none)
#define GPIO34_I2S_RCLK_O     QC7XX_PINMUX(34, I2S, none)
#define GPIO34_I2C0_SCL       QC7XX_PINMUX(34, I2C0, none)
#define GPIO34_I2C1_SCL       QC7XX_PINMUX(34, I2C1, none)
#define GPIO34_UART0_RTS      QC7XX_PINMUX(34, UART, UART0_RTS)
#define GPIO34_UART0_CTS      QC7XX_PINMUX(34, UART, UART0_CTS)
#define GPIO34_UART0_TXD      QC7XX_PINMUX(34, UART, UART0_TXD)
#define GPIO34_UART0_RXD      QC7XX_PINMUX(34, UART, UART0_RXD)
#define GPIO34_UART1_RTS      QC7XX_PINMUX(34, UART, UART1_RTS)
#define GPIO34_UART1_CTS      QC7XX_PINMUX(34, UART, UART1_CTS)
#define GPIO34_UART1_TXD      QC7XX_PINMUX(34, UART, UART1_TXD)
#define GPIO34_UART1_RXD      QC7XX_PINMUX(34, UART, UART1_RXD)
#define GPIO34_ISO11898_TXD   QC7XX_PINMUX(34, UART, ISO11898_TXD)
#define GPIO34_ISO11898_RXD   QC7XX_PINMUX(34, UART, ISO11898_RXD)
#define GPIO34_RMII_MDIO      QC7XX_PINMUX(34, EMAC, none)
#define GPIO34_CAM0_DAT7      QC7XX_PINMUX(34, CAM, none)
#define GPIO34_SWGPIO34       QC7XX_PINMUX(34, GPIO, none)
#define GPIO34_PWM0_CH2P      QC7XX_PINMUX(34, PWM0, PWM_SEL0)
#define GPIO34_PWM0_CH1P      QC7XX_PINMUX(34, PWM0, PWM_SEL1)
#define GPIO34_M0_JTAG_TDO    QC7XX_PINMUX(34, JTAG, none)

/* Not autogenerated! */
#define GPIO0_CLKOUT          QC7XX_PINMUX(0, CLKOUT, none)
#define GPIO23_CLKOUT         QC7XX_PINMUX(23, CLKOUT, none)
/* clang-format on */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_QCC74X_PINCTRL_H_ */
