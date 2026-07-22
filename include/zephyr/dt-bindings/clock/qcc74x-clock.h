/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_QCC74X_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_QCC74X_H_

/* Supported CPU clock Sources */
#define QC7XX_MCU_SYS_CLK_RC32M            (0)
#define QC7XX_MCU_SYS_CLK_XTAL             (1)
#define QC7XX_MCU_SYS_CLK_TOP_AUPLL_DIV2   (2)
#define QC7XX_MCU_SYS_CLK_TOP_AUPLL_DIV1   (3)
#define QC7XX_MCU_SYS_CLK_TOP_WIFIPLL_240M (4)
#define QC7XX_MCU_SYS_CLK_TOP_WIFIPLL_320M (5)

/* Supported CPU frequencies */

/* Supported PLL configurations (those values doesn't exist in HAL) */
#define QC7XX_WIFI_PLL_960M (0)
#define QC7XX_WIFI_PLL_OFF  (1)

#define QC7XX_AUDIO_PLL_491P52M (0)
#define QC7XX_AUDIO_PLL_451P58M (1)
#define QC7XX_AUDIO_PLL_400M    (2)
#define QC7XX_AUDIO_PLL_384M    (3)
#define QC7XX_AUDIO_PLL_245P76M (4)
#define QC7XX_AUDIO_PLL_240M    (5)
#define QC7XX_AUDIO_PLL_OFF     (6)

/** XTAL types supported (should match with the ones here:
 *  hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_glb.h
 */
#define QC7XX_XTAL_NONE  (0) /*!< XTAL is none */
#define QC7XX_XTAL_24M   (1) /*!< XTAL is 24M */
#define QC7XX_XTAL_32M   (2) /*!< XTAL is 32M */
#define QC7XX_XTAL_38P4M (3) /*!< XTAL is 38.4M */
#define QC7XX_XTAL_40M   (4) /*!< XTAL is 40M */
#define QC7XX_XTAL_26M   (5) /*!< XTAL is 26M */
#define QC7XX_XTAL_RC32M (6) /*!< XTAL is RC32M */
#define QC7XX_XTAL_MAX   (7) /*!< type max num */

/* Supported 32k clock sources.
 * Definitions shall match with the ones here:
 * hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_hbn.h */
#define QC7XX_HBN_32K_RC             (0) /*!< HBN use rc 32k */
#define QC7XX_HBN_32K_XTAL           (1) /*!< HBN use xtal 32k */
#define QC7XX_HBN_32K_DIG            (3) /*!< HBN use dig 32k */

/* Supported DIG clock sources.
 * Definitions shall match with the ones here:
 * hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_glb.h */
#define QC7XX_DIG_CLK_WIFIPLL_32M    (0) /*!< select WIFIPLL 32M as DIG clock source */
#define QC7XX_DIG_CLK_XCLK           (1) /*!< select XCLK as DIG clock source */
#define QC7XX_DIG_CLK_AUPLL          (2) /*!< select AUPLL as DIG clock source */
#define QC7XX_DIG_CLK_OFF          (255) /*!< turn off the DIG clock [default] >*/

