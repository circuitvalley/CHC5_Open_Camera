// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell PTP driver for 88E1510, 88E1512, 88E1514 and 88E1518 PHYs
 *
 * Ideas taken from 88E6xxx DSA and DP83640 drivers. This file
 * implements the packet timestamping support only (PTP).  TAI
 * support is separate.
 */
#include <linux/bitfield.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/marvell_ptp.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/uaccess.h>

/* Global configuration */

/* This defines which incoming or outgoing PTP frames are timestamped.
 * MV88E6xxx DSA sets messages types 0-3 (sync, delay request, pdelay
 * request and pdelay response.)
 *
 * We need to timestamp `t1' the departure of Sync (0) messages so that
 * timestamp can be sent in the Follow_Up message. We also need to
 * timestamp the arrival of these messages to give `t2'.
 *
 * We need to timestamp the transmission of the Delay_Req (1) messages
 * for `t3' and the arrival of this mssage for `t4'.
 *
 * For IEEE1588 v2, we also need to timestamp the PDelay_Req (3) and
 * PDelay_Resp (4) messages.
 *
 * The Follow_Up (8) and Delay_Resp (9) messages do not need to be
 * timestamped.
 *
 * PTP_MSGTYPE_PDELAY_REQ, PTP_MSGTYPE_PDELAY_RESP
 */
#define MV_PTP_MSD_ID_TS_EN	(BIT(PTP_MSGTYPE_SYNC) | \
				 BIT(PTP_MSGTYPE_DELAY_REQ) | \
				 BIT(PTP_MSGTYPE_PDELAY_REQ) | \
				 BIT(PTP_MSGTYPE_PDELAY_RESP))

/* Direct Sync messages to Arr0 and delay messages to Arr1. MV88E6xxx
 * DSA sets message type 3 (pdelay response.)
 *
 * Putting Delay_Req (1) arrival into Arr1 means that if we have a busy
 * network with Sync (0) messages also being received, we still get a
 * hardware timestamp for the Delay_Req message.
 *
 * PTP_MSGTYPE_PDELAY_RESP
 */
#define MV_PTP_TS_ARR_PTR	(BIT(PTP_MSGTYPE_DELAY_REQ) | \
				 BIT(PTP_MSGTYPE_PDELAY_RESP))

/* Armada 38x and 88e151x calls this PTP Global Configuration 0:
 * 15:0 PTPEType - Ethernet type
 */
#define PTPG_ETYPE			0

/* Armada 38x and 88e151x calls this PTP Global Configuration1
 * 15:0 MsgIDTSEn - Message Identifier Time Stamp Enable
 * 15:0 MsgType (88E6393x) Message Type Time Stamp Enable
 */
#define PTPG_MSGIDTSEN			1

/* Armada 38x and 88e151x calls this PTP Global Configuration2
 * 15:0 TSArrPtr - Time Stamp Arrival Time Pointer
 */
#define PTPG_TSARRPTR			2

/* Armada 38x calls this PTP Global Status0. 88E151x "PTP Global Status".
 * Armada 38x: 5:0 PTPInt - Port interrupt
 * 88E151x   : 0: PTPInt - Interrupt
 */
#define PTPG_STATUS				8

#define TX_TIMEOUT_MS	40
#define RX_TIMEOUT_MS	40

#define RX_TS_MAX_AGE_MS	1000

static unsigned int rx_drain_ms = 200;
module_param(rx_drain_ms, uint, 0644);
MODULE_PARM_DESC(rx_drain_ms,
		 "empty the PHY arrival timestamp registers every N ms while RX timestamping is on (0 = off)");

#define PTP_PORT_CONFIG_0			0
#define PTP_PORT_CONFIG_0_DISTSPECCHECK		BIT(11)
#define PTP_PORT_CONFIG_0_DISTSOVERWRITE	BIT(1)
#define PTP_PORT_CONFIG_0_DISPTP		BIT(0)
#define PTP_PORT_CONFIG_1			1
#define PTP_PORT_CONFIG_1_IPJUMP		GENMASK(13, 8)
#define PTP_PORT_CONFIG_1_ETJUMP		GENMASK(4, 0)
#define PTP_PORT_CONFIG_2			2
#define PTP_PORT_CONFIG_2_DEPINTEN		BIT(1)
#define PTP_PORT_CONFIG_2_ARRINTEN		BIT(0)

