/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_ipc

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <hardware/ipc_reg.h>
#include <qcc75x_glb.h>
#include "qc7xx_ipc.h"

LOG_MODULE_REGISTER(ipm_qcc74x, CONFIG_IPM_LOG_LEVEL);

static struct qc7xx_device_s *ipc_cpu0;
static struct qc7xx_device_s *ipc_cpu1;

__packed struct ipm_qcc74x_control {
	uint16_t dest_cpu_msg_id[2];
	atomic_val_t lock;
};

struct ipm_qcc74x_memory {
	uint8_t *cpu0_2_cpu1_shm;
	uint8_t *cpu1_2_cpu0_shm;
};

struct ipm_qcc74x_config {
	int irq_source_cpu0;
	int irq_priority_cpu0;
	int irq_flags_cpu0;
	int irq_source_cpu1;
	int irq_priority_cpu1;
	int irq_flags_cpu1;
	void (*irq_config)(const struct device *dev);
};

struct ipm_qcc74x_data {
	ipm_callback_t callback;
	void *user_data;
	uint32_t this_core_id;
	uint32_t other_core_id;
	uint32_t shm_size;
	struct ipm_qcc74x_memory shm;
	struct ipm_qcc74x_control *control;
};

static int ipm_qcc74x_send(const struct device *dev, int wait, uint32_t id, const void *data,
			 int size)
{
	struct ipm_qcc74x_data *driver_data = (struct ipm_qcc74x_data *)dev->data;

	if (driver_data->this_core_id == 0) {
		qc7xx_ipc_trig(ipc_cpu1, 1 << id);
	} else {
		qc7xx_ipc_trig(ipc_cpu0, 1 << id);
	}

	return 0;
}

static void ipm_qcc74x_register_cb(const struct device *dev, ipm_callback_t cb, void *user_data)
{
	struct ipm_qcc74x_data *data = (struct ipm_qcc74x_data *)dev->data;

	unsigned int key = irq_lock();
	data->callback = cb;
	data->user_data = user_data;
	irq_unlock(key);
}

static int ipm_qcc74x_max_data_size_get(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0; /* no data transport, only id/signaling */
}

static uint32_t ipm_oapl_max_id_val_get(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 15; /* 16 possible doorbells (0-15) */
}

static int ipm_qcc74x_set_enabled(const struct device *dev, int enable)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enable);
	return 0;
}

static void ipm_qcc74x_isr(const struct device *dev)
{
	struct ipm_oapl_data *driver_data = dev->data;
	uint32_t ipc_intsta = 0;

	if (driver_data->this_core_id == 0) {
		ipc_intsta = qc7xx_ipc_get_intsta(ipc_cpu0);
	} else {
		ipc_intsta = qc7xx_ipc_get_intsta(ipc_cpu1);
	}

	if (driver_data->callback) {
		driver_data->callback(dev, driver_data->user_data, 0, NULL);
	}

	if (driver_data->this_core_id == 0) {
		qc7xx_ipc_clear(ipc_cpu0, ipc_intsta);
	} else {
		qc7xx_ipc_clear(ipc_cpu1, ipc_intsta);
	}
}

static int ipm_qcc74x_init(const struct device *dev)
{
	const struct ipm_qcc74x_config *cfg = dev->config;
	struct ipm_qcc74x_data *data = (struct ipm_qcc74x_data *)dev->data;

	if (GLB_CORE_ID_AP == GLB_Get_Core_Type()) {
		data->this_core_id = 0;
		data->other_core_id = 1;
	} else {
		data->this_core_id = 1;
		data->other_core_id = 0;
	}

	ipc_cpu0 = qc7xx_device_get_by_name("ipc0_ch0");
	if (NULL == ipc_cpu0) {
		printk("ipc0_ch0 get failed\n");
		return -ENODEV;
	}
	qc7xx_ipc_init(ipc_cpu0);
	qc7xx_ipc_int_unmask(ipc_cpu0, IPC_BITS_ALL);

	ipc_cpu1 = qc7xx_device_get_by_name("ipc0_ch1");
	if (NULL == ipc_cpu1) {
		printk("ipc0_ch1 get failed\n");
		return -ENODEV;
	}
	qc7xx_ipc_init(ipc_cpu1);
	qc7xx_ipc_int_unmask(ipc_cpu1, IPC_BITS_ALL);

	if (cfg->irq_config) {
		cfg->irq_config(dev);
	}

	return 0;
}

