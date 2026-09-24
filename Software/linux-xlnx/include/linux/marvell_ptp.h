/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MARVELL_PTP_H
#define LINUX_MARVELL_PTP_H

#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/timecounter.h>

struct device;
struct ethtool_ts_info;
struct marvell_tai;
struct netlink_ext_ack;

struct marvell_extts {
	u32 time;
	u8 status;
#define MV_STATUS_EVENTCAPVALID	BIT(8)
};

#define MARVELL_TAI_READ_FAILED	U64_MAX

struct marvell_tai_ops {
	int (*tai_hw_enable)(struct device *dev);
	void (*tai_hw_disable)(struct device *dev);
	u64 (*tai_clock_read)(struct device *dev,
			      struct ptp_system_timestamp *sts);
	int (*tai_extts_read)(struct device *dev, int reg,
			      struct marvell_extts *extts);
	int (*tai_pin_verify)(struct device *dev, int pin,
			      enum ptp_pin_function func, unsigned int chan);
	int (*tai_pin_setup)(struct device *dev, int pin,
			     enum ptp_pin_function func, int enable);
	int (*tai_write)(struct device *dev, u8 reg, u16 val);
	int (*tai_modify)(struct device *dev, u8 reg, u16 mask, u16 val);
	long (*tai_aux_work)(struct device *dev);
};

/* TAI module */
struct marvell_tai_param {
	u32 cc_mult_num;
	u32 cc_mult_den;
	u32 cc_mult;
	int cc_shift;
};

struct marvell_tai_pins {
	struct ptp_pin_desc *pins;
	int n_pins;
	int n_ext_ts;
};

u64 marvell_tai_cyc2time(struct marvell_tai *tai, u32 cyc);
u64 marvell_tai_gettime_ns(struct marvell_tai *tai);
int marvell_tai_ptp_clock_index(struct marvell_tai *tai);
int marvell_tai_schedule(struct marvell_tai *tai, unsigned long delay);
void marvell_tai_cancel_worker(struct marvell_tai *tai);
void marvell_tai_remove(struct marvell_tai *tai);
int marvell_tai_probe(struct marvell_tai **taip,
		      const struct marvell_tai_ops *ops,
		      const struct marvell_tai_param *param,
		      const struct marvell_tai_pins *pins,
		      const char *name, struct device *dev);

/* Timestamping module */
struct marvell_hwts {
	u32 time;
	u16 stat;
#define MV_STATUS_INTSTATUS_MASK	0x0006
#define MV_STATUS_INTSTATUS_NORMAL	0x0000
#define MV_STATUS_VALID			BIT(0)
	u16 seq;
};

enum marvell_ts_reg {
	MARVELL_TS_ARR0,
	MARVELL_TS_ARR1,
	MARVELL_TS_DEP,
};

struct marvell_ts_ops {
	int (*ts_global_write)(struct device *dev, u8 reg, u16 val);
	int (*ts_port_enable)(struct device *dev, u8 port);
	void (*ts_port_disable)(struct device *dev, u8 port);
	int (*ts_port_read_ts)(struct device *dev, struct marvell_hwts *ts,
			       u8 port, enum marvell_ts_reg ts_reg);
	int (*ts_port_write)(struct device *dev, u8 port, u8 reg, u16 val);
	int (*ts_port_modify)(struct device *dev, u8 port, u8 reg, u16 mask,
			      u16 val);
};

struct marvell_ts_caps {
	u32 rx_filters;
};

struct marvell_rxts {
	struct list_head node;
	u64 ns;
	unsigned long expires;
	u16 seq;
};

struct marvell_rxq {
	spinlock_t rx_lock;
	struct list_head rx_free;
	struct list_head rx_pend;
	struct sk_buff_head rx_queue;
	struct marvell_rxts rx_ts[64];
};

struct marvell_ts {
	struct marvell_tai *tai;
	const struct marvell_ts_ops *ops;
	struct device *dev;

	/* We only support one outstanding transmit skb */
	struct sk_buff *tx_skb;
	enum hwtstamp_tx_types tx_type;

	struct marvell_rxq rxq[2];
	enum hwtstamp_rx_filters rx_filter;

	const struct marvell_ts_caps *caps;
	u8 port;

	bool irq_handler_called;
};

bool marvell_ts_rxtstamp(struct marvell_ts *ts, struct sk_buff *skb, int type);
bool marvell_ts_txtstamp(struct marvell_ts *ts, struct sk_buff *skb, int type);
int marvell_ts_hwtstamp_get(struct marvell_ts *ts,
			    struct kernel_hwtstamp_config *kcfg);
int marvell_ts_hwtstamp_set(struct marvell_ts *ts,
			    struct kernel_hwtstamp_config *kcfg,
			    struct netlink_ext_ack *ack);
int marvell_ts_info(struct marvell_ts *ts, struct ethtool_ts_info *ts_info);
long marvell_ts_aux_work(struct marvell_ts *ts);
irqreturn_t marvell_ts_irq(struct marvell_ts *ts);
int marvell_ts_global_config(struct device *dev,
			     const struct marvell_ts_ops *ops);
int marvell_ts_port_reconfig(struct marvell_ts *ts);

void marvell_ts_remove(struct marvell_ts *ts);
int marvell_ts_probe(struct marvell_ts *ts, struct device *dev,
		     struct marvell_tai *tai,
		     const struct marvell_ts_caps *caps,
		     const struct marvell_ts_ops *ops, u8 port);

#endif
