/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qcom_qcc74x_bt_hci

#include <zephyr/kernel.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/bluetooth/controller.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>

#include "common/bt_str.h"
#include <btble_inc/btble_lib_api.h>
#include <btble_inc/hci_onchip.h>
#include <rfparam_adapter.h>

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_hci_qcc74x);

#define HCI_RX_BUF_SIZE BT_BUF_RX_SIZE

#define BT_HCI_EVT_CC_PARAM_OFFSET  0x05
#define BT_HCI_CCEVT_HDR_PARLEN     0x03
#define BT_HCI_CSEVT_LEN            0x06
#define BT_HCI_CSVT_PARLEN          0x04
#define BT_HCI_EVT_LE_PARAM_OFFSET  0x02
#define BT_HCI_EVT_DBG_PARAM_OFFSET 0x02

/**
 * @struct bt_hci_evt_hdr_c
 * @brief Packed structure for HCI event header with variable value field.
 *
 * This structure represents a compact HCI event header used for internal
 * processing, including the event code, length, and a small variable buffer.
 */
struct qcc74x_bt_hci_evt_hdr_c {
	uint8_t evt;
	uint8_t len;
	uint8_t val[4];
} __packed;

/**
 * @struct hci_driver_data
 * @brief Driver-specific data structure.
 *
 * Holds the callback function for receiving HCI packets from the controller.
 */
struct qcc74x_hci_driver_data {
	bt_hci_recv_t recv;
};

static struct k_fifo recv_fifo;
static const struct device *g_dev;

/**
 * @brief Convert controller event to host HCI event format.
 *
 * This function maps internal controller event types to standard HCI event
 * formats for the host stack.
 *
 * @param hdr Pointer to the HCI event header structure to fill.
 * @param type Type of the event (e.g., BT_HCI_CMD_CMP_EVT).
 * @param src_id Source ID or opcode depending on event type.
 * @param param Event parameters.
 * @param param_len Length of the parameters.
 * @return 0 on success.
 */
static uint8_t qcc74x_ctrl_evt_2_host(struct qcc74x_bt_hci_evt_hdr_c *hdr, uint8_t type,
				    uint16_t src_id, uint8_t *param, uint8_t param_len)
{
	if (type == BT_HCI_CMD_CMP_EVT) {
		hdr->evt = BT_HCI_EVT_CMD_COMPLETE;
		hdr->len = BT_HCI_CCEVT_HDR_PARLEN + 2;
		hdr->val[0] = 0x01;
		sys_put_le16(src_id, &hdr->val[1]);
	} else if (type == BT_HCI_CMD_STAT_EVT) {
		hdr->evt = BT_HCI_EVT_CMD_STATUS;
		hdr->len = BT_HCI_CSVT_PARLEN + 2;
		hdr->val[0] = param[0];
		hdr->val[1] = 0x01;
		sys_put_le16(src_id, &hdr->val[2]);
	} else if (type == BT_HCI_LE_EVT) {
		hdr->evt = BT_HCI_EVT_LE_META_EVENT;
		hdr->len = 2;
	} else if (type == BT_HCI_EVT) {
		hdr->evt = src_id;
		hdr->len = 2;
	} else if (type == BT_HCI_DBG_EVT) {
		hdr->evt = BT_HCI_EVT_VENDOR;
		hdr->len = 2;
	}
	return 0;
}

/**
 * @brief Send HCI packet to the controller via on-chip interface.
 *
 * Prepares and sends HCI commands or ACL data to the Opal controller.
 *
 * @param buf Network buffer containing the HCI packet.
 * @return 0 on success, negative error code on failure.
 */
