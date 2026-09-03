/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/soc/qualcomm/qcc74x_hbn.h>
#include <zephyr/sys/poweroff.h>

static int hbn_configure(const struct shell *sh, const struct qcc74x_hbn_config *config)
{
	int rc;

	rc = qcc74x_hbn_configure(config);
	if (rc != 0) {
		shell_error(sh, "Failed to configure HBN: %d", rc);
		return rc;
	}

	return 0;
}

static int cmd_hbn_show(const struct shell *sh, size_t argc, char **argv)
{
	struct qcc74x_hbn_wake_info info;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = qcc74x_hbn_wake_info_get(&info);
	if (rc != 0) {
		shell_error(sh, "Failed to read HBN wake information: %d", rc);
		return rc;
	}

	shell_print(sh, "HBN wake: woke=%u raw=0x%08x retained=%u",
		    info.woke_from_hbn, info.raw_hbn_status, info.valid);
	if (!info.valid) {
		shell_print(sh, "HBN retained parameters unavailable");
		return 0;
	}

	shell_print(sh, "HBN retained: reason=0x%08x boot2=%u",
		    info.wakeup_reason, info.boot2_return);
	shell_print(sh, "HBN retained: cycles=%llu rtc=%llu",
			    (unsigned long long)info.requested_sleep_cycles,
			    (unsigned long long)info.rtc_wakeup_count);
	shell_print(sh, "HBN retained: io=0x%02x edge=0x%02x acomp=0x%02x acomp_edge=0x%02x",
			    info.wakeup_io_bits, info.wakeup_io_edge_bits,
			    info.wakeup_acomp_bits, info.wakeup_acomp_edge_bits);

	return 0;
}

static int cmd_hbn_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	qcc74x_hbn_wake_info_clear();
	shell_print(sh, "HBN wake information cleared");

	return 0;
}

static int cmd_hbn_rtc(const struct shell *sh, size_t argc, char **argv)
{
	struct qcc74x_hbn_config config = {0};
	unsigned long long timeout_ms;
	int err = 0;
	int rc;

	ARG_UNUSED(argc);

	timeout_ms = shell_strtoull(argv[1], 0, &err);
	if (err != 0) {
		shell_error(sh, "Invalid timeout: %s", argv[1]);
		return -EINVAL;
	}

	config.timeout_ms = timeout_ms;

	rc = hbn_configure(sh, &config);
	if (rc != 0) {
		return rc;
	}

	shell_print(sh, "Entering QCC74x HBN0 with RTC wake in %llu ms", timeout_ms);
	sys_poweroff();

	CODE_UNREACHABLE;
}

static int cmd_hbn_acomp(const struct shell *sh, size_t argc, char **argv)
{
	struct qcc74x_hbn_config config = {0};
	enum qcc74x_hbn_acomp_trigger trigger;
	unsigned long gpio;
	int err = 0;
	int rc;

	ARG_UNUSED(argc);

	gpio = shell_strtoul(argv[1], 0, &err);
	if ((err != 0) || (gpio > UINT8_MAX)) {
		shell_error(sh, "Invalid GPIO: %s", argv[1]);
		return -EINVAL;
	}

	if (strcmp(argv[2], "falling") == 0) {
		trigger = QCC74X_HBN_ACOMP_TRIGGER_FALLING;
	} else if (strcmp(argv[2], "rising") == 0) {
		trigger = QCC74X_HBN_ACOMP_TRIGGER_RISING;
	} else if (strcmp(argv[2], "both") == 0) {
		trigger = QCC74X_HBN_ACOMP_TRIGGER_BOTH;
	} else {
		shell_error(sh, "Invalid ACOMP edge: %s", argv[2]);
		return -EINVAL;
	}

	config.acomp.enabled = true;
	config.acomp.input_pin = (uint8_t)gpio;
	config.acomp.trigger = trigger;

	rc = hbn_configure(sh, &config);
	if (rc != 0) {
		return rc;
	}

	shell_print(sh, "Entering QCC74x HBN0 with GPIO%lu %s ACOMP wake",
		    gpio, argv[2]);
	sys_poweroff();

	CODE_UNREACHABLE;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_hbn,
	SHELL_CMD_ARG(show, NULL, "Show captured HBN wake information", cmd_hbn_show, 1, 0),
	SHELL_CMD_ARG(clear, NULL, "Clear captured HBN wake information", cmd_hbn_clear, 1, 0),
	SHELL_CMD_ARG(rtc, NULL,
		      "Enter HBN0 with RTC wake: hbn rtc <timeout_ms>",
		      cmd_hbn_rtc, 2, 0),
	SHELL_CMD_ARG(acomp, NULL,
		      "Enter HBN0 with ACOMP: hbn acomp <gpio> <falling|rising|both>",
		      cmd_hbn_acomp, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(hbn, &sub_hbn, "QCC74x HBN commands", NULL);
