/*
 * Copyright (c) 2024 MASSDRIVER EI (massdriver.space)
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 * Not a contribution.
 */

#define DT_DRV_COMPAT qcom_qcc74x_udc

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

#include <hardware/usb_v2_reg.h>
#include <hardware/pds_reg.h>
#include <hardware/glb_reg.h>
#include "udc_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(udc_qcc74x, CONFIG_UDC_DRIVER_LOG_LEVEL);

/* USB speed encoding used in OTG_CSR / speed detection register */
#define USB_QCC7XX_SPEED_LOW  1
#define USB_QCC7XX_SPEED_FULL 0
#define USB_QCC7XX_SPEED_HIGH 2

/* Field masks & bit offsets in FIFO configuration registers (FCFG, FMAP) */
#define USB_QCC7XX_FX_X_MASK   0x3F
#define USB_QCC7XX_FX_X_OFFSET 8

/* Bit offset between consecutive endpoint IN/OUT max packet size registers */
#define USB_QCC7XX_XPS_X_OFFSET 4

/* Hardware FIFO capacity in high-speed mode (bytes) */
#define USB_QCC7XX_HSFIFOCAP (512)

/* Endpoint direction values used in EPMAP registers */
#define USB_QCC7XX_EP_DIR_IN    0
#define USB_QCC7XX_EP_DIR_OUT   1
#define USB_QCC7XX_FIFO_DIR_OUT 0
#define USB_QCC7XX_FIFO_DIR_IN  1
#define USB_QCC7XX_FIFO_DIR_BID 2
#define USB_QCC7XX_FIFO_EP_NONE 15

/* SOF / frame interval timer values after reset */
#define USB_QCC7XX_TIMER_AFTER_RESET_HS (0x44C)
#define USB_QCC7XX_TIMER_AFTER_RESET_FS (0x2710)

/* Reset complete timeout used in software (30 ms) */
#define USB_QCC7XX_TIMER_AFTER_RESET_T K_MSEC(30)

/* Common interrupt bit - often used to detect control transfer completion */
#define USB_MCX_COMEND_INT (1 << 3)

/* Workaround timeout scaling: ~10 ms per 1 KB of transfer */
#define UBFQCC7XX_EVT_CHECK_EP_TIME(size) K_MSEC((int)(size * 10))

/* Number of endpoints */
#define NUM_BIDIR_ENDPOINTS DT_PROP(DT_NODELABEL(usb), num_bidir_endpoints)

/**
 * @brief Static configuration for the Opal USB Device Controller driver
 *
 * Contains hardware base address, interrupt control functions, clock/reset
 * dependencies and pre-allocated endpoint configuration arrays.
 * Most fields are populated from devicetree.
 */
struct udc_qcc74x_config {
	uint32_t base;
	size_t num_of_eps;
	void (*irq_enable_func)(const struct device *const dev);
	void (*irq_disable_func)(const struct device *const dev);
	struct udc_ep_config *ep_cfg_in;
	struct udc_ep_config *ep_cfg_out;
	int speed_idx;
	const struct device *clock_dev;
	const clock_control_subsys_t clock_id;
	const uint8_t clock_source;
	const struct reset_dt_spec reset;
};

/**
 * @brief Private runtime data for the Opal USB driver instance
 *
 * Stores driver state that changes during operation.
 */
struct udc_qcc74x_data {
	bool ep_is_in[NUM_BIDIR_ENDPOINTS];
	bool setup_received;
	uint8_t setup[8];
	k_timepoint_t reset_expiration;
	/* Workaround for the first few packets not triggering the interrupt
	 * when very quickly interacting with the USB */
	uint32_t wa_reset_packet_count;
};

/**
 * @brief Type of asynchronous event scheduled via workqueue
 *
 * Used to defer different kinds of USB event handling from ISR context.
 */
enum udc_qcc74x_ev_type {
	/* Trigger next transfer */
	UBFQCC7XX_EVT_XFER,
	/* Packet dma complete  for ctrl fifo*/
	UBFQCC7XX_EVT_CTRL_END,
	/* Packet dma complete for specific endpoint */
	UBFQCC7XX_EVT_END,
	/* Workaround for interrupts not triggering and other EP timeouts */
	UBFQCC7XX_EVT_CHECK_EP,
};

/**
 * @brief Event descriptor used for delayed USB processing
 *
 * Instances are allocated from @ref udc_qcc74x_ev_slab and scheduled via
 * k_work_delayable on the USB driver's workqueue.
 */
struct udc_qcc74x_ev {
	const struct device *dev;
	uint8_t ep_addr;
	struct k_work_delayable work;
	enum udc_qcc74x_ev_type event;
};

/**
 * @brief Memory slab for allocating @ref udc_qcc74x_ev structures
 *
 * Used to schedule deferred USB event handling from interrupt context.
 * Size is controlled by Kconfig option `CONFIG_UDC_QCC74X_EVENT_COUNT`.
 */
K_MEM_SLAB_DEFINE(udc_qcc74x_ev_slab, sizeof(struct udc_qcc74x_ev), CONFIG_UDC_QCC74X_EVENT_COUNT,
		  sizeof(void *));

/**
 * @brief Submit a delayed or immediate work item for USB event processing
 *
 * @param dev Device instance pointer
 * @param ep_addr Endpoint address (including direction bit)
 * @param event Type of event to process
 * @param delay Delay before executing the work (K_NO_WAIT = immediate)
 */
static void udc_qcc74x_ev_submit(const struct device *const dev, const uint8_t ep_addr,
			       const enum udc_qcc74x_ev_type event, k_timeout_t delay);


static enum udc_bus_speed udc_qcc74x_device_speed(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	uint32_t speed = UDC_BUS_UNKNOWN;

	/* Reset or init ongoing, result would be incorrect */
	while (!sys_timepoint_expired(priv->reset_expiration)) {
		k_msleep(1);
	}

	speed = sys_read32(cfg->base + USB_OTG_CSR_OFFSET);
	speed &= USB_SPD_TYP_HOV_POV_MASK;
	speed = speed >> USB_SPD_TYP_HOV_POV_SHIFT;

	if (speed == USB_QCC7XX_SPEED_FULL) {
		return UDC_BUS_SPEED_FS;
	} else if (speed == USB_QCC7XX_SPEED_HIGH) {
		return UDC_BUS_SPEED_HS;
	} else {
		return UDC_BUS_UNKNOWN;
	}

	return UDC_BUS_UNKNOWN;
}

/**
 * @brief Map physical FIFO index to logical endpoint index (speed dependent)
 *
 * @param dev Device instance pointer
 * @param fifo FIFO index
 *
 * @return Logical endpoint index
 */
static uint8_t udc_qcc74x_fifo_get_ep(const struct device *const dev, const uint8_t fifo)
{
	if (udc_qcc74x_device_speed(dev) == UDC_BUS_SPEED_FS) {
		return fifo;
	}

	if (fifo < 2) {
		return 1;
	}

	return 2;
}

/**
 * @brief Acknowledge completion of control transfer stage
 *
 * Sets CX_DONE bit to tell hardware the control transaction is finished.
 *
 * @param dev Device instance pointer
 */
static void udc_qcc74x_ctrl_ack(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_DEV_CXCFE_OFFSET);
	tmp |= USB_CX_DONE;
	sys_write32(tmp, cfg->base + USB_DEV_CXCFE_OFFSET);
}

/**
 * @brief Send zero-length packet on IN endpoint (status stage)
 *
 * @param dev Device instance pointer
 * @param ep_idx Endpoint index (1-based, not including direction bit)
 */