static int qcc74x_onchiphci_send_2_controller(struct net_buf *buf)
{
	uint16_t opcode;
	uint16_t dest_id = 0x00;
	uint8_t buf_type;
	uint8_t pkt_type;
	hci_pkt_struct pkt;

	buf_type = bt_buf_get_type(buf);
	LOG_DBG("type:%d", buf_type);
	switch (buf_type) {
	case BT_BUF_CMD: {
		struct bt_hci_cmd_hdr *chdr;

		if (buf->len < sizeof(struct bt_hci_cmd_hdr)) {
			return -EINVAL;
		}
		chdr = (void *)buf->data;
		if (buf->len < chdr->param_len) {
			return -EINVAL;
		}
		pkt_type = BT_HCI_CMD;
		opcode = sys_le16_to_cpu(chdr->opcode);
		/* move buf to the payload */
		net_buf_pull(buf, sizeof(struct bt_hci_cmd_hdr));
		pkt.p.hci_cmd.opcode = opcode;
		pkt.p.hci_cmd.param_len = chdr->param_len;
		pkt.p.hci_cmd.params = buf->data;
		break;
	}

	case BT_BUF_ACL_OUT: {
		struct bt_hci_acl_hdr *acl;
		/* Connhandle +l2cap field */
		uint16_t connhdl_l2cf, tlt_len;

		if (buf->len < sizeof(struct bt_hci_acl_hdr)) {
			return -EINVAL;
		}

		pkt_type = BT_HCI_ACL_DATA;
		acl = (void *)buf->data;
		tlt_len = sys_le16_to_cpu(acl->len);
		connhdl_l2cf = sys_le16_to_cpu(acl->handle);
		/* move buf to the payload */
		net_buf_pull(buf, sizeof(struct bt_hci_acl_hdr));

		if (buf->len < tlt_len) {
			return -EINVAL;
		}

		/* Get connection_handle */
		dest_id = bt_acl_handle(connhdl_l2cf);
		pkt.p.acl_data.conhdl = dest_id;
		pkt.p.acl_data.pb_bc_flag = bt_acl_flags(connhdl_l2cf);
		pkt.p.acl_data.len = tlt_len;
		pkt.p.acl_data.buffer = (uint8_t *)buf->data;

		break;
	}
#if defined(CONFIG_BT_CTLR_ISO_TX_BUFFERS)
	case BT_BUF_ISO_OUT:
		err = iso_handle(buf);
		break;
#endif
	default:
		return -EINVAL;
	}

	return bt_onchiphci_send(pkt_type, dest_id, &pkt);
}

static int qcc74x_hci_driver_send(const struct device *dev, struct net_buf *buf)
{
	if (!buf->len) {
		LOG_ERR("Empty HCI packet");
		return -EINVAL;
	}
	LOG_DBG("%s", bt_hex(buf->data, buf->len));

	int err = qcc74x_onchiphci_send_2_controller(buf);
	if (!err) {
		net_buf_unref(buf);
	}
	LOG_DBG("Exit: %d", err);
	return err;
}

/**
 * @brief Check if an event packet is discardable.
 *
 * Determines if certain events (like advertising reports) can be safely
 * discarded if no buffer is available.
 *
 * @param hdr HCI event header.
 * @param param Event parameters.
 * @param param_len Length of parameters.
 * @return true if discardable, false otherwise.
 */
static bool event_packet_is_discardable(struct qcc74x_bt_hci_evt_hdr_c *hdr, uint8_t *param,
					uint8_t param_len)
{
	switch (hdr->evt) {
	case BT_HCI_EVT_LE_META_EVENT: {
		struct bt_hci_evt_le_meta_event *me = (void *)hdr->val;
		switch (me->subevent) {
		case BT_HCI_EVT_LE_ADVERTISING_REPORT:
			return true;
#if defined(CONFIG_BT_EXT_ADV)
		case BT_HCI_EVT_LE_EXT_ADVERTISING_REPORT: {
			const struct bt_hci_evt_le_ext_advertising_report *ext_adv =
				(void *)&param[1];

			return (ext_adv->num_reports == 1) &&
			       ((ext_adv->adv_info->evt_type & BT_HCI_LE_ADV_EVT_TYPE_LEGACY) != 0);
		}
#endif
		default:
			return false;
		}
	}
	case BT_HCI_EVT_VENDOR: {
		uint8_t subevent = param[0];

		switch (subevent) {
		default:
			return false;
		}
	}
	default:
		return false;
	}
}

/**
 * @brief Process an HCI event packet.
 *
 * Converts internal events to host format, allocates buffers, and passes to the
 * recv callback.
 *
 * @param dev Device instance.
 * @param pkt_type Packet type.
 * @param src_id Source ID.
 * @param param Event parameters.
 * @param param_len Length of parameters.
 * @return 0 on success, negative error code on failure.
 */
