/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_emac

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/random/random.h>
#include <zephyr/types.h>
#include <ethernet/eth_stats.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/phy.h>

#include <qc7xx_emac.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emac_qcc74x, CONFIG_ETHERNET_LOG_LEVEL);

#define MAC_RESET_TIMEOUT_MS 100
#define EMAC_RESET_PULSE_MS  200
#define EMAC_QCC74X_BUFF_ALIGN 32U
#define EMAC_QCC74X_BUFF_SIZE                                                                        \
	((NET_ETH_MAX_FRAME_SIZE + 4 + 8 + EMAC_QCC74X_BUFF_ALIGN) & ~EMAC_QCC74X_BUFF_ALIGN)

#define QCOM_OUI_B0 0x8C
#define QCOM_OUI_B1 0xFD
#define QCOM_OUI_B2 0xF0

struct emac_qcc74x_buff_s {
	/* tx buff def */
	uint8_t __ALIGNED(EMAC_QCC74X_BUFF_ALIGN)
		emac_tx_buff[CONFIG_ETH_QCC74X_EMAC_TX_BUFF_CNT][EMAC_QCC74X_BUFF_SIZE];
	/* rx buff def */
	uint8_t __ALIGNED(EMAC_QCC74X_BUFF_ALIGN)
		emac_rx_buff[CONFIG_ETH_QCC74X_EMAC_RX_BUFF_CNT][EMAC_QCC74X_BUFF_SIZE];
};

/* Runtime data for a QCC74x EMAC controller instance */
struct emac_qcc74x_dev_data {
	struct net_if *iface;
	struct qc7xx_device_s emac_dev;
	struct emac_qcc74x_buff_s *emac_buff;
	uint8_t mac_addr[6];
	bool mac_random_flag;
	struct k_msgq tx_pool_queue;
	struct k_msgq rx_process_queue;
	K_KERNEL_STACK_MEMBER(rx_thread_stack, CONFIG_ETH_QCC74X_EMAC_RX_THREAD_STACK_SIZE);
	struct k_thread rx_thread;
};

/* Configuration for a QCC74x EMAC controller instance */
struct emac_qcc74x_config {
	const struct device *emac_qcc74x_phy_dev;
	const struct device *clock_dev;
	const struct pinctrl_dev_config *pcfg;
	int rx_clk_invert;
	int ref_clk_output;
	int irq_source;
	int irq_priority;
	void (*irq_config_func)(void);
	const struct gpio_dt_spec reset_gpio; /* GPIO for reset pin */
	uint32_t reg_base;
	struct emac_qcc74x_buff_s *emac_buff; /* DMA buffer (rodata ptr → nocache RAM) */
};

static enum ethernet_hw_caps emac_qcc74x_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);
	return ETHERNET_LINK_100BASE;
}

static int emac_qcc74x_set_config(const struct device *dev, enum ethernet_config_type type,
				const struct ethernet_config *config)
{
	struct emac_qcc74x_dev_data *const dev_data = dev->data;
	int res = 0;

	switch (type) {
	case ETHERNET_CONFIG_TYPE_MAC_ADDRESS:
		memcpy(dev_data->mac_addr, config->mac_address.addr, 6);
		net_if_set_link_addr(dev_data->iface, dev_data->mac_addr,
				     sizeof(dev_data->mac_addr), NET_LINK_ETHERNET);
		res = 0;
		break;
	default:
		res = -ENOTSUP;
		break;
	}

	return res;
}

