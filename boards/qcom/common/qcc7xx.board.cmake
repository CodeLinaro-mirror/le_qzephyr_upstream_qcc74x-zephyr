# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

board_set_flasher_ifnset(qconn_flash)
# west's generic multi-image reset logic defaults to appending --reset
# whenever the runner declares reset support, unless --no-reset is
# already present. Set that default here so --reset only takes effect
# when the user explicitly passes it on the command line.
board_runner_args(qconn_flash "--no-reset")
board_finalize_runner_args(qconn_flash)

# Resolve the QC7XX HAL SDK path. Prefer the west-provided module dir
# (module name: hal_qualcomm -> ZEPHYR_HAL_QUALCOMM_MODULE_DIR), which is
# layout-independent. Fall back to the legacy sibling path for out-of-tree
# checkouts that place the HAL at modules/hal/hal_qcom.
if(DEFINED ZEPHYR_HAL_QUALCOMM_MODULE_DIR)
  set(QC7XX_SDK_PATH ${ZEPHYR_HAL_QUALCOMM_MODULE_DIR} CACHE STRING "QC7XX SDK path")
elseif(DEFINED ZEPHYR_HAL_QCOM_MODULE_DIR)
  set(QC7XX_SDK_PATH ${ZEPHYR_HAL_QCOM_MODULE_DIR} CACHE STRING "QC7XX SDK path")
else()
  get_filename_component(_hal_qcom_fallback "${ZEPHYR_BASE}/../modules/hal/hal_qcom" ABSOLUTE)
  set(QC7XX_SDK_PATH ${_hal_qcom_fallback} CACHE STRING "QC7XX SDK path")
endif()
assert(QC7XX_SDK_PATH "QC7XX_SDK_PATH is not set")

board_set_debugger_ifnset(jlink)
board_runner_args(jlink "--device=RISC-V" "--iface=JTAG" "--speed=4000")
board_runner_args(jlink "--tool-opt=-noir")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