/* Definitions shall match QCC74x in `modules\hal\hal_qcom\qcc7xx\drivers\lhal\include\qc7xx_peri.h` */
#define QC7XX_PERIPHERAL_CPU         (0)
#define QC7XX_PERIPHERAL_SDU0        (1)
#define QC7XX_PERIPHERAL_SEC0        (2)
#define QC7XX_PERIPHERAL_DMA0        (3)
#define QC7XX_PERIPHERAL_CCI         (4)
#define QC7XX_PERIPHERAL_GPADC0      (5)
#define QC7XX_PERIPHERAL_GPDAC0      (6)
#define QC7XX_PERIPHERAL_TZ1         (7)
#define QC7XX_PERIPHERAL_TZ2         (8)
#define QC7XX_PERIPHERAL_EF_CTRL     (9)
#define QC7XX_PERIPHERAL_SF_CTRL     (10)
#define QC7XX_PERIPHERAL_EMAC0       (11)
#define QC7XX_PERIPHERAL_UART0       (12)
#define QC7XX_PERIPHERAL_UART1       (13)
#define QC7XX_PERIPHERAL_SPI0        (15)
#define QC7XX_PERIPHERAL_I2C0        (16)
#define QC7XX_PERIPHERAL_PWM0        (17)
#define QC7XX_PERIPHERAL_TIMER0      (18)
#define QC7XX_PERIPHERAL_IR          (19)
#define QC7XX_PERIPHERAL_CHECKSUM    (20)
#define QC7XX_PERIPHERAL_I2S0        (21)
#define QC7XX_PERIPHERAL_PSRAM1_CTRL (22)
#define QC7XX_PERIPHERAL_USB20       (23)
#define QC7XX_PERIPHERAL_AUDAC0      (24)
#define QC7XX_PERIPHERAL_SDH0        (25)
#define QC7XX_PERIPHERAL_I2C1        (26)
#define QC7XX_PERIPHERAL_DBI         (27)
#define QC7XX_PERIPHERAL_AUADC0      (28)
#define QC7XX_PERIPHERAL_DMA_GPIO    (29)
#define QC7XX_PERIPHERAL_MM_MISC     (30)
#define QC7XX_PERIPHERAL_JENC        (31)
#define QC7XX_PERIPHERAL_D2XB        (32)
#define QC7XX_PERIPHERAL_D2XA        (33)
#define QC7XX_PERIPHERAL_EMI_MISC    (34)
#define QC7XX_PERIPHERAL_CAM0        (35)
#define QC7XX_PERIPHERAL_RTC         (36)
#define QC7XX_PERIPHERAL_PKA         (37)
#define QC7XX_PERIPHERAL_GLB         (38)
#define QC7XX_PERIPHERAL_SEC_ENG     (39)
#define QC7XX_PERIPHERAL_SEC_DBG     (40)
#define QC7XX_PERIPHERAL_MIX         (41)
#define QC7XX_PERIPHERAL_PDS         (42)
#define QC7XX_PERIPHERAL_WIFI_NORMAL (43)

/* This definitions doesn't match with:
 * modules\hal\hal_qcom\qcc7xx\drivers\lhal\include\qc7xx_peri.h
 */
/* CAN wasn't present in hal before */
#define QC7XX_PERIPHERAL_CAN         (252)
/* We need to distinguish between TIMER0, TIMER1 and watchdog */
#define QC7XX_PERIPHERAL_WATCHDOG    (253)
#define QC7XX_PERIPHERAL_TIMER1      (254)