static int emac_qcc74x_send(const struct device *dev, struct net_pkt *pkt)
{
	struct emac_qcc74x_dev_data *dev_data = dev->data;
	struct qc7xx_emac_trans_desc_s trans_desc;
	size_t len = net_pkt_get_len(pkt);

	if (k_msgq_get(&dev_data->tx_pool_queue, &trans_desc, K_MSEC(10)) < 0) {
		LOG_INF("EMAC has no tx buff");
		return -ENOBUFS;
	}

	if (net_pkt_read(pkt, trans_desc.buff_addr, len) < 0) {
		k_msgq_put(&dev_data->tx_pool_queue, &trans_desc, K_NO_WAIT);
		LOG_ERR("net_pkt_read read error");
		return -EIO;
	}

	trans_desc.data_len = len;
	trans_desc.attr_flag = 0;
	trans_desc.err_status = 0;

	if (qc7xx_emac_queue_tx_push(&dev_data->emac_dev, &trans_desc) < 0) {
		k_msgq_put(&dev_data->tx_pool_queue, &trans_desc, K_NO_WAIT);
		LOG_ERR("qc7xx_emac_queue_tx_push error");
		return -EIO;
	}

	return 0;
}

/**
 * @brief EMAC receive thread
 *
 * Continuously processes received Ethernet frames in a dedicated thread.
 *
 * @param arg1 Pointer to the device structure
 * @param arg2 Unused argument
 * @param arg3 Unused argument
 */
FUNC_NORETURN static void emac_qcc74x_rx_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct emac_qcc74x_dev_data *dev_data = dev->data;
	struct qc7xx_emac_trans_desc_s trans_desc;
	int res = 0;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	LOG_DBG("RX thread start");

	while (1) {
		res = k_msgq_get(&dev_data->rx_process_queue, &trans_desc, K_FOREVER);
		if (res < 0) {
			continue;
		}

		/* Remove crc size */
		trans_desc.data_len -= 4;
		LOG_DBG("RX thread get data:%d", trans_desc.data_len);

		struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(
			dev_data->iface, trans_desc.data_len, AF_UNSPEC, 0, K_MSEC(100));

		if (pkt == NULL) {
			/* Unable to allocate a new pkt */
			eth_stats_update_errors_rx(dev_data->iface);
			LOG_ERR("Could not allocate rx buffer");
		} else if (net_pkt_available_buffer(pkt) < trans_desc.data_len) {
			/* pkt buffer not enough */
			eth_stats_update_errors_rx(dev_data->iface);
			net_pkt_unref(pkt);
			LOG_ERR("pkt buffer not enough");
		} else if (net_pkt_write(pkt, trans_desc.buff_addr, trans_desc.data_len) < 0) {
			/* Unable to write frame into the pkt */
			eth_stats_update_errors_rx(dev_data->iface);
			net_pkt_unref(pkt);
			LOG_ERR("Unable to write frame into the pkt");
		} else if (net_recv_data(dev_data->iface, pkt) < 0) {
			/* Upper layers are not ready to receive packets */
			eth_stats_update_errors_rx(dev_data->iface);
			net_pkt_unref(pkt);
			LOG_ERR("Failed to enqueue frame into rx queue");
		}

		qc7xx_emac_queue_rx_push(&dev_data->emac_dev, &trans_desc);
	}
}

/**
 * @brief EMAC interrupt callback
 *
 * Handles interrupt events for the EMAC device, such as RX and TX completion.
 *
 * @param arg Pointer to the device structure
 * @param irq_event Type of interrupt event
 * @param trans_desc Pointer to the EMAC transaction descriptor
 */
static void lwip_emac_irq_cb(void *arg, uint32_t irq_event,
			     struct qc7xx_emac_trans_desc_s *trans_desc)
{
	const struct device *dev = arg;
	struct emac_qcc74x_dev_data *dev_data = dev->data;