static void udc_qcc74x_ep_ack(const struct device *const dev, const uint8_t ep_idx)
{
	uint32_t tmp = 0U;
	const struct udc_qcc74x_config *const cfg = dev->config;

	tmp = sys_read32(cfg->base + USB_DEV_INMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp |= USB_TX0BYTE_IEP1;
	sys_write32(tmp,
		    cfg->base + USB_DEV_INMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
}

/**
 * @brief Configure FIFO mapping, type, block count and enable/disable
 *
 * @param dev Device instance pointer
 * @param fifo_idx FIFO index (1..4)
 * @param config Endpoint configuration structure
 * @param block_num Number of 512-byte blocks allocated
 * @param enabled true = enable FIFO, false = disable
 */
static void udc_qcc74x_fifo_configure(const struct device *const dev, const uint8_t fifo_idx,
				    struct udc_ep_config *const config, const uint8_t block_num,
				    const bool enabled)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint8_t ep_type = config->attributes & USB_EP_TRANSFER_TYPE_MASK;
	uint32_t tmp = 0U;

	__ASSERT_NO_MSG(fifo_idx < (NUM_BIDIR_ENDPOINTS));

	tmp = sys_read32(cfg->base + USB_DEV_FCFG_OFFSET);
	tmp &= ~(USB_QCC7XX_FX_X_MASK << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET));
	tmp |= (ep_type << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET + USB_BLK_TYP_F0_SHIFT));
	tmp |= ((block_num - 1) << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET + USB_BLKNO_F0_SHIFT));
	if (config->mps > USB_QCC7XX_HSFIFOCAP) {
		tmp |= (1U << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET + USB_BLKSZ_F0));
	}
	if (enabled) {
		tmp |= (1U << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET + USB_EN_F0));
	} else {
		tmp &= ~(1U << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET + USB_EN_F0));
	}
	sys_write32(tmp, cfg->base + USB_DEV_FCFG_OFFSET);
}

/**
 * @brief Set maximum packet size for OUT endpoint
 *
 * @param dev Device instance pointer
 * @param ep_idx Endpoint index (1-based)
 * @param ep_mps Maximum packet size in bytes
 */
static void udc_qcc74x_ep_set_out_mps(const struct device *const dev, const uint8_t ep_idx,
				    const uint16_t ep_mps)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_DEV_OUTMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp |= USB_RSTG_OEP1;
	sys_write32(tmp,
		    cfg->base + USB_DEV_OUTMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_OUTMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp &= ~USB_RSTG_OEP1;
	sys_write32(tmp,
		    cfg->base + USB_DEV_OUTMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_OUTMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp &= ~USB_MAXPS_OEP1_MASK;
	tmp |= ep_mps;
	sys_write32(tmp,
		    cfg->base + USB_DEV_OUTMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
}

/**
 * @brief Set maximum packet size for IN endpoint
 *
 * @param dev Device instance pointer
 * @param ep_idx Endpoint index (1-based)
 * @param ep_mps Maximum packet size in bytes
 */
static void udc_qcc74x_ep_set_in_mps(const struct device *const dev, const uint8_t ep_idx,
				   const uint16_t ep_mps)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_DEV_INMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp |= USB_RSTG_IEP1;
	sys_write32(tmp,
		    cfg->base + USB_DEV_INMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_INMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp &= ~USB_RSTG_IEP1;
	sys_write32(tmp,
		    cfg->base + USB_DEV_INMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_INMPS1_OFFSET +
			 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
	tmp &= ~USB_MAXPS_IEP1_MASK;
	tmp |= ep_mps;
	tmp &= ~USB_TX_NUM_HBW_IEP1_MASK;
	sys_write32(tmp,
		    cfg->base + USB_DEV_INMPS1_OFFSET + (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
}

/**
 * @brief Reset control endpoint FIFO
 *
 * @param dev Device instance pointer
 */
static void udc_qcc74x_fifo_reset_ctrl(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_DEV_CXCFE_OFFSET);
	tmp |= USB_CX_CLR;
	sys_write32(tmp, cfg->base + USB_DEV_CXCFE_OFFSET);
}

/**
 * @brief Reset selected data FIFO
 *
 * @param dev Device instance pointer
 * @param fifo_idx FIFO index (1..4)
 */
static void udc_qcc74x_fifo_reset(const struct device *const dev, const uint8_t fifo_idx)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_DEV_FIBC0_OFFSET + 4 * (fifo_idx - 1));
	tmp |= USB_FFRST0_HOV;
	sys_write32(tmp, cfg->base + USB_DEV_FIBC0_OFFSET + 4 * (fifo_idx - 1));
}

/**
 * @brief Map endpoint to FIFO (direction specific)
 *
 * @param dev Device instance pointer
 * @param ep_idx Endpoint index (1..4)
 * @param fifo_idx FIFO index (1..4)
 * @param ep_dir USB_QCC7XX_EP_DIR_IN / USB_QCC7XX_EP_DIR_OUT
 */
static void udc_qcc74x_ep_setfifo(const struct device *const dev, const uint8_t ep_idx,
				const uint8_t fifo_idx, const uint8_t ep_dir)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;
	const uint8_t ep_dir_bit = ep_dir * 4;

	if (ep_idx < NUM_BIDIR_ENDPOINTS) {
		tmp = sys_read32(cfg->base + USB_DEV_EPMAP0_OFFSET);
		tmp &= ~(0xf << ((ep_idx - 1) * 8 + ep_dir_bit));
		tmp |= ((fifo_idx - 1) << ((ep_idx - 1) * 8 + ep_dir_bit));
		sys_write32(tmp, cfg->base + USB_DEV_EPMAP0_OFFSET);
	} else {
		tmp = sys_read32(cfg->base + USB_DEV_EPMAP1_OFFSET);
		tmp &= ~(0xf << ((ep_idx - 5) * 8 + ep_dir_bit));
		tmp |= ((fifo_idx - 1) << ((ep_idx - 5) * 8 + ep_dir_bit));
		sys_write32(tmp, cfg->base + USB_DEV_EPMAP1_OFFSET);
	}
}

/**
 * @brief Map FIFO to endpoint and set FIFO direction
 *
 * @param dev Device instance pointer
 * @param ep_idx Endpoint index (1..4)
 * @param fifo_idx FIFO index (1..4)
 * @param fifo_dir USB_QCC7XX_FIFO_DIR_OUT / IN / BID
 */
static void udc_qcc74x_fifo_setep(const struct device *const dev, const uint8_t ep_idx,
				const uint8_t fifo_idx, const uint8_t fifo_dir)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	__ASSERT_NO_MSG(ep_idx < (NUM_BIDIR_ENDPOINTS));
	__ASSERT_NO_MSG(fifo_idx < (NUM_BIDIR_ENDPOINTS));
	__ASSERT_NO_MSG(fifo_dir <= USB_QCC7XX_FIFO_DIR_BID);

	tmp = sys_read32(cfg->base + USB_DEV_FMAP_OFFSET);
	tmp &= ~(USB_QCC7XX_FX_X_MASK << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET));
	tmp |= (ep_idx << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET));
	tmp |= (fifo_dir << ((fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET + USB_DIR_FIFO0_SHIFT));
	sys_write32(tmp, cfg->base + USB_DEV_FMAP_OFFSET);
}

/**
 * @brief Start VDMA read from FIFO to memory buffer (non-control EP)
 *
 * @param dev Device instance pointer
 * @param fifo_idx FIFO index (1..4)
 * @param buf Destination buffer
 * @param len Number of bytes to receive
 */
