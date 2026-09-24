// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell PTP driver for 88E1510, 88E1512, 88E1514 and 88E1518 PHYs
 *
 * Ideas taken from 88E6xxx DSA and DP83640 drivers. This file
 * implements the packet timestamping support only (PTP).  TAI
 * support is separate.
 */
#include <linux/device.h>
#include <linux/macb_zynq_ptp.h>
#include <linux/marvell_phy_ptp.h>
#include <linux/marvell_ptp.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/uaccess.h>

#include "marvell_ptp.h"

#define MARVELL_PAGE_MISC			6
#define GCR					20
#define GCR_PTP_POWER_DOWN			BIT(9)
#define GCR_PTP_REF_CLOCK_SOURCE		BIT(8)
#define GCR_PTP_INPUT_SOURCE			BIT(7)
#define GCR_PTP_OUTPUT				BIT(6)

#define MARVELL_PAGE_PTP_PORT_1			8
#define PTPP1_ARR0_STATUS			8
#define PTPP1_ARR1_STATUS			12
#define MARVELL_PAGE_PTP_PORT_2			9
#define PTPP2_DEP_STATUS			0

#define MARVELL_PAGE_TAI_GLOBAL			12
#define TAI_CFG0				0
#define TAI_CFG0_EVENTCAPOV			BIT(15)
#define TAI_CFG0_EVENTCTRSTART			BIT(14)
#define TAI_CFG0_EVENTPHASE			BIT(13)
#define TAI_EVENT_STATUS			9
#define TAI_EVENT_STATUS_ERR			BIT(9)
#define TAI_EVENT_STATUS_VALID			BIT(8)
#define TAI_EVENT_STATUS_COUNT			GENMASK(7, 0)
#define TAI_EVENT_TIME_LO			10
#define TAI_EVENT_TIME_HI			11

#define MARVELL_PAGE_LED			3
#define LED_FUNC_CTRL				16
#define LED_FUNC_LED1				GENMASK(7, 4)
#define LED_FUNC_LED1_HI_Z			(0xa << 4)
#define MARVELL_PAGE_PTP_GLOBAL			14
#define PTPG_READPLUS_COMMAND			14
#define PTPG_READPLUS_DATA			15

/* 88E151x has PTP Global Configuration 3
 * 0 TSAtSFD - Timestamp at start of frame delimiter
 */
#define PTPG_CONFIG_3				3
#define PTPG_CONFIG_3_TSATSFD			BIT(0)

struct marvell_phy_ptp {
	struct mii_timestamper mii_ts;
	struct marvell_ts ts;
	unsigned long follow_next;
	bool event_in;
	u16 led1_saved;
};

static DEFINE_MUTEX(marvell_phy_ptp_ext_lock);

static bool follow_mac_clock = true;
module_param(follow_mac_clock, bool, 0644);
MODULE_PARM_DESC(follow_mac_clock,
		 "keep the attached Zynq GEM clock on this PHY's PTP clock (replaces phc2sys)");

static unsigned int follow_ms = 200;
module_param(follow_ms, uint, 0644);
MODULE_PARM_DESC(follow_ms, "period of the follow_mac_clock samples in ms");

static struct marvell_phy_ptp *mii_ts_to_phy_ptp(struct mii_timestamper *mii_ts)
{
	return container_of(mii_ts, struct marvell_phy_ptp, mii_ts);
}

static bool marvell_phy_ptp_rxtstamp(struct mii_timestamper *mii_ts,
				     struct sk_buff *skb, int type)
{
	struct marvell_phy_ptp *phy_ptp = mii_ts_to_phy_ptp(mii_ts);

	return marvell_ts_rxtstamp(&phy_ptp->ts, skb, type);
}

static void marvell_phy_ptp_txtstamp(struct mii_timestamper *mii_ts,
				     struct sk_buff *skb, int type)
{
	struct marvell_phy_ptp *phy_ptp = mii_ts_to_phy_ptp(mii_ts);

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) ||
	    !marvell_ts_txtstamp(&phy_ptp->ts, skb, type))
		kfree_skb(skb);
}