	switch (irq_event) {
	case EMAC_IRQ_EVENT_RX_BUSY:
		LOG_DBG("RX busy");
		break;

	case EMAC_IRQ_EVENT_RX_CTRL_FRAME:
		LOG_INF("RX ctrl frame, drop.");
		trans_desc->attr_flag = 0;
		trans_desc->err_status = 0;
		qc7xx_emac_queue_rx_push(&dev_data->emac_dev, trans_desc);
		break;

	case EMAC_IRQ_EVENT_RX_ERR_FRAME:
		LOG_INF("RX err frame sta %d, drop.", trans_desc->err_status);
		trans_desc->attr_flag = 0;
		trans_desc->err_status = 0;
		qc7xx_emac_queue_rx_push(&dev_data->emac_dev, trans_desc);
		break;

	case EMAC_IRQ_EVENT_RX_FRAME:
		LOG_DBG("RX done");
		if (k_msgq_put(&dev_data->rx_process_queue, trans_desc, K_NO_WAIT) != 0) {
			LOG_ERR("Failed to put data into rx_process_queue");
		}
		break;

	case EMAC_IRQ_EVENT_TX_FRAME:
		LOG_DBG("TX Done");
		if (k_msgq_put(&dev_data->tx_pool_queue, trans_desc, K_NO_WAIT) != 0) {
			LOG_ERR("Failed to put data into tx_pool_queue");
		}
		break;

	case EMAC_IRQ_EVENT_TX_ERR_FRAME:
		if (trans_desc->err_status & (~EMAC_TX_STA_ERR_CS)) {
			LOG_ERR("TX Error:%08x", trans_desc->err_status);
		}
		if (k_msgq_put(&dev_data->tx_pool_queue, trans_desc, K_NO_WAIT) != 0) {
			LOG_ERR("Failed to put data into tx_pool_queue");
		}
		break;

	default:
		break;
	}
}

/**
 * @brief EMAC interrupt service routine
 *
 * Processes interrupts for the EMAC device by calling the low-level ISR
 * handler.
 *
 * @param arg Pointer to the device structure
 */
static void emac_qcc74x_isr(void *arg)
{
	const struct device *dev = arg;
	struct emac_qcc74x_dev_data *dev_data = dev->data;

	void qc7xx_emac_isr(int irq, void *arg);
	qc7xx_emac_isr(0, &dev_data->emac_dev);
}

/**
 * @brief Generate MAC address
 *
 * Generates a MAC address, either randomly or using a default prefix, based on
 * the random_flag.
 *
 * @param mac_addr Array to store the generated MAC address
 * @param random_flag If true, generate a random MAC address; otherwise, use a
 * default prefix
 * @return 0 on success, negative error code on failure
 */
static int generate_mac_addr(uint8_t mac_addr[6], bool random_flag)
{
	if (random_flag) {
		sys_rand_get(&mac_addr[0], 6U);
	} else {
		mac_addr[0] = QCOM_OUI_B0;
		mac_addr[1] = QCOM_OUI_B1;
		mac_addr[2] = QCOM_OUI_B2;

		/* Set MAC address locally administered, unicast (LAA) */
		mac_addr[0] |= 0x02;

		sys_rand_get(&mac_addr[3], 3U);
	}

	return 0;
}

/**
 * @brief Handle PHY link state changes
 *
 * Updates the EMAC device configuration based on changes in the PHY link state.
 *
 * @param phy_dev Pointer to the PHY device
 * @param state Pointer to the PHY link state structure
 * @param user_data Pointer to the EMAC device structure
 */
