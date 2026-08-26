/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_flash_controller

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>

#include <qc7xx_flash.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_qcc74x, CONFIG_FLASH_LOG_LEVEL);

#define SOC_NV_FLASH_NODE  DT_INST(0, soc_nv_flash)
#define FLASH_WRITE_BLK_SZ DT_PROP(SOC_NV_FLASH_NODE, write_block_size)
#define FLASH_ERASE_BLK_SZ DT_PROP(SOC_NV_FLASH_NODE, erase_block_size)
#define FLASH_SEM_TIMEOUT  (k_is_in_isr() ? K_NO_WAIT : K_FOREVER)

/**
 * @brief Device configuration structure for the Opal flash driver.
 *
 * This structure is currently empty as no runtime configuration is required.
 */
struct flash_qcc74x_dev_config {
};

/**
 * @brief Device data structure for the Opal flash driver.
 *
 * This structure holds runtime data for the driver, including synchronization
 * primitives when multithreading is enabled.
 */
struct flash_qcc74x_dev_data {
#ifdef CONFIG_MULTITHREADING
	/** Semaphore for synchronizing access to the flash hardware. */
	struct k_sem sem;
#endif
};

/**
 * @brief Flash parameters for the Opal device.
 *
 * This constant structure defines the write block size and erase value
 * for the flash memory.
 */
static const struct flash_parameters flash_qcc74x_parameters = {
	.write_block_size = FLASH_WRITE_BLK_SZ,
	.erase_value = 0xff,
};

#ifdef CONFIG_MULTITHREADING
/**
 * @brief Acquire the semaphore for flash access.
 *
 * This function takes the semaphore to ensure exclusive access to the flash.
 *
 * @param dev Pointer to the device structure.
 */
static inline void flash_qcc74x_sem_take(const struct device *dev)
{
	struct flash_qcc74x_dev_data *data = dev->data;

	k_sem_take(&data->sem, FLASH_SEM_TIMEOUT);
}

/**
 * @brief Release the semaphore for flash access.
 *
 * This function gives back the semaphore after flash operations.
 *
 * @param dev Pointer to the device structure.
 */
static inline void flash_qcc74x_sem_give(const struct device *dev)
{
	struct flash_qcc74x_dev_data *data = dev->data;

	k_sem_give(&data->sem);
}
#else

#define flash_qcc74x_sem_take(dev)                                                                   \
	do {                                                                                       \
	} while (0)
#define flash_qcc74x_sem_give(dev)                                                                   \
	do {                                                                                       \
	} while (0)

#endif /* CONFIG_MULTITHREADING */

static int flash_qcc74x_read(const struct device *dev, off_t address, void *buffer, size_t length)
{
	int ret = 0;

	flash_qcc74x_sem_take(dev);
	ret = qc7xx_flash_read(address, buffer, length);
	flash_qcc74x_sem_give(dev);

	if (ret != 0) {
		LOG_ERR("flash_read failed %d", ret);
		return -EIO;
	}
	return 0;
}

static int flash_qcc74x_write(const struct device *dev, off_t address, const void *buffer,
			    size_t length)
{
	int ret = 0;

	flash_qcc74x_sem_take(dev);
	ret = qc7xx_flash_write(address, (uint8_t *)buffer, length);
	flash_qcc74x_sem_give(dev);

	if (ret != 0) {
		LOG_ERR("flash_write failed %d", ret);
		return -EIO;
	}
	return 0;
}

static int flash_qcc74x_erase(const struct device *dev, off_t start, size_t len)
{
	int ret = 0;

	flash_qcc74x_sem_take(dev);
	ret = qc7xx_flash_erase(start, len);
	flash_qcc74x_sem_give(dev);
	if (ret != 0) {
		LOG_ERR("flash_erase_region failed %d", ret);
		return -EIO;
	}
	return 0;
}

#if CONFIG_FLASH_PAGE_LAYOUT
/**
 * @brief Flash pages layout for the Opal device.
 *
 * This constant structure defines the number of pages and page size for
 * the flash memory layout.
 */
static const struct flash_pages_layout flash_qcc74x_pages_layout = {
	.pages_count = DT_REG_SIZE(SOC_NV_FLASH_NODE) / FLASH_ERASE_BLK_SZ,
	.pages_size = DT_PROP(SOC_NV_FLASH_NODE, erase_block_size),
};

void flash_qcc74x_page_layout(const struct device *dev, const struct flash_pages_layout **layout,
			    size_t *layout_size)
{
	*layout = &flash_qcc74x_pages_layout;
	*layout_size = 1;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static const struct flash_parameters *flash_qcc74x_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &flash_qcc74x_parameters;
}

/**
 * @brief Initialize the flash driver.
 *
 * This function initializes the underlying Opal flash hardware and
 * sets up the semaphore if multithreading is enabled.
 *
 * @param dev Pointer to the device structure.
 * @return 0 on success, 0 even on failure (as per original code; consider revising).
 */
static int flash_qcc74x_init(const struct device *dev)
{
	uint32_t ret = 0;
#ifdef CONFIG_MULTITHREADING
	struct flash_qcc74x_dev_data *const dev_data = dev->data;

	k_sem_init(&dev_data->sem, 1, 1);
#endif /* CONFIG_MULTITHREADING */
	ret = qc7xx_flash_init();
	if (ret != 0) {
		LOG_ERR("flash init failed %d", ret);
		return 0;
	}
	return 0;
}

/**
 * @brief Flash driver API structure.
 *
 * This structure defines the function pointers for the Zephyr flash API
 * implementation.
 */
static DEVICE_API(flash, flash_qcc74x_driver_api) = {
	.read = flash_qcc74x_read,
	.write = flash_qcc74x_write,
	.erase = flash_qcc74x_erase,
	.get_parameters = flash_qcc74x_get_parameters,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = flash_qcc74x_page_layout,
#endif
};

static struct flash_qcc74x_dev_data flash_qcc74x_data;

DEVICE_DT_INST_DEFINE(0, flash_qcc74x_init, NULL, &flash_qcc74x_data, NULL, POST_KERNEL,
		      CONFIG_FLASH_INIT_PRIORITY, &flash_qcc74x_driver_api);