static int marvell_phy_ptp_ts_info(struct mii_timestamper *mii_ts,
				   struct ethtool_ts_info *ts_info)
{
	struct marvell_phy_ptp *phy_ptp = mii_ts_to_phy_ptp(mii_ts);

	return marvell_ts_info(&phy_ptp->ts, ts_info);
}

/* TAI accessor functions */
static int marvell_phy_tai_hw_enable(struct device *dev)
{
	struct phy_device *phydev = to_phy_device(dev);

	return phy_modify_paged(phydev, MARVELL_PAGE_MISC, GCR,
				GCR_PTP_POWER_DOWN, 0);
}

static void marvell_phy_tai_hw_disable(struct device *dev)
{
	struct phy_device *phydev = to_phy_device(dev);

	phy_modify_paged(phydev, MARVELL_PAGE_MISC, GCR,
			 GCR_PTP_POWER_DOWN, GCR_PTP_POWER_DOWN);
}

static u64 marvell_phy_tai_clock_read(struct device *dev,
				      struct ptp_system_timestamp *sts)
{
	struct phy_device *phydev = to_phy_device(dev);
	int err = 0, oldpage, lo = -1, hi = -1;

	oldpage = phy_select_page(phydev, MARVELL_PAGE_PTP_GLOBAL);
	if (oldpage >= 0) {
		/* 88e151x says to write 0x8e0e */
		ptp_read_system_prets(sts);
		err = __phy_write(phydev, PTPG_READPLUS_COMMAND, 0x8e0e);
		ptp_read_system_postts(sts);
		lo = __phy_read(phydev, PTPG_READPLUS_DATA);
		hi = __phy_read(phydev, PTPG_READPLUS_DATA);
	}
	err = phy_restore_page(phydev, oldpage, err);

	if (err || lo < 0 || hi < 0)
		return MARVELL_TAI_READ_FAILED;

	return (u32)lo | (u32)hi << 16;
}

static int marvell_phy_tai_write(struct device *dev, u8 reg, u16 val)
{
	struct phy_device *phydev = to_phy_device(dev);

	return phy_write_paged(phydev, MARVELL_PAGE_TAI_GLOBAL, reg, val);
}

static int marvell_phy_tai_modify(struct device *dev, u8 reg, u16 mask, u16 val)
{
	struct phy_device *phydev = to_phy_device(dev);

	return phy_modify_paged(phydev, MARVELL_PAGE_TAI_GLOBAL,
				reg, mask, val);
}

static void marvell_phy_ptp_follow(struct phy_device *phydev,
				   struct marvell_phy_ptp *phy_ptp)
{
	struct net_device *ndev = phydev->attached_dev;
	int err = 0, oldpage, lo = -1, hi = -1;
	u64 pre = 0, post = 0;

	if (!ndev)
		return;

	oldpage = phy_select_page(phydev, MARVELL_PAGE_PTP_GLOBAL);
	if (oldpage >= 0) {
		err = macb_zynq_ptp_now_ns(ndev, &pre);
		if (!err) {
			err = __phy_write(phydev, PTPG_READPLUS_COMMAND, 0x8e0e);
			if (!err)
				err = macb_zynq_ptp_now_ns(ndev, &post);
			lo = __phy_read(phydev, PTPG_READPLUS_DATA);
			hi = __phy_read(phydev, PTPG_READPLUS_DATA);
		}
	}
	err = phy_restore_page(phydev, oldpage, err);
	if (err || lo < 0 || hi < 0)
		return;

	macb_zynq_ptp_follow(ndev,
			     marvell_tai_cyc2time(phy_ptp->ts.tai,
						  (u32)lo | (u32)hi << 16),
			     pre, post);
}

