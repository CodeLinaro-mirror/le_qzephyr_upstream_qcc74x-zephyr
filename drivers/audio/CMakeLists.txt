# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library_amend()
zephyr_library_property(ALLOW_EMPTY TRUE)
zephyr_library_sources_ifdef(CONFIG_AUDIO_ADC_QCC74X audio_adc_qcc74x.c)
zephyr_library_sources_ifdef(CONFIG_AUDIO_DAC_QCC74X audio_dac_qcc74x.c)
