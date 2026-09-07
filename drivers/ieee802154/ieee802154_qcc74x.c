/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * IEEE 802.15.4 driver for QCC74x/QCC75x using lmac154
 *
 * The driver implements the Zephyr ieee802154_radio_api so the Zephyr
 * OpenThread platform layer (modules/openthread/platform/radio.c) can drive
 * the lmac154 radio. Event handling follows the lmac154 callback model:
 *   - RX frames: registered through lmac154_registerEventCallback()
 *   - TX completion: provided per-transmit via txParam.tx_done_cb
 * Both are invoked from the lmac154 ISR, which must be wired to the M154 IRQ.
 */

#define DT_DRV_COMPAT qcom_qcc74x_ieee802154

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/ieee802154_pkt.h>
#include <zephyr/net/openthread.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/time_units.h>

#include <compiler/compiler_ld.h>

#include <string.h>
#include <errno.h>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

/* lmac154 headers */
#include "lmac154.h"
#include "lmac154_frame.h"
#include "lmac154_fpt.h"
#include "rfparam_adapter.h"
#include "zb_timer.h"

/* Workaround: Override version_lmac154 to avoid linker section conflict */
const char *version_lmac154 = "lmac154";

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ieee802154_qcc74x, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

/* IEEE 802.15.4 channel base for the 2.4 GHz O-QPSK band */
#define QCC74X_CHANNEL_MIN	11

/*
 * The lmac154 hardware AES (CCM*) engine accesses its buffers through DMA and
 * requires 16-byte aligned addresses for encryption/decryption. Control the
 * alignment in one place; round sizes up to the same boundary so buffers that
 * share a region stay aligned.
 */
#define QCC74X_AES_ALIGN	16
#define QCC74X_AES_ALIGN_UP(x)	ROUND_UP((x), QCC74X_AES_ALIGN)

/* AES staging sizes for the MAC-security offload path, rounded up to the AES
 * alignment. a_data is authenticated in place from the frame buffer, so only
 * the message-data (plaintext payload) and ciphertext+MIC outputs need their
 * own 16-byte aligned staging buffers.
 */
#define QCC74X_AES_M_DATA_SIZE	QCC74X_AES_ALIGN_UP(IEEE802154_MAX_PHY_PACKET_SIZE)
#define QCC74X_AES_C_DATA_SIZE	QCC74X_AES_ALIGN_UP(IEEE802154_MAX_PHY_PACKET_SIZE + 16)

/* Number of MAC keys cached for the security offload (Thread uses key id 1/2/3). */
#define QCC74X_NUM_MAC_KEYS	4

/* Driver configuration (per-instance, holds the IRQ wiring helper) */
struct qcc74x_ieee802154_cfg {
	void (*irq_config_func)(void);
};

struct qcc74x_ieee802154_stats {
	atomic_t set_channel;
	atomic_t start;
	atomic_t stop;
	atomic_t tx;
	atomic_t tx_sec;
	atomic_t tx_trigger_fail;
	atomic_t tx_timeout;
	atomic_t tx_done;
	atomic_t tx_ok;
	atomic_t tx_no_ack;
	atomic_t tx_cca_fail;
	atomic_t tx_abort;
	atomic_t rx_done;
	atomic_t rx_drop_status;
	atomic_t rx_drop_error;
	atomic_t rx_error_mask;
	atomic_t rx_drop_len;
	atomic_t rx_alloc_fail;
	atomic_t rx_delivered;
	atomic_t enh_ack_req;
	atomic_t enh_ack_sent;
	atomic_t enh_ack_fail;
	atomic_t cfg_promisc;
	atomic_t cfg_event_handler;
	atomic_t cfg_rx_on_idle;
	atomic_t cfg_mac_keys;
	atomic_t cfg_frame_counter;
	atomic_t cfg_unsupported;
	atomic_t filter_ieee_addr;
	atomic_t filter_short_addr;
	atomic_t filter_pan_id;
};

/* Driver context */
struct qcc74x_ieee802154_data {
	/* Network interface */
	struct net_if *iface;

	/* Operating state */
	uint8_t channel;
	uint16_t short_addr;
	uint16_t pan_id;

	/* Extended address (EUI-64) */
	uint8_t ext_addr[8];

	/* Enhanced ACK header IE (link metrics), cached from
	 * configure(ENH_ACK_HEADER_IE). Appended to enh-ack frames.
	 */
	uint8_t enh_ack_ie[32];
	uint8_t enh_ack_ie_len;

	/* Enhanced ACK TX metadata: set by qcc74x_prepare_enh_ack() in the RX
	 * ISR when this device replies with a secured enh-ack, and consumed
	 * later in the same qcc74x_rxDone_cb() call to populate the RX net_pkt
	 * ack metadata (ack_seb/fc/keyid) so OpenThread core learns the frame
	 * was acked with a secured enh-ack. Written and read in one ISR pass,
	 * so no atomic is needed.
	 */
	uint8_t last_rx_ack_seb;
	uint32_t last_rx_ack_fc;
	uint8_t last_rx_ack_keyid;

	/*
	 * MAC-security offload state (IEEE802154_HW_TX_SEC). The frame counter is
	 * shared by workqueue TX and ISR Enhanced ACK generation.
	 */
	atomic_t frame_cnt;
	uint32_t mac_keys[QCC74X_NUM_MAC_KEYS][4];
	uint32_t mac_key_set;
	uint32_t nonce_ext_addr[2];

	/* TX: persistent parameter block (lmac154 dereferences it from ISR until
	 * the transfer completes). The frame staging buffer lives outside this
	 * struct in a non-cacheable region (see qcc74x_tx_buf below).
	 */
	lmac154_txParam_ext_t tx_param;

	/* TX completion synchronization: signaled from the lmac154 ISR, awaited
	 * in qcc74x_tx(). Per the Zephyr/OpenThread radio contract, tx() is
	 * synchronous and its return value maps to otPlatRadioTxDone().
	 */
	struct k_sem tx_done;
	lmac154_tx_status_t tx_status;
	bool tx_busy;
	/* RF-FSM watchdog work (periodic lmac154_monitor) */
	struct k_work_delayable monitor_work;

	/* Peer ACK captured by lmac154 when a TX is ACKED. Forwarded to the OT
	 * platform layer via ieee802154_handle_ack() from qcc74x_tx() so OT can
	 * parse enh-ack contents (e.g. link-metrics IEs) via otPlatRadioTxDone.
	 */
	uint32_t ack_frame[32];
	uint32_t ack_frame_len;
	uint8_t ack_lqi;	/* LQI of the captured ACK, sampled in tx_done ISR */
	int8_t ack_rssi;	/* RSSI (dBm) of the captured ACK */

	struct qcc74x_ieee802154_stats stats;

	bool initialized;
};

/*
 * Enhanced ACK buffer, non-cacheable (DMA accessed by lmac154 hardware AES
 * for MAC-security offload on ACKs). 16-byte aligned.
 */
static ATTR_NOCACHE_NOINIT_RAM_SECTION __aligned(QCC74X_AES_ALIGN)
	uint8_t qcc74x_ack_buf[QCC74X_AES_ALIGN_UP(IEEE802154_MAX_PHY_PACKET_SIZE)];

/*
 * Enhanced ACK security context and key staging buffer.
 * Kept in non-cacheable memory because lmac154_encryptPacket() reads these
 * via DMA. Previously these were stack-local in qcc74x_prepare_enh_ack()
 * which ran in ISR context; the ~48-byte lmac154_security_t plus
 * encryptPacket's internal depth risked ISR stack overflow.
 */
static ATTR_NOCACHE_NOINIT_RAM_SECTION __aligned(QCC74X_AES_ALIGN)
	lmac154_security_t qcc74x_ack_sec;
static ATTR_NOCACHE_NOINIT_RAM_SECTION __aligned(QCC74X_AES_ALIGN) uint32_t qcc74x_ack_key[4];

/*
 * TX frame staging buffer. Also serves as the AES ciphertext output buffer
 * (sec.c_data) for the MAC-security offload path, hence 16-byte aligned.
 *
 * lmac154 reads the frame via DMA, so the buffer MUST be non-cacheable: the
 * QCC74x has a DCACHE but the runtime flush API (sys_cache_*) is not enabled
 * in this build. Cache coherency is handled by placing DMA buffers in the
 * non-cacheable OCRAM alias through the HAL's .nocache_noinit_ram section.
 * Otherwise dirty cache lines are not written back before the radio DMA reads
 * them, corrupting the first MAC-secured frames Thread exchanges at startup.
 */