/*
 * Clock options for 74x
+------------+--------------------------------------+-------------------------------+
| Peripheral | Clock source options (documentation) | Clock source options GLB      |
+------------+--------------------------------------+-------------------------------+
| PWM        | BCLK                                 |                               |
|            | XCLK                                 |                               |
|            | f32k                                 |                               |
+------------+--------------------------------------+-------------------------------+
| I2C        | BCLK                                 | GLB_I2C_CLK_BCLK 0            |
|            | XCLK                                 | GLB_I2C_CLK_XCLK 1            |
+------------+--------------------------------------+-------------------------------+
| SPI        | MUXPLL_160M_sel                      | GLB_SPI_CLK_MCU_MUXPLL_160M 0 |
|            | XCLK                                 | GLB_SPI_CLK_XCLK            1 |
+------------+--------------------------------------+-------------------------------+
| I2S        | AUPLL_other                          |                               |
+------------+--------------------------------------+-------------------------------+
| UART       | BCLK                                 | GLB_UART_CLK_BCLK     0       |
|            | MUXPLL_160M_sel                      | GLB_UART_CLK_PLL_160M 1       |
|            | XCLK                                 | HBN_UART_CLK_XCLK     2       |
+------------+--------------------------------------+-------------------------------+
| ADC        |                                      | GLB_ADC_CLK_WIFIPLL_96M 0     |
|            | XCLK                                 | GLB_ADC_CLK_XCLK        1     |
|            | RC_32KHz                             |                               |
|            | XTAL32KHz                            |                               |
|            | dig_32KHz                            |                               |
|            | AUPLL_other                          |                               |
+------------+--------------------------------------+-------------------------------+
| DAC        | 32MHz                                | It uses DIG_512K clock        |
|            | XCLK                                 |                               |
|            | AudioDAC                             |                               |
+------------+--------------------------------------+-------------------------------+
| DMA        | XCLK                                 |                               |
|            | BCLK                                 |                               |
+------------+--------------------------------------+-------------------------------+
| IR         | XCLK                                 | GLB_IR_CLK_SRC_XCLK 0         |
+------------+--------------------------------------+-------------------------------+
| CAN        | XCLK                                 | PERIPHERAL_CLOCK_CAN_ENABLE() |
+------------+--------------------------------------+-------------------------------+
| Audio      | AUPLL_other                          | GLB_AUDIO_ADC_AUPLL 0         |
+------------+--------------------------------------+-------------------------------+
| SEC        | BCLK                                 | GLB_PKA_CLK_MCU_BCLK          |
|            | MUXPLL_160M                          | GLB_PKA_CLK_MCU_MUXPLL_160    |
+------------+--------------------------------------+-------------------------------+
| DBI        | MUXPLL_160M_sel                      | GLB_DBI_CLK_MCU_MUXPLL_160M 0 |
|            | XCLK                                 | GLB_DBI_CLK_XCLK            1 |
+------------+--------------------------------------+-------------------------------+
| Watchdog   | XCLK                                 |                               |
|            | pll_sel                              |                               |
|            | RC_32KHz                             |                               |
|            | XTAL32KHz                            |                               |
|            | dig_32KHz                            |                               |
|            | GPIO_CLK_IN                          |                               |
+------------+--------------------------------------+-------------------------------+
| USB        | WIFIPLL                              | GLB_Set_USB_CLK_From_WIFIPLL()|
+------------+--------------------------------------+-------------------------------+
| CAM        | XCLK                                 | GLB_CAM_CLK_XCLK           0  |
|            | WIFIPLL_96M                          | GLB_CAM_CLK_WIFIPLL_96M    1  |
|            | TOP_AUPLL_DIV5                       | GLB_CAM_CLK_TOP_AUPLL_DIV5 2  |
+------------+--------------------------------------+-------------------------------+
| MJPEG      | XCLK                                 | UnGate(GLB_AHB_CLOCK_IP_MJPEG)|
+-----------------------------------------------------------------------------------+
*/

/* Macro for default clock source
 * (To be used when there is no software choice) */
#define QC7XX_CLK_DEFAULT  (0)

/* Definitions shall match QCC74x in modules/hal/hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_glb.h */
#define QC7XX_I2C_CLK_BCLK                    (0) /*!< Select bus clk as I2C clock */
#define QC7XX_I2C_CLK_XCLK                    (1) /*!< Select xclk as I2C clock */

/* PWM clock definitions taken from modules/hal/hal_qcom/qcc7xx/drivers/lhal/src/qc7xx_pwm_v2.c (CLK_SEL_MASK) */
#define QC7XX_PWM_CLK_XCLK                    (0)
#define QC7XX_PWM_CLK_BCLK                    (1)
#define QC7XX_PWM_CLK_32K_CLK                 (2)

/* In case of I2S clock management means output clock reference control (input clock can not be modified) */
#define QC7XX_I2S_SEL_I2S_DI_DO               (0) /*!< Select i2s_di input or i2s_do output */
#define QC7XX_I2S_SEL_I2S_REF_CLK             (1) /*!< Select reference clock on di or do   */

#define QC7XX_ADC_CLK_AUPLL                   (0) /*!< use AUPLL as ADC clock */
#define QC7XX_ADC_CLK_XCLK                    (1) /*!< use XCLK as ADC clock */

#define QC7XX_SDH_CLK_WIFIPLL_96M             (0) /*!< Select WIFIPLL_96M as SDH clock */
#define QC7XX_SDH_CLK_TOP_AUPLL_DIV5          (1) /*!< Select TOP_AUPLL_DIV5 as SDH clock */

