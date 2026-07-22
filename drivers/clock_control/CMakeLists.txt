# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Contains code under the following license:
#
# Not a contribution.
#
# Copyright (c) Zephyr Project members and individual contributors.
# SPDX-License-Identifier: Apache-2.0.

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_CLOCK_CONTROL_QCC74X	
	clock_control_qcc74x.c
	qcc7xx_clk_common_ll.c
	qcc7xx_clk_dig_ll.c
	qcc7xx_clk_pwm_ll.c
	qcc7xx_clk_spi_ll.c
	qcc7xx_clk_timer_ll.c
	qcc7xx_clk_uart_ll.c)