static ATTR_NOCACHE_NOINIT_RAM_SECTION __aligned(QCC74X_AES_ALIGN)
	uint8_t qcc74x_tx_buf[QCC74X_AES_ALIGN_UP(IEEE802154_MAX_PHY_PACKET_SIZE)];

/*
 * AES staging buffers for the MAC-security offload path:
 *   - qcc74x_aes_m_data: plaintext payload (sec.m_data). Staged separately so
 *     the AES engine does not encrypt in place.
 *   - qcc74x_aes_c_data: encrypted payload + MIC output (sec.c_data). Kept in
 *     its own 16-byte aligned buffer (rather than overlapping the frame) so
 *     lmac154's post-encrypt reassembly is a clean, non-overlapping copy.
 * a_data (the authenticated MAC header) is read in place from qcc74x_tx_buf,
 * so it needs no separate buffer.
 *
 * Both are 16-byte aligned (QCC74X_AES_ALIGN) and non-cacheable so the AES
 * engine can DMA them.
 */
static ATTR_NOCACHE_NOINIT_RAM_SECTION __aligned(QCC74X_AES_ALIGN)
	uint8_t qcc74x_aes_m_data[QCC74X_AES_M_DATA_SIZE];
static ATTR_NOCACHE_NOINIT_RAM_SECTION __aligned(QCC74X_AES_ALIGN)
	uint8_t qcc74x_aes_c_data[QCC74X_AES_C_DATA_SIZE];

/* Forward declarations */
static int qcc74x_ieee802154_init(const struct device *dev);
static void qcc74x_iface_init(struct net_if *iface);
static enum ieee802154_hw_caps qcc74x_get_capabilities(const struct device *dev);
static int qcc74x_cca(const struct device *dev);
static int qcc74x_ed_scan(const struct device *dev, uint16_t duration,
			  energy_scan_done_cb_t done_cb);
static int qcc74x_set_channel(const struct device *dev, uint16_t channel);
static int qcc74x_filter(const struct device *dev, bool set,
			 enum ieee802154_filter_type type,
			 const struct ieee802154_filter *filter);
static int qcc74x_set_txpower(const struct device *dev, int16_t dbm);
static int qcc74x_start(const struct device *dev);
static int qcc74x_stop(const struct device *dev);
static int qcc74x_tx(const struct device *dev, enum ieee802154_tx_mode mode,
		     struct net_pkt *pkt, struct net_buf *frag);
static int qcc74x_configure(const struct device *dev,
			    enum ieee802154_config_type type,
			    const struct ieee802154_config *config);
#if defined(CONFIG_NET_PKT_TXTIME)
static net_time_t qcc74x_get_time(const struct device *dev);
#endif

/*
 * RF-FSM watchdog: periodically call lmac154_monitor() to detect and
 * recover from a stuck radio FSM state. Without this, a stuck FSM
 * causes every subsequent TX to time out (Bouffalo calls this in its
 * main loop).
 */
#define QCC74X_MONITOR_TIMEOUT	10000   /* Bouffalo's value */
static void qcc74x_monitor_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct qcc74x_ieee802154_data *data =
		CONTAINER_OF(dwork, struct qcc74x_ieee802154_data, monitor_work);

	lmac154_monitor(QCC74X_MONITOR_TIMEOUT);
	k_work_reschedule(&data->monitor_work,
			  K_MSEC(CONFIG_IEEE802154_QCC74X_MONITOR_INTERVAL_MS));
}

/* -------------------------------------------------------------------------- */
/* lmac154 event callbacks (invoked from the lmac154 ISR)                     */
/* -------------------------------------------------------------------------- */

/*
 * TX completion callback: registered per-transmit via tx_param.tx_done_cb.
 * Stores the result and unblocks qcc74x_tx() which is blocked on tx_done.
 */
static void qcc74x_txDone_cb(lmac154_tx_status_t status, uint32_t *ack_frame,
			      uint32_t ack_frame_len)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ieee802154));
	struct qcc74x_ieee802154_data *data = dev->data;

	data->tx_status = status;

	if (ack_frame && ack_frame_len > 0 && ack_frame_len <= sizeof(data->ack_frame)) {
		memcpy(data->ack_frame, ack_frame, ack_frame_len);
		data->ack_frame_len = ack_frame_len;
		/* Sample the ACK's signal metrics now, while lmac154 still holds
		 * the just-received ACK as the most recent RX frame.
		 */
		data->ack_lqi = (uint8_t)lmac154_getLQI();
		data->ack_rssi = (int8_t)lmac154_getRSSI();
	} else {
		data->ack_frame_len = 0;
	}

	k_sem_give(&data->tx_done);
}

/*
 * Construct an enhanced ACK for a 802.15.4-2015 frame that requested one.
 *
 * The enhanced ACK is built from the received frame and placed in
 * qcc74x_ack_buf; it is then handed to lmac154_writeTxAckFrame().
 *
 * Follows Bouffalo ot_radio_prepare_enh_ack() but adapted to the Zephyr
 * driver's buffer model (non-cacheable, 16-byte aligned).
 *
 * Returns ACK length on success, 0 on failure.
 */
static int ATTR_TCM_SECTION
qcc74x_prepare_enh_ack(lmac154_receiveInfo_t *info, uint32_t *rx_frame,
				       struct qcc74x_ieee802154_data *data)
{
	uint8_t *rx = (uint8_t *)rx_frame;
	uint8_t *ack = qcc74x_ack_buf;
	uint16_t fcf = (uint16_t)rx_frame[0];
	int ack_len;
	uint8_t seq = rx[2];
	bool has_sec;
	int sec_mic_len;
	lmac154_aux_hdr_t *p_rx_aux = NULL;
	lmac154_aux_hdr_t *p_ack_aux = NULL;

	/* Start with an empty ACK header (FCF = ACK + version 2) */
	memset(ack, 0, sizeof(qcc74x_ack_buf));
	ack[0] = LMAC154_FRAME_CONTROL_FRAME_TYPE_ACK;
	ack[1] = (uint8_t)(LMAC154_FRAME_VERSION_2015 >> 8);
	ack[2] = seq;
	ack_len = 3;

	/* Destination PAN ID: copy from the received frame at the matching
	 * offset (Bouffalo pattern; works for PAN-compressed Thread frames).
	 */
	ack[ack_len]     = rx[ack_len];
	ack[ack_len + 1] = rx[ack_len + 1];
	ack_len += 2;

	/* Destination address = received frame's source */
	if (info->is_src_xaddr) {
		memcpy(&ack[ack_len], info->src_addr.addr, 8);
		ack_len += 8;
		*(uint16_t *)ack |= LMAC154_FRAME_ADDR_DEST_EXT;
	} else if (info->is_src_saddr) {
		ack[ack_len]     = info->src_addr.addr[0];
		ack[ack_len + 1] = info->src_addr.addr[1];
		ack_len += 2;
		*(uint16_t *)ack |= LMAC154_FRAME_ADDR_DEST_SHORT;
	}

	/* Frame Pending */
	if (info->is_frame_pended) {
		ack[0] |= LMAC154_FRAME_FRAME_PENDING_MASK;
	}