struct marvell_ts_cb {
	const struct ptp_header *hdr;
	unsigned long timeout;
	u16 seq;
};
#define MARVELL_TS_CB(skb)	((struct marvell_ts_cb *)(skb)->cb)

/* RX queue support */

/* Deliver a skb with its timestamp back to the networking core */
static void marvell_rxq_rx(struct sk_buff *skb, u64 ns)
{
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);

	pr_debug("rx: seq %u delivering timestamp\n", MARVELL_TS_CB(skb)->seq);

	memset(shhwtstamps, 0, sizeof(*shhwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(ns);
	netif_rx(skb);
}

/* Get a rx timestamp entry. Try the free list, and if that fails,
 * steal the oldest off the pending list.
 */
static struct marvell_rxts *marvell_rxq_get_rxts(struct marvell_rxq *rxq)
{
	if (!list_empty(&rxq->rx_free))
		return list_first_entry(&rxq->rx_free, struct marvell_rxts,
					node);

	return list_last_entry(&rxq->rx_pend, struct marvell_rxts, node);
}

static void marvell_rxq_init(struct marvell_rxq *rxq)
{
	int i;

	spin_lock_init(&rxq->rx_lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_pend);
	skb_queue_head_init(&rxq->rx_queue);

	for (i = 0; i < ARRAY_SIZE(rxq->rx_ts); i++)
		list_add_tail(&rxq->rx_ts[i].node, &rxq->rx_free);
}

static void marvell_rxq_purge(struct marvell_rxq *rxq)
{
	struct sk_buff_head list;
	unsigned long flags;

	__skb_queue_head_init(&list);
	spin_lock_irqsave(&rxq->rx_lock, flags);
	skb_queue_splice_init(&rxq->rx_queue, &list);
	spin_unlock_irqrestore(&rxq->rx_lock, flags);

	__skb_queue_purge(&list);
}

static void marvell_rxq_rx_ts(struct marvell_rxq *rxq, u16 seq, u64 ns)
{
	struct marvell_rxts *rxts;
	struct sk_buff *skb;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&rxq->rx_lock, flags);

	/* Search the rx queue for a matching skb */
	skb_queue_walk(&rxq->rx_queue, skb) {
		if (MARVELL_TS_CB(skb)->seq == seq) {
			__skb_unlink(skb, &rxq->rx_queue);
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("rx: seq %u skb not found, pending\n", seq);

		rxts = marvell_rxq_get_rxts(rxq);
		rxts->ns = ns;
		rxts->seq = seq;
		rxts->expires = jiffies + msecs_to_jiffies(RX_TS_MAX_AGE_MS);
		list_move(&rxts->node, &rxq->rx_pend);
	}

	spin_unlock_irqrestore(&rxq->rx_lock, flags);

	if (found)
		marvell_rxq_rx(skb, ns);
}

static bool marvell_rxq_rxtstamp(struct marvell_rxq *rxq, struct sk_buff *skb,
				 u16 seq, const struct ptp_header *hdr)
{
	struct marvell_rxts *rxts, *tmp;
	unsigned long flags;
	bool found = false;
	u64 ns = 0;

	spin_lock_irqsave(&rxq->rx_lock, flags);

	/* Search the pending receive timestamps for a matching seqid */
	list_for_each_entry_safe(rxts, tmp, &rxq->rx_pend, node) {
		if (time_after(jiffies, rxts->expires)) {
			list_move_tail(&rxts->node, &rxq->rx_free);
			continue;
		}
		if (rxts->seq == seq) {
			found = true;
			ns = rxts->ns;
			/* Move this timestamp entry to the free list */
			list_move_tail(&rxts->node, &rxq->rx_free);
			break;
		}
	}

	if (!found) {
		pr_debug("rx: seq %u pending ts not found, queueing\n", seq);

		/* Store the seqid and queue the skb. Do this under the lock
		 * to ensure we don't miss any timestamps appended to the
		 * rx_pend list.
		 */
		MARVELL_TS_CB(skb)->hdr = hdr;
		MARVELL_TS_CB(skb)->seq = seq;
		MARVELL_TS_CB(skb)->timeout = jiffies +
			msecs_to_jiffies(RX_TIMEOUT_MS);
		__skb_queue_tail(&rxq->rx_queue, skb);
	}

	spin_unlock_irqrestore(&rxq->rx_lock, flags);

	if (found)
		/* We found the corresponding timestamp. If we can add the
		 * timestamp, do we need to go through the netif_rx_ni()
		 * path, or would it be more efficient to add the timestamp
		 * and return "false" from marvell_ts_rxtstamp() instead?
		 */
		marvell_rxq_rx(skb, ns);

	return found;
}

static void marvell_rxq_expire(struct marvell_rxq *rxq,
			       struct sk_buff_head *list)
{
	struct marvell_rxts *rxts, *tmp;
	unsigned long flags;
	struct sk_buff *skb;

	spin_lock_irqsave(&rxq->rx_lock, flags);
	list_for_each_entry_safe(rxts, tmp, &rxq->rx_pend, node)
		if (time_after(jiffies, rxts->expires))
			list_move_tail(&rxts->node, &rxq->rx_free);
	while ((skb = skb_dequeue(&rxq->rx_queue)) != NULL) {
		if (!time_is_before_jiffies(MARVELL_TS_CB(skb)->timeout)) {
			__skb_queue_head(&rxq->rx_queue, skb);
			break;
		}
		__skb_queue_tail(list, skb);
	}
	spin_unlock_irqrestore(&rxq->rx_lock, flags);
}

/* Extract the sequence ID */
static u16 ptp_seqid(const struct ptp_header *ptp_hdr)
{
	const __be16 *seqp = &ptp_hdr->sequence_id;

	return be16_to_cpup(seqp);
}

static u8 ptp_msgid(const struct ptp_header *ptp_hdr)
{
	return ptp_hdr->tsmt & 15;
}

static void marvell_ts_schedule(struct marvell_ts *ts)
{
	marvell_tai_schedule(ts->tai, 0);
}

/* Check for a rx timestamp entry, try to find the corresponding skb and
 * deliver it, otherwise add the rx timestamp to the queue of pending
 * timestamps.
 */
static int marvell_ts_rx_ts(struct marvell_ts *ts, int q)
{
	enum marvell_ts_reg reg;
	struct marvell_hwts hwts;
	s64 age;
	int err;
	u64 ns;

	if (q)
		reg = MARVELL_TS_ARR1;
	else
		reg = MARVELL_TS_ARR0;

	err = ts->ops->ts_port_read_ts(ts->dev, &hwts, ts->port, reg);
	dev_dbg(ts->dev, "p%uq%u: rx: read_ts %d\n", ts->port, q, err);
	if (err <= 0)
		return 0;

	dev_dbg(ts->dev, "p%uq%u: tx: stat=0x%x seq=%u ts=%u\n",
		ts->port, q, hwts.stat, hwts.seq, hwts.time);

	if ((hwts.stat & MV_STATUS_INTSTATUS_MASK) !=
	    MV_STATUS_INTSTATUS_NORMAL)
		dev_dbg(ts->dev,
			"p%uq%u: rx: timestamp overrun (stat=0x%x seq=%u)\n",
			ts->port, q, hwts.stat, hwts.seq);

	ns = marvell_tai_cyc2time(ts->tai, hwts.time);

	age = (s64)(marvell_tai_gettime_ns(ts->tai) - ns);
	if (abs(age) > (s64)RX_TS_MAX_AGE_MS * NSEC_PER_MSEC) {
		dev_dbg(ts->dev,
			"p%uq%u: rx: dropping stale timestamp (seq=%u age=%lld ms)\n",
			ts->port, q, hwts.seq, div_s64(age, NSEC_PER_MSEC));
		return 1;
	}

	marvell_rxq_rx_ts(&ts->rxq[q], hwts.seq, ns);

	return 1;
}

/* Check whether the packet is suitable for timestamping, and if so,
 * try to find a pending timestamp for it. If no timestamp is found,
 * queue the packet with a timeout.
 */
bool marvell_ts_rxtstamp(struct marvell_ts *ts, struct sk_buff *skb, int type)
{
	const struct ptp_header *ptp_hdr;
	u16 msgidvec, seq;
	unsigned int q;
	u8 msgid;

	if (ts->rx_filter == HWTSTAMP_FILTER_NONE)
		return false;

	ptp_hdr = ptp_parse_header(skb, type);
	if (!ptp_hdr)
		return false;

	msgid = ptp_msgid(ptp_hdr);
	seq = ptp_seqid(ptp_hdr);

	/* Only check for timestamps for PTP packets whose message ID value
	 * is one that we are capturing timestamps for. This is part of the
	 * global configuration and is therefore fixed.
	 */
	msgidvec = BIT(msgid);
	if (msgidvec & ~MV_PTP_MSD_ID_TS_EN) {
		dev_dbg(ts->dev, "p%u: rx: not timestamping msgid %u seq %u\n",
			ts->port, msgid, seq);
		return false;
	}

	/* Determine the queue which the timestamp for this message ID will
	 * appear. This is part of the global configuration and is therefore
	 * fixed.
	 */
	q = !!(msgidvec & MV_PTP_TS_ARR_PTR);

	dev_dbg(ts->dev, "p%uq%u: rx: timestamping msgid %u seq %u\n",
		ts->port, q, msgid, seq);

	if (!marvell_rxq_rxtstamp(&ts->rxq[q], skb, seq, ptp_hdr))
		marvell_ts_schedule(ts);

	return true;
}
EXPORT_SYMBOL_GPL(marvell_ts_rxtstamp);

/* Move any expired skbs on to our own list, and then hand the contents of
 * our list to netif_rx() - this avoids calling netif_rx() with our
 * mutex held.
 */
static void marvell_ts_rx_expire(struct marvell_ts *ts)
{
	const struct ptp_header *ptp_hdr;
	struct sk_buff_head list;
	struct sk_buff *skb;
	int i;

	__skb_queue_head_init(&list);

	for (i = 0; i < ARRAY_SIZE(ts->rxq); i++)
		marvell_rxq_expire(&ts->rxq[i], &list);

	while ((skb = __skb_dequeue(&list)) != NULL) {
		ptp_hdr = MARVELL_TS_CB(skb)->hdr;
		dev_dbg(ts->dev, "p%u: rx: expiring skb: seq=%u msgid=%u\n",
			ts->port, MARVELL_TS_CB(skb)->seq,
			ptp_msgid(ptp_hdr));
		netif_rx(skb);
	}
}

/* Complete the transmit timestamping; this is called to read the transmit
 * timestamp from the PHY, and report back the transmitted timestamp.
 */
static int marvell_ts_txtstamp_complete(struct marvell_ts *ts)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb = ts->tx_skb;
	struct marvell_hwts hwts;
	int err;
	u64 ns;

	err = ts->ops->ts_port_read_ts(ts->dev, &hwts, ts->port,
				       MARVELL_TS_DEP);
	dev_dbg(ts->dev, "p%u: tx: read_ts %d\n", ts->port, err);
	if (err < 0)
		goto fail;

	if (err == 0) {
		if (time_is_before_jiffies(MARVELL_TS_CB(skb)->timeout)) {
			dev_warn_ratelimited(ts->dev,
					     "p%u: tx: timestamp timeout\n",
					     ts->port);
			goto free;
		}
		return 0;
	}

	dev_dbg(ts->dev, "p%u: tx: stat=0x%x seq=%u ts=%u\n", ts->port,
		hwts.stat, hwts.seq, hwts.time);

	/* Check the status */
	if ((hwts.stat & MV_STATUS_INTSTATUS_MASK) !=
	    MV_STATUS_INTSTATUS_NORMAL) {
		dev_warn_ratelimited(ts->dev,
				     "p%u: tx: timestamp overrun (stat=0x%x seq=%u)\n",
				     ts->port, hwts.stat, hwts.seq);
		goto free;
	}

	/* Reject if the sequence number doesn't match */
	if (hwts.seq != MARVELL_TS_CB(skb)->seq) {
		dev_warn_ratelimited(ts->dev,
				     "p%u: tx: timestamp unexpected sequence id\n",
				     ts->port);
		goto free;
	}

	ts->tx_skb = NULL;

	/* Set the timestamp */
	ns = marvell_tai_cyc2time(ts->tai, hwts.time);
	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ns_to_ktime(ns);
	skb_complete_tx_timestamp(skb, &shhwtstamps);
	return 1;

fail:
	dev_err_ratelimited(ts->dev, "p%u: failed reading PTP: %pe\n",
			    ts->port, ERR_PTR(err));
free:
	dev_kfree_skb_any(skb);
	ts->tx_skb = NULL;
	return -1;
}

