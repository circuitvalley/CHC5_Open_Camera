/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Zynq-7000 GEM PTP clock interface
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#ifndef _LINUX_MACB_ZYNQ_PTP_H
#define _LINUX_MACB_ZYNQ_PTP_H

#include <linux/errno.h>
#include <linux/types.h>

struct net_device;

#if IS_ENABLED(CONFIG_MACB_ZYNQ_PTP)
int macb_zynq_ptp_now_ns(struct net_device *ndev, u64 *ns);
int macb_zynq_ptp_follow(struct net_device *ndev, u64 ref_ns, u64 gem_pre_ns,
			 u64 gem_post_ns);
bool macb_zynq_ptp_following(struct net_device *ndev);
#else
static inline int macb_zynq_ptp_now_ns(struct net_device *ndev, u64 *ns)
{
	return -EOPNOTSUPP;
}

static inline int macb_zynq_ptp_follow(struct net_device *ndev, u64 ref_ns,
				       u64 gem_pre_ns, u64 gem_post_ns)
{
	return -EOPNOTSUPP;
}

static inline bool macb_zynq_ptp_following(struct net_device *ndev)
{
	return false;
}
#endif

#endif
