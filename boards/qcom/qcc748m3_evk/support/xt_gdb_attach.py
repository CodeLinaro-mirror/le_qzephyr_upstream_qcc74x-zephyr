#!/usr/bin/env python3
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
r"""
xt_gdb_attach.py - Start XuanTie DebugServer and attach GDB to QCC748m

Usage:
    python xt_gdb_attach.py [--elf build\zephyr\zephyr.elf]
    python xt_gdb_attach.py [--gdb_client <path_to_gdb_client> --xt_server <path_to_xtserver> --elf <path_to_elf>]
    python xt_gdb_attach.py --test [--elf build\zephyr\zephyr.elf]
"""

import argparse
import os
import subprocess
import sys
import time

# ── defaults ──────────────────────────────────────────────────────────────────
SCRIPT_DIR       = os.path.dirname(os.path.abspath(__file__))
WORKSPACE_ROOT   = os.path.normpath(os.path.join(SCRIPT_DIR, *[".."] * 5))
DEFAULT_GDBCLI   = ""
DEFAULT_XTSERVER = ""
DEFAULT_ELF      = os.path.join(WORKSPACE_ROOT, "build", "zephyr", "zephyr.elf")

# ── argument parser ───────────────────────────────────────────────────────────
parser = argparse.ArgumentParser(description="Attach GDB to QCC748m via XuanTie DebugServer")
parser.add_argument("--gdb_client", default=DEFAULT_GDBCLI,
                    help=f"Path to riscv64-zephyr-elf-gdb.exe (default: {DEFAULT_GDBCLI})")
parser.add_argument("--xt_server",  default=DEFAULT_XTSERVER,
                    help=f"Path to DebugServerConsole.exe (default: {DEFAULT_XTSERVER})")
parser.add_argument("--elf",        default=DEFAULT_ELF,
                    help="Path to zephyr.elf (default: <workspace>/build/zephyr/zephyr.elf)")
parser.add_argument("--test",       action="store_true",
                    help="Test mode: run bt + info registers in batch mode and exit")
args = parser.parse_args()

# ── validate ──────────────────────────────────────────────────────────────────
for label, path in [("gdb_client", args.gdb_client),
                    ("xt_server",  args.xt_server),
                    ("elf",        args.elf)]:
    if not os.path.exists(path):
        print(f"ERROR: {label} not found: {path}", file=sys.stderr)
        sys.exit(1)

# ── start DebugServerConsole in a new window ──────────────────────────────────
print(f"Starting XuanTie DebugServer: {args.xt_server}")
server_proc = subprocess.Popen(
    [args.xt_server],
    creationflags=subprocess.CREATE_NEW_CONSOLE
)

time.sleep(1)

# ── launch GDB ────────────────────────────────────────────────────────────────
if args.test:
    # ── test mode: batch GDB, capture output, exit ────────────────────────────
    gdb_args = [args.gdb_client, "--batch", "--quiet",
                "-x", os.path.join(SCRIPT_DIR, "xt_gdbinit_test"),
                args.elf]
    try:
        result = subprocess.run(gdb_args, capture_output=True, text=True, timeout=5)
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        output = (e.stdout or "") + (e.stderr or "")
    finally:
        if server_proc.poll() is None:
            server_proc.kill()
    print(output)
    sys.exit(0)
else:
    # ── interactive mode: launch GDB with gdbinit ─────────────────────────────
    print(f"Launching GDB: {args.elf}")
    try:
        subprocess.run([args.gdb_client, "-x", os.path.join(SCRIPT_DIR, "xt_gdbinit"), args.elf])
    except KeyboardInterrupt:
        pass

    # ── fallback cleanup (hook-quit in xt_gdbinit already does taskkill) ──────
    if server_proc.poll() is None:
        server_proc.kill()