/* Check whether the skb will be timestamped on transmit; we only support
 * a single outstanding skb. Add it if the slot is available. It is the
 * responsibility of the caller to check tx_flags.
 */
bool marvell_ts_txtstamp(struct marvell_ts *ts, struct sk_buff *skb, int type)
{
	const struct ptp_header *ptp_hdr;
	u8 msgid;

	if (ts->tx_type != HWTSTAMP_TX_ON)
		return false;

	ptp_hdr = ptp_parse_header(skb, type);
	if (!ptp_hdr)
		return false;

	msgid = ptp_msgid(ptp_hdr);
	if (BIT(msgid) & ~MV_PTP_MSD_ID_TS_EN) {
		dev_dbg(ts->dev, "p%u: tx: not timestamping msgid %u seq %u\n",
			ts->port, msgid, ptp_seqid(ptp_hdr));
		return false;
	}

	MARVELL_TS_CB(skb)->seq = ptp_seqid(ptp_hdr);
	MARVELL_TS_CB(skb)->timeout = jiffies +
		msecs_to_jiffies(TX_TIMEOUT_MS);

	dev_dbg(ts->dev, "p%u: tx: new, msgid=%u seq=%u\n", ts->port,
		msgid, MARVELL_TS_CB(skb)->seq);

	if (cmpxchg(&ts->tx_skb, NULL, skb) != NULL)
		return false;

	/* DP83640 marks the skb for hw timestamping. Since the MAC driver
	 * may call skb_tx_timestamp() but may not support timestamping
	 * itself, it may not set this flag. So, we need to do this here.
	 */
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	/* Only schedule the aux_work if we haven't seen an interrupt. */
	if (!ts->irq_handler_called)
		marvell_ts_schedule(ts);

	return true;
}
EXPORT_SYMBOL_GPL(marvell_ts_txtstamp);