static DEVICE_API(ipm, ipm_qcc74x_driver_api) = {
	.send = ipm_qcc74x_send,
	.register_callback = ipm_qcc74x_register_cb,
	.max_data_size_get = ipm_qcc74x_max_data_size_get,
	.max_id_val_get = ipm_qcc74x_max_id_val_get,
	.set_enabled = ipm_qcc74x_set_enabled,
};

#define IPM_QCC74X_IRQ_CONNECT(idx, subidx)                                                          \
	COND_CODE_1(DT_INST_IRQ_HAS_IDX(idx, subidx),						  \
	(/* If the 'interrupts' property exists, register the interrupt */	                  \
	 IRQ_CONNECT(DT_INST_IRQ_BY_IDX(idx, subidx, irq),					  \
			 DT_INST_IRQ_BY_IDX(idx, subidx, priority),	                          \
			 ipm_qcc74x_isr,				                                  \
			 DEVICE_DT_INST_GET(idx), 0);					          \
	 irq_enable(DT_INST_IRQ_BY_IDX(idx, subidx, irq));				          \
	), \
	(/* If the 'interrupts' property does not exist, leave this empty */\
	))

#define IPM_QCC74X_IRQ_CONFIG_FUNC(idx)                                                              \
	static void ipm_qcc74x_irq_config_func_##idx(const struct device *dev)                       \
	{                                                                                          \
		if (GLB_CORE_ID_AP == GLB_Get_Core_Type()) {                                       \
			IPM_QCC74X_IRQ_CONNECT(idx, 0);                                              \
		} else {                                                                           \
			IPM_QCC74X_IRQ_CONNECT(idx, 1);                                              \
		}                                                                                  \
	}

#define IPM_QCC74X_SHM_SIZE_BY_IDX(idx) DT_INST_PROP(idx, shared_memory_size)

#define IPM_QCC74X_SHM_ADDR_BY_IDX(idx) DT_REG_ADDR(DT_PHANDLE(DT_DRV_INST(idx), shared_memory))

#define IPM_QCC74X_INIT(idx)                                                                         \
	IPM_QCC74X_IRQ_CONFIG_FUNC(idx)                                                              \
	static struct ipm_qcc74x_config ipm_qcc74x_device_cfg_##idx = {                                \
		.irq_source_cpu0 = DT_INST_IRQ_BY_IDX(idx, 0, irq),                                \
		.irq_priority_cpu0 = DT_INST_IRQ_BY_IDX(idx, 0, priority),                         \
		.irq_flags_cpu0 = DT_INST_IRQ_BY_IDX(idx, 0, flags),                               \
		.irq_source_cpu1 = DT_INST_IRQ_BY_IDX(idx, 1, irq),                                \
		.irq_priority_cpu1 = DT_INST_IRQ_BY_IDX(idx, 1, priority),                         \
		.irq_flags_cpu1 = DT_INST_IRQ_BY_IDX(idx, 1, flags),                               \
		.irq_config = ipm_qcc74x_irq_config_func_##idx,                                      \
	};                                                                                         \
                                                                                                   \
	static struct ipm_qcc74x_data ipm_qcc74x_device_data_##idx = {};                               \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, &ipm_qcc74x_init, NULL, &ipm_qcc74x_device_data_##idx,              \
			      &ipm_qcc74x_device_cfg_##idx, POST_KERNEL,                             \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &ipm_qcc74x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IPM_QCC74X_INIT);
