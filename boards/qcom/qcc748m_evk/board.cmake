# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0

# Windows: replace gdb-py.exe with gdb.exe to fix Ctrl+C after 'continue'
if(CMAKE_HOST_WIN32 AND DEFINED CMAKE_GDB)
  get_filename_component(_gdb_dir  "${CMAKE_GDB}" DIRECTORY)
  get_filename_component(_gdb_name "${CMAKE_GDB}" NAME)
  if(_gdb_name MATCHES ".*-py\\.exe$")
    string(REPLACE "-py.exe" ".exe" _gdb_name_no_py "${_gdb_name}")
    set(_gdb_no_py "${_gdb_dir}/${_gdb_name_no_py}")
    if(EXISTS "${_gdb_no_py}")
      set(CMAKE_GDB "${_gdb_no_py}" CACHE FILEPATH "Path to GDB" FORCE)
    endif()
  endif()
endif()

# OpenOCD debug runner via CH347 board-mounted USB-JTAG
# Requires J3 jumpers (pins 3-10) installed and openocd.exe in PATH
board_runner_args(openocd --cmd-pre-init "source [find qcc748m.cfg]")
board_runner_args(openocd --use-elf --no-load --no-init)
# QCC748 workaround: 'reset init' triggers second JTAG scan which fails after reset;
# replace with 'halt' to avoid re-scanning the JTAG chain.
board_runner_args(openocd "--cmd-reset-halt=catch { halt }")
board_runner_args(openocd --gdb-init "set mem inaccessible-by-default off")
board_runner_args(openocd --gdb-init "set architecture riscv:rv32")
board_runner_args(openocd --gdb-init "set remotetimeout 250")
board_runner_args(openocd --gdb-init "set print asm-demangle on")
board_runner_args(openocd --gdb-init "set backtrace limit 32")
board_runner_args(openocd --gdb-init "mem 0x22FC0000 0x23010000 rw")
board_runner_args(openocd --gdb-init "mem 0x62FC0000 0x63010000 rw")
board_runner_args(openocd --gdb-init "mem 0x90000000 0x90020000 ro")
board_runner_args(openocd --gdb-init "mem 0xA8000000 0xA8800000 rw")
board_runner_args(openocd --gdb-init "mem 0xA0000000 0xA0400000 ro")
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/../common/qcc7xx.board.cmake)