int marvell_ts_hwtstamp_get(struct marvell_ts *ts,
			    struct kernel_hwtstamp_config *kcfg)
{
	kcfg->flags = 0;
	kcfg->tx_type = ts->tx_type;
	kcfg->rx_filter = ts->rx_filter;

	return 0;
}
EXPORT_SYMBOL_GPL(marvell_ts_hwtstamp_get);

int marvell_ts_hwtstamp_set(struct marvell_ts *ts,
			    struct kernel_hwtstamp_config *kcfg,
			    struct netlink_ext_ack *ack)
{
	u16 cfg0 = PTP_PORT_CONFIG_0_DISPTP;
	bool enabled = false;
	bool old_enabled;
	u16 cfg2 = 0;
	int err;

	if (kcfg->flags)
		return -EINVAL;

	switch (kcfg->tx_type) {
	case HWTSTAMP_TX_OFF:
		break;

	case HWTSTAMP_TX_ON:
		cfg0 = 0;
		cfg2 |= PTP_PORT_CONFIG_2_DEPINTEN;
		enabled = true;
		break;

	default:
		return -ERANGE;
	}

	switch (kcfg->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;

	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:

	/* UDPv4/IP PTP v2*/
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:

	/* 802.1AS PTP v2 */
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:

	/* 802.1AS and/or UDPv4/IP PTP v2 */
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		/* We accept 802.1AS, IEEE 1588v1 and IEEE 1588v2. We could
		 * filter on 802.1AS using the transportSpecific field, but
		 * that affects the transmit path too.
		 */
		kcfg->rx_filter = HWTSTAMP_FILTER_SOME;
		cfg0 = 0;
		cfg2 |= PTP_PORT_CONFIG_2_ARRINTEN;
		enabled = true;
		break;

	default:
		return -ERANGE;
	}