static void udc_qcc74x_vdma_startread(const struct device *const dev, const uint8_t fifo_idx,
				    uint8_t *const buf, const uint32_t len)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_VDMA_F0PS1_OFFSET +
			 (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);
	tmp &= ~USB_VDMA_LEN_CXF_MASK;
	tmp &= ~USB_VDMA_IO_CXF;
	tmp &= ~USB_VDMA_TYPE_CXF;
	tmp |= (len << USB_VDMA_LEN_CXF_SHIFT);
	sys_write32(tmp,
		    cfg->base + USB_VDMA_F0PS1_OFFSET + (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);

	sys_write32((uint32_t)buf,
		    cfg->base + USB_VDMA_F0PS2_OFFSET + (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);

	tmp = sys_read32(cfg->base + USB_VDMA_F0PS1_OFFSET +
			 (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);
	tmp |= USB_VDMA_START_CXF;
	sys_write32(tmp,
		    cfg->base + USB_VDMA_F0PS1_OFFSET + (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);

	sys_cache_data_flush_and_invd_range(buf, len);
}

/**
 * @brief Start VDMA write from memory to FIFO (non-control EP)
 *
 * @param dev Device instance pointer
 * @param fifo_idx FIFO index (1..4)
 * @param data Source data buffer
 * @param len Number of bytes to transmit
 */
static void udc_qcc74x_vdma_startwrite(const struct device *const dev, const uint8_t fifo_idx,
				     uint8_t *data, const uint32_t len)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	sys_cache_data_flush_and_invd_range(data, len);

	tmp = sys_read32(cfg->base + USB_VDMA_F0PS1_OFFSET +
			 (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);
	tmp &= ~USB_VDMA_LEN_CXF_MASK;
	tmp &= ~USB_VDMA_IO_CXF;
	tmp |= USB_VDMA_TYPE_CXF;
	tmp |= (len << USB_VDMA_LEN_CXF_SHIFT);
	sys_write32(tmp,
		    cfg->base + USB_VDMA_F0PS1_OFFSET + (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);

	sys_write32((uint32_t)data,
		    cfg->base + USB_VDMA_F0PS2_OFFSET + (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);

	tmp = sys_read32(cfg->base + USB_VDMA_F0PS1_OFFSET +
			 (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);
	tmp |= USB_VDMA_START_CXF;
	sys_write32(tmp,
		    cfg->base + USB_VDMA_F0PS1_OFFSET + (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET);
}

/**
 * @brief Start VDMA read for control endpoint (EP0 OUT)
 *
 * @param dev Device instance pointer
 * @param buf Destination buffer
 * @param len Expected number of bytes
 */
static void udc_qcc74x_vdma_startread_ctrl(const struct device *const dev, uint8_t *buf, uint32_t len)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_VDMA_CXFPS1_OFFSET);
	tmp &= ~USB_VDMA_LEN_CXF_MASK;
	tmp &= ~USB_VDMA_IO_CXF;
	tmp &= ~USB_VDMA_TYPE_CXF;
	tmp |= (len << USB_VDMA_LEN_CXF_SHIFT);
	sys_write32(tmp, cfg->base + USB_VDMA_CXFPS1_OFFSET);

	sys_write32((uint32_t)buf, cfg->base + USB_VDMA_CXFPS2_OFFSET);

	priv->ep_is_in[0] = false;

	tmp = sys_read32(cfg->base + USB_VDMA_CXFPS1_OFFSET);
	tmp |= USB_VDMA_START_CXF;
	sys_write32(tmp, cfg->base + USB_VDMA_CXFPS1_OFFSET);

	sys_cache_data_flush_and_invd_range(buf, len);
}

/**
 * @brief Start VDMA write for control endpoint (EP0 IN)
 *
 * @param dev Device instance pointer
 * @param data Source data buffer
 * @param len Number of bytes to send
 */
static void udc_qcc74x_vdma_startwrite_ctrl(const struct device *const dev, uint8_t *data,
					  const uint32_t len)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	uint32_t tmp = 0U;

	sys_cache_data_flush_and_invd_range(data, len);

	tmp = sys_read32(cfg->base + USB_VDMA_CXFPS1_OFFSET);
	tmp &= ~USB_VDMA_LEN_CXF_MASK;
	tmp &= ~USB_VDMA_IO_CXF;
	tmp |= USB_VDMA_TYPE_CXF;
	tmp |= (len << USB_VDMA_LEN_CXF_SHIFT);
	sys_write32(tmp, cfg->base + USB_VDMA_CXFPS1_OFFSET);

	sys_write32((uint32_t)data, cfg->base + USB_VDMA_CXFPS2_OFFSET);

	priv->ep_is_in[0] = true;

	tmp = sys_read32(cfg->base + USB_VDMA_CXFPS1_OFFSET);
	tmp |= USB_VDMA_START_CXF;
	sys_write32(tmp, cfg->base + USB_VDMA_CXFPS1_OFFSET);
}

/**
 * @brief Choose appropriate FIFO for given endpoint based on MPS
 *
 * @param ep_cfg Endpoint configuration structure
 *
 * @return FIFO index (1..4)
 */
static uint8_t udc_qcc74x_ep_get_fifo(struct udc_ep_config *const ep_cfg)
{
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);

	if (ep_cfg->mps > USB_QCC7XX_HSFIFOCAP) {
		if (ep_idx == 1) {
			return 1;
		} else {
			return 3;
		}
	}

	return ep_idx;
}

static int udc_qcc74x_set_address(const struct device *const dev, const uint8_t addr)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	if ((sys_read32(cfg->base + USB_DEV_ADR_OFFSET) & USB_DEVADR_MASK) != addr) {
		LOG_DBG("Set new address %u for %p", addr, dev);
		tmp = sys_read32(cfg->base + USB_DEV_ADR_OFFSET);
		tmp &= ~USB_DEVADR_MASK;
		tmp |= addr;
		sys_write32(tmp, cfg->base + USB_DEV_ADR_OFFSET);
	} else {
		LOG_INF("New address %u for %p already set.", addr, dev);
	}

	return 0;
}

/**
 * @brief Get number of remaining bytes in control endpoint VDMA transfer
 *
 * @param dev Device instance pointer
 *
 * @return Remaining bytes
 */
static uint32_t udc_qcc74x_ctrl_remain(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = (sys_read32(cfg->base + USB_VDMA_CXFPS1_OFFSET) & USB_VDMA_LEN_CXF_MASK);

	return (tmp >> USB_VDMA_LEN_CXF_SHIFT);
}

/**
 * @brief Start reception of SETUP packet on control endpoint
 *
 * @param dev Device instance pointer
 */
static void udc_qcc74x_ctrl_setup_start(const struct device *const dev)
{
	struct udc_qcc74x_data *const priv = udc_get_private(dev);

	/* Read 8-byte SETUP packet into private buffer, then notify stack */
	udc_qcc74x_vdma_startread_ctrl(dev, priv->setup, 8U);
}

/**
 * @brief Start DATA OUT stage on control endpoint
 *
 * @param dev Device instance pointer
 * @param size Expected data length
 */
static void udc_qcc74x_ctrl_dout_start(const struct device *const dev, const uint16_t size)
{
	struct net_buf *buf = NULL;
	struct udc_ep_config *const ep_cfg = udc_get_ep_cfg(dev, USB_CONTROL_EP_OUT);

	LOG_DBG("Ctrl dout start ep 0x%02x", ep_cfg->addr);

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		LOG_ERR("No buffer queued for ctrl OUT");
		return;
	}

	udc_qcc74x_vdma_startread_ctrl(dev, buf->data, size);
}

/**
 * @brief Start DATA IN stage on control endpoint
 *
 * @param dev Device instance pointer
 */
static void udc_qcc74x_ctrl_din_start(const struct device *const dev)
{
	struct net_buf *buf = NULL;
	struct udc_ep_config *const ep_cfg = udc_get_ep_cfg(dev, USB_CONTROL_EP_IN);

	LOG_DBG("Ctrl din start ep 0x%02x", ep_cfg->addr);

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		udc_submit_event(dev, UDC_EVT_ERROR, -ENODATA);
		return;
	}

	LOG_DBG("Start DMA for buf %p, data %p, len %i", (void *)buf, (void *)buf->data, buf->len);

	udc_qcc74x_vdma_startwrite_ctrl(dev, buf->data, buf->len);
}

/**
 * @brief Handle completion of control transfer stage
 *
 * Processes SETUP / DATA / STATUS stages and advances control state machine.
 *
 * @param dev Device instance pointer
 *
 * @return 0 on success, negative error code on failure
 */
static int udc_qcc74x_ctrl_evt_end(const struct device *const dev)
{
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	struct net_buf *buf = NULL;
	int err = 0;

	if (priv->setup_received) {
		/* SETUP stage completed — notify the stack */
		priv->setup_received = false;
		udc_setup_received(dev, priv->setup);
	} else {
		/* DATA or STATUS stage completed — dequeue and submit */
		struct udc_ep_config *ep_cfg_out = udc_get_ep_cfg(dev, USB_CONTROL_EP_OUT);
		struct udc_ep_config *ep_cfg_in = udc_get_ep_cfg(dev, USB_CONTROL_EP_IN);

		buf = udc_buf_get(ep_cfg_out);
		if (buf != NULL) {
			err = udc_submit_ep_event(dev, buf, 0);
		} else {
			buf = udc_buf_get(ep_cfg_in);
			if (buf != NULL) {
				err = udc_submit_ep_event(dev, buf, 0);
			}
		}
	}

	return err;
}

/**
 * @brief Get number of remaining bytes in endpoint VDMA transfer
 *
 * @param dev Device instance pointer
 * @param fifo_idx FIFO index (1..4)
 *
 * @return Remaining bytes
 */