	/* MAC security: if the received frame was secured, the enhanced ACK
	 * must also carry security (Thread 1.2+). Copy the aux header and
	 * fill the frame counter from the driver's cached value.
	 */
	has_sec = (fcf & LMAC154_FRAME_SECURITY_MASK) != 0;
	if (has_sec) {
		uint32_t aux_offset = info->mac_hdr_size;

		p_rx_aux = (lmac154_aux_hdr_t *)(rx + aux_offset);

		if (p_rx_aux->sec_lvl) {
			uint8_t key_id = p_rx_aux->key_id[0] & 0x3;

			if ((data->mac_key_set & BIT(key_id)) == 0) {
				return 0;
			}

			ack[0] |= LMAC154_FRAME_SECURITY_MASK;
			p_ack_aux = (lmac154_aux_hdr_t *)(ack + ack_len);
			memcpy(p_ack_aux, p_rx_aux, sizeof(*p_ack_aux));
			p_ack_aux->key_id[0] = key_id;
			ack_len += 6; /* aux header without key_id tail */

			/* Header IE: link metrics enh-ack IE. Per 802.15.4-2015, Header
			 * IEs go AFTER the aux security header (MHR = FCF+seq+addr+aux+IE),
			 * matching Bouffalo ot_radio_prepare_enh_ack. Placing IE before aux
			 * made the peer parse IE bytes as the aux header -> security failed
			 * -> data-frame ack rejected -> 14-15x retransmit -> ping timeout.
			 */
			if (data->enh_ack_ie_len > 0) {
				memcpy(&ack[ack_len], data->enh_ack_ie, data->enh_ack_ie_len);
				ack_len += data->enh_ack_ie_len;
				*(uint16_t *)ack |= LMAC154_FRAME_IE_MASK;
			}

			/* Atomically reserve a frame counter (fetch-and-inc): the ISR
			 * enh-ack path and the workqueue TX path share data->frame_cnt,
			 * so a separate get+inc raced and produced duplicate fc (peer
			 * rejected the enh-ack). atomic_add returns the pre-increment
			 * value and is ISR-safe (CONFIG_ATOMIC_OPERATIONS_C spinlock). */
			uint32_t ack_fc = (uint32_t)atomic_add(&data->frame_cnt, 1);
			p_ack_aux->frame_cnt[0] = ack_fc & 0xff;
			p_ack_aux->frame_cnt[1] = (ack_fc >> 8) & 0xff;
			p_ack_aux->frame_cnt[2] = (ack_fc >> 16) & 0xff;
			p_ack_aux->frame_cnt[3] = (ack_fc >> 24) & 0xff;

			/* Use persistent non-cacheable buffers for AES DMA.
			 * Previously used stack-local lmac154_security_t which
			 * risked ISR stack overflow and cache coherency issues.
			 */
			memcpy(qcc74x_ack_key, data->mac_keys[key_id],
			       sizeof(qcc74x_ack_key));

			memset(&qcc74x_ack_sec, 0, sizeof(qcc74x_ack_sec));
			qcc74x_ack_sec.a_data_length = ack_len;
			qcc74x_ack_sec.m_data_length = 0;
			qcc74x_ack_sec.m_data = NULL;
			qcc74x_ack_sec.a_data = (uint32_t *)ack;
			qcc74x_ack_sec.c_data = (uint32_t *)(ack + ack_len);
			qcc74x_ack_sec.key = qcc74x_ack_key;
			qcc74x_ack_sec.nonce.frame_cnt = sys_cpu_to_be32(ack_fc);
			qcc74x_ack_sec.nonce.security_level =
				p_ack_aux->sec_lvl;
			memcpy(qcc74x_ack_sec.nonce.ext_addr,
			       data->nonce_ext_addr,
			       sizeof(qcc74x_ack_sec.nonce.ext_addr));

			sec_mic_len = lmac154_encryptPacket(&qcc74x_ack_sec);
			if (sec_mic_len < 0) {
				return 0;
			}

			/* Record secured-enh-ack metadata so qcc74x_rxDone_cb()
			 * can propagate it to the RX net_pkt (ack_seb/fc/keyid).
			 */
			data->last_rx_ack_seb = 1;
			data->last_rx_ack_fc = ack_fc;
			data->last_rx_ack_keyid = key_id;

			/* MIC is appended after the header */
			ack_len += sec_mic_len;
		}
	}

	{
		int ret = lmac154_writeTxAckFrame(ack, ack_len);
		if (ret < 0) {
			return 0;
		}
	}

	return ack_len;
}

/*
 * RX callback: registered through lmac154_registerEventCallback().  For
 * 802.15.4-2015 frames that request an Enhanced ACK the ACK is constructed
 * here (in ISR context, before the ACK turnaround time expires) and
 * committed through lmac154_writeTxAckFrame().  Normal frames are handed
 * to the network stack, which forwards them to the OpenThread L2.
 */
static void ATTR_TCM_SECTION
qcc74x_rxDone_cb(lmac154_rx_status_t status, lmac154_receiveInfo_t *info,
			 uint32_t *frame)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ieee802154));
	struct qcc74x_ieee802154_data *data = dev->data;
	struct net_pkt *pkt;
	uint8_t rx_length;

	/* Clear enh-ack TX metadata from any prior frame; qcc74x_prepare_enh_ack()
	 * may set it below if we reply with a secured enh-ack for this frame.
	 */
	data->last_rx_ack_seb = 0;

	if (status != LMAC154_RX_STATUS_RX_DONE) {
		/* ACK_SENT / ACK_ERR carry no data frame to deliver */
		atomic_inc(&data->stats.rx_drop_status);
		return;
	}

	/* Enhanced ACK: 2015 frames that request one need the ACK submitted
	 * before we exit the ISR; otherwise the radio earns no ACK and the
	 * peer resends.  OT normally manages ACK generation, but lmac154's
	 * dual-stack ISR simply flags the request; we construct and fire it
	 * here (same pattern Bouffalo uses in ot_radio_prepare_enh_ack).
	 */
	if (info && info->is_enh_ack_requested) {
		atomic_inc(&data->stats.enh_ack_req);
		int ret = qcc74x_prepare_enh_ack(info, frame, data);
		if (ret > 0) {
			atomic_inc(&data->stats.enh_ack_sent);
		} else {
			atomic_inc(&data->stats.enh_ack_fail);
		}
	}

	if (info == NULL || info->rx_error) {
		atomic_inc(&data->stats.rx_drop_error);
		if (info != NULL) {
			atomic_or(&data->stats.rx_error_mask, info->rx_error);
		}
		return;
	}

	rx_length = info->rx_length;
	if (rx_length == 0 || rx_length > IEEE802154_MAX_PHY_PACKET_SIZE) {
		atomic_inc(&data->stats.rx_drop_len);
		return;
	}

	atomic_inc(&data->stats.rx_done);

	pkt = net_pkt_rx_alloc_with_buffer(data->iface, rx_length,
					   AF_UNSPEC, 0, K_NO_WAIT);
	if (!pkt) {
		atomic_inc(&data->stats.rx_alloc_fail);
		return;
	}

	if (net_pkt_write(pkt, (uint8_t *)frame, rx_length) < 0) {
		net_pkt_unref(pkt);
		return;
	}

	net_pkt_set_ieee802154_lqi(pkt, (uint8_t)lmac154_getLQI());
	net_pkt_set_ieee802154_rssi_dbm(pkt, (int16_t)lmac154_getRSSI());
	/* Report whether the ACK we sent for this frame set the Frame Pending
	 * Bit (lmac154's FPT lookup result) so OT core gets mAckedWithFramePending.
	 */
	net_pkt_set_ieee802154_ack_fpb(pkt, info->is_frame_pended);

	/* If we replied to this frame with a secured enh-ack, propagate the
	 * enh-ack metadata so OT core sets mAckedWithSecEnhAck / AckFrameCounter
	 * / AckKeyId (consumed by the CSL receiver sync and link-metrics paths).
	 */
	if (data->last_rx_ack_seb) {
		net_pkt_set_ieee802154_ack_seb(pkt, true);
		net_pkt_set_ieee802154_ack_fc(pkt, data->last_rx_ack_fc);
		net_pkt_set_ieee802154_ack_keyid(pkt, data->last_rx_ack_keyid);
		data->last_rx_ack_seb = 0;
	}

	if (net_recv_data(data->iface, pkt) < 0) {
		net_pkt_unref(pkt);
		return;
	}

	atomic_inc(&data->stats.rx_delivered);
}

/* -------------------------------------------------------------------------- */
/* IEEE 802.15.4 radio API                                                    */
/* -------------------------------------------------------------------------- */

static const struct ieee802154_radio_api qcc74x_ieee802154_radio_api = {
	.iface_api.init = qcc74x_iface_init,
	.get_capabilities = qcc74x_get_capabilities,
	.cca = qcc74x_cca,
	.set_channel = qcc74x_set_channel,
	.filter = qcc74x_filter,
	.set_txpower = qcc74x_set_txpower,
	.start = qcc74x_start,
	.stop = qcc74x_stop,
	.tx = qcc74x_tx,
	.ed_scan = qcc74x_ed_scan,
#if defined(CONFIG_NET_PKT_TXTIME)
	.get_time = qcc74x_get_time,
#endif
	.configure = qcc74x_configure,
};