	old_enabled = ts->tx_type != HWTSTAMP_TX_OFF ||
		      ts->rx_filter != HWTSTAMP_FILTER_NONE;
	if (ts->ops->ts_port_enable && enabled && !old_enabled) {
		err = ts->ops->ts_port_enable(ts->dev, ts->port);
		if (err)
			return err;
	}

	err = ts->ops->ts_port_modify(ts->dev, ts->port, PTP_PORT_CONFIG_0,
				      PTP_PORT_CONFIG_0_DISPTP, cfg0);
	if (err)
		return err;

	err = ts->ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_2,
				     cfg2);
	if (err)
		return err;

	if (ts->ops->ts_port_disable && !enabled && old_enabled)
		ts->ops->ts_port_disable(ts->dev, ts->port);

	ts->tx_type = kcfg->tx_type;
	ts->rx_filter = kcfg->rx_filter;

	if (ts->rx_filter != HWTSTAMP_FILTER_NONE)
		marvell_ts_schedule(ts);

	return 0;
}
EXPORT_SYMBOL_GPL(marvell_ts_hwtstamp_set);

int marvell_ts_info(struct marvell_ts *ts, struct ethtool_ts_info *ts_info)
{
	if (!ts->tai)
		return 0;

	ts_info->so_timestamping |= SOF_TIMESTAMPING_TX_HARDWARE |
				    SOF_TIMESTAMPING_RX_HARDWARE |
				    SOF_TIMESTAMPING_RAW_HARDWARE;

	ts_info->phc_index = marvell_tai_ptp_clock_index(ts->tai);

	ts_info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			    BIT(HWTSTAMP_TX_ON);

	ts_info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			      ts->caps->rx_filters;

	return 0;
}
EXPORT_SYMBOL_GPL(marvell_ts_info);

