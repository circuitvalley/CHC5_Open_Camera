// SPDX-License-Identifier: GPL-2.0-only
/*
 * Zynq-7000 GEM PTP clock
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/macb_zynq_ptp.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/timecounter.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "macb.h"

#define ZPTP_NAME		"macb-zynq-ptp"

#define ZPTP_CC_SHIFT		29
#define ZPTP_OVERFLOW_PERIOD	(8 * HZ)

#define ZPTP_MAX_ADJ_PPB	64000000

#define ZPTP_TX_TIMEOUT		msecs_to_jiffies(200)

#define ZPTP_RX_MAX_AGE		(100 * NSEC_PER_MSEC)

static u64 zptp_raw(struct macb *bp, struct ptp_system_timestamp *sts)
{
	u32 ns, ns2, sec;

	ptp_read_system_prets(sts);
	ns = gem_readl(bp, TN) & TSU_NSEC_MAX_VAL;
	ptp_read_system_postts(sts);
	sec = gem_readl(bp, TSL);
	ns2 = gem_readl(bp, TN) & TSU_NSEC_MAX_VAL;
	if (ns2 < ns) {
		sec = gem_readl(bp, TSL);
		ns = ns2;
	}

	return (u64)sec * NSEC_PER_SEC + ns;
}

static u64 zptp_cc_read(const struct cyclecounter *cc)
{
	struct macb *bp = container_of(cc, struct macb, zptp_cc);

	return zptp_raw(bp, NULL);
}

static u64 zptp_raw_to_ns(struct macb *bp, u64 raw)
{
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	ns = timecounter_cyc2time(&bp->zptp_tc, raw);
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return ns;
}

static int zptp_gettimex(struct ptp_clock_info *ptp, struct timespec64 *ts,
			 struct ptp_system_timestamp *sts)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	ns = timecounter_cyc2time(&bp->zptp_tc, zptp_raw(bp, sts));
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	*ts = ns_to_timespec64(ns);
	return 0;
}

static int zptp_settime(struct ptp_clock_info *ptp,
			const struct timespec64 *ts)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	timecounter_init(&bp->zptp_tc, &bp->zptp_cc, timespec64_to_ns(ts));
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return 0;
}

static int zptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	timecounter_adjtime(&bp->zptp_tc, delta);
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return 0;
}

static int zptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;
	u32 mult;

	mult = (u32)adjust_by_scaled_ppm(bp->zptp_mult, scaled_ppm);

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	timecounter_read(&bp->zptp_tc);
	bp->zptp_cc.mult = mult;
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return 0;
}

static int zptp_enable(struct ptp_clock_info *ptp,
		       struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static long zptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	timecounter_read(&bp->zptp_tc);
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return ZPTP_OVERFLOW_PERIOD;
}

static const struct ptp_clock_info zptp_caps = {
	.owner		= THIS_MODULE,
	.name		= ZPTP_NAME,
	.max_adj	= ZPTP_MAX_ADJ_PPB,
	.adjfine	= zptp_adjfine,
	.adjtime	= zptp_adjtime,
	.gettimex64	= zptp_gettimex,
	.settime64	= zptp_settime,
	.enable		= zptp_enable,
	.do_aux_work	= zptp_do_aux_work,
};

static int zptp_group(struct sk_buff *skb)
{
	struct ptp_header *hdr;
	unsigned int type;

	type = ptp_classify_raw(skb);
	if ((type & PTP_CLASS_PMASK) != PTP_CLASS_L2)
		return -1;

	hdr = ptp_parse_header(skb, type);
	if (!hdr)
		return -1;

	switch (ptp_get_msgtype(hdr, type)) {
	case PTP_MSGTYPE_SYNC:
	case PTP_MSGTYPE_DELAY_REQ:
		return ZPTP_EV;
	case PTP_MSGTYPE_PDELAY_REQ:
	case PTP_MSGTYPE_PDELAY_RESP:
		return ZPTP_PEER;
	default:
		return -1;
	}
}

static u64 zptp_latch(struct macb *bp, int sec_reg, int ns_reg)
{
	u32 sec = bp->macb_reg_readl(bp, sec_reg);
	u32 ns = bp->macb_reg_readl(bp, ns_reg) & TSU_NSEC_MAX_VAL;

	return (u64)sec * NSEC_PER_SEC + ns;
}

static const struct {
	u32 rx_ints, tx_ints;
	int rx_sec, rx_ns, tx_sec, tx_ns;
} zptp_groups[ZPTP_NGROUPS] = {
	[ZPTP_EV] = {
		MACB_ZPTP_RX_EV_INTS, MACB_ZPTP_TX_EV_INTS,
		GEM_EFRSL, GEM_EFRN, GEM_EFTSL, GEM_EFTN,
	},
	[ZPTP_PEER] = {
		MACB_ZPTP_RX_PEER_INTS, MACB_ZPTP_TX_PEER_INTS,
		GEM_PEFRSL, GEM_PEFRN, GEM_PEFTSL, GEM_PEFTN,
	},
};

void zynq_gem_ptp_irq(struct macb *bp, u32 status)
{
	bool kick = false;
	int g;

	status &= bp->zptp_ints;
	if (!status)
		return;

	spin_lock(&bp->zptp_ts_lock);
	for (g = 0; g < ZPTP_NGROUPS; g++) {
		u32 rx = status & zptp_groups[g].rx_ints;
		u32 tx = status & zptp_groups[g].tx_ints;

		if (rx) {
			struct macb_zptp_rx *slot = &bp->zptp_rx[g];

			slot->raw = zptp_latch(bp, zptp_groups[g].rx_sec,
					       zptp_groups[g].rx_ns);
			slot->irqs += hweight32(rx);
			bp->zptp_stats.rx_irqs[g] += hweight32(rx);
		}

		if (tx) {
			struct macb_zptp_tx *slot = &bp->zptp_tx[g];
			u64 raw = zptp_latch(bp, zptp_groups[g].tx_sec,
					     zptp_groups[g].tx_ns);

			bp->zptp_stats.tx_irqs[g] += hweight32(tx);
			if (slot->skb && !slot->stamped &&
			    raw >= slot->queued_raw) {
				slot->raw = raw;
				slot->stamped = true;
				kick = true;
			} else {
				bp->zptp_stats.tx_orphan++;
			}
		}
	}
	spin_unlock(&bp->zptp_ts_lock);

	if (kick)
		mod_delayed_work(system_wq, &bp->zptp_tx_work, 0);
}

void zynq_gem_ptp_rxstamp(struct macb *bp, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *hwts;
	struct macb_zptp_rx *slot;
	unsigned long flags;
	bool ok = false;
	u64 raw = 0, now;
	int g;

	skb_push(skb, ETH_HLEN);
	g = zptp_group(skb);
	skb_pull(skb, ETH_HLEN);
	if (g < 0)
		return;

	now = zptp_raw(bp, NULL);

	spin_lock_irqsave(&bp->zptp_ts_lock, flags);
	slot = &bp->zptp_rx[g];
	if (slot->irqs - slot->used == 1 && now >= slot->raw &&
	    now - slot->raw < ZPTP_RX_MAX_AGE) {
		raw = slot->raw;
		ok = true;
		bp->zptp_stats.rx_stamped++;
	} else {
		bp->zptp_stats.rx_unmatched++;
	}
	slot->used = slot->irqs;
	spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

	if (!ok)
		return;

	hwts = skb_hwtstamps(skb);
	memset(hwts, 0, sizeof(*hwts));
	hwts->hwtstamp = ns_to_ktime(zptp_raw_to_ns(bp, raw));
}

void zynq_gem_ptp_txqueue(struct macb *bp, struct sk_buff *skb)
{
	struct sk_buff *stale = NULL;
	struct macb_zptp_tx *slot;
	unsigned long flags;
	u64 now;
	int g;

	g = zptp_group(skb);
	if (g < 0)
		return;

	now = zptp_raw(bp, NULL);

	spin_lock_irqsave(&bp->zptp_ts_lock, flags);
	slot = &bp->zptp_tx[g];
	if (slot->skb) {
		if (time_before(jiffies, slot->start + ZPTP_TX_TIMEOUT)) {
			bp->zptp_stats.tx_busy++;
			spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);
			return;
		}
		stale = slot->skb;
		bp->zptp_stats.tx_timeout++;
	}
	slot->skb = skb_get(skb);
	slot->start = jiffies;
	slot->queued_raw = now;
	slot->stamped = false;
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

	dev_kfree_skb_any(stale);
	schedule_delayed_work(&bp->zptp_tx_work, ZPTP_TX_TIMEOUT + 1);
}

static void zptp_tx_work(struct work_struct *work)
{
	struct macb *bp = container_of(to_delayed_work(work), struct macb,
				       zptp_tx_work);
	bool pending = false;
	int g;

	for (g = 0; g < ZPTP_NGROUPS; g++) {
		struct macb_zptp_tx *slot = &bp->zptp_tx[g];
		struct sk_buff *skb = NULL;
		bool stamped = false;
		unsigned long flags;
		u64 raw = 0;

		spin_lock_irqsave(&bp->zptp_ts_lock, flags);
		if (slot->skb && (slot->stamped ||
				  time_after_eq(jiffies, slot->start + ZPTP_TX_TIMEOUT))) {
			skb = slot->skb;
			stamped = slot->stamped;
			raw = slot->raw;
			slot->skb = NULL;
			slot->stamped = false;
			if (stamped)
				bp->zptp_stats.tx_stamped++;
			else
				bp->zptp_stats.tx_timeout++;
		}
		pending |= !!slot->skb;
		spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

		if (!skb)
			continue;

		if (stamped) {
			struct skb_shared_hwtstamps hwts = { };

			hwts.hwtstamp = ns_to_ktime(zptp_raw_to_ns(bp, raw));
			skb_tstamp_tx(skb, &hwts);
		}
		dev_kfree_skb_any(skb);
	}

	if (pending)
		schedule_delayed_work(&bp->zptp_tx_work, ZPTP_TX_TIMEOUT);
}

static void zptp_tx_flush(struct macb *bp)
{
	int g;

	for (g = 0; g < ZPTP_NGROUPS; g++) {
		struct sk_buff *skb;
		unsigned long flags;

		spin_lock_irqsave(&bp->zptp_ts_lock, flags);
		skb = bp->zptp_tx[g].skb;
		bp->zptp_tx[g].skb = NULL;
		bp->zptp_tx[g].stamped = false;
		spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

		dev_kfree_skb_any(skb);
	}
}

static void zptp_set_ints(struct macb *bp, u32 ints)
{
	struct macb_queue *queue = &bp->queues[0];
	unsigned long flags;
	int g;

	spin_lock_irqsave(&bp->zptp_ts_lock, flags);
	for (g = 0; g < ZPTP_NGROUPS; g++)
		bp->zptp_rx[g].used = bp->zptp_rx[g].irqs;
	bp->zptp_ints = ints;
	spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

	queue_writel(queue, IDR, MACB_ZPTP_INTS & ~ints);
	if (ints)
		queue_writel(queue, IER, ints);
}

static ssize_t zynq_ptp_stats_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct macb *bp = netdev_priv(dev_get_drvdata(dev));
	struct macb_zptp_follow fo;
	struct macb_zptp_stats st;
	unsigned long flags;
	u32 ints;

	spin_lock_irqsave(&bp->zptp_ts_lock, flags);
	st = bp->zptp_stats;
	ints = bp->zptp_ints;
	spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	fo = bp->zptp_follow;
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return sysfs_emit(buf,
			  "tx_type %d rx_filter %d ints 0x%08x\n"
			  "rx_ev_irqs %u rx_peer_irqs %u rx_stamped %u rx_unmatched %u\n"
			  "tx_ev_irqs %u tx_peer_irqs %u tx_stamped %u tx_timeout %u tx_busy %u tx_orphan %u\n"
			  "follow_state %d samples %u rejected %u steps %u offset_ns %lld window_ns %llu freq_ppb %lld\n",
			  bp->tstamp_config.tx_type, bp->tstamp_config.rx_filter, ints,
			  st.rx_irqs[ZPTP_EV], st.rx_irqs[ZPTP_PEER],
			  st.rx_stamped, st.rx_unmatched,
			  st.tx_irqs[ZPTP_EV], st.tx_irqs[ZPTP_PEER],
			  st.tx_stamped, st.tx_timeout, st.tx_busy, st.tx_orphan,
			  fo.state, fo.samples, fo.rejected, fo.steps,
			  fo.last_offset_ns, fo.last_window_ns, fo.freq_ppb);
}
static DEVICE_ATTR_RO(zynq_ptp_stats);

static umode_t zynq_ptp_attr_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct net_device *ndev = dev_get_drvdata(kobj_to_dev(kobj));

	return ndev && macb_zynq_ptp(netdev_priv(ndev)) ? attr->mode : 0;
}

static struct attribute *zynq_ptp_attrs[] = {
	&dev_attr_zynq_ptp_stats.attr,
	NULL
};

static const struct attribute_group zynq_ptp_group = {
	.attrs = zynq_ptp_attrs,
	.is_visible = zynq_ptp_attr_visible,
};

const struct attribute_group *macb_zynq_ptp_groups[] = {
	&zynq_ptp_group,
	NULL
};

void zynq_gem_ptp_setup(struct macb *bp)
{
	spin_lock_init(&bp->zptp_ts_lock);
	spin_lock_init(&bp->tsu_clk_lock);
	INIT_DELAYED_WORK(&bp->zptp_tx_work, zptp_tx_work);
	bp->zptp_running = false;
}

static void zynq_gem_ptp_init(struct net_device *ndev)
{
	struct macb *bp = netdev_priv(ndev);
	unsigned long flags, rate;
	u32 incr;

	spin_lock_irqsave(&bp->zptp_ts_lock, flags);
	memset(bp->zptp_rx, 0, sizeof(bp->zptp_rx));
	memset(bp->zptp_tx, 0, sizeof(bp->zptp_tx));
	memset(&bp->zptp_stats, 0, sizeof(bp->zptp_stats));
	spin_unlock_irqrestore(&bp->zptp_ts_lock, flags);

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	bp->zptp_running = false;
	memset(&bp->zptp_follow, 0, sizeof(bp->zptp_follow));
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	memset(&bp->tstamp_config, 0, sizeof(bp->tstamp_config));
	zptp_set_ints(bp, 0);

	rate = clk_get_rate(bp->pclk);
	if (!rate) {
		netdev_err(ndev, "PTP: pclk rate unknown, no PTP clock\n");
		return;
	}
	incr = clamp_t(u32, DIV_ROUND_CLOSEST(NSEC_PER_SEC, rate),
		       1, GENMASK(GEM_NSINCR_SIZE - 1, 0));
	bp->tsu_rate = rate;

	gem_writel(bp, TI, 0);
	gem_writel(bp, TSL, 0);
	gem_writel(bp, TN, 0);
	gem_writel(bp, TI, GEM_BF(NSINCR, incr));

	bp->zptp_mult = div64_u64((u64)NSEC_PER_SEC << ZPTP_CC_SHIFT,
				  (u64)incr * rate);
	bp->zptp_cc.read = zptp_cc_read;
	bp->zptp_cc.mask = CYCLECOUNTER_MASK(64);
	bp->zptp_cc.shift = ZPTP_CC_SHIFT;
	bp->zptp_cc.mult = bp->zptp_mult;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	timecounter_init(&bp->zptp_tc, &bp->zptp_cc, ktime_get_real_ns());
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	bp->ptp_clock_info = zptp_caps;
	bp->ptp_clock = ptp_clock_register(&bp->ptp_clock_info, &ndev->dev);
	if (IS_ERR_OR_NULL(bp->ptp_clock)) {
		if (IS_ERR(bp->ptp_clock))
			netdev_err(ndev, "PTP clock register failed: %ld\n",
				   PTR_ERR(bp->ptp_clock));
		bp->ptp_clock = NULL;
		gem_writel(bp, TI, 0);
		return;
	}
	ptp_schedule_worker(bp->ptp_clock, ZPTP_OVERFLOW_PERIOD);

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	bp->zptp_running = true;
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	netdev_info(ndev, "%s: /dev/ptp%d, timer %lu Hz, %u ns increment\n",
		    ZPTP_NAME, ptp_clock_index(bp->ptp_clock), rate, incr);
}

static void zynq_gem_ptp_remove(struct net_device *ndev)
{
	struct macb *bp = netdev_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	bp->zptp_running = false;
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	zptp_set_ints(bp, 0);
	memset(&bp->tstamp_config, 0, sizeof(bp->tstamp_config));
	cancel_delayed_work_sync(&bp->zptp_tx_work);
	zptp_tx_flush(bp);

	if (bp->ptp_clock) {
		ptp_clock_unregister(bp->ptp_clock);
		bp->ptp_clock = NULL;
	}
	gem_writel(bp, TI, 0);
}

static s32 zynq_gem_get_ptp_max_adj(void)
{
	return ZPTP_MAX_ADJ_PPB;
}

static unsigned int zynq_gem_get_tsu_rate(struct macb *bp)
{
	return clk_get_rate(bp->pclk);
}

static int zynq_gem_get_ts_info(struct net_device *ndev,
				struct ethtool_ts_info *info)
{
	struct macb *bp = netdev_priv(ndev);

	ethtool_op_get_ts_info(ndev, info);
	if (!bp->ptp_clock)
		return 0;

	info->phc_index = ptp_clock_index(bp->ptp_clock);
	info->so_timestamping |= SOF_TIMESTAMPING_TX_HARDWARE |
				 SOF_TIMESTAMPING_RX_HARDWARE |
				 SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT);

	return 0;
}

static bool zynq_gem_phy_timestamps(struct net_device *ndev)
{
	return phy_has_hwtstamp(ndev->phydev);
}

static int zynq_gem_get_hwtst(struct net_device *ndev, struct ifreq *ifr)
{
	struct macb *bp = netdev_priv(ndev);

	if (zynq_gem_phy_timestamps(ndev))
		return phylink_mii_ioctl(bp->phylink, ifr, SIOCGHWTSTAMP);

	return copy_to_user(ifr->ifr_data, &bp->tstamp_config,
			    sizeof(bp->tstamp_config)) ? -EFAULT : 0;
}

static int zynq_gem_set_hwtst(struct net_device *ndev, struct ifreq *ifr,
			      int cmd)
{
	struct macb *bp = netdev_priv(ndev);
	struct hwtstamp_config cfg;
	u32 ints = 0;

	if (zynq_gem_phy_timestamps(ndev))
		return phylink_mii_ioctl(bp->phylink, ifr, cmd);

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;
	if (cfg.flags)
		return -EINVAL;
	if (!bp->ptp_clock)
		return -EOPNOTSUPP;

	switch (cfg.tx_type) {
	case HWTSTAMP_TX_OFF:
		break;
	case HWTSTAMP_TX_ON:
		ints |= MACB_ZPTP_TX_INTS;
		break;
	default:
		return -ERANGE;
	}

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		ints |= MACB_ZPTP_RX_INTS;
		break;
	default:
		return -ERANGE;
	}

	bp->tstamp_config = cfg;
	zptp_set_ints(bp, ints);
	if (!(ints & MACB_ZPTP_TX_INTS))
		zptp_tx_flush(bp);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

int macb_zynq_ptp_now_ns(struct net_device *ndev, u64 *ns)
{
	unsigned long flags;
	struct macb *bp;
	int ret = -ENODEV;

	if (!ndev || !macb_is_macb_netdev(ndev))
		return -ENODEV;

	bp = netdev_priv(ndev);
	if (!macb_zynq_ptp(bp) || !READ_ONCE(bp->zptp_running))
		return -ENODEV;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	if (bp->zptp_running) {
		*ns = timecounter_cyc2time(&bp->zptp_tc, zptp_raw(bp, NULL));
		ret = 0;
	}
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(macb_zynq_ptp_now_ns);

#define ZPTP_FOLLOW_KP_DIV	50
#define ZPTP_FOLLOW_KI_DIV	5000
#define ZPTP_FOLLOW_STEP_NS	NSEC_PER_MSEC
#define ZPTP_FOLLOW_MAX_PPB	1000000
#define ZPTP_FOLLOW_FREQ_WAIT	(2 * NSEC_PER_SEC)

static void zptp_follow_set_freq(struct macb *bp, s64 ppb)
{
	long scaled_ppm;

	ppb = clamp_t(s64, ppb, -ZPTP_FOLLOW_MAX_PPB, ZPTP_FOLLOW_MAX_PPB);
	scaled_ppm = (long)div_s64(ppb * 65536, 1000);
	timecounter_read(&bp->zptp_tc);
	bp->zptp_cc.mult = (u32)adjust_by_scaled_ppm(bp->zptp_mult, scaled_ppm);
}

int macb_zynq_ptp_follow(struct net_device *ndev, u64 ref_ns, u64 gem_pre_ns,
			 u64 gem_post_ns)
{
	struct macb_zptp_follow *f;
	u64 window, dt;
	unsigned long flags;
	struct macb *bp;
	s64 offset, rate;

	if (!ndev || !macb_is_macb_netdev(ndev))
		return -ENODEV;
	bp = netdev_priv(ndev);
	if (!macb_zynq_ptp(bp) || gem_post_ns < gem_pre_ns)
		return -ENODEV;

	window = gem_post_ns - gem_pre_ns;
	offset = (s64)(ref_ns - (gem_pre_ns + window / 2));

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	if (!bp->zptp_running) {
		spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);
		return -ENODEV;
	}
	f = &bp->zptp_follow;
	f->samples++;
	if (!f->min_window_ns || window < f->min_window_ns)
		f->min_window_ns = window;
	if (window > 2 * f->min_window_ns + 20 * NSEC_PER_USEC) {
		f->rejected++;
		goto out;
	}
	f->last_offset_ns = offset;
	f->last_window_ns = window;

	switch (f->state) {
	case 0:
		timecounter_adjtime(&bp->zptp_tc, offset);
		f->steps++;
		f->t0_ref_ns = ref_ns;
		f->state = 1;
		break;
	case 1:
		if (abs(offset) > ZPTP_FOLLOW_STEP_NS || ref_ns < f->t0_ref_ns) {
			timecounter_adjtime(&bp->zptp_tc, offset);
			f->steps++;
			f->t0_ref_ns = ref_ns;
			break;
		}
		dt = ref_ns - f->t0_ref_ns;
		if (dt < ZPTP_FOLLOW_FREQ_WAIT)
			break;
		f->freq_ppb += div64_s64(offset * (s64)NSEC_PER_SEC, (s64)dt);
		f->freq_ppb = clamp_t(s64, f->freq_ppb, -ZPTP_FOLLOW_MAX_PPB,
				      ZPTP_FOLLOW_MAX_PPB);
		zptp_follow_set_freq(bp, f->freq_ppb);
		timecounter_adjtime(&bp->zptp_tc, offset);
		f->steps++;
		f->state = 2;
		break;
	default:
		dt = ref_ns - f->last_ref_ns;
		if (abs(offset) > ZPTP_FOLLOW_STEP_NS) {
			timecounter_adjtime(&bp->zptp_tc, offset);
			f->steps++;
			break;
		}
		if (!dt || dt > NSEC_PER_SEC)
			break;
		rate = div64_s64(offset * (s64)NSEC_PER_SEC, (s64)dt);
		f->freq_ppb += div_s64(rate, ZPTP_FOLLOW_KI_DIV);
		f->freq_ppb = clamp_t(s64, f->freq_ppb, -ZPTP_FOLLOW_MAX_PPB,
				      ZPTP_FOLLOW_MAX_PPB);
		zptp_follow_set_freq(bp, f->freq_ppb +
				     div_s64(rate, ZPTP_FOLLOW_KP_DIV));
		break;
	}
	f->last_ref_ns = ref_ns;
out:
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(macb_zynq_ptp_follow);

struct macb_ptp_info zynq_gem_ptp_info = {
	.ptp_init	 = zynq_gem_ptp_init,
	.ptp_remove	 = zynq_gem_ptp_remove,
	.get_ptp_max_adj = zynq_gem_get_ptp_max_adj,
	.get_tsu_rate	 = zynq_gem_get_tsu_rate,
	.get_ts_info	 = zynq_gem_get_ts_info,
	.get_hwtst	 = zynq_gem_get_hwtst,
	.set_hwtst	 = zynq_gem_set_hwtst,
};