/*
 * Top-level M154 interrupt handler: the lmac154 ISR is returned by
 * lmac154_getInterruptCallback(); Zephyr requires a const struct device *
 * signature, so we wrap it.
 */
static void qcc74x_isr(const struct device *dev)
{
	lmac154_isr_t handler;

	ARG_UNUSED(dev);

	handler = lmac154_getInterruptCallback();
	if (handler) {
		handler();
	}
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

extern int qc7xx_sys_em_config(void);

/*
 * Bring up lmac154 for Thread. Mirrors the proven sequence in the Bouffalo
 * OpenThread port (lmac154_app_init): OpenThread manages retransmission at the
 * MAC layer, so the radio is told not to retry; 802.15.4-2015 framing is
 * enabled so IEs/enhanced ACKs are handled by the hardware.
 */
static int qcc74x_radio_hw_init(void)
{
	int ret;

	/* Wi-Fi and Bluetooth initialize the shared RF in their respective device
	 * initialization paths in the same way. A common cross-radio initialization
	 * service is not currently provided by the QCC74x HAL.
	 */
	ret = qc7xx_sys_em_config();
	if (ret != 0) {
		LOG_ERR("EM configuration failed: %d", ret);
		return ret;
	}

	ret = rfparam_init(0, NULL, 0);
	if (ret != 0) {
		LOG_ERR("RF parameter initialization failed: %d", ret);
		return ret;
	}

	LOG_DBG("hw init: lmac154_init");
	lmac154_init();

	/* Coexistence: on QCC74x the RF front-end is shared between WiFi/BT and
	 * 15.4. The coex arbiter must be enabled or 15.4 is not granted air time
	 * and no frames actually go over the air (mirrors the Bouffalo port).
	 */
	LOG_DBG("hw init: lmac154_enableCoex");
	lmac154_enableCoex();

	/* Thread-specific MAC configuration */
	LOG_DBG("hw init: std2015/retry0/enhAck/rxOnWhenIdle/fptClear");
	lmac154_setStd2015Extra(true);
	lmac154_setTxRetry(0);	/* OT retries at the MAC layer */
	lmac154_setAckWaitTime(1500);
	/* Time this device waits to RECEIVE the peer's enh-ack after it transmits
	 * a frame (lmac154_enh_ack_timeout, consumed at lmac154.c:3428-3434). This
	 * is NOT a budget for this device's own enh-ack TX; that deadline is
	 * enforced inside lmac154_writeTxAckFrame() via a symbol-counter diff
	 * check (lmac154.c:519). The Bouffalo port value 0x2e0 (736us) was too
	 * short to receive a full peer enh-ack carrying IE + security (Thread
	 * 1.2+, ~1.2-1.8ms on the wire @250kbps) -> truncated reception ->
	 * spurious NO_ACK -> OT retransmits -> unstable attach. Symbol formula
	 * (matches Bouffalo OT port): (AIFS + 10 + (6+42)*2) sym = 118 sym = 1888us.
	 */
	lmac154_setEnhAckWaitTime((LMAC154_AIFS + 10 + (6 + 42) * 2)
				  << LMAC154_US_PER_SYMBOL_BITS);
	lmac154_setRxStateWhenIdle(true);
	lmac154_setRxFiltering(true);
	lmac154_fptClear();

	/* Sync the MAC symbol counter (free-running HW register) to the system
	 * clock so CSL/timed-TX offsets are consistent (parity with Bouffalo).
	 */
	zb_timer_cfg((uint32_t)(k_uptime_get() * 1000U) >> LMAC154_US_PER_SYMBOL_BITS);

	/* Sensible radio defaults; OT adjusts channel/power before each TX */
	lmac154_setChannel(LMAC154_CHANNEL_11);
	lmac154_setPanId(0xffff);
	lmac154_setShortAddr(0xffff);
	lmac154_setTxPower(LMAC154_TX_POWER_10dBm);
	lmac154_setCCAMode(LMAC154_CCA_MODE_ED_AND_CS);
	lmac154_setEDThreshold(CONFIG_IEEE802154_QCC74X_CCA_THRESHOLD);
	lmac154_setTxDataRate(LMAC154_DATA_RATE_250K);

	/* OT enables RX explicitly via start(); keep it off during init */
	lmac154_disableRx();
	LOG_DBG("hw init: done");

	return 0;
}

static int qcc74x_ieee802154_init(const struct device *dev)
{
	const struct qcc74x_ieee802154_cfg *cfg = dev->config;
	struct qcc74x_ieee802154_data *data = dev->data;
	int ret;

	LOG_INF("QCC74x IEEE 802.15.4 init");

	ret = qcc74x_radio_hw_init();
	if (ret != 0) {
		return ret;
	}

	/* Register the RX-frame callback (lmac154 ISR dispatches through it) */
	lmac154_registerEventCallback(LMAC154_STACK_1, qcc74x_rxDone_cb);

	/* TX completion handshake */
	k_sem_init(&data->tx_done, 0, 1);

	/* Attach the lmac154 ISR to the M154 IRQ line */
	cfg->irq_config_func();

	/* Periodic RF-FSM watchdog: lmac154_monitor() detects the radio FSM
	 * stuck state (RX length == 255), resets RX/TX/CSMA, and re-triggers
	 * pending TX.  Bouffalo calls this in its main loop; without it the
	 * FSM never recovers from a stuck state and every TX hits the 200ms
	 * completion timeout.  The function self-throttles internally.
	 */
	k_work_init_delayable(&data->monitor_work, qcc74x_monitor_handler);
	k_work_reschedule(&data->monitor_work, K_MSEC(CONFIG_IEEE802154_QCC74X_MONITOR_INTERVAL_MS));

	data->channel = QCC74X_CHANNEL_MIN;
	data->initialized = true;

	LOG_INF("QCC74x IEEE 802.15.4 ready");

	return 0;
}

/*
 * Update the local extended address everywhere it is needed: the radio
 * address filter, the driver cache, and the CCM* nonce (which uses the source
 * EUI-64 byte-reversed). Called only when the address changes, so the 8-byte
 * reverse runs at most a couple of times over the device lifetime - never per
 * transmit.
 */
static void qcc74x_update_mac_addr(struct qcc74x_ieee802154_data *data,
				   const uint8_t *addr)
{
	uint8_t *nonce = (uint8_t *)data->nonce_ext_addr;

	memcpy(data->ext_addr, addr, sizeof(data->ext_addr));
	lmac154_setLongAddr((uint8_t *)addr);

	for (int i = 0; i < 8; i++) {
		nonce[i] = addr[7 - i];
	}
}

/*
 * Network interface initialization. The link address (EUI-64) MUST be set
 * before ieee802154_init() because the OpenThread L2 reads it to derive the
 * interface address. ieee802154_init() enters openthread_l2_init() ->
 * openthread_init(), which creates the OpenThread instance and unlocks the
 * "ot" shell.
 */
static void qcc74x_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct qcc74x_ieee802154_data *data = dev->data;
	uint8_t hw_addr[8];

	data->iface = iface;

	lmac154_getLongAddr(hw_addr);
	qcc74x_update_mac_addr(data, hw_addr);
	net_if_set_link_addr(iface, data->ext_addr, sizeof(data->ext_addr),
			     NET_LINK_IEEE802154);

	ieee802154_init(iface);
}

static enum ieee802154_hw_caps qcc74x_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER |
	       IEEE802154_HW_CSMA | IEEE802154_HW_TX_RX_ACK |
	       IEEE802154_HW_RX_TX_ACK |
	       IEEE802154_RX_ON_WHEN_IDLE |
	       IEEE802154_HW_TX_SEC |
#if defined(CONFIG_NET_PKT_TXTIME)
	       IEEE802154_HW_TXTIME |
#endif
	       IEEE802154_HW_ENERGY_SCAN;
}

static int qcc74x_cca(const struct device *dev)
{
	int rssi;

	ARG_UNUSED(dev);

	return lmac154_runCCA(&rssi) ? -EBUSY : 0;
}