static int marvell_ts_port_config(struct marvell_ts *ts)
{
	const struct marvell_ts_ops *ops = ts->ops;
	int err;

	/* Disable transport specific check (if the PTP common header)
	 * Disable timestamp overwriting (so we can read a stable entry.)
	 * Disable PTP
	 */
	err = ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_0,
				 PTP_PORT_CONFIG_0_DISTSPECCHECK |
				 PTP_PORT_CONFIG_0_DISTSOVERWRITE |
				 PTP_PORT_CONFIG_0_DISPTP);
	if (err < 0)
		return err;

	/* Set ether-type jump to 12 (to ether protocol)
	 * Set IP jump to 2 (to skip over ether protocol)
	 * Does this mean it won't pick up on VLAN packets?
	 */
	err = ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_1,
				 FIELD_PREP(PTP_PORT_CONFIG_1_IPJUMP, 2) |
				 FIELD_PREP(PTP_PORT_CONFIG_1_ETJUMP, 12));
	if (err < 0)
		return err;

	/* Disable all interrupts */
	ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_2, 0);

	return 0;
}

int marvell_ts_port_reconfig(struct marvell_ts *ts)
{
	const struct marvell_ts_ops *ops = ts->ops;
	int err;

	err = ops->ts_port_modify(ts->dev, ts->port, PTP_PORT_CONFIG_0,
				  PTP_PORT_CONFIG_0_DISTSPECCHECK |
				  PTP_PORT_CONFIG_0_DISTSOVERWRITE,
				  PTP_PORT_CONFIG_0_DISTSPECCHECK |
				  PTP_PORT_CONFIG_0_DISTSOVERWRITE);
	if (err < 0)
		return err;

	return ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_1,
				  FIELD_PREP(PTP_PORT_CONFIG_1_IPJUMP, 2) |
				  FIELD_PREP(PTP_PORT_CONFIG_1_ETJUMP, 12));
}
EXPORT_SYMBOL_GPL(marvell_ts_port_reconfig);