static int qcc74x_event_packet_process(const struct device *dev, uint8_t pkt_type, uint16_t src_id,
				     uint8_t *param, uint8_t param_len)
{
	struct qcc74x_bt_hci_evt_hdr_c hdr;
	qcc74x_ctrl_evt_2_host(&hdr, pkt_type, src_id, param, param_len);
	bool discardable = event_packet_is_discardable(&hdr, param, param_len);
	struct net_buf *evt_buf;

	if (hdr.len + param_len > HCI_RX_BUF_SIZE) {
		LOG_ERR("Event buffer too small. %u > %u", hdr.len + param_len, HCI_RX_BUF_SIZE);
		return -ENOMEM;
	}

	if (hdr.evt == BT_HCI_EVT_LE_META_EVENT) {
		struct bt_hci_evt_le_meta_event *me = (void *)&param[0];

		LOG_DBG("LE Meta Event (0x%02x), len (%u)", me->subevent, hdr.len + param_len);
	} else if (hdr.evt == BT_HCI_EVT_CMD_COMPLETE) {
		struct bt_hci_evt_cmd_complete *cc = (void *)&param[0];
		struct bt_hci_evt_cc_status *ccs = (void *)&param[3];
		uint16_t opcode = sys_le16_to_cpu(cc->opcode);

		LOG_DBG("Command Complete (0x%04x) status: 0x%02x,"
			" ncmd: %u, len %u",
			opcode, ccs->status, cc->ncmd, hdr.len + param_len);
	} else if (hdr.evt == BT_HCI_EVT_CMD_STATUS) {
		struct bt_hci_evt_cmd_status *cs = (void *)&param[0];
		uint16_t opcode = sys_le16_to_cpu(cs->opcode);

		LOG_DBG("Command Status (0x%04x) status: 0x%02x", opcode, cs->status);
	} else {
		LOG_DBG("Event (0x%02x) len %u", hdr.evt, hdr.len + param_len);
	}

	evt_buf = bt_buf_get_evt(hdr.evt, discardable, K_NO_WAIT);

	if (!evt_buf) {
		if (discardable) {
			LOG_DBG("Discarding event");
			return 0;
		}

		LOG_DBG("No event buffer available");
		return -ENOBUFS;
	}
	net_buf_add_mem(evt_buf, &hdr, hdr.len);
	net_buf_add_mem(evt_buf, param, param_len);

	struct qcc74x_hci_driver_data *driver_data = dev->data;
	LOG_DBG("%s", bt_hex(evt_buf->data, evt_buf->len));
	(void)driver_data->recv(dev, evt_buf);

	return 0;
}

/**
 * @brief Process an ACL data packet.
 *
 * Handles incoming ACL data from the controller and passes it to the recv
 * callback.
 *
 * @param dev Device instance.
 * @param param Data parameters.
 * @param param_len Length of parameters.
 * @return 0 on success, negative error code on failure.
 */
static int data_packet_process(const struct device *dev, uint8_t *param, uint8_t param_len)
{
	struct net_buf *data_buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_NO_WAIT);
	uint8_t *buf_data = net_buf_tail(data_buf);
	uint16_t tlt_len = bt_onchiphci_handle_rx_acl(param, buf_data);
	net_buf_add(data_buf, tlt_len);

	struct bt_hci_acl_hdr *hdr;
	uint16_t hf, handle, len;
	uint8_t flags, pb, bc;

	if (!data_buf) {
		LOG_DBG("No data buffer available");
		return -ENOBUFS;
	}
	hdr = (void *)buf_data;
	len = sys_le16_to_cpu(hdr->len);
	hf = sys_le16_to_cpu(hdr->handle);
	handle = bt_acl_handle(hf);
	flags = bt_acl_flags(hf);
	pb = bt_acl_flags_pb(flags);
	bc = bt_acl_flags_bc(flags);

	if (len + sizeof(*hdr) > HCI_RX_BUF_SIZE) {
		LOG_ERR("Event buffer too small. %u > %u", len + sizeof(hdr), HCI_RX_BUF_SIZE);
		return -ENOMEM;
	}

	LOG_DBG("Data: handle (0x%02x), PB(%01d), BC(%01d), len(%u)", handle, pb, bc, len);

	struct qcc74x_hci_driver_data *driver_data = dev->data;
	LOG_DBG("%s", bt_hex(data_buf->data, data_buf->len));
	driver_data->recv(dev, data_buf);

	return 0;
}