static int qcc74x_ed_scan(const struct device *dev, uint16_t duration,
			  energy_scan_done_cb_t done_cb)
{
	int rssi;
	int rssi_max = INT_MIN;

	ARG_UNUSED(dev);

	if (duration == 0) {
		duration = 1;
	}
	for (uint16_t i = 0; i < duration; i++) {
		if (!lmac154_runCCA(&rssi) && rssi > rssi_max) {
			rssi_max = rssi;
		}
		k_busy_wait(1000);
	}

	done_cb(dev, (rssi_max > INT_MIN) ? rssi_max : -128);
	return 0;
}

static int qcc74x_set_channel(const struct device *dev, uint16_t channel)
{
	struct qcc74x_ieee802154_data *data = dev->data;

	if (channel < QCC74X_CHANNEL_MIN || channel > 26) {
		LOG_WRN("Invalid channel: %u", channel);
		return -EINVAL;
	}

	atomic_inc(&data->stats.set_channel);

	/* OT calls set_channel on every Receive()/TX (platform/radio.c unconditionally
	 * re-arms RX with set_channel+start). Short-circuit when unchanged so lmac154
	 * is not re-tuned on every frame; that re-tune jitter correlates with the
	 * earlier triggerParamTxExt races. The stat still counts OT's calls.
	 */
	if (channel == data->channel) {
		return 0;
	}

	data->channel = channel;
	lmac154_setChannel((lmac154_channel_t)(channel - QCC74X_CHANNEL_MIN));

	return 0;
}

