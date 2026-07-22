/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_QCC75X_INTMUX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_QCC75X_INTMUX_H_

#define IRQ_NUM_BASE 16

/* RISC-V */
#define SSOFT_IRQn 1          /*!< 1 RISCV supervisor software Interrupt */
#define MSOFT_IRQn 3          /*!< 3 RISCV machine software Interrupt */
#define STIME_IRQn 5          /*!< 5 RISCV supervisor time Interrupt */
#define MTIME_IRQn 7          /*!< 7 RISCV machine time Interrupt */
#define SEXT_IRQn 9           /*!< 9 RISCV S-mode external Interrupt */
#define MEXT_IRQn 11          /*!< 11 RISCV M-mode external Interrupt */
#define CLIC_SOFT_PEND_IRQn 12 /*!< 12 RISCV CLIC software pending Interrupt */

#if (__riscv_xlen == 64)
#define HPM_OVF_IRQn 17       /*!< 17 RISCV HPM counter overflow Interrupt */
#endif

/* QC75X */
#define BMX_MCU_BUS_ERR_IRQn (IRQ_NUM_BASE + 0)                    /*!< bmx mcu bus_err_int Interrupt */
#define MJDEC_IRQn (IRQ_NUM_BASE + 1)                              /*!< MJDEC Interrupt */
#define DBI_IRQn (IRQ_NUM_BASE + 2)                                /*!< DBI Interrupt */
#define USB_IRQn (IRQ_NUM_BASE + 3)                                /*!< USB Interrupt */
// TODO: This is identical to 74x except this:
#define WIFI_TO_CPU_IRQn (IRQ_NUM_BASE + 4)                        /*!< WIFI To CPU Interrupt */
#define RF_TOP_INT0_IRQn (IRQ_NUM_BASE + 5)                        /*!< RF_TOP_INT0 Interrupt */
#define RF_TOP_INT1_IRQn (IRQ_NUM_BASE + 6)                        /*!< RF_TOP_INT1 Interrupt */
#define SDIO_IRQn (IRQ_NUM_BASE + 7)                               /*!< SDIO Interrupt */
#define WIFI_TBTT_SLEEP_IRQn (IRQ_NUM_BASE + 8)                    /*!< gauge_itf_int Interrupt */
#define SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC_IRQn (IRQ_NUM_BASE + 9)  /*!< sec_eng_id1 Interrupt */
#define SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC_IRQn (IRQ_NUM_BASE + 10) /*!< sec_eng_id0 Interrupt */
#define SEC_ENG_ID1_CDET_IRQn (IRQ_NUM_BASE + 11)                  /*!< sec_eng_id1_cdet Interrupt */
#define SEC_ENG_ID0_CDET_IRQn (IRQ_NUM_BASE + 12)                  /*!< sec_eng_id0_cdet Interrupt */
#define SF_CTRL_ID1_IRQn (IRQ_NUM_BASE + 13)                       /*!< sf_ctrl_id1 Interrupt */
#define SF_CTRL_ID0_IRQn (IRQ_NUM_BASE + 14)                       /*!< sf_ctrl_id0 Interrupt */
#define DMA0_ALL_IRQn (IRQ_NUM_BASE + 15)                          /*!< DMA0_INTR_ALL Interrupt */
#define DVP2BUS_INT0_IRQn (IRQ_NUM_BASE + 16)                      /*!< DVP2BUS_INTR_ALL Interrupt */
#define SDH_IRQn (IRQ_NUM_BASE + 17)                               /*!< sdh Interrupt */
#define DVP2BUS_INT1_IRQn (IRQ_NUM_BASE + 18)                      /*!< MM System All Interrupt */
#define WIFI_TBTT_WAKEUP_IRQn (IRQ_NUM_BASE + 19)                  /*!< IR TX Interrupt */
#define IRRX_IRQn (IRQ_NUM_BASE + 20)                              /*!< IR RX Interrupt */
#define DMA1_ALL_IRQn (IRQ_NUM_BASE + 21)                          /*!< DMA1_INTR_ALL Interrupt */
#define AUPDM_IRQn (IRQ_NUM_BASE + 22)                             /*!< aupdm_int Interrupt */
#define MJPEG_IRQn (IRQ_NUM_BASE + 23)                             /*!< MJPEG Interrupt */
#define EMAC_IRQn (IRQ_NUM_BASE + 24)                              /*!< EMAC Interrupt */
#define GPADC_DMA_IRQn (IRQ_NUM_BASE + 25)                         /*!< GPADC_DMA Interrupt */
#define EFUSE_IRQn (IRQ_NUM_BASE + 26)                             /*!< Efuse Interrupt */
#define SPI0_IRQn (IRQ_NUM_BASE + 27)                              /*!< SPI0 Interrupt */
#define UART0_IRQn (IRQ_NUM_BASE + 28)                             /*!< UART0 Interrupt */
#define UART1_IRQn (IRQ_NUM_BASE + 29)                             /*!< UART1 Interrupt */
#define UART2_IRQn (IRQ_NUM_BASE + 30)                             /*!< UART2 Interrupt */
#define GPIO_DMA_IRQn (IRQ_NUM_BASE + 31)                          /*!< GPIO DMA Interrupt */
#define I2C0_IRQn (IRQ_NUM_BASE + 32)                              /*!< I2C0 Interrupt */
#define PWM_IRQn (IRQ_NUM_BASE + 33)                               /*!< PWM Interrupt */
#define PEC_INT0_IRQn (IRQ_NUM_BASE + 34)                          /*!< PEC_INT0 Interrupt */
#define PEC_INT1_IRQn (IRQ_NUM_BASE + 35)                          /*!< PEC_INT1 Interrupt */
#define TIMER0_CH0_IRQn (IRQ_NUM_BASE + 36)                        /*!< Timer0 Channel 0 Interrupt */
#define TIMER0_CH1_IRQn (IRQ_NUM_BASE + 37)                        /*!< Timer0 Channel 1 Interrupt */
#define TIMER0_WDT_IRQn (IRQ_NUM_BASE + 38)                        /*!< Timer0 Watch Dog Interrupt */
#define I2C1_IRQn (IRQ_NUM_BASE + 39)                              /*!< I2C1 Interrupt */
#define I2S_IRQn (IRQ_NUM_BASE + 40)                               /*!< I2S Interrupt */
#define ANA_OCP_OUT_TO_CPU_0_IRQn (IRQ_NUM_BASE + 41)              /*!< ana_ocp_out_to_cpu_irq0 Interrupt */
#define RESERVED42_IRQn (IRQ_NUM_BASE + 42)                        /*!< Reserved Interrupt */
#define XTAL_RDY_SCAN_IRQn (IRQ_NUM_BASE + 43)                     /*!< XTAL_RDY_SCAN Interrupt */
#define GPIO_INT0_IRQn (IRQ_NUM_BASE + 44)                         /*!< GPIO Interrupt */
#define DM_IRQn (IRQ_NUM_BASE + 45)                                /*!< DM Interrupt */
#define BT_IRQn (IRQ_NUM_BASE + 46)                                /*!< BT Interrupt */
#define M154_REQ_ACK_IRQn (IRQ_NUM_BASE + 47)                      /*!< M154 req enh ack Interrupt */
#define M154_INT_IRQn (IRQ_NUM_BASE + 48)                          /*!< M154 Interrupt */
#define M154_AES_IRQn (IRQ_NUM_BASE + 49)                          /*!< m154 aes Interrupt */
#define PDS_WAKEUP_IRQn (IRQ_NUM_BASE + 50)                        /*!< PDS Wakeup Interrupt */
#define HBN_OUT0_IRQn (IRQ_NUM_BASE + 51)                          /*!< Hibernate out 0 Interrupt */
#define HBN_OUT1_IRQn (IRQ_NUM_BASE + 52)                          /*!< Hibernate out 1 Interrupt */
#define BOD_IRQn (IRQ_NUM_BASE + 53)                               /*!< BOR Interrupt */
#define WIFI_IRQn (IRQ_NUM_BASE + 54)                              /*!< WIFI To CPU Interrupt */
#define BZ_PHY_INT_IRQn (IRQ_NUM_BASE + 55)                        /*!< BZ phy Interrupt */
#define BLE_IRQn (IRQ_NUM_BASE + 56)                               /*!< BLE Interrupt */
#define MAC_INT_TIMER_IRQn (IRQ_NUM_BASE + 57)                     /*!< MAC Tx Rx Timer Interrupt */
#define MAC_INT_MISC_IRQn (IRQ_NUM_BASE + 58)                      /*!< MAC Tx Rx Misc Interrupt */
#define MAC_INT_RX_TRIGGER_IRQn (IRQ_NUM_BASE + 59)                /*!< MAC Rx Trigger Interrupt */
#define MAC_INT_TX_TRIGGER_IRQn (IRQ_NUM_BASE + 60)                /*!< MAC tx Trigger Interrupt */
#define MAC_INT_GEN_IRQn (IRQ_NUM_BASE + 61)                       /*!< MAC Gen Interrupt */
#define MAC_INT_PROT_TRIGGER_IRQn (IRQ_NUM_BASE + 62)              /*!< MAC Prot Trigger Interrupt */
#define WIFI_IPC_IRQn (IRQ_NUM_BASE + 63)                          /*!< WIFI IPC Interrupt */
#define IPC0_CH0_IRQn (IRQ_NUM_BASE + 64)                          /*!< IPC0 Channel 0 Interrupt */
#define IPC0_CH1_IRQn (IRQ_NUM_BASE + 65)                          /*!< IPC0 Channel 1 Interrupt */
#define IPC1_CH0_IRQn (IRQ_NUM_BASE + 66)                          /*!< IPC1 Channel 0 Interrupt */
#define IPC1_CH1_IRQn (IRQ_NUM_BASE + 67)                          /*!< IPC1 Channel 1 Interrupt */
#define RESERVED68_IRQn (IRQ_NUM_BASE + 68)                        /*!< Reserved Interrupt 68 */
#define RESERVED69_IRQn (IRQ_NUM_BASE + 69)                        /*!< Reserved Interrupt 69 */
#define CANFD_IRQn (IRQ_NUM_BASE + 70)                             /*!< CAN FD Interrupt */
#define RASTER2BLOCK_IRQn (IRQ_NUM_BASE + 87)                      /*!< Raster2Block Interrupt */
#define DTSRC_IRQn (IRQ_NUM_BASE + 88)                             /*!< DTSRC Interrupt */
#define OSD0_IRQn (IRQ_NUM_BASE + 89)                              /*!< OSD0 Interrupt */
#define OSD1_IRQn (IRQ_NUM_BASE + 90)                              /*!< OSD1 Interrupt */

#define IRQn_LAST (IRQ_NUM_BASE + 91                               /*!< Last Interrupt Number */

#define IRQ_DEFAULT_PRIORITY	3

#endif