static uint32_t udc_qcc74x_ep_remain(const struct device *const dev, const uint8_t fifo_idx)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = (sys_read32(cfg->base + USB_VDMA_F0PS1_OFFSET +
			  (fifo_idx - 1) * USB_QCC7XX_FX_X_OFFSET) &
	       USB_VDMA_LEN_CXF_MASK);

	return (tmp >> USB_VDMA_LEN_CXF_SHIFT);
}

/**
 * @brief Start OUT transfer on data endpoint
 *
 * @param dev Device instance pointer
 * @param ep_cfg Endpoint configuration
 */
static void udc_qcc74x_ep_dout_start(const struct device *const dev,
				   struct udc_ep_config *const ep_cfg)
{
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	struct net_buf *buf = NULL;
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);

	LOG_DBG("Dout start ep 0x%02x", ep_cfg->addr);

	if (priv->ep_is_in[ep_idx]) {
		LOG_ERR("Unexpected ep 0x%02x dout token", ep_cfg->addr);
	}

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		LOG_ERR("No buffer for OUT ep 0x%02x", ep_cfg->addr);
		udc_submit_event(dev, UDC_EVT_ERROR, -ENOBUFS);
	} else {
		priv->ep_is_in[ep_idx] = false;
		udc_qcc74x_vdma_startread(dev, udc_qcc74x_ep_get_fifo(ep_cfg), buf->data, buf->size);
		if (priv->wa_reset_packet_count > 0) {
			udc_qcc74x_ev_submit(dev, ep_cfg->addr, UBFQCC7XX_EVT_CHECK_EP,
					   UBFQCC7XX_EVT_CHECK_EP_TIME(buf->size));
			priv->wa_reset_packet_count--;
		}
	}
}

/**
 * @brief Start IN transfer on data endpoint
 *
 * @param dev Device instance pointer
 * @param ep_cfg Endpoint configuration
 */
static void udc_qcc74x_ep_din_start(const struct device *const dev,
				  struct udc_ep_config *const ep_cfg)
{
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	struct net_buf *buf = NULL;
	const uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);

	LOG_DBG("Din start ep 0x%02x", ep_cfg->addr);

	if (!priv->ep_is_in[ep_idx]) {
		LOG_ERR("Unexpected ep 0x%02x din token", ep_cfg->addr);
	}

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		LOG_ERR("No buffer for IN ep 0x%02x", ep_cfg->addr);
		udc_submit_event(dev, UDC_EVT_ERROR, -ENOBUFS);
	} else {
		priv->ep_is_in[ep_idx] = true;
		udc_qcc74x_vdma_startwrite(dev, udc_qcc74x_ep_get_fifo(ep_cfg), buf->data, buf->len);
		if (priv->wa_reset_packet_count > 0) {
			udc_qcc74x_ev_submit(dev, ep_cfg->addr, UBFQCC7XX_EVT_CHECK_EP,
					   UBFQCC7XX_EVT_CHECK_EP_TIME(buf->len));
			priv->wa_reset_packet_count--;
		}
	}
}

/**
 * @brief Handle completion of endpoint transfer (non-control)
 *
 * Updates buffer length and submits event to upper layer.
 *
 * @param dev Device instance pointer
 * @param ep_cfg Endpoint configuration
 *
 * @return 0 on success, negative error code otherwise
 */
static int udc_qcc74x_ep_evt_end(const struct device *const dev, struct udc_ep_config *const ep_cfg)
{
	struct net_buf *buf = NULL;
	uint32_t remain = 0U;

	buf = udc_buf_get(ep_cfg);
	LOG_DBG("Event end for 0x%02x got buf %lx, len %u, size %u", ep_cfg->addr, (uintptr_t)buf,
		buf->len, buf->size);
	if (buf == NULL) {
		return -ENODATA;
	}
	if (USB_EP_DIR_IS_OUT(ep_cfg->addr)) {
		remain = udc_qcc74x_ep_remain(dev, udc_qcc74x_ep_get_fifo(ep_cfg));
		LOG_DBG("%u bytes transferred out of %u, %u bytes remaining", ep_cfg->mps - remain,
			ep_cfg->mps, remain);
		net_buf_add(buf, ep_cfg->mps - remain);
	} else {
		net_buf_pull(buf, buf->len);
	}

	return udc_submit_ep_event(dev, buf, 0);
}

/**
 * @brief Work handler – starts next transfer when previous one finished
 *
 * @param dev Device instance pointer
 * @param ep_cfg Endpoint configuration
 */
static void udc_qcc74x_work_handler_xfer(const struct device *const dev,
				       struct udc_ep_config *const ep_cfg)
{
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	const struct net_buf *buf = udc_buf_peek(ep_cfg);
	const uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);

	if (USB_EP_DIR_IS_OUT(ep_cfg->addr)) {
		priv->ep_is_in[ep_idx] = false;
		udc_ep_set_busy(ep_cfg, true);
		udc_qcc74x_ep_dout_start(dev, ep_cfg);
	} else if (buf->len == 0) {
		LOG_DBG("IN: EMPTY LENGTH");
		udc_qcc74x_ep_ack(dev, ep_idx);
		udc_qcc74x_ep_evt_end(dev, ep_cfg);
	} else {
		if (udc_get_buf_info(buf)->zlp) {
			LOG_DBG("IN: ZLP");
		}
		priv->ep_is_in[ep_idx] = true;
		udc_ep_set_busy(ep_cfg, true);
		udc_qcc74x_ep_din_start(dev, ep_cfg);
	}
}

/**
 * @brief Work handler – timeout check / interrupt lost workaround
 *
 * @param dev Device instance pointer
 * @param ep_cfg Endpoint configuration
 */
static void udc_qcc74x_work_handler_check(const struct device *const dev,
					struct udc_ep_config *const ep_cfg)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	int err = 0;
	uint32_t done =
		sys_read32(cfg->base + USB_DEV_ISG3_OFFSET) & (1U << udc_qcc74x_ep_get_fifo(ep_cfg));

	if (udc_ep_is_busy(ep_cfg)) {
		err = udc_qcc74x_ep_evt_end(dev, ep_cfg);
		udc_ep_set_busy(ep_cfg, false);
		if (unlikely(err)) {
			udc_submit_event(dev, UDC_EVT_ERROR, err);
		}
		sys_write32(done, cfg->base + USB_DEV_ISG3_OFFSET);
	} else {
		/* Not busy, interrupt worked, we have nothing to do */
		return;
	}
}

/**
 * @brief Generic delayed work handler for all USB events
 *
 * @param item Work item pointer
 */
static void udc_qcc74x_work_handler(struct k_work *item)
{
	struct k_work_delayable *item_delayable = k_work_delayable_from_work(item);
	const struct udc_qcc74x_ev *const ev = CONTAINER_OF(item_delayable, struct udc_qcc74x_ev, work);
	struct udc_ep_config *const ep_cfg = udc_get_ep_cfg(ev->dev, ev->ep_addr);
	int err = 0;

	LOG_DBG("Dev %p, ep 0x%02x, event %u", ev->dev, ev->ep_addr, ev->event);

	if (unlikely(ep_cfg == NULL)) {
		err = -ENODATA;
		LOG_ERR("Unexpected Invalid Endpoint Configuration in Work Queue");
	} else {
		switch (ev->event) {
		case UBFQCC7XX_EVT_CTRL_END:
			err = udc_qcc74x_ctrl_evt_end(ev->dev);
			break;
		case UBFQCC7XX_EVT_END:
			if (udc_ep_is_busy(ep_cfg)) {
				err = udc_qcc74x_ep_evt_end(ev->dev, ep_cfg);
				udc_ep_set_busy(ep_cfg, false);
			}
			break;
		case UBFQCC7XX_EVT_XFER:
			udc_qcc74x_work_handler_xfer(ev->dev, ep_cfg);
			break;
		case UBFQCC7XX_EVT_CHECK_EP:
			udc_qcc74x_work_handler_check(ev->dev, ep_cfg);
			break;
		default:
			break;
		}
	}

	if (unlikely(err)) {
		udc_submit_event(ev->dev, UDC_EVT_ERROR, err);
	}

	k_mem_slab_free(&udc_qcc74x_ev_slab, (void *)ev);
}