static int qcc74x_filter(const struct device *dev, bool set,
			 enum ieee802154_filter_type type,
			 const struct ieee802154_filter *filter)
{
	struct qcc74x_ieee802154_data *data = dev->data;

	if (!set) {
		return -ENOTSUP;
	}

	switch (type) {
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		atomic_inc(&data->stats.filter_ieee_addr);
		qcc74x_update_mac_addr(data, filter->ieee_addr);
		break;

	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		atomic_inc(&data->stats.filter_short_addr);
		lmac154_setShortAddr(filter->short_addr);
		data->short_addr = filter->short_addr;
		break;

	case IEEE802154_FILTER_TYPE_PAN_ID:
		atomic_inc(&data->stats.filter_pan_id);
		lmac154_setPanId(filter->pan_id);
		data->pan_id = filter->pan_id;
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}

static int qcc74x_set_txpower(const struct device *dev, int16_t dbm)
{
	ARG_UNUSED(dev);

	if (dbm < 0) {
		dbm = 0;
	} else if (dbm > 14) {
		dbm = 14;
	}

	lmac154_setTxPower((lmac154_tx_power_t)dbm);

	return 0;
}

static int qcc74x_start(const struct device *dev)
{
	struct qcc74x_ieee802154_data *data = dev->data;

	if (!data->initialized) {
		return -ENODEV;
	}

	LOG_INF("start: enableRx (ch=%u)", data->channel);
	atomic_inc(&data->stats.start);
	lmac154_enableRx();

	return 0;
}

static int qcc74x_stop(const struct device *dev)
{
	struct qcc74x_ieee802154_data *data = dev->data;

	if (!data->initialized) {
		return -ENODEV;
	}

	atomic_inc(&data->stats.stop);

	/* If a TX is mid-flight, lmac154 still owns tx_param and disabling RX
	 * now can disturb the TX FSM. OpenThread does not normally call stop()
	 * during TX, so this is defensive: skip the disable and let the next
	 * start() re-establish RX.
	 */
	if (data->tx_busy) {
		LOG_WRN("stop: deferred, TX in flight");
		return 0;
	}

	lmac154_disableRx();

	return 0;
}

/*
 * Transmit a frame. Uses the extended lmac154 TX path so completion is reported
 * through tx_param.tx_done_cb (the model required for Thread). tx() blocks until
 * the radio signals completion; its return value is mapped to the errno the
 * OpenThread platform layer translates into otPlatRadioTxDone().
 */
static int qcc74x_tx(const struct device *dev, enum ieee802154_tx_mode mode,
		     struct net_pkt *pkt, struct net_buf *frag)
{
	struct qcc74x_ieee802154_data *data = dev->data;
	uint16_t len = frag->len;
	uint16_t fcf;
	uint32_t a_data_len;
	bool need_sec;

	if (!data->initialized) {
		return -ENODEV;
	}

	if (len == 0 || len > IEEE802154_MAX_PHY_PACKET_SIZE) {
		return -EINVAL;
	}

	atomic_inc(&data->stats.tx);

	/* lmac154 accesses the frame as 32-bit words, so stage it aligned (and
	 * non-cacheable) before any processing.
	 */
	memcpy(qcc74x_tx_buf, frag->data, len);

	fcf = sys_get_le16(qcc74x_tx_buf);
	a_data_len = LMAC154_FRAME_IS_FRAME_2015(fcf)
		? lmac154_get_mhr_len_2015(qcc74x_tx_buf)
		: lmac154_get_mhr_len(qcc74x_tx_buf);

	/* HW MAC-security offload: when OT did not pre-encrypt (frame_secured is
	 * false) and the frame carries security, set up the AES-CCM* transform.
	 */
	need_sec = !net_pkt_ieee802154_frame_secured(pkt) &&
		   LMAC154_FRAME_IS_NORMAL_SECURITY(fcf);

	memset(&data->tx_param, 0, sizeof(data->tx_param));
	/* The nonce source address rarely changes; restore it across the memset
	 * instead of re-reversing on every transmit.
	 */
	memcpy(data->tx_param.sec.nonce.ext_addr, data->nonce_ext_addr,
	       sizeof(data->nonce_ext_addr));

	data->tx_param.pkt = (uint32_t *)qcc74x_tx_buf;
	data->tx_param.pkt_length = len;
	data->tx_param.tx_channel = data->channel - QCC74X_CHANNEL_MIN;
	data->tx_param.resume_channel = data->tx_param.tx_channel;
	data->tx_param.csma_ca_max_backoff = 4;	/* macMaxCSMABackoffs default */
	data->tx_param.is_cca = (mode == IEEE802154_TX_MODE_CSMA_CA) ? 1 : 0;
	data->tx_param.tx_done_cb = qcc74x_txDone_cb;

#if defined(CONFIG_NET_PKT_TXTIME)
	if (mode == IEEE802154_TX_MODE_TXTIME ||
	    mode == IEEE802154_TX_MODE_TXTIME_CCA) {
		uint64_t tx_at_us = net_pkt_timestamp_ns(pkt) / NSEC_PER_USEC;
		uint64_t now_us = zb_timer_get_current_time_us();
		uint32_t now_sym = zb_timer_get_current_time();
		uint32_t tx_at_sym;
		int32_t delay_sym;

		/* lmac154 takes an absolute base plus a relative delay, both in
		 * 16-us MAC symbols. The packet timestamp uses the same radio clock
		 * returned by qcc74x_get_time().
		 */
		if ((int64_t)(tx_at_us - now_us) <= 0) {
			return -EINVAL;
		}

		tx_at_sym = (uint32_t)(tx_at_us >> LMAC154_US_PER_SYMBOL_BITS);
		delay_sym = (int32_t)(tx_at_sym - now_sym);
		if (delay_sym <= 0) {
			return -EINVAL;
		}

		data->tx_param.tx_timing.base_time = now_sym;
		data->tx_param.tx_timing.delay_time = (uint32_t)delay_sym;
		/* lmac154 rejects a non-zero backoff count for delayed TX. */
		data->tx_param.csma_ca_max_backoff = 0;
	}
#endif

	if (need_sec) {
		lmac154_aux_hdr_t *aux = NULL;
		uint32_t aux_len = lmac154_parse_aux_hdr(qcc74x_tx_buf, a_data_len, &aux);
		uint32_t mic_len;
		uint8_t sec_lvl;
		uint8_t key_id;

		if (aux_len == 0 || aux == NULL) {
			LOG_WRN("Secured frame with bad aux header");
			return -EINVAL;
		}

		key_id = aux->key_id[0] & 0x3;
		if ((data->mac_key_set & BIT(key_id)) == 0) {
			LOG_WRN("No MAC key cached for key id %u", key_id);
			return -EINVAL;
		}

		sec_lvl = aux->sec_lvl & 0x3;
		if (sec_lvl == 0) {
			LOG_WRN("Invalid security level %u", aux->sec_lvl);
			return -EINVAL;
		}
		mic_len = 4U << (sec_lvl - 1);

		/* Atomically reserve a frame counter (fetch-and-inc) so ISR enh-ack
		 * and workqueue TX never read the same value. atomic_add returns the
		 * pre-increment value (ISR-safe via CONFIG_ATOMIC_OPERATIONS_C). */
		uint32_t fc = (uint32_t)atomic_add(&data->frame_cnt, 1);
		sys_put_le32(fc, aux->frame_cnt);
		data->tx_param.sec.nonce.security_level = aux->sec_lvl;
		data->tx_param.sec.nonce.frame_cnt = sys_cpu_to_be32(fc);
		data->tx_param.sec.key = data->mac_keys[key_id];

		/* a_data includes the MAC header, aux security header, and
		 * any Information Elements (IEs); IEs are authenticated but
		 * not encrypted (IEEE 802.15.4-2015 / Bouffalo pattern).
		 */
		{
			uint32_t ie_len = 0;

			if (fcf & LMAC154_FRAME_IE_MASK) {
				uint8_t *ie = qcc74x_tx_buf + a_data_len + aux_len;
				uint32_t rem = len - mic_len - a_data_len - aux_len;
				uint32_t pos = 0;

				while (pos + 2 <= rem) {
					uint8_t hdr = ie[pos];
					uint8_t dl  = ie[pos + 1];

					pos += 2 + dl;
					ie_len += 2 + dl;
					if (hdr & 0x80)
						break;
					if (pos > rem) {
						ie_len = 0;
						break;
					}
				}
			}

			data->tx_param.sec.a_data_length =
				a_data_len + aux_len + ie_len;

			/* Legacy (2003/2006) CMD frames: command id byte is
			 * authenticated as part of a_data.
			 */
			if (LMAC154_FRAME_IS_CMD(fcf) &&
			    !LMAC154_FRAME_IS_FRAME_2015(fcf))
				data->tx_param.sec.a_data_length += 1;
		}

		if (len < mic_len + data->tx_param.sec.a_data_length) {
			LOG_WRN("Secured frame too short (%u)", len);
			return -EINVAL;
		}
		data->tx_param.sec.m_data_length =
			len - mic_len - data->tx_param.sec.a_data_length;
		data->tx_param.sec.a_data = (uint32_t *)qcc74x_tx_buf;
		memcpy(qcc74x_aes_m_data,
		       qcc74x_tx_buf + data->tx_param.sec.a_data_length,
		       data->tx_param.sec.m_data_length);
		data->tx_param.sec.m_data = (uint32_t *)qcc74x_aes_m_data;
		data->tx_param.sec.c_data = (uint32_t *)qcc74x_aes_c_data;
		data->tx_param.is_tx_security = 1;
		atomic_inc(&data->stats.tx_sec);

		/* Tell the OT platform layer the frame is now encrypted. */
		net_pkt_set_ieee802154_frame_secured(pkt, true);
	}

	/* Drop any completion signal left by a previous frame so this take()
	 * is not satisfied by a stale sem count.
	 */
	k_sem_reset(&data->tx_done);

	/* Assert tx_busy for the duration of the transfer so stop() does not
	 * yank RX off (and disturb the lmac154 FSM) while a TX is in flight.
	 */
	data->tx_busy = true;

	int trigger_ret = lmac154_triggerParamTxExt(&data->tx_param);

	/* triggerParamTxExt returns:
	 *   >=0 (0 submitted / 1 queued / 2 CSL backoff / 3 acking): lmac154 has
	 *     accepted the frame and scheduled an alarm; it WILL call tx_done_cb.
	 *   <0: real failure (-6 AES, -5 delayed-TX window expired, -2/-3 bad
	 *     param combination).
	 * Treat >=0 as success and wait for the callback. Mapping any non-zero
	 * return to an error (as before) mis-reported the queued/backoff cases as
	 * an abort, which starved OpenThread's TX and left the device detached.
	 */
	if (trigger_ret < 0) {
		atomic_inc(&data->stats.tx_trigger_fail);
		LOG_WRN("tx: triggerParamTxExt failed: %d", trigger_ret);
		data->tx_busy = false;
		return (trigger_ret == -2 || trigger_ret == -3) ? -EINVAL : -EIO;
	}
	int take_ret = k_sem_take(&data->tx_done,
				  K_MSEC(CONFIG_IEEE802154_QCC74X_TX_TIMEOUT_MS));
	data->tx_busy = false;

	if (take_ret != 0) {
		/* TX completion timed out. Do NOT call lmac154_monitor() here:
		 * the periodic RF-FSM watchdog (qcc74x_monitor_handler) already
		 * detects the stuck state (RX length 255) and recovers the FSM.
		 * Calling monitor on-demand from this TX-abort path races with
		 * idle RX windows and breaks frame reception; FSM recovery is
		 * left to the periodic watchdog alone.
		 */
		atomic_inc(&data->stats.tx_timeout);
		LOG_WRN("tx: completion timeout (%d ms), mode=%d len=%u fcf=0x%04x trigger=%d rf_state=%d, aborting",
			CONFIG_IEEE802154_QCC74X_TX_TIMEOUT_MS, mode, len, fcf,
			trigger_ret, (int)lmac154_getRFState());
		return -EIO;
	}

	atomic_inc(&data->stats.tx_done);
	/* Deliver the peer's ACK (captured by lmac154 when the TX was ACKED) to
	 * the OT platform layer via ieee802154_handle_ack(), which caches the ACK
	 * PSDU for the subsequent otPlatRadioTxDone(). Without this OT always
	 * gets aAckFrame=NULL and can never parse enh-ack contents (link-metrics
	 * IEs). lmac154 provides a raw MAC frame from the FCF (no PHY length
	 * prefix); QCC74x runs INCL_FCS so ack_frame_len includes the 2-byte FCS.
	 */
	if (data->ack_frame_len > 0) {
		struct net_pkt *ack_pkt = net_pkt_rx_alloc_with_buffer(
			data->iface, data->ack_frame_len, AF_UNSPEC, 0, K_NO_WAIT);

		if (ack_pkt == NULL) {
			LOG_WRN("Failed to allocate ACK packet");
		} else if (net_pkt_write(ack_pkt, data->ack_frame,
					 data->ack_frame_len) < 0) {
			LOG_WRN("Failed to write ACK data to packet");
			net_pkt_unref(ack_pkt);
		} else {
			net_pkt_set_ieee802154_lqi(ack_pkt, data->ack_lqi);
			net_pkt_set_ieee802154_rssi_dbm(ack_pkt, (int16_t)data->ack_rssi);
			net_pkt_cursor_init(ack_pkt);
			if (ieee802154_handle_ack(data->iface, ack_pkt) != NET_OK) {
				LOG_INF("ACK packet not handled by platform layer");
			}
			net_pkt_unref(ack_pkt);
		}
		data->ack_frame_len = 0;
	}

	/* Map lmac154 status -> errno expected by the OT platform radio layer:
	 *   0        -> OT_ERROR_NONE
	 *   -ENOMSG  -> OT_ERROR_NO_ACK
	 *   -EBUSY   -> OT_ERROR_CHANNEL_ACCESS_FAILURE
	 *   -EIO     -> OT_ERROR_ABORT
	 */
	switch (data->tx_status) {
	case LMAC154_TX_STATUS_TX_FINISHED:
	case LMAC154_TX_STATUS_ACKED:
		atomic_inc(&data->stats.tx_ok);
		return 0;
	case LMAC154_TX_STATUS_NO_ACK:
		atomic_inc(&data->stats.tx_no_ack);
		return -ENOMSG;
	case LMAC154_TX_STATUS_CSMA_FAILED:
	case LMAC154_TX_STATUS_CCA_FAILED:
		atomic_inc(&data->stats.tx_cca_fail);
		return -EBUSY;
	case LMAC154_TX_STATUS_TX_ABORTED:
	case LMAC154_TX_STATUS_HW_ERROR:
	case LMAC154_TX_STATUS_DELAY_ERROR:
	default:
		atomic_inc(&data->stats.tx_abort);
		return -EIO;
	}
}

static int qcc74x_configure(const struct device *dev,
			    enum ieee802154_config_type type,
			    const struct ieee802154_config *config)
{
	struct qcc74x_ieee802154_data *data = dev->data;

	switch (type) {
	case IEEE802154_CONFIG_AUTO_ACK_FPB:
		/* OT enables/disables source-address matching for the Frame Pending
		 * Bit. Forward to lmac154. In 2015 SW-ack mode the FPB is driven by
		 * the FPT contents (managed via ACK_FPB below) rather than this HW
		 * auto-ack register, but mirror Bouffalo for parity and future
		 * HW-auto-ack use. THREAD mode matches lmac154's default semantics.
		 */
		lmac154_setFramePendingSourceMatch(config->auto_ack_fpb.enabled);
		break;

	case IEEE802154_CONFIG_ACK_FPB: {
		/* Add/remove a source address in the lmac154 Frame Pending Table.
		 * FPT lookup (HW-accelerated for enh-ack, SW for legacy ack) sets
		 * FPB=1 for in-table addresses, 0 otherwise; matches
		 * IEEE802154_FPB_ADDR_MATCH_THREAD. addr is little-endian for short
		 * (2B) and extended (8B); the 8 extended bytes are copied in OT
		 * order (no reversal), matching Bouffalo and the HW lookup path.
		 */
		lmac154_fpt_status_t fst;

		if (config->ack_fpb.addr == NULL) {
			/* NULL addr + disabled: clear all entries of the given type
			 * (per-type, like Bouffalo's ClearSrcMatch*Entries).
			 */
			if (!config->ack_fpb.enabled) {
				if (!config->ack_fpb.extended) {
					uint16_t list[128];
					uint8_t num = ARRAY_SIZE(list);

					lmac154_fptGetShortAddrList(list, &num);
					for (uint8_t i = 0; i < num; i++) {
						lmac154_fptRemoveShortAddr(list[i]);
					}
				} else {
					uint8_t list[32][8];
					uint8_t num = ARRAY_SIZE(list);

					lmac154_fptGetLongAddrList(list, &num);
					for (uint8_t i = 0; i < num; i++) {
						uint32_t xaddr[2];

						memcpy(xaddr, list[i], sizeof(xaddr));
						lmac154_fptRemoveLongAddr(xaddr);
					}
				}
			}
			break;
		}

		if (config->ack_fpb.extended) {
			uint32_t xaddr[2];

			memcpy(xaddr, config->ack_fpb.addr, sizeof(xaddr));
			if (config->ack_fpb.enabled) {
				fst = lmac154_fptSetLongAddrPending(xaddr, 1);
				if (fst == LMAC154_FPT_STATUS_NO_RESOURCE) {
					return -ENOMEM;
				}
			} else {
				fst = lmac154_fptRemoveLongAddr(xaddr);
				if (fst == LMAC154_FPT_STATUS_ADDR_NOT_FOUND) {
					return -ENOENT;
				}
			}
		} else {
			uint16_t sadr = sys_get_le16(config->ack_fpb.addr);

			if (config->ack_fpb.enabled) {
				fst = lmac154_fptSetShortAddrPending(sadr, 1);
				if (fst == LMAC154_FPT_STATUS_NO_RESOURCE) {
					return -ENOMEM;
				}
			} else {
				fst = lmac154_fptRemoveShortAddr(sadr);
				if (fst == LMAC154_FPT_STATUS_ADDR_NOT_FOUND) {
					return -ENOENT;
				}
			}
		}
		break;
	}

	case IEEE802154_CONFIG_PROMISCUOUS:
		atomic_inc(&data->stats.cfg_promisc);
		if (config->promiscuous) {
			lmac154_enableRxPromiscuousMode(0, 0);
		} else {
			lmac154_disableRxPromiscuousMode();
		}
		break;

	case IEEE802154_CONFIG_EVENT_HANDLER:
		/* Accepted but unused: TX completion is reported via tx(). */
		atomic_inc(&data->stats.cfg_event_handler);
		break;

	case IEEE802154_CONFIG_RX_ON_WHEN_IDLE:
		atomic_inc(&data->stats.cfg_rx_on_idle);
		lmac154_setRxStateWhenIdle(config->rx_on_when_idle);
		break;

	case IEEE802154_CONFIG_MAC_KEYS: {
		/* OT installs prev/curr/next MAC keys; cache each by its key id
		 * so TX can pick the right one from the frame's aux header.
		 */
		const struct ieee802154_key *key = config->mac_keys;

		atomic_inc(&data->stats.cfg_mac_keys);
		for (uint8_t i = 0; i < QCC74X_NUM_MAC_KEYS && key != NULL &&
				    key->key_value != NULL; i++, key++) {
			uint8_t idx = (key->key_id_mode == 1) ? (key->key_id[0] & 0x3) : 0;

			memcpy(data->mac_keys[idx], key->key_value,
			       sizeof(data->mac_keys[idx]));
			data->mac_key_set |= BIT(idx);
		}
		break;
	}

	case IEEE802154_CONFIG_FRAME_COUNTER:
		atomic_inc(&data->stats.cfg_frame_counter);
		atomic_set(&data->frame_cnt, config->frame_counter);
		break;

	case IEEE802154_CONFIG_FRAME_COUNTER_IF_LARGER:
		atomic_inc(&data->stats.cfg_frame_counter);
		if (config->frame_counter > (uint32_t)atomic_get(&data->frame_cnt)) {
			atomic_set(&data->frame_cnt, config->frame_counter);
		}
		break;

	case IEEE802154_CONFIG_ENH_ACK_HEADER_IE: {
		/* Cache the enh-ack header IE (link metrics) so it can be
		 * appended when constructing enhanced ACKs in the ISR.
		 * The IE is a 2-byte header (element_id + length) followed
		 * by the content bytes. Total = 2 + ie->length.
		 */
		const struct ieee802154_header_ie *ie = config->ack_ie.header_ie;
		if (ie != NULL) {
			uint8_t ie_len = sizeof(*ie) + ie->length;
			if (ie_len <= sizeof(data->enh_ack_ie)) {
				memcpy(data->enh_ack_ie, ie, ie_len);
				data->enh_ack_ie_len = ie_len;
			}
		} else {
			data->enh_ack_ie_len = 0;
		}
		break;
	}

	default:
		atomic_inc(&data->stats.cfg_unsupported);
		LOG_WRN("unsupported cfg type %u", (unsigned)type);
		return -ENOTSUP;
	}

	return 0;
}

#if defined(CONFIG_NET_PKT_TXTIME)
static net_time_t qcc74x_get_time(const struct device *dev)
{
	ARG_UNUSED(dev);

	return (net_time_t)zb_timer_get_current_time_us() * NSEC_PER_USEC;
}
#endif

#if defined(CONFIG_SHELL)
static void qcc74x_stats_clear(struct qcc74x_ieee802154_stats *stats)
{
	atomic_t *first = &stats->set_channel;
	size_t count = sizeof(*stats) / sizeof(*first);

	for (size_t i = 0; i < count; i++) {
		atomic_set(&first[i], 0);
	}
}

static int qcc74x_shell_stats(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ieee802154));
	struct qcc74x_ieee802154_data *data = dev->data;
	struct qcc74x_ieee802154_stats *s = &data->stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "set_channel: %ld", (long)atomic_get(&s->set_channel));
	shell_print(sh, "start: %ld", (long)atomic_get(&s->start));
	shell_print(sh, "stop: %ld", (long)atomic_get(&s->stop));
	shell_print(sh, "tx: %ld", (long)atomic_get(&s->tx));
	shell_print(sh, "tx_sec: %ld", (long)atomic_get(&s->tx_sec));
	shell_print(sh, "tx_trigger_fail: %ld", (long)atomic_get(&s->tx_trigger_fail));
	shell_print(sh, "tx_timeout: %ld", (long)atomic_get(&s->tx_timeout));
	shell_print(sh, "tx_done: %ld", (long)atomic_get(&s->tx_done));
	shell_print(sh, "tx_ok: %ld", (long)atomic_get(&s->tx_ok));
	shell_print(sh, "tx_no_ack: %ld", (long)atomic_get(&s->tx_no_ack));
	shell_print(sh, "tx_cca_fail: %ld", (long)atomic_get(&s->tx_cca_fail));
	shell_print(sh, "tx_abort: %ld", (long)atomic_get(&s->tx_abort));
	shell_print(sh, "rx_done: %ld", (long)atomic_get(&s->rx_done));
	shell_print(sh, "rx_drop_status: %ld", (long)atomic_get(&s->rx_drop_status));
	shell_print(sh, "rx_drop_error: %ld", (long)atomic_get(&s->rx_drop_error));
	shell_print(sh, "rx_error_mask: 0x%lx", (long)atomic_get(&s->rx_error_mask));
	shell_print(sh, "rx_drop_len: %ld", (long)atomic_get(&s->rx_drop_len));
	shell_print(sh, "rx_alloc_fail: %ld", (long)atomic_get(&s->rx_alloc_fail));
	shell_print(sh, "rx_delivered: %ld", (long)atomic_get(&s->rx_delivered));
	shell_print(sh, "enh_ack_req: %ld", (long)atomic_get(&s->enh_ack_req));
	shell_print(sh, "enh_ack_sent: %ld", (long)atomic_get(&s->enh_ack_sent));
	shell_print(sh, "enh_ack_fail: %ld", (long)atomic_get(&s->enh_ack_fail));
	shell_print(sh, "cfg_promisc: %ld", (long)atomic_get(&s->cfg_promisc));
	shell_print(sh, "cfg_event_handler: %ld", (long)atomic_get(&s->cfg_event_handler));
	shell_print(sh, "cfg_rx_on_idle: %ld", (long)atomic_get(&s->cfg_rx_on_idle));
	shell_print(sh, "cfg_mac_keys: %ld", (long)atomic_get(&s->cfg_mac_keys));
	shell_print(sh, "cfg_frame_counter: %ld", (long)atomic_get(&s->cfg_frame_counter));
	shell_print(sh, "cfg_unsupported: %ld", (long)atomic_get(&s->cfg_unsupported));
	shell_print(sh, "filter_ieee_addr: %ld", (long)atomic_get(&s->filter_ieee_addr));
	shell_print(sh, "filter_short_addr: %ld", (long)atomic_get(&s->filter_short_addr));
	shell_print(sh, "filter_pan_id: %ld", (long)atomic_get(&s->filter_pan_id));
	/* FCS sanity: driver advertises IEEE802154_HW_FCS and hands up
	 * info->rx_length (FCS included). OpenThread consumes it under
	 * CONFIG_IEEE802154_L2_PKT_INCL_FCS=y; confirm incl_fcs reads 1.
	 */
	shell_print(sh, "fcs: hw_fcs_cap=1 incl_fcs=%d",
		    IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS));

	return 0;
}