static long marvell_phy_tai_aux_work(struct device *dev)
{
	struct phy_device *phydev = to_phy_device(dev);
	struct marvell_phy_ptp *phy_ptp;
	unsigned long period;
	long delay;

	if (!phydev->mii_ts)
		return -1;

	phy_ptp = mii_ts_to_phy_ptp(phydev->mii_ts);

	delay = marvell_ts_aux_work(&phy_ptp->ts);

	if (follow_mac_clock && phy_ptp->ts.rx_filter != HWTSTAMP_FILTER_NONE) {
		period = msecs_to_jiffies(follow_ms ? follow_ms : 100);
		if (time_after_eq(jiffies, phy_ptp->follow_next) ||
		    time_after(phy_ptp->follow_next, jiffies + period)) {
			phy_ptp->follow_next = jiffies + period;
			marvell_phy_ptp_follow(phydev, phy_ptp);
		}
		if (delay < 0 || delay > (long)period)
			delay = period;
	}

	return delay;
}

static const struct marvell_tai_ops marvell_phy_tai_ops = {
	.tai_hw_enable = marvell_phy_tai_hw_enable,
	.tai_hw_disable = marvell_phy_tai_hw_disable,
	.tai_clock_read = marvell_phy_tai_clock_read,
	.tai_write = marvell_phy_tai_write,
	.tai_modify = marvell_phy_tai_modify,
	.tai_aux_work = marvell_phy_tai_aux_work,
};

static const struct marvell_tai_param marvell_phy_tai_param = {
	/* This assumes a 125MHz clock */
	.cc_mult_num = 1 << 9,
	.cc_mult_den = 15625U,
	.cc_mult = 8 << 28,
	.cc_shift = 28,
};

static int marvell_phy_ts_global_write(struct device *dev, u8 reg, u16 val)
{
	struct phy_device *phydev = to_phy_device(dev);

	return phy_write_paged(phydev, MARVELL_PAGE_PTP_GLOBAL, reg, val);
}

static const struct {
	u8 page;
	u8 reg;
} marvell_phy_ts_ts_reg[] = {
	[MARVELL_TS_ARR0] = {
		.page = MARVELL_PAGE_PTP_PORT_1,
		.reg = PTPP1_ARR0_STATUS,
	},
	[MARVELL_TS_ARR1] = {
		.page = MARVELL_PAGE_PTP_PORT_1,
		.reg = PTPP1_ARR1_STATUS,
	},
	[MARVELL_TS_DEP] = {
		.page = MARVELL_PAGE_PTP_PORT_2,
		.reg = PTPP2_DEP_STATUS,
	},
};

/* Read the status, timestamp and PTP common header sequence from the PHY.
 * Apparently, reading these are atomic, but there is no mention how the
 * PHY treats this access as atomic. So, we set the DisTSOverwrite bit
 * when configuring the PHY.
 */
static int marvell_phy_ts_port_read_ts(struct device *dev,
				       struct marvell_hwts *hwts, u8 port,
				       enum marvell_ts_reg ts_reg)
{
	struct phy_device *phydev = to_phy_device(dev);
	int oldpage, page, reg;
	int ret = 0;

	page = marvell_phy_ts_ts_reg[ts_reg].page;
	reg = marvell_phy_ts_ts_reg[ts_reg].reg;

	/* Read status register */
	oldpage = phy_select_page(phydev, page);
	if (oldpage >= 0) {
		ret = __phy_read(phydev, reg);
		if (ret < 0)
			goto restore;

		hwts->stat = ret;
		if (!(hwts->stat & MV_STATUS_VALID)) {
			ret = 0;
			goto restore;
		}

		/* Read low timestamp */
		ret = __phy_read(phydev, reg + 1);
		if (ret < 0)
			goto restore;

		hwts->time = ret;

		/* Read high timestamp */
		ret = __phy_read(phydev, reg + 2);
		if (ret < 0)
			goto restore;

		hwts->time |= (u32)ret << 16;

		/* Read sequence */
		ret = __phy_read(phydev, reg + 3);
		if (ret < 0)
			goto restore;

		hwts->seq = ret;

		/* Clear valid */
		__phy_write(phydev, reg, 0);

		ret = 1;
	}
restore:
	return phy_restore_page(phydev, oldpage, ret);
}

