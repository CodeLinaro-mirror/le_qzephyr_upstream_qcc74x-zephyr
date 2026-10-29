# Used by QCC-IDE
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

set target-async on
target extended-remote :3333

set history filename gdb_history.log
set history save on
set print pretty on
set pagination off
set output-radix 16

# RISC-V (QCC74x) specifics, mirrored from qcc748m3_evk/board.cmake --gdb-init entries
set mem inaccessible-by-default off
set architecture riscv:rv32
set remotetimeout 250
set print asm-demangle on
set backtrace limit 32

mem 0x22FC0000 0x23010000 rw
mem 0x62FC0000 0x63010000 rw
mem 0x90000000 0x90020000 ro
mem 0xA8000000 0xA8800000 rw
mem 0xA0000000 0xA0400000 ro