/**
 * @brief HCI packet receive handler callback.
 *
 * Dispatches incoming packets from the on-chip HCI interface to appropriate
 * processors.
 *
 * @param pkt_type Packet type.
 * @param src_id Source ID.
 * @param param Packet parameters.
 * @param param_len Length of parameters.
 */
static void qcc74x_onchiphci_rx_packet_handler(uint8_t pkt_type, uint16_t src_id, uint8_t *param,
					     uint8_t param_len)
{
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	int err;
	switch (pkt_type) {
	case BT_HCI_CMD_CMP_EVT:
	case BT_HCI_CMD_STAT_EVT:
	case BT_HCI_LE_EVT:
	case BT_HCI_EVT:
	case BT_HCI_DBG_EVT:
		err = qcc74x_event_packet_process(dev, pkt_type, src_id, param, param_len);
		if (err) {
			LOG_ERR("Event process err:%d", err);
		}
		break;
	case BT_HCI_ACL_DATA:
		err = data_packet_process(dev, param, param_len);
		if (err) {
			LOG_ERR("Data process err:%d", err);
		}
		break;
	default:
		LOG_ERR("Unexpected msg_type: %u. This if-else needs a new branch", pkt_type);
		break;
	}
}

static int qcc74x_hci_driver_open(const struct device *dev, bt_hci_recv_t recv)
{
	struct qcc74x_hci_driver_data *data = dev->data;

#ifdef CONFIG_SOC_SERIES_QCC75X
	extern void btblecontroller_set_mac(uint8_t mac[6]);
	uint8_t bd_addr_mac[6] = {0x22, 0x22, 0x22, 0x88, 0x88, 0x88};
	btblecontroller_set_mac(bd_addr_mac);
#endif
	btble_controller_init(0);

	k_fifo_init(&recv_fifo);
	bt_onchiphci_interface_init(qcc74x_onchiphci_rx_packet_handler);
	data->recv = recv;
	g_dev = dev;
	LOG_DBG("Success.");

	return 0;
}

static int qcc74x_hci_driver_close(const struct device *dev)
{
	btble_controller_deinit();
	struct qcc74x_hci_driver_data *data = dev->data;
	data->recv = NULL;
	g_dev = NULL;

	return 0;
}

/**
 * @brief HCI driver API structure.
 *
 * Defines the interface functions for the Bluetooth HCI driver.
 */
static DEVICE_API(bt_hci, qcc74x_hci_driver_api) = {
	.open = qcc74x_hci_driver_open,
	.close = qcc74x_hci_driver_close,
	.send = qcc74x_hci_driver_send,
};

/**
 * @brief Initialize the HCI driver.
 *
 * Configures system EM and initializes RF parameters.
 *
 * @param dev Device instance.
 * @return 0 on success.
 */
static int qcc74x_hci_driver_init(const struct device *dev)
{
	extern int qc7xx_sys_em_config(void);
	qc7xx_sys_em_config();
	if (0 != rfparam_init(0, NULL, 0)) {
		LOG_ERR("PHY RF init failed!");
		return 0;
	}

	return 0;
}

/**
 * @def BT_HCI_CONTROLLER_INIT(inst)
 * @brief Macro to initialize a single HCI controller instance.
 *
 * Defines driver data and device for a given instance.
 */
#define BT_HCI_CONTROLLER_INIT(inst)                                                               \
	static struct qcc74x_hci_driver_data data_##inst;                                            \
	DEVICE_DT_INST_DEFINE(inst, qcc74x_hci_driver_init, NULL, &data_##inst, NULL, POST_KERNEL,   \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &qcc74x_hci_driver_api)

/* Only a single instance is supported */
BT_HCI_CONTROLLER_INIT(0)