/**
 * @brief Submit a delayed or immediate event for asynchronous USB processing
 *
 * Allocates an event structure from a memory slab, initializes a delayable
 * work item, and schedules it on the USB driver's workqueue.
 *
 * This function is used to defer endpoint transfer start, completion handling,
 * timeout checks (workaround for missed interrupts), etc.
 *
 * @param dev       Pointer to the USB device structure
 * @param ep_addr   Endpoint address (includes direction bit: 0x80 for IN)
 * @param event     Type of event to process (see enum udc_qcc74x_ev_type)
 * @param delay     Timeout before execution (use K_NO_WAIT for immediate)
 */
static void udc_qcc74x_ev_submit(const struct device *const dev, const uint8_t ep_addr,
			       const enum udc_qcc74x_ev_type event, k_timeout_t delay)
{
	struct udc_qcc74x_ev *ev = NULL;
	int ret = 0;

	LOG_DBG("SUBMIT %x %d", ep_addr, event);

	ret = k_mem_slab_alloc(&udc_qcc74x_ev_slab, (void **)&ev, K_NO_WAIT);
	if (ret < 0) {
		udc_submit_event(dev, UDC_EVT_ERROR, ret);
		LOG_ERR("Failed to allocate slab");
		return;
	}

	ev->dev = dev;
	ev->ep_addr = ep_addr;
	ev->event = event;
	k_work_init_delayable(&ev->work, udc_qcc74x_work_handler);
	ret = k_work_schedule_for_queue(udc_get_work_q(), &ev->work, delay);
	if (ret < 0) {
		udc_submit_event(dev, UDC_EVT_ERROR, ret);
		LOG_ERR("Failed to submit event");
		return;
	}
}

static int udc_qcc74x_ep_enqueue(const struct device *const dev, struct udc_ep_config *const config,
			       struct net_buf *buf)
{
	const uint8_t ep_idx = USB_EP_GET_IDX(config->addr);

	LOG_DBG("%p enqueue %p for ep 0x%02x", dev, buf, config->addr);

	if (config->stat.halted) {
		LOG_DBG("ep 0x%02x halted", config->addr);
		return 0;
	}

	if (udc_ep_is_busy(config)) {
		LOG_WRN("ep 0x%02x is busy", config->addr);
		return -EBUSY;
	}

	if (ep_idx == 0) {
		if (USB_EP_DIR_IS_OUT(config->addr)) {
			udc_buf_put(config, buf);
			udc_qcc74x_ctrl_dout_start(dev, buf->size);
		} else if (buf->len == 0) {
			udc_qcc74x_ctrl_ack(dev);
			udc_submit_ep_event(dev, buf, 0);
		} else {
			udc_buf_put(config, buf);
			udc_qcc74x_ctrl_din_start(dev);
		}
	} else {
		if (udc_buf_peek(config) == NULL) {
			udc_buf_put(config, buf);
			udc_qcc74x_work_handler_xfer(dev, config);
		} else {
			udc_buf_put(config, buf);
			udc_qcc74x_ev_submit(dev, config->addr, UBFQCC7XX_EVT_XFER, K_NO_WAIT);
		}
	}

	return 0;
}

static int udc_qcc74x_ep_dequeue(const struct device *const dev, struct udc_ep_config *const cfg)
{
	unsigned int lock_key = 0U;

	lock_key = irq_lock();
	udc_ep_cancel_queued(dev, cfg);
	irq_unlock(lock_key);

	return 0;
}

static int udc_qcc74x_ep_enable(const struct device *const dev, struct udc_ep_config *const config)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	const uint8_t ep_idx = USB_EP_GET_IDX(config->addr);
	uint32_t tmp = 0U;

	LOG_DBG("Enable ep 0x%02x", config->addr);

	if (USB_EP_DIR_IS_OUT(config->addr)) {
		udc_qcc74x_ep_set_out_mps(dev, ep_idx, config->mps);
	} else {
		udc_qcc74x_ep_set_in_mps(dev, ep_idx, config->mps);
	}

	if (config->mps > USB_QCC7XX_HSFIFOCAP) {
		if (ep_idx > 2) {
			LOG_DBG("We need to use 2 FIFO per ep if mps > 512");
			return -ENOTSUP;
		}
		if (ep_idx == 1) {
			udc_qcc74x_ep_setfifo(dev, ep_idx, 1, USB_QCC7XX_EP_DIR_IN);
			udc_qcc74x_ep_setfifo(dev, ep_idx, 1, USB_QCC7XX_EP_DIR_OUT);
			udc_qcc74x_fifo_setep(dev, ep_idx, 1, USB_QCC7XX_FIFO_DIR_BID);
			udc_qcc74x_fifo_setep(dev, ep_idx, 2, USB_QCC7XX_FIFO_DIR_BID);
			udc_qcc74x_fifo_configure(dev, 0, config, 1, true);
			udc_qcc74x_fifo_configure(dev, 1, config, 1, false);
		} else if (ep_idx == 2) {
			udc_qcc74x_ep_setfifo(dev, ep_idx, 3, USB_QCC7XX_EP_DIR_IN);
			udc_qcc74x_ep_setfifo(dev, ep_idx, 3, USB_QCC7XX_EP_DIR_OUT);
			udc_qcc74x_fifo_setep(dev, ep_idx, 3, USB_QCC7XX_FIFO_DIR_BID);
			udc_qcc74x_fifo_setep(dev, ep_idx, 4, USB_QCC7XX_FIFO_DIR_BID);
			udc_qcc74x_fifo_configure(dev, 3, config, 1, true);
			udc_qcc74x_fifo_configure(dev, 4, config, 1, false);
		}
	} else {
		udc_qcc74x_ep_setfifo(dev, ep_idx, ep_idx, USB_QCC7XX_EP_DIR_IN);
		udc_qcc74x_ep_setfifo(dev, ep_idx, ep_idx, USB_QCC7XX_EP_DIR_OUT);
		udc_qcc74x_fifo_setep(dev, ep_idx, ep_idx, USB_QCC7XX_FIFO_DIR_BID);
		udc_qcc74x_fifo_configure(dev, ep_idx, config, 1, true);
	}

	tmp = sys_read32(cfg->base + USB_DEV_ADR_OFFSET);
	tmp |= USB_AFT_CONF;
	sys_write32(tmp, cfg->base + USB_DEV_ADR_OFFSET);

	return 0;
}

static int udc_qcc74x_ep_disable(const struct device *const dev, struct udc_ep_config *const config)
{
	LOG_DBG("Disable ep 0x%02x", config->addr);

	udc_ep_set_busy(config, false);

	return 0;
}

