/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MARVELL_PTP_H
#define MARVELL_PTP_H

#include <linux/irqreturn.h>

struct phy_device;

#if IS_ENABLED(CONFIG_MARVELL_PHY_PTP)
irqreturn_t marvell_phy_ptp_irq(struct phy_device *phydev);
int devm_marvell_phy_ptp_probe(struct phy_device *phydev);
void marvell_phy_ptp_remove(struct phy_device *phydev);
bool marvell_phy_ptp_event_active(struct phy_device *phydev);
#else
static inline int marvell_phy_ptp_dummy_probe(void)
{
	return 0;
}
#define devm_marvell_phy_ptp_probe(x...) marvell_phy_ptp_dummy_probe()
static inline bool marvell_phy_ptp_event_active(struct phy_device *phydev)
{
	return false;
}

#endif

#endif
