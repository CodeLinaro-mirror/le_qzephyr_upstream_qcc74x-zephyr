/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>

#include <antenna_al.h>

#if defined(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_STATIC)
#include <wifi_mgmr_cli.h>
#endif

#if defined(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_STATIC)
static int qc7xx_antenna_scan_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	wifi_antenna_scan_cmd((int)argc, argv);
	return 0;
}

static int qc7xx_antenna_connect_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	wifi_antenna_scan_connect_cmd((int)argc, argv);
	return 0;
}
#endif

static int qc7xx_antenna_status_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	antenna_pair_t pair = antenna_hal_get_selected_pair();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "current=%u count=%u static=%u dynamic=%u",
		    antenna_hal_get_current_antenna(), antenna_hal_get_antenna_count(),
		    antenna_hal_is_static_div_enabled(),
		    antenna_hal_is_dynamic_div_enabled());
	if (pair.valid) {
		shell_print(shell, "selected pair: primary=%u secondary=%u", pair.primary,
			    pair.secondary);
	} else {
		shell_print(shell, "selected pair: none");
	}

	return 0;
}

static int qc7xx_antenna_switch_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	char *end;
	long antenna;
	int ret;

	ARG_UNUSED(argc);

	errno = 0;
	antenna = strtol(argv[1], &end, 0);
	if (errno != 0 || *argv[1] == '\0' || *end != '\0' || antenna < 0 ||
	    antenna >= antenna_hal_get_antenna_count()) {
		shell_error(shell, "antenna must be in range 0..%u",
			    antenna_hal_get_antenna_count() - 1U);
		return -EINVAL;
	}

	ret = antenna_hal_switch_antenna((antenna_id_t)antenna);
	if (ret != 0) {
		shell_error(shell, "failed to select antenna %ld: %d", antenna, ret);
		return ret < 0 ? ret : -EIO;
	}

	shell_print(shell, "selected antenna %ld", antenna);
	return 0;
}

#if defined(CONFIG_QCC74X_WIFI_ANTENNA_DIVERSITY_STATIC)
SHELL_CMD_ARG_REGISTER(wifi_antenna_scan, NULL,
		       "Scan on each antenna: [-s <ssid>] [-c <channels>] "
		       "[-b <bssid>] [-t <dwell_ms>]",
		       qc7xx_antenna_scan_shell_cmd, 1, 8);
SHELL_CMD_ARG_REGISTER(wifi_antenna_connect, NULL,
		       "Scan and connect: [-b <bssid>] [-2|-3|-w] [-f <0|1|2>] "
		       "[-D] [-c <channel>] <ssid> [password]",
		       qc7xx_antenna_connect_shell_cmd, 2, 12);
#endif
SHELL_CMD_ARG_REGISTER(wifi_antenna_status, NULL, "Show antenna diversity status",
		       qc7xx_antenna_status_shell_cmd, 1, 0);
SHELL_CMD_ARG_REGISTER(wifi_antenna_switch, NULL, "Select antenna 0 or 1",
		       qc7xx_antenna_switch_shell_cmd, 2, 0);