static int udc_qcc74x_ep_set_halt(const struct device *const dev, struct udc_ep_config *const config)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	const uint8_t ep_idx = USB_EP_GET_IDX(config->addr);
	uint32_t tmp = 0U;

	LOG_DBG("Set halt ep 0x%02x", config->addr);

	if (ep_idx == 0) {
		tmp = sys_read32(cfg->base + USB_DEV_CXCFE_OFFSET);
		tmp |= USB_CX_STL;
		sys_write32(tmp, cfg->base + USB_DEV_CXCFE_OFFSET);
	} else {
		if (USB_EP_DIR_IS_OUT(config->addr)) {
			tmp = sys_read32(cfg->base + USB_DEV_OUTMPS1_OFFSET +
					 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
			tmp |= USB_STL_OEP1;
			sys_write32(tmp, cfg->base + USB_DEV_OUTMPS1_OFFSET +
						 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
		} else {
			tmp = sys_read32(cfg->base + USB_DEV_INMPS1_OFFSET +
					 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
			tmp |= USB_STL_IEP1;
			sys_write32(tmp, cfg->base + USB_DEV_INMPS1_OFFSET +
						 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
		}
		config->stat.halted = true;
	}

	return 0;
}

static int udc_qcc74x_ep_clear_halt(const struct device *const dev,
				  struct udc_ep_config *const config)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	const uint8_t ep_idx = USB_EP_GET_IDX(config->addr);
	uint32_t tmp = 0U;

	LOG_DBG("Clear halt ep 0x%02x", config->addr);

	if (ep_idx == 0) {
		tmp = sys_read32(cfg->base + USB_DEV_CXCFE_OFFSET);
		tmp &= ~USB_CX_STL;
		sys_write32(tmp, cfg->base + USB_DEV_CXCFE_OFFSET);
	} else {
		if (USB_EP_DIR_IS_OUT(config->addr)) {
			tmp = sys_read32(cfg->base + USB_DEV_OUTMPS1_OFFSET +
					 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
			tmp &= ~USB_STL_OEP1;
			sys_write32(tmp, cfg->base + USB_DEV_OUTMPS1_OFFSET +
						 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
		} else {
			tmp = sys_read32(cfg->base + USB_DEV_INMPS1_OFFSET +
					 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
			tmp &= ~USB_STL_IEP1;
			sys_write32(tmp, cfg->base + USB_DEV_INMPS1_OFFSET +
						 (ep_idx - 1) * USB_QCC7XX_XPS_X_OFFSET);
		}
		udc_qcc74x_ev_submit(dev, config->addr, UBFQCC7XX_EVT_XFER, K_NO_WAIT);
	}

	config->stat.halted = false;

	return 0;
}

static int udc_qcc74x_host_wakeup(const struct device *const dev)
{
	LOG_DBG("Remote wakeup from %p", dev);

	return -ENOTSUP;
}

static int udc_qcc74x_enable(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	uint32_t tmp = 0U;

	LOG_DBG("Enable device %p", dev);

	tmp = sys_read32(PDS_BASE + PDS_USB_CTL_OFFSET);
	tmp |= PDS_REG_USB_IDDIG_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_CTL_OFFSET);

	/* 'disable global irq' */
	tmp = sys_read32(cfg->base + USB_DEV_CTL_OFFSET);
	tmp &= ~USB_GLINT_EN_HOV;
	sys_write32(tmp, cfg->base + USB_DEV_CTL_OFFSET);

	/* force unplug signal */
	tmp = sys_read32(cfg->base + USB_PHY_TST_OFFSET);
	tmp |= USB_UNPLUG;
	sys_write32(tmp, cfg->base + USB_PHY_TST_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_CTL_OFFSET);
	tmp &= ~USB_CAP_RMWAKUP;
	tmp |= USB_CHIP_EN_HOV;
	if (cfg->speed_idx < UDC_BUS_SPEED_HS) {
		tmp |= USB_FORCE_FS;
	} else {
		tmp &= ~USB_FORCE_FS;
	}
	sys_write32(tmp, cfg->base + USB_DEV_CTL_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_CTL_OFFSET);
	tmp |= USB_SFRST_HOV;
	sys_write32(tmp, cfg->base + USB_DEV_CTL_OFFSET);

	/* wait for soft reset */
	while ((sys_read32(cfg->base + USB_DEV_CTL_OFFSET) & USB_SFRST_HOV) != 0) {
	}

	tmp = sys_read32(cfg->base + USB_DEV_ADR_OFFSET);
	tmp &= ~USB_AFT_CONF;
	sys_write32(tmp, cfg->base + USB_DEV_ADR_OFFSET);

	tmp = sys_read32(cfg->base + USB_DEV_SMT_OFFSET);
	tmp &= ~USB_SOFMT_MASK;
	if (cfg->speed_idx == UDC_BUS_SPEED_HS) {
		tmp |= USB_QCC7XX_TIMER_AFTER_RESET_HS;
	} else {
		tmp |= USB_QCC7XX_TIMER_AFTER_RESET_FS;
	}
	sys_write32(tmp, cfg->base + USB_DEV_SMT_OFFSET);

	/* 'MISGx': Mask Interrupts Source Group x
	 * 'ISGx' : Interrupts Source Group x (set is clear, read is status)
	 */

	/* Clear irqs group 0 */
	sys_write32(0xFFFFFFFFU, cfg->base + USB_DEV_ISG0_OFFSET);
	/* Clear irqs group 1 */
	sys_write32(0xFFFFFFFFU, cfg->base + USB_DEV_ISG1_OFFSET);
	/* Clear irqs group 2 */
	sys_write32(0x3FFU, cfg->base + USB_DEV_ISG2_OFFSET);
	/* Clear irqs group 3 */
	sys_write32(0xFFFFFFFFU, cfg->base + USB_DEV_ISG3_OFFSET);

	/* Enable IRQs in group 0 for setup */
	tmp = sys_read32(cfg->base + USB_DEV_MISG0_OFFSET);
	tmp &= ~(USB_MCX_SETUP_INT);
	tmp |= USB_MCX_COMFAIL_INT | USB_MCX_COMABORT_INT | USB_MCX_COMEND_INT | USB_MCX_IN_INT |
	       USB_MCX_OUT_INT;
	sys_write32(tmp, cfg->base + USB_DEV_MISG0_OFFSET);

	/* Disable IRQs in group 1 (fifo interrupts) */
	sys_write32(0xFFFFFFFFU, cfg->base + USB_DEV_MISG1_OFFSET);

	/* Enable some group 2 interrupts */
	sys_write32(0xFFFFFFE0U, cfg->base + USB_DEV_MISG2_OFFSET);

	/* Enable some group 3 interrupts (DMA completion interrupts) */
	sys_write32(0xFFFFFFE0U, cfg->base + USB_DEV_MISG3_OFFSET);

	/* Enable group irqs */
	tmp = sys_read32(cfg->base + USB_DEV_MIGR_OFFSET);
	tmp &= ~(USB_MINT_G0 | USB_MINT_G1 | USB_MINT_G2 | USB_MINT_G3 | USB_MINT_G4);
	sys_write32(tmp, cfg->base + USB_DEV_MIGR_OFFSET);

	tmp = sys_read32(cfg->base + USB_GLB_INT_OFFSET);
	tmp |= USB_MHC_INT;
	tmp |= USB_MOTG_INT;
	tmp &= ~USB_MDEV_INT;
	sys_write32(tmp, cfg->base + USB_GLB_INT_OFFSET);

	sys_write32(0xFFFFFFFFU, cfg->base + USB_DEV_EPMAP0_OFFSET);
	sys_write32(0xFFU, cfg->base + USB_DEV_EPMAP0_OFFSET);

	for (uint8_t i = 1; i < cfg->num_of_eps; i++) {
		udc_qcc74x_fifo_setep(dev, USB_QCC7XX_FIFO_EP_NONE, i, USB_QCC7XX_FIFO_DIR_OUT);
	}

	for (uint8_t i = 1; i < cfg->num_of_eps; i++) {
		udc_qcc74x_fifo_reset(dev, i);
	}

	/* Enable 'vdma' (virtual dma) */
	tmp = sys_read32(cfg->base + USB_VDMA_CTRL_OFFSET);
	tmp |= USB_VDMA_EN;
	sys_write32(tmp, cfg->base + USB_VDMA_CTRL_OFFSET);

	/* Disable force unplug signal */
	tmp = sys_read32(cfg->base + USB_PHY_TST_OFFSET);
	tmp &= ~USB_UNPLUG;
	sys_write32(tmp, cfg->base + USB_PHY_TST_OFFSET);

	/* Enable global irq */
	tmp = sys_read32(cfg->base + USB_DEV_CTL_OFFSET);
	tmp |= USB_GLINT_EN_HOV;
	sys_write32(tmp, cfg->base + USB_DEV_CTL_OFFSET);

	priv->reset_expiration = sys_timepoint_calc(USB_QCC7XX_TIMER_AFTER_RESET_T);

	return 0;
}

static int udc_qcc74x_disable(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	tmp = sys_read32(cfg->base + USB_DEV_CTL_OFFSET);
	tmp &= ~USB_GLINT_EN_HOV;
	sys_write32(tmp, cfg->base + USB_DEV_CTL_OFFSET);

	tmp = sys_read32(cfg->base + USB_PHY_TST_OFFSET);
	tmp |= USB_UNPLUG;
	sys_write32(tmp, cfg->base + USB_PHY_TST_OFFSET);

	return 0;
}

/**
 * @brief Initialize clock source for USB peripheral
 *
 * @param dev Device instance pointer
 *
 * @return 0 on success, negative errno on failure
 */
static int udc_qcc74x_clock_init(const struct device *const dev)
{
	const struct udc_qcc74x_config *cfg = dev->config;
	int ret = 0;

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	/* Configure clock */
	ret = clock_control_configure(cfg->clock_dev,
				      (clock_control_subsys_t)(uintptr_t)cfg->clock_id, (void *)0);
	if (ret != 0) {
		LOG_ERR("Failed to configure USB clock: %d", ret);
		return ret;
	}
	k_usleep(5);

	/* Enable clock */
	ret = clock_control_on(cfg->clock_dev, (clock_control_subsys_t)(uintptr_t)cfg->clock_id);
	if (ret != 0) {
		if (ret == -EALREADY) {
			LOG_WRN("Clock for USB driver is already enabled");
		} else {
			LOG_ERR("Failed to enable USB clock: %d", ret);
			return ret;
		}
	}

	k_usleep(5);

	return 0;
}

/**
 * @brief Initialize USB PHY hardware
 *
 * @param dev Device instance pointer
 */
static void udc_qcc74x_phy_init(const struct device *const dev)
{
	uint32_t tmp = 0U;

	tmp = sys_read32(PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);
	tmp &= ~PDS_REG_USB_PHY_XTLSEL_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);

	tmp = sys_read32(PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);
	tmp |= PDS_REG_PU_USB20_PSW_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);

	tmp = sys_read32(PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);
	tmp |= PDS_REG_USB_PHY_PONRST_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);

	k_usleep(1);

	/* Enable reset */
	tmp = sys_read32(PDS_BASE + PDS_USB_CTL_OFFSET);
	tmp &= ~PDS_REG_USB_SW_RST_N_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_CTL_OFFSET);

	k_usleep(1);

	/* Unsuspend */
	tmp = sys_read32(PDS_BASE + PDS_USB_CTL_OFFSET);
	tmp |= PDS_REG_USB_EXT_SUSP_N_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_CTL_OFFSET);

	k_msleep(5);

	/* Disable reset */
	tmp = sys_read32(PDS_BASE + PDS_USB_CTL_OFFSET);
	tmp |= PDS_REG_USB_SW_RST_N_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_CTL_OFFSET);

	k_msleep(5);
}

