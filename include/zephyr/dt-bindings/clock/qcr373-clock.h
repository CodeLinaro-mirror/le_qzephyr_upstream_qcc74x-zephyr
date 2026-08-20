/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_QCR373_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_QCR373_H_

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
 * hal_qcom/qcc7xx/drivers/soc/qcr373/std/include/qcr373_hbn.h */
#define QC7XX_HBN_32K_RC                        (0) /*!< HBN use rc 32k */
#define QC7XX_HBN_32K_XTAL                      (1) /*!< HBN use xtal 32k */
#define QC7XX_HBN_32K_DIG                       (3) /*!< HBN use dig 32k */

/* Supported DIG clock sources.
 * Definitions shall match with the ones here:
 * hal_qcom/qcc7xx/drivers/soc/qcr373/std/include/qcr373_glb.h */
#define QC7XX_DIG_CLK_WIFIPLL_32M               (0) /*!< select WIFIPLL 32M as DIG clock source */
#define QC7XX_DIG_CLK_XCLK                      (1) /*!< select XCLK as DIG clock source */
#define QC7XX_DIG_CLK_AUPLL                     (2) /*!< select AUPLL as DIG clock source */
#define QC7XX_DIG_CLK_OFF                     (255) /*!< turn off the DIG clock [default] >*/

/* Definitions shall match QCC75x in `modules\hal\hal_qcom\qcc7xx\drivers\lhal\include\qc7xx_peri.h` */
#define QC7XX_PERIPHERAL_CPU                    (0)
#define QC7XX_PERIPHERAL_SDU0                   (1)
#define QC7XX_PERIPHERAL_SEC0                   (2)
#define QC7XX_PERIPHERAL_DMA0                   (3)
#define QC7XX_PERIPHERAL_DMA1                   (4)
#define QC7XX_PERIPHERAL_DMA2                   (5)
#define QC7XX_PERIPHERAL_CCI                    (6)
#define QC7XX_PERIPHERAL_GPADC0                 (7)
#define QC7XX_PERIPHERAL_GPADC1                 (8)
#define QC7XX_PERIPHERAL_GPDAC0                 (9)
#define QC7XX_PERIPHERAL_TZ1                    (10)
#define QC7XX_PERIPHERAL_TZ2                    (11)
#define QC7XX_PERIPHERAL_EF_CTRL                (12)
#define QC7XX_PERIPHERAL_SF_CTRL                (13)
#define QC7XX_PERIPHERAL_EMAC0                  (14)
#define QC7XX_PERIPHERAL_UART0                  (15)
#define QC7XX_PERIPHERAL_UART1                  (16)
#define QC7XX_PERIPHERAL_UART2                  (17)
#define QC7XX_PERIPHERAL_UART3                  (18)
#define QC7XX_PERIPHERAL_SPI0                   (19)
#define QC7XX_PERIPHERAL_SPI1                   (20)
#define QC7XX_PERIPHERAL_I2C0                   (21)
#define QC7XX_PERIPHERAL_I2C1                   (22)
#define QC7XX_PERIPHERAL_I2C2                   (23)
#define QC7XX_PERIPHERAL_PWM0                   (24)
#define QC7XX_PERIPHERAL_TIMER0                 (25)
#define QC7XX_PERIPHERAL_IR                     (26)
#define QC7XX_PERIPHERAL_CHECKSUM               (27)
#define QC7XX_PERIPHERAL_I2S0                   (28)
#define QC7XX_PERIPHERAL_PSRAM1_CTRL            (29)
#define QC7XX_PERIPHERAL_USB20                  (30)
#define QC7XX_PERIPHERAL_AUADC0                 (31)
#define QC7XX_PERIPHERAL_AUDAC0                 (32)
#define QC7XX_PERIPHERAL_SDH0                   (33)
#define QC7XX_PERIPHERAL_DBI                    (34)
#define QC7XX_PERIPHERAL_DMA_GPIO               (35)
#define QC7XX_PERIPHERAL_MM_MISC                (36)
#define QC7XX_PERIPHERAL_GLB                    (37)
#define QC7XX_PERIPHERAL_PDS                    (38)
#define QC7XX_PERIPHERAL_MIX                    (39)
#define QC7XX_PERIPHERAL_SEC_ENG                (40)
#define QC7XX_PERIPHERAL_SEC_DBG                (41)
#define QC7XX_PERIPHERAL_RTC                    (42)
#define QC7XX_PERIPHERAL_JENC                   (43)
#define QC7XX_PERIPHERAL_D2XB                   (44)
#define QC7XX_PERIPHERAL_D2XA                   (45)
#define QC7XX_PERIPHERAL_SEC_PKA                (46)
#define QC7XX_PERIPHERAL_CAM0                   (47)
#define QC7XX_PERIPHERAL_EMI_MISC               (48)
#define QC7XX_PERIPHERAL_PEC                    (49)
#define QC7XX_PERIPHERAL_WIFI_NORMAL            (50)