static int marvell_phy_ts_port_write(struct device *dev, u8 port, u8 reg,
				     u16 val)
{
	struct phy_device *phydev = to_phy_device(dev);

	return phy_write_paged(phydev, MARVELL_PAGE_PTP_PORT_1 + (reg >> 4),
			       reg & 15, val);
}

static int marvell_phy_ts_port_modify(struct device *dev, u8 port, u8 reg,
				      u16 mask, u16 val)
{
	struct phy_device *phydev = to_phy_device(dev);

	return phy_modify_paged(phydev, MARVELL_PAGE_PTP_PORT_1 + (reg >> 4),
				reg & 15, mask, val);
}

static const struct marvell_ts_ops marvell_phy_ts_ops = {
	.ts_global_write = marvell_phy_ts_global_write,
	.ts_port_read_ts = marvell_phy_ts_port_read_ts,
	.ts_port_write = marvell_phy_ts_port_write,
	.ts_port_modify = marvell_phy_ts_port_modify,
};

static int marvell_phy_ptp_event_apply(struct phy_device *phydev);

static int marvell_phy_ptp_setup(struct device *dev)
{
	struct phy_device *phydev = to_phy_device(dev);
	int err;

	err = marvell_phy_tai_hw_enable(dev);
	if (err < 0)
		return err;

	err = marvell_ts_global_config(dev, &marvell_phy_ts_ops);
	if (err)
		return err;

	/* PHY specific global configuration - set TSAtSFD, timestamp at SFD */
	err = marvell_phy_ts_global_write(dev, PTPG_CONFIG_3,
					  PTPG_CONFIG_3_TSATSFD);
	if (err < 0)
		return err;

	if (phydev->mii_ts &&
	    mii_ts_to_phy_ptp(phydev->mii_ts)->event_in)
		return marvell_phy_ptp_event_apply(phydev);

	return 0;
}

static int marvell_phy_ptp_hwtstamp(struct mii_timestamper *mii_ts,
				    struct ifreq *ifr)
{
	struct marvell_phy_ptp *phy_ptp = mii_ts_to_phy_ptp(mii_ts);
	struct kernel_hwtstamp_config kcfg;
	struct hwtstamp_config cfg;
	int err;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	hwtstamp_config_to_kernel(&kcfg, &cfg);

	if (kcfg.tx_type != HWTSTAMP_TX_OFF ||
	    kcfg.rx_filter != HWTSTAMP_FILTER_NONE) {
		err = marvell_phy_ptp_setup(phy_ptp->ts.dev);
		if (err)
			return err;

		err = marvell_ts_port_reconfig(&phy_ptp->ts);
		if (err < 0)
			return err;
	}

	err = marvell_ts_hwtstamp_set(&phy_ptp->ts, &kcfg, NULL);
	if (err)
		return err;

	hwtstamp_config_from_kernel(&cfg, &kcfg);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static struct marvell_phy_ptp *marvell_phy_ptp_get(struct phy_device *phydev)
{
	if (!phydev || !phydev->mii_ts ||
	    phydev->mii_ts->rxtstamp != marvell_phy_ptp_rxtstamp)
		return NULL;

	return mii_ts_to_phy_ptp(phydev->mii_ts);
}

static int marvell_phy_ptp_event_apply(struct phy_device *phydev)
{
	int err;

	err = phy_modify_paged(phydev, MARVELL_PAGE_LED, LED_FUNC_CTRL,
			       LED_FUNC_LED1, LED_FUNC_LED1_HI_Z);
	if (err < 0)
		return err;

	err = phy_modify_paged(phydev, MARVELL_PAGE_MISC, GCR,
			       GCR_PTP_INPUT_SOURCE, GCR_PTP_INPUT_SOURCE);
	if (err < 0)
		return err;

	err = phy_modify_paged(phydev, MARVELL_PAGE_TAI_GLOBAL, TAI_CFG0,
			       TAI_CFG0_EVENTCAPOV | TAI_CFG0_EVENTCTRSTART |
			       TAI_CFG0_EVENTPHASE,
			       TAI_CFG0_EVENTCAPOV | TAI_CFG0_EVENTCTRSTART);
	if (err < 0)
		return err;

	err = phy_write_paged(phydev, MARVELL_PAGE_TAI_GLOBAL,
			      TAI_EVENT_STATUS, 0);
	return err < 0 ? err : 0;
}

static int marvell_phy_ptp_event_off(struct phy_device *phydev,
				     struct marvell_phy_ptp *phy_ptp)
{
	int err;