static int udc_qcc74x_init(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	int ret = 0;

	ret = udc_qcc74x_clock_init(dev);
	if (ret < 0) {
		LOG_ERR("Failed to configure clock");
		return ret;
	}

	udc_qcc74x_phy_init(dev);

	/* Set endpoints and fifo mappings to disabled */
	sys_write32(0xFFFFFFFFU, cfg->base + USB_DEV_EPMAP0_OFFSET);
	sys_write32(0xFFU, cfg->base + USB_DEV_EPMAP0_OFFSET);
	udc_qcc74x_fifo_setep(dev, USB_QCC7XX_FIFO_EP_NONE, 1, USB_QCC7XX_FIFO_DIR_OUT);
	udc_qcc74x_fifo_setep(dev, USB_QCC7XX_FIFO_EP_NONE, 2, USB_QCC7XX_FIFO_DIR_OUT);
	udc_qcc74x_fifo_setep(dev, USB_QCC7XX_FIFO_EP_NONE, 3, USB_QCC7XX_FIFO_DIR_OUT);
	udc_qcc74x_fifo_setep(dev, USB_QCC7XX_FIFO_EP_NONE, 4, USB_QCC7XX_FIFO_DIR_OUT);

	udc_qcc74x_fifo_reset_ctrl(dev);
	for (uint8_t i = 1; i < cfg->num_of_eps; i++) {
		udc_qcc74x_fifo_reset(dev, i);
	}

	ret = udc_ep_enable_internal(dev, USB_CONTROL_EP_OUT, USB_EP_TYPE_CONTROL, 64, 0);
	if (ret < 0) {
		LOG_ERR("Failed to enable control endpoint");
		return ret;
	}

	ret = udc_ep_enable_internal(dev, USB_CONTROL_EP_IN, USB_EP_TYPE_CONTROL, 64, 0);
	if (ret < 0) {
		LOG_ERR("Failed to enable control endpoint");
		return ret;
	}

	cfg->irq_enable_func(dev);

	LOG_INF("Initialized");

	return 0;
}

static int udc_qcc74x_shutdown(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	uint32_t tmp = 0U;

	cfg->irq_disable_func(dev);

	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_OUT)) {
		LOG_ERR("Failed to disable control endpoint");
		return -EIO;
	}

	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_IN)) {
		LOG_ERR("Failed to disable control endpoint");
		return -EIO;
	}

	tmp = sys_read32(PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);
	tmp &= ~PDS_REG_USB_PHY_XTLSEL_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);

	tmp = sys_read32(PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);
	tmp &= ~PDS_REG_PU_USB20_PSW_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);

	tmp = sys_read32(PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);
	tmp &= ~PDS_REG_USB_PHY_PONRST_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_PHY_CTRL_OFFSET);

	tmp = sys_read32(PDS_BASE + PDS_USB_CTL_OFFSET);
	tmp &= ~PDS_REG_USB_EXT_SUSP_N_MSK;
	sys_write32(tmp, PDS_BASE + PDS_USB_CTL_OFFSET);

	return 0;
}

/**
 * @brief Pre-initialization step – registers endpoints with UDC core
 *
 * @param dev Device instance pointer
 *
 * @return 0 on success, negative errno on failure
 */
