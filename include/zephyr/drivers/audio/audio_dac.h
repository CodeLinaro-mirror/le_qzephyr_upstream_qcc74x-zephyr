/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_AUDIO_DAC_H_
#define ZEPHYR_INCLUDE_DRIVERS_AUDIO_DAC_H_

#include <zephyr/kernel.h>

#include <zephyr/audio/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio DAC driver APIs
 * @defgroup audio_dac_interface Audio DAC driver APIs
 * @since 1.0
 * @version 1.0.0
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Container for audio DAC information specified in devicetree.
 */
struct audio_dac_dt_spec {
	/** Pointer to the device structure for the audio DAC driver instance. */
	const struct device *dev;
};

struct audio_dac_cfg {
	void *buffer;
	size_t buffer_size;
	audio_pcm_rate_t sampling_rate;
	audio_pcm_width_t sample_width;
};

/**
 * @brief Type definition of audio DAC API configure function.
 * See audio_dac_configure() for argument descriptions.
 */
typedef int (*audio_dac_api_configure)(const struct device *dev, const struct audio_dac_cfg *cfg);

/**
 * @brief Type definition of audio DAC API start function.
 * See audio_dac_start() for argument descriptions.
 */
typedef int (*audio_dac_api_start)(const struct device *dev);

/**
 * @brief Type definition of audio DAC API stop function.
 * See audio_dac_stop() for argument descriptions.
 */
typedef int (*audio_dac_api_stop)(const struct device *dev);

/**
 * @brief Audio DAC driver API
 *
 * This is the mandatory API any audio DAC driver needs to expose.
 */
__subsystem struct audio_dac_driver_api {
	audio_dac_api_configure configure;
	audio_dac_api_start start;
	audio_dac_api_stop stop;
};

/**
 * @brief Configure audio DAC.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param cfg Audio DAC configuration.
 * @retval 0 if successful.
 * @retval negative on error.
 */
__syscall int audio_dac_configure(const struct device *dev, const struct audio_dac_cfg *cfg);

static inline int z_impl_audio_dac_configure(const struct device *dev,
					     const struct audio_dac_cfg *cfg)
{
	return DEVICE_API_GET(audio_dac, dev)->configure(dev, cfg);
}

/**
 * @brief Start audio playback.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @retval 0 if successful.
 * @retval negative on error.
 */
__syscall int audio_dac_start(const struct device *dev);

static inline int z_impl_audio_dac_start(const struct device *dev)
{
	return DEVICE_API_GET(audio_dac, dev)->start(dev);
}

/**
 * @brief Stop audio playback.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @retval 0 if successful.
 * @retval negative on error.
 */
__syscall int audio_dac_stop(const struct device *dev);

static inline int z_impl_audio_dac_stop(const struct device *dev)
{
	return DEVICE_API_GET(audio_dac, dev)->stop(dev);
}

/**
 * @brief Validate that the audio DAC device is ready.
 *
 * @param spec Audio DAC specification from devicetree
 *
 * @retval true if the audio DAC device is ready for use and false otherwise.
 */
static inline bool audio_dac_is_ready_dt(const struct audio_dac_dt_spec *spec)
{
	return device_is_ready(spec->dev);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/audio_dac.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_AUDIO_DAC_H_ */