/* This definitions doesn't match with:
 * modules\hal\hal_qcom\qcc7xx\drivers\lhal\include\qc7xx_peri.h
 */
/* CAN wasn't present in hal before */
#define QC7XX_PERIPHERAL_CAN                    (252)
/* We need to distinguish between TIMER0, TIMER1 and watchdog */
#define QC7XX_PERIPHERAL_WATCHDOG               (253)
#define QC7XX_PERIPHERAL_TIMER1                 (254)

/* Macro for default clock source
 * (To be used when there is no software choice) */
#define QC7XX_CLK_DEFAULT  (0)

/* Definitions shall match QCC75x in modules/hal/hal_qcom/qcc7xx/drivers/soc/qcr373/std/include/qcr373_glb.h */
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

#define QC7XX_SPI_CLK_MCU_MUXPLL_160M         (0) /*!< Select MCU MUXPLL 160M as SPI clock */
#define QC7XX_SPI_SPI_CLK_XCLK                (1) /*!< Select xclk as SPI clock */

/* Definitions shall match QCC75x in modules/hal/hal_qcom/qcc7xx/drivers/soc/qcr373/std/include/qcr373_glb.h */
#define QC7XX_SFLASH_CLK_WIFIPLL_120M         (0)
#define QC7XX_SFLASH_CLK_XTAL                 (1)
#define QC7XX_SFLASH_CLK_WIFIPLL_48M          (2)
#define QC7XX_SFLASH_CLK_WIFIPLL_80M          (3)
#define QC7XX_SFLASH_CLK_BCLK                 (4)
#define QC7XX_SFLASH_CLK_WIFIPLL_96M          (5)

/* Definitions shall match QCC75x in modules/hal/hal_qcom/qcc7xx/drivers/soc/qcr373/std/include/qcr373_glb.h */
#define QC7XX_PSRAMB_CLK_BCLK                 (0)
#define QC7XX_PSRAMB_CLK_480M                 (1)
#define QC7XX_PSRAMB_CLK_320M                 (2)
#define QC7XX_PSRAMB_CLK_240M                 (3)

#define QC7XX_SDH_CLK_WIFIPLL_96M             (0) /*!< WIFIPLL_96M as SDH clock */

#define QC7XX_DBI_CLK_MCU_MUXPLL_160M         (0) /*!< Select MCU MUXPLL 160M as DBI clock */
#define QC7XX_DBI_CLK_XCLK                    (1) /*!< Select XCLK as DBI clock */

#define QC7XX_PKA_CLK_MCU_BCLK                (0) /*!< Select MCU_BCLK as PKA clock */
#define QC7XX_PKA_CLK_WIFIPLL_160M            (1) /*!< Select WIFIPLL_160M as PKA clock */

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

/* MTimer clock source definitions should match hal_qcom/qcc7xx/drivers/soc/qcr373/std/include/qcr373_clock.h */
#define QC7XX_MTIMER_CLK_MCU_XCLK             (0)
#define QC7XX_MTIMER_CLK_MCU_ROOT_CLK         (1)

#define QC7XX_USB20_CLK_WIFIPLL               (0) /*!< Select WIFIPLL as USB20 clock */

#define QC7XX_CAM_CLK_XCLK                    (0) /*!< Select XCLK as CAM clock */
#define QC7XX_CAM_CLK_WIFIPLL_96M             (1) /*!< Select WIFIPLL_96M as CAM clock */
#define QC7XX_CAM_CLK_TOP_AUPLL_DIV5          (2) /*!< Select TOP_AUPLL_DIV5 as CAM clock */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_QCR373_H_ */