static void phy_link_state_changed(const struct device *phy_dev, struct phy_link_state *state,
				   void *user_data)
{
	const struct device *dev = (const struct device *)user_data;
	struct emac_qcc74x_dev_data *dev_data = dev->data;

	ARG_UNUSED(phy_dev);

	if (state->is_up) {
		LOG_DBG("EMAC link up");
		k_msgq_purge(&dev_data->tx_pool_queue);
		for (int i = 0; i < CONFIG_ETH_QCC74X_EMAC_TX_BUFF_CNT; i++) {
			struct qc7xx_emac_trans_desc_s tx_desc = {
				.buff_addr = dev_data->emac_buff->emac_tx_buff[i],
			};
			if (k_msgq_put(&dev_data->tx_pool_queue, &tx_desc, K_NO_WAIT) != 0) {
				LOG_ERR("Failed to put data into tx_pool_queue");
			}
		}

		k_msgq_purge(&dev_data->rx_process_queue);
		for (int i = 0; i < CONFIG_ETH_QCC74X_EMAC_RX_BUFF_CNT; i++) {
			struct qc7xx_emac_trans_desc_s rx_desc = {
				.buff_addr = dev_data->emac_buff->emac_rx_buff[i],
			};
			qc7xx_emac_queue_rx_push(&dev_data->emac_dev, &rx_desc);
		}

		switch (state->speed) {
#if (EMAC_SPEED_10M_SUPPORT)
		case LINK_HALF_10BASE:
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_FULL_DUPLEX,
						   false);
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_SPEED_10M, 0);
			break;
		case LINK_FULL_10BASE:
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_FULL_DUPLEX,
						   true);
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_SPEED_10M, 0);
			break;
#endif
		case LINK_HALF_100BASE:
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_FULL_DUPLEX,
						   false);
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_SPEED_100M, 0);
			break;
		case LINK_FULL_100BASE:
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_FULL_DUPLEX,
						   true);
			qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_SPEED_100M, 0);
			break;
		default:
			LOG_ERR("EMAC unsupported speed: %d", state->speed);
			break;
		}

		/* Enable tx and rx */
		qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_TX_EN, true);
		qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_RX_EN, true);
		net_eth_carrier_on(dev_data->iface);
	} else {
		LOG_DBG("EMAC link down");
		net_eth_carrier_off(dev_data->iface);

		/* Disable tx and rx, and clean tx/rx bd */
		qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_TX_EN, false);
		qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_RX_EN, false);
		qc7xx_emac_bd_ctrl_clean(&dev_data->emac_dev);
		k_sleep(K_MSEC(2));
	}
}

/**
 * @brief Initialize the EMAC device
 *
 * Initializes the Qualcomm QCC74x EMAC device, including hardware setup, buffer
 * allocation, MAC address configuration, and optional reset pulse application.
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative error code on failure
 */
