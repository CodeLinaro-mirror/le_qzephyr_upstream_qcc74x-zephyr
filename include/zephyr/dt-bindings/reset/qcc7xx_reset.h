/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef QCOM_QCC7XX_RESET_H_
#define QCOM_QCC7XX_RESET_H_

/* Peripheral reset ids
 *
 * These correspond to the GLB_AHB_MCU_SW_... defines in HAL (qcc74x_glb.h,
 * qcc75x_glb.h), but as those headers are not suitable for including in an
 * application, these are provided here. */
#define QC7XX_RESET_RSV0        (0)
#define QC7XX_RESET_RSV1        (1)
#define QC7XX_RESET_WIFI        (4)
#define QC7XX_RESET_BTDM        (8)
#define QC7XX_RESET_ZIGBEE      (9)
#define QC7XX_RESET_BLE2        (10)
#define QC7XX_RESET_ZIGBEE2     (11)
#define QC7XX_RESET_EMI_MISC    (16)
#define QC7XX_RESET_MM_MISC     (17)
#define QC7XX_RESET_PSRAM1_CTRL (18)
#define QC7XX_RESET_EXT_USB     (19)
#define QC7XX_RESET_EXT_AUPWM   (21)
#define QC7XX_RESET_EXT_SDH     (22)
#define QC7XX_RESET_EXT_EMAC    (23)
#define QC7XX_RESET_D2XA        (25)
#define QC7XX_RESET_D2XB        (26)
#define QC7XX_RESET_JENC        (27)
#define QC7XX_RESET_GLB         (32)
#define QC7XX_RESET_MIX         (33)
#define QC7XX_RESET_GPIP        (34)
#define QC7XX_RESET_SEC_DBG     (35)
#define QC7XX_RESET_SEC_ENG     (36)
#define QC7XX_RESET_TZ1         (37)
#define QC7XX_RESET_RSV38       (38)
#define QC7XX_RESET_EFUSE       (39)
#define QC7XX_RESET_RSV40       (40)
#define QC7XX_RESET_PERI        (41)
#define QC7XX_RESET_RSV42       (42)
#define QC7XX_RESET_SF          (43)
#define QC7XX_RESET_DMA         (44)
#define QC7XX_RESET_SDU         (45)
#define QC7XX_RESET_PDS         (46)
#define QC7XX_RESET_RSV47       (47)
#define QC7XX_RESET_UART0       (48)
#define QC7XX_RESET_UART1       (49)
#define QC7XX_RESET_SPI         (50)
#define QC7XX_RESET_I2C0        (51)
#define QC7XX_RESET_PWM         (52)
#define QC7XX_RESET_TIMER       (53)
#define QC7XX_RESET_IR_REMOTE   (54)
#define QC7XX_RESET_CHECKSUM    (55)
#define QC7XX_RESET_DBI         (56)
#define QC7XX_RESET_I2C1        (57)
#define QC7XX_RESET_I2S         (59)
#define QC7XX_RESET_AUSOLO      (60)
#define QC7XX_RESET_RSV61       (61)
#define QC7XX_RESET_RSV62       (62)
#define QC7XX_RESET_RSV63       (63)
#define QC7XX_RESET_PWRON       (64)
#define QC7XX_RESET_CPU         (65)
#define QC7XX_RESET_SYS         (66)
#define QC7XX_RESET_CHIP        (69)

#endif