static int udc_qcc74x_driver_preinit(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	struct udc_data *const data = dev->data;
	uint16_t mps = 512U;
	int err = 0;

	k_mutex_init(&data->mutex);

	data->caps.rwup = true;
	data->caps.mps0 = UDC_MPS0_64;
	if (cfg->speed_idx == UDC_BUS_SPEED_HS) {
		data->caps.hs = true;
		mps = 1024;
	}

	for (int i = 0; i < cfg->num_of_eps; i++) {
		cfg->ep_cfg_out[i].caps.out = 1;
		if (i == 0) {
			cfg->ep_cfg_out[i].caps.control = 1;
			cfg->ep_cfg_out[i].caps.mps = 64;
		} else {
			cfg->ep_cfg_out[i].caps.bulk = 1;
			cfg->ep_cfg_out[i].caps.interrupt = 1;
			cfg->ep_cfg_out[i].caps.iso = 1;
			cfg->ep_cfg_out[i].caps.mps = mps;
		}

		cfg->ep_cfg_out[i].addr = USB_EP_DIR_OUT | i;
		err = udc_register_ep(dev, &cfg->ep_cfg_out[i]);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	for (int i = 0; i < cfg->num_of_eps; i++) {
		cfg->ep_cfg_in[i].caps.in = 1;
		if (i == 0) {
			cfg->ep_cfg_in[i].caps.control = 1;
			cfg->ep_cfg_in[i].caps.mps = 64;
		} else {
			cfg->ep_cfg_in[i].caps.bulk = 1;
			cfg->ep_cfg_in[i].caps.interrupt = 1;
			cfg->ep_cfg_in[i].caps.iso = 1;
			cfg->ep_cfg_in[i].caps.mps = mps;
		}

		cfg->ep_cfg_in[i].addr = USB_EP_DIR_IN | i;
		err = udc_register_ep(dev, &cfg->ep_cfg_in[i]);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	LOG_INF("Device %p (max. speed %d)", dev, cfg->speed_idx);

	return 0;
}

static void udc_qcc74x_lock(const struct device *const dev)
{
	udc_lock_internal(dev, K_FOREVER);
}

static void udc_qcc74x_unlock(const struct device *const dev)
{
	udc_unlock_internal(dev);
}

/**
 * @brief USB interrupt service routine
 *
 * Handles all interrupt groups, dispatches events to workqueue when needed.
 *
 * @param dev Device instance pointer
 */
static void udc_qcc74x_isr(const struct device *const dev)
{
	const struct udc_qcc74x_config *const cfg = dev->config;
	struct udc_qcc74x_data *const priv = udc_get_private(dev);
	uint32_t glb_intstatus = 0U;
	uint32_t dev_intstatus = 0U;
	uint32_t group_intstatus = 0U;
	uint32_t tmp = 0U;

	glb_intstatus = sys_read32(cfg->base + USB_GLB_ISR_OFFSET);

	if (glb_intstatus & USB_DEV_INT) {
		dev_intstatus = sys_read32(cfg->base + USB_DEV_IGR_OFFSET);
		if (dev_intstatus & USB_INT_G0) {
			group_intstatus = sys_read32(cfg->base + USB_DEV_ISG0_OFFSET);
			group_intstatus &= ~sys_read32(cfg->base + USB_DEV_MISG0_OFFSET);

			if (group_intstatus & USB_CX_COMABT_INT) {
				udc_submit_event(dev, UDC_EVT_ERROR, -ECANCELED);
				LOG_ERR("Control command abort");
			}

			/* Better to miss a setup than
			 * have the memory leak this creates
			 */
			if (group_intstatus & USB_CX_SETUP_INT && !priv->setup_received) {
				priv->ep_is_in[0] = false;
				priv->setup_received = true;
				udc_qcc74x_ctrl_setup_start(dev);
			} else if (group_intstatus & USB_CX_SETUP_INT) {
				LOG_ERR("Double Setup");
			}

			if (group_intstatus & USB_CX_COMFAIL_INT) {
				udc_submit_event(dev, UDC_EVT_ERROR, -EIO);
				LOG_ERR("Control command Fail");
			}

			/* Clear isr */
			sys_write32(group_intstatus, cfg->base + USB_DEV_ISG0_OFFSET);
		}
		if (dev_intstatus & USB_INT_G1) {
			group_intstatus = sys_read32(cfg->base + USB_DEV_ISG1_OFFSET);
			group_intstatus &= ~sys_read32(cfg->base + USB_DEV_MISG1_OFFSET);

			sys_write32(group_intstatus, cfg->base + USB_DEV_ISG1_OFFSET);
		}
		if (dev_intstatus & USB_INT_G2) {
			group_intstatus = sys_read32(cfg->base + USB_DEV_ISG2_OFFSET);
			group_intstatus &= ~sys_read32(cfg->base + USB_DEV_MISG2_OFFSET);

			/* Suspended */
			if (group_intstatus & USB_SUSP_INT) {
				sys_write32(USB_SUSP_INT, cfg->base + USB_DEV_ISG2_OFFSET);

				udc_qcc74x_fifo_reset_ctrl(dev);
				for (uint8_t i = 1; i < cfg->num_of_eps; i++) {
					udc_qcc74x_fifo_reset(dev, i);
				}

				udc_set_suspended(dev, true);
				udc_submit_event(dev, UDC_EVT_SUSPEND, 0);
			}

			/* Resumed */
			if (group_intstatus & USB_RESM_INT) {
				sys_write32(USB_RESM_INT, cfg->base + USB_DEV_ISG2_OFFSET);

				udc_set_suspended(dev, false);
				udc_submit_event(dev, UDC_EVT_RESUME, 0);
			}

			if (group_intstatus & USBRST_INT) {
				sys_write32(USBRST_INT, cfg->base + USB_DEV_ISG2_OFFSET);

				udc_qcc74x_fifo_reset_ctrl(dev);
				for (uint8_t i = 1; i < cfg->num_of_eps; i++) {
					udc_qcc74x_fifo_reset(dev, i);
				}

				tmp = sys_read32(cfg->base + USB_DEV_SMT_OFFSET);
				tmp &= ~USB_SOFMT_MASK;
				if (cfg->speed_idx == UDC_BUS_SPEED_HS) {
					tmp |= USB_QCC7XX_TIMER_AFTER_RESET_HS;
				} else {
					tmp |= USB_QCC7XX_TIMER_AFTER_RESET_FS;
				}
				sys_write32(tmp, cfg->base + USB_DEV_SMT_OFFSET);

				priv->reset_expiration =
					sys_timepoint_calc(USB_QCC7XX_TIMER_AFTER_RESET_T);

				udc_submit_event(dev, UDC_EVT_RESET, 0);
			}

			if (group_intstatus & USB_ISO_SEQ_ERR_INT) {
				sys_write32(USB_ISO_SEQ_ERR_INT, cfg->base + USB_DEV_ISG2_OFFSET);

				udc_submit_event(dev, UDC_EVT_ERROR, -EIO);
				LOG_ERR("Isosynchronous sequence error");
			}

			if (group_intstatus & USB_ISO_SEQ_ABORT_INT) {
				sys_write32(USB_ISO_SEQ_ABORT_INT, cfg->base + USB_DEV_ISG2_OFFSET);

				udc_submit_event(dev, UDC_EVT_ERROR, -ECANCELED);
				LOG_ERR("Isosynchronous sequence aborted");
			}
		}
		if (dev_intstatus & USB_INT_G3) {
			group_intstatus = sys_read32(cfg->base + USB_DEV_ISG3_OFFSET);
			group_intstatus &= ~sys_read32(cfg->base + USB_DEV_MISG3_OFFSET);
			sys_write32(group_intstatus, cfg->base + USB_DEV_ISG3_OFFSET);

			if (group_intstatus & USB_VDMA_CMPLT_CXF) {
				if (priv->ep_is_in[0]) {
					udc_qcc74x_ev_submit(dev, USB_CONTROL_EP_IN,
							   UBFQCC7XX_EVT_CTRL_END, K_NO_WAIT);
					udc_qcc74x_ctrl_ack(dev);
				} else {
					udc_qcc74x_ev_submit(dev, USB_CONTROL_EP_OUT,
							   UBFQCC7XX_EVT_CTRL_END, K_NO_WAIT);
				}
			}

			for (uint8_t i = 1; i < cfg->num_of_eps; i++) {
				if (group_intstatus & (1U << i)) {
					if (priv->ep_is_in[udc_qcc74x_fifo_get_ep(dev, i)]) {
						udc_qcc74x_ev_submit(
							dev,
							USB_EP_DIR_IN |
								udc_qcc74x_fifo_get_ep(dev, i),
							UBFQCC7XX_EVT_END, K_NO_WAIT);
					} else {
						udc_qcc74x_ev_submit(
							dev,
							USB_EP_DIR_OUT |
								udc_qcc74x_fifo_get_ep(dev, i),
							UBFQCC7XX_EVT_END, K_NO_WAIT);
					}
				}
			}
		}
		if (dev_intstatus & USB_INT_G4) {
			/* Nothing we care about in group 4 */
		}
	}
}

static const struct udc_api udc_qcc74x_api = {
	.lock = udc_qcc74x_lock,
	.unlock = udc_qcc74x_unlock,
	.device_speed = udc_qcc74x_device_speed,
	.init = udc_qcc74x_init,
	.enable = udc_qcc74x_enable,
	.disable = udc_qcc74x_disable,
	.shutdown = udc_qcc74x_shutdown,
	.set_address = udc_qcc74x_set_address,
	.host_wakeup = udc_qcc74x_host_wakeup,
	.ep_enable = udc_qcc74x_ep_enable,
	.ep_disable = udc_qcc74x_ep_disable,
	.ep_set_halt = udc_qcc74x_ep_set_halt,
	.ep_clear_halt = udc_qcc74x_ep_clear_halt,
	.ep_enqueue = udc_qcc74x_ep_enqueue,
	.ep_dequeue = udc_qcc74x_ep_dequeue,
};

#define UDC_QCC74X_DEVICE_DEFINE(n)                                                                  \
	static void udc_irq_enable_func##n(const struct device *const dev)                         \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), udc_qcc74x_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
                                                                                                   \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
                                                                                                   \
	static void udc_irq_disable_func##n(const struct device *const dev)                        \
	{                                                                                          \
		irq_disable(DT_INST_IRQN(n));                                                      \
	}                                                                                          \
                                                                                                   \
	static struct udc_ep_config ep_cfg_out[DT_INST_PROP(n, num_bidir_endpoints)];              \
	static struct udc_ep_config ep_cfg_in[DT_INST_PROP(n, num_bidir_endpoints)];               \
                                                                                                   \
	static const struct udc_qcc74x_config udc_qcc74x_config_##n = {                                \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.num_of_eps = DT_INST_PROP(n, num_bidir_endpoints),                                \
		.ep_cfg_in = ep_cfg_out,                                                           \
		.ep_cfg_out = ep_cfg_in,                                                           \
		.speed_idx = DT_ENUM_IDX(DT_DRV_INST(n), maximum_speed),                           \
		.irq_enable_func = udc_irq_enable_func##n,                                         \
		.irq_disable_func = udc_irq_disable_func##n,                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                    \
		.clock_source = (uint8_t)DT_INST_CLOCKS_CELL(n, source),                           \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
	};                                                                                         \
                                                                                                   \
	static struct udc_qcc74x_data udc_priv_##n = {.setup_received = false,                       \
						    .wa_reset_packet_count = 12};                  \
                                                                                                   \
	static struct udc_data udc_data_##n = {                                                    \
		.mutex = Z_MUTEX_INITIALIZER(udc_data_##n.mutex),                                  \
		.priv = &udc_priv_##n,                                                             \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, udc_qcc74x_driver_preinit, NULL, &udc_data_##n,                     \
			      &udc_qcc74x_config_##n, POST_KERNEL,                                   \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &udc_qcc74x_api);

DT_INST_FOREACH_STATUS_OKAY(UDC_QCC74X_DEVICE_DEFINE)