int emac_qcc74x_init(const struct device *dev)
{
	const struct emac_qcc74x_config *dev_cfg = dev->config;
	struct emac_qcc74x_dev_data *dev_data = dev->data;
	int res = 0;

	dev_data->emac_buff = dev_cfg->emac_buff;

	/* Apply reset pulse if reset-gpios is specified */
	if (dev_cfg->reset_gpio.port != NULL) {
		if (!device_is_ready(dev_cfg->reset_gpio.port)) {
			LOG_ERR("Reset GPIO device %s not ready", dev_cfg->reset_gpio.port->name);
			return -ENODEV;
		}

		res = gpio_pin_configure_dt(&dev_cfg->reset_gpio, GPIO_OUTPUT);
		if (res < 0) {
			LOG_ERR("Failed to configure reset GPIO: %d", res);
			return res;
		}

		/* Set pin to inactive state (low for ACTIVE_HIGH, high for ACTIVE_LOW) */
		res = gpio_pin_set_dt(&dev_cfg->reset_gpio, 0);
		if (res < 0) {
			LOG_ERR("Failed to set reset GPIO to inactive state: %d", res);
			return res;
		}

		/* Apply reset pulse (active state: high for ACTIVE_HIGH, low for ACTIVE_LOW) */
		res = gpio_pin_set_dt(&dev_cfg->reset_gpio, 1); /* Assert reset */
		if (res < 0) {
			LOG_ERR("Failed to assert reset GPIO: %d", res);
			return res;
		}
		k_msleep(EMAC_RESET_PULSE_MS); /* Hold reset for defined duration */
		res = gpio_pin_set_dt(&dev_cfg->reset_gpio, 0); /* Deassert reset */
		if (res < 0) {
			LOG_ERR("Failed to deassert reset GPIO: %d", res);
			return res;
		}
		LOG_DBG("Applied %d ms reset pulse", EMAC_RESET_PULSE_MS);
	}

	struct qc7xx_emac_config_s emac_cfg = {
		.mac_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		.clk_internal_mode = dev_cfg->ref_clk_output, /* 0 for clock from PHY, 1
								 for clock from inside */
		.md_clk_div = 0xFF,                           /* NOT set here */
		.min_frame_len = NET_ETH_MINIMAL_FRAME_SIZE + 4,
		.max_frame_len = EMAC_QCC74X_BUFF_SIZE,
	};

	LOG_DBG("EMAC %s initialize, max_frame_len: %d", dev->name, emac_cfg.max_frame_len);

	if (emac_cfg.max_frame_len > EMAC_QCC74X_BUFF_SIZE) {
		LOG_ERR("EMAC max_frame_len %d is too long, max %d", emac_cfg.max_frame_len,
			EMAC_QCC74X_BUFF_SIZE);
		return -EINVAL;
	}

	dev_data->emac_dev.reg_base = dev_cfg->reg_base;
	dev_data->emac_dev.irq_num = dev_cfg->irq_source;

	res = pinctrl_apply_state(dev_cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (res < 0) {
		return res;
	}

	res = k_msgq_alloc_init(&dev_data->tx_pool_queue, sizeof(struct qc7xx_emac_trans_desc_s),
				CONFIG_ETH_QCC74X_EMAC_TX_BUFF_CNT);
	if (res < 0) {
		LOG_ERR("Failed to alloc tx_pool_queue");
		return res;
	}

	res = k_msgq_alloc_init(&dev_data->rx_process_queue, sizeof(struct qc7xx_emac_trans_desc_s),
				CONFIG_ETH_QCC74X_EMAC_RX_BUFF_CNT);
	if (res < 0) {
		LOG_ERR("Failed to alloc rx_process_queue");
		return res;
	}

	if (dev_data->mac_random_flag) {
		/* Generate random MAC address */
		res = generate_mac_addr(dev_data->mac_addr, true);
		if (res != 0) {
			LOG_ERR("Failed to generate MAC address");
			return res;
		}
	} else if (net_eth_is_addr_unspecified((struct net_eth_addr *)dev_data->mac_addr)) {
		/* Generate default MAC address */
		res = generate_mac_addr(dev_data->mac_addr, false);
		if (res != 0) {
			LOG_ERR("Failed to generate MAC address");
			return res;
		}
	}
	LOG_INF("Using MAC address: %02x:%02x:%02x:%02x:%02x:%02x", dev_data->mac_addr[0],
		dev_data->mac_addr[1], dev_data->mac_addr[2], dev_data->mac_addr[3],
		dev_data->mac_addr[4], dev_data->mac_addr[5]);

	memcpy(emac_cfg.mac_addr, dev_data->mac_addr, 6);
	qc7xx_emac_init(&dev_data->emac_dev, &emac_cfg);
	if (dev_cfg->rx_clk_invert) {
		qc7xx_emac_feature_control(&dev_data->emac_dev, EMAC_CMD_SET_MAC_RX_CLK_INVERT,
					   true);
	}
	qc7xx_emac_irq_attach(&dev_data->emac_dev, lwip_emac_irq_cb, (void *)dev);

	if (dev_cfg->irq_config_func) {
		dev_cfg->irq_config_func();
	}

	k_tid_t tid = k_thread_create(
		&dev_data->rx_thread, dev_data->rx_thread_stack,
		K_KERNEL_STACK_SIZEOF(dev_data->rx_thread_stack), emac_qcc74x_rx_thread, (void *)dev,
		NULL, NULL, CONFIG_ETH_QCC74X_EMAC_RX_THREAD_PRIORITY, K_ESSENTIAL, K_NO_WAIT);
	if (IS_ENABLED(CONFIG_THREAD_NAME)) {
		k_thread_name_set(tid, "qc7xx_emac_rx");
	}

	LOG_DBG("EMAC %s init success", dev->name);

	return 0;
}

static const struct device *emac_qcc74x_phy_get(const struct device *dev)
{
	const struct emac_qcc74x_config *dev_cfg = dev->config;
	return dev_cfg->emac_qcc74x_phy_dev;
}

static void emac_qcc74x_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	const struct emac_qcc74x_config *dev_cfg = dev->config;
	struct emac_qcc74x_dev_data *dev_data = dev->data;

	dev_data->iface = iface;

	net_if_set_link_addr(iface, dev_data->mac_addr, sizeof(dev_data->mac_addr),
			     NET_LINK_ETHERNET);

	ethernet_init(iface);

	if (device_is_ready(dev_cfg->emac_qcc74x_phy_dev)) {
		phy_link_callback_set(dev_cfg->emac_qcc74x_phy_dev, phy_link_state_changed,
				      (void *)dev);
	} else {
		LOG_ERR("PHY device %s not ready", dev_cfg->emac_qcc74x_phy_dev->name);
	}

	/* Do not start the interface until PHY link is up */
	net_if_carrier_off(iface);
}

