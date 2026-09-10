/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <export/csidma_cmd.h>

static int csidma_start_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	cmd_csidma_start((int)argc, argv);
	return 0;
}

static int csidma_stop_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	cmd_csidma_stop((int)argc, argv);
	return 0;
}

static int csidma_ready_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	cmd_csidma_ready((int)argc, argv);
	return 0;
}

static int csidma_debug_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);

	cmd_csidma_debug((int)argc, argv);
	return 0;
}

SHELL_CMD_ARG_REGISTER(csidma_start, NULL,
		       "Start CSI DMA capture: -a <mac> [-b <0-5>] [-i <ms>] [-r <0|1>]",
		       csidma_start_shell_cmd, 1, 12);
SHELL_CMD_ARG_REGISTER(csidma_stop, NULL, "Stop CSI DMA capture [buffer index]",
		       csidma_stop_shell_cmd, 1, 1);
SHELL_CMD_ARG_REGISTER(csidma_ready, NULL, "Re-arm a CSI DMA buffer",
		       csidma_ready_shell_cmd, 2, 0);
SHELL_CMD_ARG_REGISTER(csidma_debug, NULL, "Enable or disable CSI DMA diagnostics",
		       csidma_debug_shell_cmd, 2, 0);