static void marvell_ts_port_disable(struct marvell_ts *ts)
{
	/* Disable PTP */
	ts->ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_0,
			       PTP_PORT_CONFIG_0_DISPTP);

	/* Disable interrupts */
	ts->ops->ts_port_write(ts->dev, ts->port, PTP_PORT_CONFIG_2, 0);

	/* Disable the port */
	if (ts->ops->ts_port_disable &&
	    (ts->tx_type != HWTSTAMP_TX_OFF ||
	     ts->rx_filter != HWTSTAMP_FILTER_NONE))
		ts->ops->ts_port_disable(ts->dev, ts->port);
}

long marvell_ts_aux_work(struct marvell_ts *ts)
{
	if (!ts->irq_handler_called) {
		if (ts->rx_filter != HWTSTAMP_FILTER_NONE) {
			marvell_ts_rx_ts(ts, 0);
			marvell_ts_rx_ts(ts, 1);
		}

		if (ts->tx_skb)
			marvell_ts_txtstamp_complete(ts);
	}

	marvell_ts_rx_expire(ts);

	if (ts->tx_skb)
		return 0;
	else if (!skb_queue_empty(&ts->rxq[0].rx_queue) ||
		 !skb_queue_empty(&ts->rxq[1].rx_queue))
		return 1;

	if (!ts->irq_handler_called && ts->rx_filter != HWTSTAMP_FILTER_NONE &&
	    rx_drain_ms)
		return msecs_to_jiffies(rx_drain_ms);

	return -1;
}
EXPORT_SYMBOL_GPL(marvell_ts_aux_work);

irqreturn_t marvell_ts_irq(struct marvell_ts *ts)
{
	irqreturn_t ret = IRQ_NONE;

	ts->irq_handler_called = true;

	if (marvell_ts_rx_ts(ts, 0))
		ret = IRQ_HANDLED;

	if (marvell_ts_rx_ts(ts, 1))
		ret = IRQ_HANDLED;

	if (ts->tx_skb && marvell_ts_txtstamp_complete(ts))
		ret = IRQ_HANDLED;

	return ret;
}
EXPORT_SYMBOL_GPL(marvell_ts_irq);

/* Configure the global (shared between ports) configuration for the PHY. */
int marvell_ts_global_config(struct device *dev,
			     const struct marvell_ts_ops *ops)
{
	int err;

	/* Set ether-type for IEEE1588 packets */
	err = ops->ts_global_write(dev, PTPG_ETYPE, ETH_P_1588);
	if (err < 0)
		return err;

	/* MsdIDTSEn - Enable timestamping on all PTP MessageIDs */
	err = ops->ts_global_write(dev, PTPG_MSGIDTSEN, MV_PTP_MSD_ID_TS_EN);
	if (err < 0)
		return err;

	/* TSArrPtr - Point to Arr0 registers */
	err = ops->ts_global_write(dev, PTPG_TSARRPTR, MV_PTP_TS_ARR_PTR);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(marvell_ts_global_config);

void marvell_ts_remove(struct marvell_ts *ts)
{
	int i;

	if (!ts->tai)
		return;

	marvell_tai_cancel_worker(ts->tai);

	/* Ensure that the port is disabled */
	marvell_ts_port_disable(ts);

	/* Free or dequeue all pending skbs */
	kfree_skb(ts->tx_skb);
	ts->tx_skb = NULL;

	for (i = 0; i < ARRAY_SIZE(ts->rxq); i++)
		marvell_rxq_purge(&ts->rxq[i]);
}
EXPORT_SYMBOL_GPL(marvell_ts_remove);

int marvell_ts_probe(struct marvell_ts *ts, struct device *dev,
		     struct marvell_tai *tai,
		     const struct marvell_ts_caps *caps,
		     const struct marvell_ts_ops *ops, u8 port)
{
	int i;

	ts->ops = ops;
	ts->dev = dev;
	ts->tai = tai;
	ts->caps = caps;
	ts->port = port;

	for (i = 0; i < ARRAY_SIZE(ts->rxq); i++)
		marvell_rxq_init(&ts->rxq[i]);

	/* Configure this PTP port */
	return marvell_ts_port_config(ts);
}
EXPORT_SYMBOL_GPL(marvell_ts_probe);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Marvell PTP library");
MODULE_LICENSE("GPL v2");