static const struct ethernet_api eth_api = {
	.iface_api.init = emac_qcc74x_iface_init,
	.get_capabilities = emac_qcc74x_get_capabilities,
	.set_config = emac_qcc74x_set_config,
	.get_phy = emac_qcc74x_phy_get,
	.send = emac_qcc74x_send,
};

#define EMAC_QCC74X_IRQ_HANDLER(idx)                                                                 \
	static void emac_qcc74x_irq_cfg_func_##idx(void)                                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(idx, 0, irq), DT_INST_IRQ_BY_IDX(idx, 0, priority), \
			    emac_qcc74x_isr, DEVICE_DT_INST_GET(idx), 0);                            \
		irq_enable(DT_INST_IRQ_BY_IDX(idx, 0, irq));                                       \
	}

#define MAC_QCC74X_DEVICE(idx)                                                                       \
	PINCTRL_DT_INST_DEFINE(idx);                                                               \
                                                                                                   \
	EMAC_QCC74X_IRQ_HANDLER(idx)                                                                 \
                                                                                                   \
	static struct emac_qcc74x_buff_s                                                              \
		__attribute__((section(".nocache_noinit_ram")))                                      \
		emac_qcc74x_buff_##idx;                                                              \
                                                                                                   \
	static const struct emac_qcc74x_config emac_qcc74x_config_##idx = {                            \
		.emac_qcc74x_phy_dev = DEVICE_DT_GET(DT_INST_PHANDLE(idx, phy_handle)),              \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(idx)),                              \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),                                       \
		.rx_clk_invert = DT_INST_PROP(idx, rx_clk_invert),                                 \
		.ref_clk_output = DT_INST_PROP(idx, ref_clk_output),                               \
		.irq_source = DT_INST_IRQ_BY_IDX(idx, 0, irq),                                     \
		.irq_priority = DT_INST_IRQ_BY_IDX(idx, 0, priority),                              \
		.irq_config_func = emac_qcc74x_irq_cfg_func_##idx,                                   \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(idx, reset_gpios, {0}),                     \
		.reg_base = DT_INST_REG_ADDR(idx),                                                 \
		.emac_buff = &emac_qcc74x_buff_##idx,                                               \
	};                                                                                         \
                                                                                                   \
	static struct emac_qcc74x_dev_data emac_qcc74x_dev_data_##idx;                              \
                                                                                                   \
	ETH_NET_DEVICE_DT_INST_DEFINE(idx, emac_qcc74x_init, NULL, &emac_qcc74x_dev_data_##idx,        \
				      &emac_qcc74x_config_##idx, CONFIG_ETH_INIT_PRIORITY, &eth_api, \
				      NET_ETH_MTU);

DT_INST_FOREACH_STATUS_OKAY(MAC_QCC74X_DEVICE)