	err = phy_modify_paged(phydev, MARVELL_PAGE_TAI_GLOBAL, TAI_CFG0,
			       TAI_CFG0_EVENTCAPOV | TAI_CFG0_EVENTCTRSTART, 0);
	if (!err)
		err = phy_modify_paged(phydev, MARVELL_PAGE_MISC, GCR,
				       GCR_PTP_INPUT_SOURCE, 0);
	if (!err)
		err = phy_modify_paged(phydev, MARVELL_PAGE_LED, LED_FUNC_CTRL,
				       LED_FUNC_LED1, phy_ptp->led1_saved);
	return err < 0 ? err : 0;
}

int marvell_phy_ptp_event_enable(struct phy_device *phydev, bool enable)
{
	struct marvell_phy_ptp *phy_ptp;
	int val, err;

	mutex_lock(&marvell_phy_ptp_ext_lock);
	phy_ptp = marvell_phy_ptp_get(phydev);
	if (!phy_ptp) {
		err = -ENODEV;
		goto out;
	}

	if (enable) {
		if (!phy_ptp->event_in) {
			val = phy_read_paged(phydev, MARVELL_PAGE_LED,
					     LED_FUNC_CTRL);
			if (val < 0) {
				err = val;
				goto out;
			}
			phy_ptp->led1_saved = val & LED_FUNC_LED1;
		}
		WRITE_ONCE(phy_ptp->event_in, true);
		err = marvell_phy_ptp_event_apply(phydev);
	} else if (phy_ptp->event_in) {
		WRITE_ONCE(phy_ptp->event_in, false);
		err = marvell_phy_ptp_event_off(phydev, phy_ptp);
	} else {
		err = 0;
	}
out:
	mutex_unlock(&marvell_phy_ptp_ext_lock);
	return err;
}
EXPORT_SYMBOL_GPL(marvell_phy_ptp_event_enable);

int marvell_phy_ptp_event_read(struct phy_device *phydev, u64 *ns, u8 *count)
{
	struct marvell_phy_ptp *phy_ptp;
	int oldpage, st = -1, lo = -1, hi = -1, err = 0;

	mutex_lock(&marvell_phy_ptp_ext_lock);
	phy_ptp = marvell_phy_ptp_get(phydev);
	if (!phy_ptp || !phy_ptp->event_in) {
		mutex_unlock(&marvell_phy_ptp_ext_lock);
		return -ENODEV;
	}

	oldpage = phy_select_page(phydev, MARVELL_PAGE_TAI_GLOBAL);
	if (oldpage >= 0) {
		st = __phy_read(phydev, TAI_EVENT_STATUS);
		if (st >= 0 && (st & TAI_EVENT_STATUS_VALID)) {
			lo = __phy_read(phydev, TAI_EVENT_TIME_LO);
			hi = __phy_read(phydev, TAI_EVENT_TIME_HI);
			err = __phy_write(phydev, TAI_EVENT_STATUS, 0);
		}
	}
	err = phy_restore_page(phydev, oldpage, err);
	if (!err && st < 0)
		err = st;
	else if (!err && !(st & TAI_EVENT_STATUS_VALID))
		err = -EAGAIN;
	else if (!err && (lo < 0 || hi < 0))
		err = -EIO;
	if (!err) {
		*count = st & TAI_EVENT_STATUS_COUNT;
		*ns = marvell_tai_cyc2time(phy_ptp->ts.tai,
					   (u32)lo | (u32)hi << 16);
	}
	mutex_unlock(&marvell_phy_ptp_ext_lock);

	return err;
}
EXPORT_SYMBOL_GPL(marvell_phy_ptp_event_read);

bool marvell_phy_ptp_event_active(struct phy_device *phydev)
{
	struct marvell_phy_ptp *phy_ptp = marvell_phy_ptp_get(phydev);

	return phy_ptp && READ_ONCE(phy_ptp->event_in);
}

/* This function should be called from the PHY threaded interrupt
 * handler to process any stored timestamps in a timely manner.
 * The presence of an interrupt has an effect on how quickly a
 * timestamp requiring received packet will be processed.
 */
irqreturn_t marvell_phy_ptp_irq(struct phy_device *phydev)
{
	struct marvell_phy_ptp *phy_ptp;

	if (!phydev->mii_ts)
		return IRQ_NONE;

	phy_ptp = mii_ts_to_phy_ptp(phydev->mii_ts);

	return marvell_ts_irq(&phy_ptp->ts);
}
EXPORT_SYMBOL_GPL(marvell_phy_ptp_irq);

void marvell_phy_ptp_remove(struct phy_device *phydev)
{
	struct marvell_phy_ptp *phy_ptp;
	struct marvell_tai *tai;

	if (!phydev->mii_ts)
		return;

	phy_ptp = mii_ts_to_phy_ptp(phydev->mii_ts);
	tai = phy_ptp->ts.tai;

	mutex_lock(&marvell_phy_ptp_ext_lock);
	if (phy_ptp->event_in) {
		phy_ptp->event_in = false;
		marvell_phy_ptp_event_off(phydev, phy_ptp);
	}
	phydev->mii_ts = NULL;
	mutex_unlock(&marvell_phy_ptp_ext_lock);
	synchronize_net();

	marvell_ts_remove(&phy_ptp->ts);
	marvell_tai_remove(tai);
}
EXPORT_SYMBOL_GPL(marvell_phy_ptp_remove);

/* 88e1510 can filter on 802.1AS frames, IEEE1588v1/v2 frames or both.
 *   802.1AS frames can be matched by TransSpec = 1
 */
static const struct marvell_ts_caps marvell_phy_ts_caps = {
	.rx_filters = BIT(HWTSTAMP_FILTER_SOME),
};

static int __marvell_phy_ptp_probe(struct phy_device *phydev)
{
	struct marvell_phy_ptp *phy_ptp;
	struct marvell_tai *tai;
	struct device *dev;
	int err;

	dev = &phydev->mdio.dev;

	phy_ptp = devm_kzalloc(dev, sizeof(*phy_ptp), GFP_KERNEL);
	if (!phy_ptp)
		return -ENOMEM;

	phy_ptp->mii_ts.rxtstamp = marvell_phy_ptp_rxtstamp;
	phy_ptp->mii_ts.txtstamp = marvell_phy_ptp_txtstamp;
	phy_ptp->mii_ts.hwtstamp = marvell_phy_ptp_hwtstamp;
	phy_ptp->mii_ts.ts_info = marvell_phy_ptp_ts_info;

	err = marvell_tai_probe(&tai, &marvell_phy_tai_ops,
				&marvell_phy_tai_param, NULL,
				"Marvell PHY", dev);
	if (err)
		return err;

	/* Setup the global PTP configuration */
	err = marvell_phy_ptp_setup(dev);
	if (err)
		goto err_tai;

	err = marvell_ts_probe(&phy_ptp->ts, dev, tai,
			       &marvell_phy_ts_caps,
			       &marvell_phy_ts_ops, 0);
	if (err)
		goto err_tai;

	phydev->mii_ts = &phy_ptp->mii_ts;

	return 0;

err_tai:
	marvell_tai_remove(tai);
	return err;
}

int devm_marvell_phy_ptp_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	void *group;
	int err;

	group = devres_open_group(dev, NULL, GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	err = __marvell_phy_ptp_probe(phydev);
	if (err) {
		devres_release_group(dev, group);
		return err;
	}

	devres_remove_group(dev, group);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_marvell_phy_ptp_probe);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Marvell PHY PTP library");
MODULE_LICENSE("GPL v2");
