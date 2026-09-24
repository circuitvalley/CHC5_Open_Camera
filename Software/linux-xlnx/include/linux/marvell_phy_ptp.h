/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell 88E151x PTP event input
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#ifndef LINUX_MARVELL_PHY_PTP_H
#define LINUX_MARVELL_PHY_PTP_H

#include <linux/errno.h>
#include <linux/types.h>

struct phy_device;

#if IS_ENABLED(CONFIG_MARVELL_PHY_PTP)
int marvell_phy_ptp_event_enable(struct phy_device *phydev, bool enable);
int marvell_phy_ptp_event_read(struct phy_device *phydev, u64 *ns, u8 *count);
#else
static inline int marvell_phy_ptp_event_enable(struct phy_device *phydev,
					       bool enable)
{
	return -EOPNOTSUPP;
}

static inline int marvell_phy_ptp_event_read(struct phy_device *phydev,
					     u64 *ns, u8 *count)
{
	return -EOPNOTSUPP;
}
#endif

#endif
