/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef QCC74X_PM_UART_H_
#define QCC74X_PM_UART_H_

#include <zephyr/toolchain.h>

int qcc74x_pm_uart_preflight(void);
int qcc74x_pm_uart_check_suspended(void);
FUNC_NORETURN void qcc74x_pm_uart_fail(int rc);

#endif
