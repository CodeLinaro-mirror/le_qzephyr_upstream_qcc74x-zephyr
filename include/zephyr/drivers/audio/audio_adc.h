/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_AUDIO_ADC_H_
#define ZEPHYR_INCLUDE_DRIVERS_AUDIO_ADC_H_

#include <zephyr/kernel.h>

#include <zephyr/audio/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio ADC driver APIs
 * @defgroup audio_adc_interface Audio ADC driver APIs
 * @since 1.0
 * @version 1.0.0
 * @ingroup io_interfaces
 * @{
 */

enum coupling_mode {
	AC,
	DC
};

struct audio_adc_cfg {
	void *buffer;
	size_t buffer_size;
	audio_pcm_rate_t sampling_rate;
	audio_pcm_width_t sample_width;
	uint8_t differential: 1;
	enum coupling_mode coupling_mode;
	uint8_t input_positive;
	uint8_t input_negative;
	uint8_t gain_db;
};

/**
 * @brief Container for audio ADC information specified in devicetree.
 */
struct audio_adc_dt_spec {
	/** Pointer to the device structure for the audio ADC driver instance. */
	const struct device *dev;
};

/**
 * @brief Type definition of audio ADC API configure function.
 * See audio_adc_configure() for argument descriptions.
 */
typedef int (*audio_adc_api_configure)(const struct device *dev, const struct audio_adc_cfg *cfg);

/**
 * @brief Type definition of audio ADC API start function.
 * See audio_adc_start() for argument descriptions.
 */
typedef int (*audio_adc_api_start)(const struct device *dev);

/**
 * @brief Type definition of audio ADC API stop function.
 * See audio_adc_stop() for argument descriptions.
 */
typedef int (*audio_adc_api_stop)(const struct device *dev);

/**
 * @brief Audio ADC driver API
 *
 * This is the mandatory API any audio ADC driver needs to expose.
 */
__subsystem struct audio_adc_driver_api {
	audio_adc_api_configure configure;
	audio_adc_api_start start;
	audio_adc_api_stop stop;
};

/**
 * @brief Configure audio ADC.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param cfg Audio ADC configuration.
 * @retval 0 if successful.
 * @retval negative on error.
 */
__syscall int audio_adc_configure(const struct device *dev, const struct audio_adc_cfg *cfg);

static inline int z_impl_audio_adc_configure(const struct device *dev,
					     const struct audio_adc_cfg *cfg)
{
	return DEVICE_API_GET(audio_adc, dev)->configure(dev, cfg);
}

/**
 * @brief Start audio recording.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @retval 0 if successful.
 * @retval negative on error.
 */
__syscall int audio_adc_start(const struct device *dev);

static inline int z_impl_audio_adc_start(const struct device *dev)
{
	return DEVICE_API_GET(audio_adc, dev)->start(dev);
}

/**
 * @brief Stop audio recording.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @retval 0 if successful.
 * @retval negative on error.
 */
__syscall int audio_adc_stop(const struct device *dev);

static inline int z_impl_audio_adc_stop(const struct device *dev)
{
	return DEVICE_API_GET(audio_adc, dev)->stop(dev);
}

/**
 * @brief Validate that the audio ADC device is ready.
 *
 * @param spec Audio ADC specification from devicetree
 *
 * @retval true if the audio ADC device is ready for use and false otherwise.
 */
static inline bool audio_adc_is_ready_dt(const struct audio_adc_dt_spec *spec)
{
	return device_is_ready(spec->dev);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/audio_adc.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_AUDIO_ADC_H_ */