#define QC7XX_DBI_CLK_MCU_MUXPLL_160M         (0) /*!< Select MCU MUXPLL 160M as DBI clock */
#define QC7XX_DBI_CLK_XCLK                    (1) /*!< Select XCLK as DBI clock */

#define QC7XX_PKA_CLK_MCU_BCLK                (0) /*!< Select MCU_BCLK as PKA clock */
#define QC7XX_PKA_CLK_MCU_MUXPLL_160M         (1) /*!< Select MCU_MUXPLL_160M as PKA clock */

#define QC7XX_SPI_CLK_MCU_MUXPLL_160M         (0) /*!< Select MCU MUXPLL 160M as SPI clock */
#define QC7XX_SPI_SPI_CLK_XCLK                (1) /*!< Select xclk as SPI clock */

/* Definitions shall match QCC74x in modules/hal/hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_glb.h */
#define QC7XX_SFLASH_CLK_WIFIPLL_120M         (0)
#define QC7XX_SFLASH_CLK_XTAL                 (1)
#define QC7XX_SFLASH_CLK_TOP_AUPLL_DIV5       (2)
#define QC7XX_SFLASH_CLK_MUXPLL_80M           (3)
#define QC7XX_SFLASH_CLK_BCLK                 (4)
#define QC7XX_SFLASH_CLK_WIFIPLL_96M          (5)

/* Definitions shall match QCC74x in modules/hal/hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_glb.h */
#define QC7XX_PSRAMB_CLK_EMI_WIFIPLL_320M     (0)
#define QC7XX_PSRAMB_CLK_EMI_AUPLL_DIV1       (1)

/* For DMA: clock management means enabling/disabling clock for a specific channel
 * (input clock can not be modified) */
#define QC7XX_DMA0_CLK_CH0                    (0) /*!< DMA0 clock ID:channel 0 */
#define QC7XX_DMA0_CLK_CH1                    (1) /*!< DMA0 clock ID:channel 1 */
#define QC7XX_DMA0_CLK_CH2                    (2) /*!< DMA0 clock ID:channel 2 */
#define QC7XX_DMA0_CLK_CH3                    (3) /*!< DMA0 clock ID:channel 3 */

#define QC7XX_IR_CLK_XCLK                     (0) /*!< use XCLK as IR clock */

#define QC7XX_UART_CLK_MCU_BCLK               (0) /*!< Select mcu_pbclk as UART clock */
#define QC7XX_UART_CLK_MUXPLL_160M            (1) /*!< Select MUXPLL 160M as UART clock */
#define QC7XX_UART_CLK_XCLK                   (2) /*!< Select XCLK as UART clock */

/* For timer clock sources definitions should match the ones in hal_qcom/qcc7xx/drivers/lhal/include/qc7xx_timer.h */
#define QC7XX_TIMER_CLK_BCLK                  (0)
#define QC7XX_TIMER_CLK_32K                   (1)
#define QC7XX_TIMER_CLK_1K                    (2)
#define QC7XX_TIMER_CLK_XTAL                  (3)
#define QC7XX_TIMER_CLK_GPIO                  (4)
#define QC7XX_TIMER_CLK_NO                    (5)

/* MTimer clock source definitions should match hal_qcom/qcc7xx/drivers/soc/qcc74x/std/include/qcc74x_clock.h */
#define QC7XX_MTIMER_CLK_MCU_XCLK             (0)
#define QC7XX_MTIMER_CLK_MCU_ROOT_CLK         (1)

#define QC7XX_USB20_CLK_WIFIPLL               (0) /*!< Select WIFIPLL as USB20 clock */

#define QC7XX_CAM_CLK_XCLK                    (0) /*!< Select XCLK as CAM clock */
#define QC7XX_CAM_CLK_WIFIPLL_96M             (1) /*!< Select WIFIPLL_96M as CAM clock */
#define QC7XX_CAM_CLK_TOP_AUPLL_DIV5          (2) /*!< Select TOP_AUPLL_DIV5 as CAM clock */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_QC74X_H_ */