static int qcc74x_shell_reset(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ieee802154));
	struct qcc74x_ieee802154_data *data = dev->data;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	qcc74x_stats_clear(&data->stats);
	shell_print(sh, "qcc154 stats reset");

	return 0;
}

static int qcc74x_shell_radio(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(ieee802154));
	struct qcc74x_ieee802154_data *data = dev->data;
	uint8_t long_addr[8];
	uint8_t long_addr2[8];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	lmac154_getLongAddr(long_addr);
	lmac154_get2ndLongAddr(long_addr2);

	shell_print(sh, "driver_channel: %u", data->channel);
	shell_print(sh, "lmac_channel: %u", (uint8_t)lmac154_getChannel() + QCC74X_CHANNEL_MIN);
	shell_print(sh, "driver_pan_id: 0x%04x", data->pan_id);
	shell_print(sh, "lmac_pan_id: 0x%04x", lmac154_getPanId());
	shell_print(sh, "lmac_pan_id_2nd: 0x%04x", lmac154_get2ndPanId());
	shell_print(sh, "driver_short_addr: 0x%04x", data->short_addr);
	shell_print(sh, "lmac_short_addr: 0x%04x", lmac154_getShortAddr());
	shell_print(sh, "lmac_short_addr_2nd: 0x%04x", lmac154_get2ndShortAddr());
	shell_print(sh, "driver_ext_addr: %02x%02x%02x%02x%02x%02x%02x%02x",
		    data->ext_addr[0], data->ext_addr[1], data->ext_addr[2],
		    data->ext_addr[3], data->ext_addr[4], data->ext_addr[5],
		    data->ext_addr[6], data->ext_addr[7]);
	shell_print(sh, "lmac_long_addr: %02x%02x%02x%02x%02x%02x%02x%02x",
		    long_addr[0], long_addr[1], long_addr[2], long_addr[3],
		    long_addr[4], long_addr[5], long_addr[6], long_addr[7]);
	shell_print(sh, "lmac_long_addr_2nd: %02x%02x%02x%02x%02x%02x%02x%02x",
		    long_addr2[0], long_addr2[1], long_addr2[2], long_addr2[3],
		    long_addr2[4], long_addr2[5], long_addr2[6], long_addr2[7]);
	shell_print(sh, "rx_on_when_idle: %u", lmac154_isRxStateWhenIdle() ? 1 : 0);
	shell_print(sh, "rx_promiscuous: %u", lmac154_isRxPromiscuousModeEnabled() ? 1 : 0);
	shell_print(sh, "rx_reject_policy: 0x%x", lmac154_getRxRejectPolicy());
	shell_print(sh, "rx_accept_policy: 0x%x", lmac154_getRxAcceptPolicy());
	shell_print(sh, "rf_state: %d", (int)lmac154_getRFState());
	shell_print(sh, "rssi: %d", lmac154_getRSSI());
	shell_print(sh, "lqi: %u", (uint8_t)lmac154_getLQI());

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(qcc154_cmds,
	SHELL_CMD_ARG(radio, NULL, "Print QCC74x IEEE 802.15.4 radio state.",
		      qcc74x_shell_radio, 1, 0),
	SHELL_CMD_ARG(stats, NULL, "Print QCC74x IEEE 802.15.4 driver counters.",
		      qcc74x_shell_stats, 1, 0),
	SHELL_CMD_ARG(reset, NULL, "Reset QCC74x IEEE 802.15.4 driver counters.",
		      qcc74x_shell_reset, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(qcc154, &qcc154_cmds, "QCC74x IEEE 802.15.4 diagnostics.", NULL);
#endif

/* -------------------------------------------------------------------------- */
/* Device / network interface instantiation                                  */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_NET_L2_OPENTHREAD)
#define L2 OPENTHREAD_L2
#define L2_CTX_TYPE NET_L2_GET_CTX_TYPE(OPENTHREAD_L2)
#define MTU 1280
#elif defined(CONFIG_NET_L2_IEEE802154)
#define L2 IEEE802154_L2
#define L2_CTX_TYPE NET_L2_GET_CTX_TYPE(IEEE802154_L2)
#define MTU IEEE802154_MTU
#elif defined(CONFIG_NET_L2_CUSTOM_IEEE802154)
#define L2 CUSTOM_IEEE802154_L2
#define L2_CTX_TYPE NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2)
#define MTU CONFIG_NET_L2_CUSTOM_IEEE802154_MTU
#endif

#define QCC74X_IEEE802154_DEFINE(inst)                                          \
	static void qcc74x_irq_config_##inst(void);                              \
	static struct qcc74x_ieee802154_data qcc74x_ieee802154_data_##inst;      \
	static const struct qcc74x_ieee802154_cfg qcc74x_ieee802154_cfg_##inst = {\
		.irq_config_func = qcc74x_irq_config_##inst,                     \
	};                                                                       \
	static void qcc74x_irq_config_##inst(void)                               \
	{                                                                        \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),     \
			    qcc74x_isr, DEVICE_DT_INST_GET(inst), 0);            \
		irq_enable(DT_INST_IRQN(inst));                                 \
	}                                                                        \
	NET_DEVICE_DT_INST_DEFINE(inst,                                          \
			qcc74x_ieee802154_init, NULL,                           \
			&qcc74x_ieee802154_data_##inst,                          \
			&qcc74x_ieee802154_cfg_##inst,                            \
			CONFIG_IEEE802154_QCC74X_INIT_PRIORITY,                  \
			&qcc74x_ieee802154_radio_api,                            \
			L2, L2_CTX_TYPE, MTU);

DT_INST_FOREACH_STATUS_OKAY(QCC74X_IEEE802154_DEFINE)
