# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3

"""
QCC74x script for west runner for flashing with QConn_Flash_Cmd.

This script provides possibility to flash applications onto
QCC74x boards using the QConn_Flash_Cmd tool via Zephyr's tools. It automates
the process of generating a temporary configuration and executing the
flashing command.
"""

# Disable warnings related to import errors
# pylint: disable=E0401

# Disable warnings related to too much arguments passed into ctor(s)
# pylint: disable=R0902,R0913,R0917

import os
import platform
import tempfile
import configparser
from time import sleep
from pathlib import Path
from runners.core import RunnerCaps, ZephyrBinaryRunner

DEFAULT_BAUD_RATE = "2000000"
DEFAULT_INTERFACE = "uart"
# HAL location relative to the workspace root (ccwiot layout).
SDK_REL_PATH = Path("modules/hal/qcom/qcc74x")
FLASH_REL_PATH = Path("tools/qc7xx_tools/QConn_Flash")
# map platform.system() with QConn_Flash executable name
FLASH_CMD_BY_SYSTEM = {
    "Windows": "QConn_Flash_Cmd.exe",
    "Darwin":  "QConn_Flash_Cmd-macos",
    "Linux":   "QConn_Flash_Cmd-ubuntu",
}

def find_workspace_root(start_dir, markers=("zephyr", "modules"), max_levels=10):
    """Walk up from start_dir and return the workspace root that contains
    the given marker directories. Returns None if not found.

    start_dir: directory to start searching from (string)
    markers: directory names that should exist in workspace root
    max_levels: how many levels up to check (avoid infinite loops)
    """
    current = os.path.abspath(start_dir)
    for _ in range(max_levels):
        parent = os.path.dirname(current)
        if parent == current:
            break
        if all(os.path.exists(os.path.join(parent, m)) for m in markers):
            return parent
        current = parent

    # Fallback: when twister runs with --short-build-path it points build_dir
    # at a junction/symlink under twister-out/twister_links/, whose path
    # string has no workspace markers above it. west exports ZEPHYR_BASE
    # (= <workspace>/zephyr) into the runner's environment; its parent is the
    # workspace root that holds the zephyr/ and modules/ markers.
    zephyr_base = os.environ.get("ZEPHYR_BASE")
    if zephyr_base:
        candidate = os.path.dirname(os.path.abspath(zephyr_base))
        if all(os.path.exists(os.path.join(candidate, m)) for m in markers):
            return candidate

    return None


class QconnFlashRunner(ZephyrBinaryRunner):
    """Qconn_Flash_Cmd runner."""

    def __init__(self, cfg, qconn_flash_cmd, interface, port, baud_rate, reset, chipname):
        """Initializes the QconnFlashRunner."""
        super().__init__(cfg)
        self.interface = interface
        self.qconn_flash_cmd = qconn_flash_cmd
        self.port = port
        self.baud_rate = baud_rate
        self.reset = reset
        self.chipname = chipname

    @classmethod
    def name(cls):
        """Returns the runner's name."""
        return "qconn_flash"

    @classmethod
    def capabilities(cls):
        """Defines the runner's capabilities."""
        return RunnerCaps(commands={"flash"}, reset=True)

    @classmethod
    def do_add_parser(cls, parser):
        """Adds command-line arguments for this runner."""
        # Required
        parser.add_argument('--port', required=True,
                            help='Serial port to use for flashing.')
        # Optional
        parser.add_argument("--interface", default=DEFAULT_INTERFACE)
        parser.add_argument("--baud_rate", default=DEFAULT_BAUD_RATE)
        parser.add_argument("--chipname", required=True,
                            help='Chip name for QConn_Flash_Cmd.')
        parser.add_argument(
            "--qconn_flash_cmd",
            help="Path to QConn_Flash_Cmd executable (auto-detected if not specified)")

    @classmethod
    def auto_detect_qconn_flash_cmd(cls, cfg):
        """Auto-detect full path to QConn_Flash_Cmd.

        Returns a str path or raises RuntimeError.
        """
        build_dir = cfg.build_dir
        workspace_root = find_workspace_root(build_dir)
        if not workspace_root:
            raise RuntimeError(f"Could not find workspace root from build directory: {build_dir}")

        sdk_path = Path(workspace_root) / SDK_REL_PATH
        if not sdk_path.exists():
            raise RuntimeError(f"SDK path not found: {sdk_path}")

        flash_cmd_name = FLASH_CMD_BY_SYSTEM.get(platform.system())
        if not flash_cmd_name:
            raise RuntimeError(f"Unsupported system {platform.system()}")

        qconn_flash_cmd = sdk_path / FLASH_REL_PATH / flash_cmd_name
        return str(qconn_flash_cmd)

    @classmethod
    def do_create(cls, cfg, args):
        """Creates an instance of the runner."""

        qconn_flash_cmd = args.qconn_flash_cmd
        if not qconn_flash_cmd:
            qconn_flash_cmd = cls.auto_detect_qconn_flash_cmd(cfg)
            print(f"Auto-detected QConn Flash Command: {qconn_flash_cmd}")

        return cls(cfg, qconn_flash_cmd=qconn_flash_cmd,
                   interface=args.interface, port=args.port,
                   baud_rate=args.baud_rate, reset=bool(args.reset),
                   chipname=args.chipname)

    def do_run(self, _command, **_kwargs):
        """Executes the flash command."""
        self.require(self.qconn_flash_cmd)
        self.ensure_output('bin')

        zephyr_out_dir = os.path.join(self.cfg.build_dir, 'zephyr')
        boot2_file = os.path.join(zephyr_out_dir, 'boot2_*.bin')
        partition_file = os.path.join(zephyr_out_dir, 'partition.bin')
        flash_prog_config_file_template = os.path.join(self.cfg.board_dir, 'flash_prog_cfg.ini')

        config = configparser.ConfigParser()
        config.read(flash_prog_config_file_template)
        config.set('boot2', 'filedir', boot2_file)
        config.set('partition', 'filedir', partition_file)
        config.set('partition1', 'filedir', partition_file)
        config.set('FW', 'filedir', self.cfg.bin_file)

        with (tempfile.NamedTemporaryFile(mode='w', suffix='.ini', delete_on_close=False)) as tmp_cfg:
            config.write(tmp_cfg)
            tmp_cfg_path = tmp_cfg.name
            self.logger.info(
                f"Generated temporary flash config: {tmp_cfg_path}")
            tmp_cfg.flush()
            cmd_flash = [self.qconn_flash_cmd,
                         '--interface', self.interface,
                         '--port', self.port,
                         '--baudrate', self.baud_rate,
                         '--config', tmp_cfg_path,
                         '--chipname', self.chipname]
            if self.reset:
                cmd_flash.append('--warm_reset')
            self.logger.info(f"Flash binary, cmd_flash: {cmd_flash}")
            self.check_call(cmd_flash)
