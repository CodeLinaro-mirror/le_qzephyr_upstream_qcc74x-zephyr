# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_include_directories(${ZEPHYR_BASE}/drivers/pwm)
zephyr_library_sources_ifdef(CONFIG_PWM_QCC74X pwm_qcc74x.c)
