# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

if(CONFIG_BUILD_WITH_TFM AND CONFIG_TFM_BL2)
	# BL2 carries a 4 KiB boot header before the XIP base; use that same
	# origin for signed images so tfm_merged.hex maps to raw flash offsets.
	math(EXPR QCR373_TFM_HEX_BASE_ADDRESS "${CONFIG_FLASH_BASE_ADDRESS} - 0x1000" OUTPUT_FORMAT HEXADECIMAL)
	math(EXPR TFM_HEX_BASE_ADDRESS_S "${QCR373_TFM_HEX_BASE_ADDRESS} + 0x20000" OUTPUT_FORMAT HEXADECIMAL)
	math(EXPR TFM_HEX_BASE_ADDRESS_NS "${QCR373_TFM_HEX_BASE_ADDRESS} + 0x40000" OUTPUT_FORMAT HEXADECIMAL)

	set_property(TARGET runners_yaml_props_target PROPERTY hex_file tfm_merged.hex)
	set_property(TARGET runners_yaml_props_target PROPERTY bin_file tfm_merged.bin)
endif()

board_runner_args(qconn_flash "--chipname=qcr373")

include(${CMAKE_CURRENT_LIST_DIR}/../common/qcc7xx.board.cmake)
