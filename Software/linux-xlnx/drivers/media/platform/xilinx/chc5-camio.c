// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 camera I/O and sync
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/macb_zynq_ptp.h>
#include <linux/marvell_phy_ptp.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <uapi/linux/chc5-v4l2-controls.h>

#define CHC5CAMIO_REG_ID_VERSION	0x00
#define CHC5CAMIO_REG_CONTROL		0x04
#define CHC5CAMIO_REG_STATUS		0x08
#define CHC5CAMIO_REG_IN_CFG		0x10
#define CHC5CAMIO_REG_IN_DELAY		0x18
#define CHC5CAMIO_REG_IN_DIVIDER	0x1C
#define CHC5CAMIO_REG_IN_STATUS		0x20
#define CHC5CAMIO_REG_STROBE_CFG	0x30
#define CHC5CAMIO_REG_STROBE_DELAY	0x34
#define CHC5CAMIO_REG_STROBE_DUR	0x3C
#define CHC5CAMIO_REG_STROBE_MINON	0x40
#define CHC5CAMIO_REG_OUT_CFG		0x50
#define CHC5CAMIO_REG_SYNC_CFG		0x60
#define CHC5CAMIO_REG_SYNC_TRIG_ROUTE	0x68
#define CHC5CAMIO_REG_SYNC_STATUS	0x6C
#define CHC5CAMIO_REG_TRIG_CFG		0x80
#define CHC5CAMIO_REG_TRIG_DELAY	0x84
#define CHC5CAMIO_REG_TRIG_WIDTH	0x88
#define CHC5CAMIO_REG_TRIG_MINPER	0x8C
#define CHC5CAMIO_REG_IN_FILTER		0x90
#define CHC5CAMIO_REG_TRIG_COUNT	0x94
#define CHC5CAMIO_REG_LINE_PERIOD	0x154
#define CHC5CAMIO_REG_LINE_CFG		0x15C
#define CHC5CAMIO_REG_FRAME_LINES	0x160
#define CHC5CAMIO_REG_PTP_CTRL		0x100
#define CHC5CAMIO_REG_PTP_TIME_LO	0x104
#define CHC5CAMIO_REG_PTP_TIME_HI	0x108
#define CHC5CAMIO_REG_PTP_RATE_ADJ	0x110
#define CHC5CAMIO_REG_PTP_STATUS	0x114
#define CHC5CAMIO_REG_EVT_NEXT_LO	0x118
#define CHC5CAMIO_REG_EVT_NEXT_HI	0x11C
#define CHC5CAMIO_REG_EVT_PERIOD	0x120
#define CHC5CAMIO_REG_EVT_CFG		0x124
#define CHC5CAMIO_REG_EVT_COUNT		0x128
#define CHC5CAMIO_REG_SCHED_NEXT_LO	0x130
#define CHC5CAMIO_REG_SCHED_NEXT_HI	0x134
#define CHC5CAMIO_REG_SCHED_PER_LO	0x138
#define CHC5CAMIO_REG_SCHED_PER_HI	0x13C
#define CHC5CAMIO_REG_SCHED_PER_FRAC	0x140

#define CHC5CAMIO_MAGIC			0xCC5Au
#define CHC5CAMIO_VER_PTP		0x0200u
#define CHC5CAMIO_VER_TRIG		0x0300u
#define CHC5CAMIO_VER_NATIVE		0x0301u

#define CHC5CAMIO_PTP_TB_EN		BIT(0)
#define CHC5CAMIO_PTP_EVT_EN		BIT(1)
#define CHC5CAMIO_PTP_SCHED_EN		BIT(2)
#define CHC5CAMIO_PTP_LOAD		BIT(3)
#define CHC5CAMIO_PTP_COMMIT		BIT(4)

#define CHC5CAMIO_PTPST_SCHED_MASK	0x3Cu
#define CHC5CAMIO_PTPST_RUNNING		BIT(2)
#define CHC5CAMIO_PTPST_EVT_REFUSED	BIT(6)

#define CHC5CAMIO_EVT_CFG_EN		BIT(0)
#define CHC5CAMIO_EVT_CFG_WIDTH(t)	(((t) & 0xFFu) << 8)

#define CHC5CAMIO_CTRL_ENABLE		BIT(0)
#define CHC5CAMIO_CTRL_SOFT_RESET	BIT(1)
#define CHC5CAMIO_CTRL_SW_TRIGGER	BIT(2)

#define CHC5CAMIO_TRIG_SRC_MASK		0x3u
#define CHC5CAMIO_TRIG_WIDTH_MODE	BIT(2)

#define CHC5CAMIO_INCFG_FUNC_SHIFT	0
#define CHC5CAMIO_INCFG_FUNC_MASK	0x3u
#define CHC5CAMIO_INCFG_ACT_SHIFT	2
#define CHC5CAMIO_INCFG_ACT_MASK	0x3u
#define CHC5CAMIO_INCFG_INVERT		BIT(4)

#define CHC5CAMIO_STRB_ENABLE		BIT(0)
#define CHC5CAMIO_STRB_INVERT		BIT(1)
#define CHC5CAMIO_STRB_SRC_SHIFT	2
#define CHC5CAMIO_STRB_SRC_MASK		0x3u

#define CHC5CAMIO_OUTCFG_SRC_SHIFT	0
#define CHC5CAMIO_OUTCFG_SRC_MASK	0x7u
#define CHC5CAMIO_OUTCFG_INVERT		BIT(3)
#define CHC5CAMIO_OUTCFG_USER_VALUE	BIT(4)
#define CHC5CAMIO_OUTCFG_NATIVE_LOW	BIT(5)

#define CHC5CAMIO_SYNC_ROLE_SHIFT	0
#define CHC5CAMIO_SYNC_ROLE_MASK	0x3u
#define CHC5CAMIO_SYNC_XVS_DIR		BIT(2)
#define CHC5CAMIO_SYNC_XHS_DIR		BIT(3)
#define CHC5CAMIO_SYNC_XVS_POL		BIT(4)
#define CHC5CAMIO_SYNC_XHS_POL		BIT(5)
#define CHC5CAMIO_SYNC_ROLE_BY_I2C	BIT(6)

#define CHC5CAMIO_TIME16_MASK		0xFFFFu
#define CHC5CAMIO_DIVIDER_MASK		0xFFu
#define CHC5CAMIO_STATUS_MASK		0x7FFu
#define CHC5CAMIO_NS31_MASK		0x7FFFFFFFu
#define CHC5CAMIO_NS24_MASK		0xFFFFFFu
#define CHC5CAMIO_SCHED_PS_MIN		1000000LL
#define CHC5CAMIO_SCHED_PS_MAX		((1LL << 48) * 1000)
#define CHC5CAMIO_INSTATUS_MASK		0xFFFFFFu
#define CHC5CAMIO_SYNCSTATUS_MASK	0xFFFFu

enum chc5camio_servo_state {
	CHC5CAMIO_SERVO_OFF,
	CHC5CAMIO_SERVO_FIND,
	CHC5CAMIO_SERVO_ARM,
	CHC5CAMIO_SERVO_STEP,
	CHC5CAMIO_SERVO_FREQ,
	CHC5CAMIO_SERVO_PI,
};

enum chc5camio_servo_ref {
	CHC5CAMIO_REF_PIN13,
	CHC5CAMIO_REF_GEM,
};

struct chc5camio_servo {
	enum chc5camio_servo_state state;
	enum chc5camio_servo_ref ref;
	enum chc5camio_servo_state after_arm;
	u64	t0_ns;
	u32	period_ns;
	s64	freq_ppb;
	s64	last_err_ns;
	u64	freq_t_ns;
	s64	freq_err_ns;
	bool	freq_have;
	bool	locked;
	bool	ptp_on;
	u32	good;
	u32	quiet;
	u32	samples, missed, rejected, steps, arms, refused, errors;
	int	last_ret;
};

struct chc5camio_device {
	struct device		*dev;
	void __iomem		*iomem;
	struct clk		*axi_clk;

	struct v4l2_device	v4l2_dev;
	struct v4l2_subdev	subdev;
	struct v4l2_ctrl_handler ctrl_handler;

	u32			control;
	u32			in_cfg;
	u32			strobe_cfg;
	u32			out_cfg;
	u32			sync_cfg;
	u32			sync_trig_route;
	u32			trig_cfg;

	struct mutex		lock;
	u32			version;

	struct ptp_clock_info	ptp_info;
	struct ptp_clock	*ptp_clock;
	struct mutex		ptp_lock;
	u32			ptp_ctrl;
	u64			sched_period_ps;
	u64			sched_start_ns;
	bool			sched_on;
	bool			sched_pending;
	struct delayed_work	sched_work;
	u32			line_period;
	unsigned long		clk_hz;
	struct device_node	*eth_np;
	struct net_device	*ndev;
	struct phy_device	*phydev;
	struct chc5camio_servo	servo;
};

static inline struct chc5camio_device *to_chc5camio(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5camio_device, subdev);
}

static inline void chc5camio_write(struct chc5camio_device *c, u32 reg, u32 val)
{
	iowrite32(val, c->iomem + reg);
}

static inline u32 chc5camio_read(struct chc5camio_device *c, u32 reg)
{
	return ioread32(c->iomem + reg);
}

static inline bool chc5camio_has(const struct chc5camio_device *c, u32 ver)
{
	return (c->version >> 16) == CHC5CAMIO_MAGIC && (c->version & 0xFFFFu) >= ver;
}

static inline u32 chc5camio_field(u32 reg, u32 mask, u32 shift, u32 val)
{
	return (reg & ~(mask << shift)) | ((val & mask) << shift);
}

static inline u32 chc5camio_bit(u32 reg, u32 bit, bool set)
{
	return set ? (reg | bit) : (reg & ~bit);
}

static void chc5camio_reset(struct chc5camio_device *c)
{
	c->control = 0;
	c->in_cfg = 0;
	c->strobe_cfg = 0;
	c->out_cfg = 0;
	c->sync_cfg = 0;
	c->sync_trig_route = 0;
	c->trig_cfg = 0;

	chc5camio_write(c, CHC5CAMIO_REG_CONTROL, 0);
	chc5camio_write(c, CHC5CAMIO_REG_IN_CFG, 0);
	if (chc5camio_has(c, CHC5CAMIO_VER_TRIG)) {
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_CFG, 0);
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_DELAY, 0);
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_WIDTH, 0);
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_MINPER, 0);
		chc5camio_write(c, CHC5CAMIO_REG_IN_FILTER, 0);
	} else {
		chc5camio_write(c, CHC5CAMIO_REG_IN_DELAY, 0);
	}
	chc5camio_write(c, CHC5CAMIO_REG_IN_DIVIDER, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_DELAY, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_DUR, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_MINON, 0);
	chc5camio_write(c, CHC5CAMIO_REG_OUT_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_SYNC_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_SYNC_TRIG_ROUTE, 0);
}

static int chc5camio_sched_apply(struct chc5camio_device *c);
static void chc5camio_sched_resync(struct chc5camio_device *c);
static bool chc5camio_servo_owns(struct chc5camio_device *c);
static bool chc5camio_on_ptp_time(struct chc5camio_device *c);
static bool chc5camio_ptp_pulses(struct chc5camio_device *c);

static int chc5camio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5camio_device *c =
		container_of(ctrl->handler, struct chc5camio_device, ctrl_handler);
	bool on = !!ctrl->val;
	u32 v = ctrl->val;
	int ret = 0;

	mutex_lock(&c->lock);

	switch (ctrl->id) {
	case CHC5CAMIO_CID_ENABLE:
		c->control = chc5camio_bit(c->control, CHC5CAMIO_CTRL_ENABLE, on);
		chc5camio_write(c, CHC5CAMIO_REG_CONTROL, c->control);
		break;
	case CHC5CAMIO_CID_SOFT_RESET:
		chc5camio_write(c, CHC5CAMIO_REG_CONTROL,
				c->control | CHC5CAMIO_CTRL_SOFT_RESET);
		chc5camio_write(c, CHC5CAMIO_REG_CONTROL, c->control);
		break;

	case CHC5CAMIO_CID_IN_FUNCTION:
		c->in_cfg = chc5camio_field(c->in_cfg, CHC5CAMIO_INCFG_FUNC_MASK,
					    CHC5CAMIO_INCFG_FUNC_SHIFT, v);
		chc5camio_write(c, CHC5CAMIO_REG_IN_CFG, c->in_cfg);
		break;
	case CHC5CAMIO_CID_IN_ACTIVATION:
		c->in_cfg = chc5camio_field(c->in_cfg, CHC5CAMIO_INCFG_ACT_MASK,
					    CHC5CAMIO_INCFG_ACT_SHIFT, v);
		chc5camio_write(c, CHC5CAMIO_REG_IN_CFG, c->in_cfg);
		break;
	case CHC5CAMIO_CID_IN_INVERT:
		c->in_cfg = chc5camio_bit(c->in_cfg, CHC5CAMIO_INCFG_INVERT, on);
		chc5camio_write(c, CHC5CAMIO_REG_IN_CFG, c->in_cfg);
		break;
	case CHC5CAMIO_CID_TRIG_DELAY_NS:
		if (chc5camio_has(c, CHC5CAMIO_VER_TRIG)) {
			chc5camio_write(c, CHC5CAMIO_REG_TRIG_DELAY, v & CHC5CAMIO_NS31_MASK);
		} else {
			u32 u = (v + 5000u) / 10000u;

			chc5camio_write(c, CHC5CAMIO_REG_IN_DELAY,
					min_t(u32, u, CHC5CAMIO_TIME16_MASK));
		}
		break;
	case CHC5CAMIO_CID_TRIG_SOURCE:
		c->trig_cfg = chc5camio_field(c->trig_cfg, CHC5CAMIO_TRIG_SRC_MASK, 0, v);
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_CFG, c->trig_cfg);
		break;
	case CHC5CAMIO_CID_TRIG_WIDTH_MODE:
		c->trig_cfg = chc5camio_bit(c->trig_cfg, CHC5CAMIO_TRIG_WIDTH_MODE, on);
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_CFG, c->trig_cfg);
		break;
	case CHC5CAMIO_CID_TRIG_SOFTWARE:
		chc5camio_write(c, CHC5CAMIO_REG_CONTROL,
				c->control | CHC5CAMIO_CTRL_SW_TRIGGER);
		break;
	case CHC5CAMIO_CID_TRIG_WIDTH_NS:
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_WIDTH, v & CHC5CAMIO_NS31_MASK);
		break;
	case CHC5CAMIO_CID_TRIG_MINPER_NS:
		chc5camio_write(c, CHC5CAMIO_REG_TRIG_MINPER, v & CHC5CAMIO_NS31_MASK);
		break;
	case CHC5CAMIO_CID_IN_FILTER_NS:
		chc5camio_write(c, CHC5CAMIO_REG_IN_FILTER, v & CHC5CAMIO_NS24_MASK);
		break;
	case CHC5CAMIO_CID_LINE_PERIOD_NS:
		c->line_period = v & CHC5CAMIO_NS24_MASK;
		chc5camio_write(c, CHC5CAMIO_REG_LINE_PERIOD, c->line_period);
		break;
	case CHC5CAMIO_CID_SCHED_PERIOD_PS:
		mutex_lock(&c->ptp_lock);
		c->sched_period_ps = *ctrl->p_new.p_s64;
		mutex_unlock(&c->ptp_lock);
		if (c->sched_on)
			ret = chc5camio_sched_apply(c);
		break;
	case CHC5CAMIO_CID_SCHED_START_NS:
		mutex_lock(&c->ptp_lock);
		c->sched_start_ns = *ctrl->p_new.p_s64;
		mutex_unlock(&c->ptp_lock);
		if (c->sched_on)
			ret = chc5camio_sched_apply(c);
		break;
	case CHC5CAMIO_CID_SCHED_ENABLE:
		mutex_lock(&c->ptp_lock);
		c->sched_on = on;
		mutex_unlock(&c->ptp_lock);
		ret = chc5camio_sched_apply(c);
		if (ret) {
			mutex_lock(&c->ptp_lock);
			c->sched_on = !on;
			mutex_unlock(&c->ptp_lock);
		}
		break;
	case CHC5CAMIO_CID_FRAME_LINES:
		chc5camio_write(c, CHC5CAMIO_REG_FRAME_LINES, v & CHC5CAMIO_NS24_MASK);
		break;
	case CHC5CAMIO_CID_LINE_XHS:
		chc5camio_write(c, CHC5CAMIO_REG_LINE_CFG, on ? 1u : 0u);
		break;
	case CHC5CAMIO_CID_IN_DIVIDER:
		chc5camio_write(c, CHC5CAMIO_REG_IN_DIVIDER, v & CHC5CAMIO_DIVIDER_MASK);
		break;

	case CHC5CAMIO_CID_STROBE_ENABLE:
		c->strobe_cfg = chc5camio_bit(c->strobe_cfg, CHC5CAMIO_STRB_ENABLE, on);
		chc5camio_write(c, CHC5CAMIO_REG_STROBE_CFG, c->strobe_cfg);
		break;
	case CHC5CAMIO_CID_STROBE_INVERT:
		c->strobe_cfg = chc5camio_bit(c->strobe_cfg, CHC5CAMIO_STRB_INVERT, on);
		chc5camio_write(c, CHC5CAMIO_REG_STROBE_CFG, c->strobe_cfg);
		break;
	case CHC5CAMIO_CID_STROBE_SRC:
		c->strobe_cfg = chc5camio_field(c->strobe_cfg, CHC5CAMIO_STRB_SRC_MASK,
						CHC5CAMIO_STRB_SRC_SHIFT, v);
		chc5camio_write(c, CHC5CAMIO_REG_STROBE_CFG, c->strobe_cfg);
		break;
	case CHC5CAMIO_CID_STROBE_DELAY:
		chc5camio_write(c, CHC5CAMIO_REG_STROBE_DELAY, v & CHC5CAMIO_TIME16_MASK);
		break;
	case CHC5CAMIO_CID_STROBE_DURATION:
		chc5camio_write(c, CHC5CAMIO_REG_STROBE_DUR, v & CHC5CAMIO_TIME16_MASK);
		break;
	case CHC5CAMIO_CID_STROBE_MINON:
		chc5camio_write(c, CHC5CAMIO_REG_STROBE_MINON, v & CHC5CAMIO_TIME16_MASK);
		break;

	case CHC5CAMIO_CID_OUT_SOURCE:
		c->out_cfg = chc5camio_field(c->out_cfg, CHC5CAMIO_OUTCFG_SRC_MASK,
					     CHC5CAMIO_OUTCFG_SRC_SHIFT, v);
		chc5camio_write(c, CHC5CAMIO_REG_OUT_CFG, c->out_cfg);
		break;
	case CHC5CAMIO_CID_OUT_INVERT:
		c->out_cfg = chc5camio_bit(c->out_cfg, CHC5CAMIO_OUTCFG_INVERT, on);
		chc5camio_write(c, CHC5CAMIO_REG_OUT_CFG, c->out_cfg);
		break;
	case CHC5CAMIO_CID_OUT_USER_VALUE:
		c->out_cfg = chc5camio_bit(c->out_cfg, CHC5CAMIO_OUTCFG_USER_VALUE, on);
		chc5camio_write(c, CHC5CAMIO_REG_OUT_CFG, c->out_cfg);
		break;
	case CHC5CAMIO_CID_NATIVE_LOW:
		c->out_cfg = chc5camio_bit(c->out_cfg, CHC5CAMIO_OUTCFG_NATIVE_LOW, on);
		chc5camio_write(c, CHC5CAMIO_REG_OUT_CFG, c->out_cfg);
		break;

	case CHC5CAMIO_CID_SYNC_ROLE:
		c->sync_cfg = chc5camio_field(c->sync_cfg, CHC5CAMIO_SYNC_ROLE_MASK,
					      CHC5CAMIO_SYNC_ROLE_SHIFT, v);
		chc5camio_write(c, CHC5CAMIO_REG_SYNC_CFG, c->sync_cfg);
		break;
	case CHC5CAMIO_CID_SYNC_XVS_POL:
		c->sync_cfg = chc5camio_bit(c->sync_cfg, CHC5CAMIO_SYNC_XVS_POL, on);
		chc5camio_write(c, CHC5CAMIO_REG_SYNC_CFG, c->sync_cfg);
		break;
	case CHC5CAMIO_CID_SYNC_XHS_POL:
		c->sync_cfg = chc5camio_bit(c->sync_cfg, CHC5CAMIO_SYNC_XHS_POL, on);
		chc5camio_write(c, CHC5CAMIO_REG_SYNC_CFG, c->sync_cfg);
		break;
	case CHC5CAMIO_CID_SYNC_ROLE_BY:
		c->sync_cfg = chc5camio_bit(c->sync_cfg, CHC5CAMIO_SYNC_ROLE_BY_I2C, on);
		chc5camio_write(c, CHC5CAMIO_REG_SYNC_CFG, c->sync_cfg);
		break;
	case CHC5CAMIO_CID_SYNC_TRIG_ROUTE:
		c->sync_trig_route = v & 0x3u;
		chc5camio_write(c, CHC5CAMIO_REG_SYNC_TRIG_ROUTE, c->sync_trig_route);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&c->lock);
	return ret;
}

static int chc5camio_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5camio_device *c =
		container_of(ctrl->handler, struct chc5camio_device, ctrl_handler);
	int ret = 0;

	mutex_lock(&c->lock);

	switch (ctrl->id) {
	case CHC5CAMIO_CID_STATUS:
		ctrl->val = chc5camio_read(c, CHC5CAMIO_REG_STATUS) &
			    CHC5CAMIO_STATUS_MASK;
		break;
	case CHC5CAMIO_CID_IN_STATUS:
		ctrl->val = chc5camio_read(c, CHC5CAMIO_REG_IN_STATUS) &
			    CHC5CAMIO_INSTATUS_MASK;
		break;
	case CHC5CAMIO_CID_SYNC_STATUS:
		ctrl->val = chc5camio_read(c, CHC5CAMIO_REG_SYNC_STATUS) &
			    CHC5CAMIO_SYNCSTATUS_MASK;
		break;
	case CHC5CAMIO_CID_TRIG_COUNT:
		*ctrl->p_new.p_s64 = chc5camio_read(c, CHC5CAMIO_REG_TRIG_COUNT);
		break;
	case CHC5CAMIO_CID_SCHED_STATUS:
		mutex_lock(&c->ptp_lock);
		ctrl->val = (chc5camio_read(c, CHC5CAMIO_REG_PTP_STATUS) &
			     CHC5CAMIO_PTPST_SCHED_MASK) |
			    (chc5camio_on_ptp_time(c) ? CHC5CAMIO_SCHED_ST_LOCKED : 0) |
			    (chc5camio_servo_owns(c) ? CHC5CAMIO_SCHED_ST_SERVO : 0);
		mutex_unlock(&c->ptp_lock);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&c->lock);
	return ret;
}

static const struct v4l2_ctrl_ops chc5camio_ctrl_ops = {
	.s_ctrl		= chc5camio_s_ctrl,
	.g_volatile_ctrl = chc5camio_g_volatile_ctrl,
};

struct chc5camio_ctrl_desc {
	u32		id;
	const char	*name;
	enum v4l2_ctrl_type type;
	s64		min;
	s64		max;
	u64		step;
	s64		def;
	u32		flags;
	const char * const *qmenu;
	u32		min_ver;
};

#define CHC5CAMIO_RO_FLAGS \
	(V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY)

static const char * const chc5_menu_in_function[]   = { "Off", "Trigger", "Sync" };
static const char * const chc5_menu_in_activation[] = { "Rising", "Falling", "Any edge", "Level" };
static const char * const chc5_menu_strobe_src[]    = { "PL Calculated", "Native Strobe" };
static const char * const chc5_menu_out_source[]    = { "Off", "User value", "Strobe",
							"Exposure-active", "Status/Ready",
							"Sync-out (XVS)", "Passthrough (Aux In)",
							"PTP pulse" };
static const char * const chc5_menu_sync_role[]     = { "Off", "Master", "Slave", "PTP" };
static const char * const chc5_menu_trig_route[]    = { "XTRIG", "XVS", "I2C" };
static const char * const chc5_menu_trig_source[]   = { "Aux In", "Software", "PTP schedule" };

static const struct chc5camio_ctrl_desc chc5camio_ctrl_table[] = {
	{ CHC5CAMIO_CID_ENABLE,          "Aux IO Enable",        V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SOFT_RESET,      "Aux IO Soft Reset",    V4L2_CTRL_TYPE_BUTTON,  0, 0, 0, 0, 0 },

	{ CHC5CAMIO_CID_IN_FUNCTION,     "Aux In Function",      V4L2_CTRL_TYPE_MENU,    0, 2, 0, 0, 0, chc5_menu_in_function },
	{ CHC5CAMIO_CID_IN_ACTIVATION,   "Aux In Activation",    V4L2_CTRL_TYPE_MENU,    0, 3, 0, 0, 0, chc5_menu_in_activation },
	{ CHC5CAMIO_CID_IN_INVERT,       "Aux In Invert",        V4L2_CTRL_TYPE_BOOLEAN, 0, 1,   1, 0, 0 },
	{ CHC5CAMIO_CID_TRIG_DELAY_NS,   "Trigger Delay ns",     V4L2_CTRL_TYPE_INTEGER, 0, 0x7FFFFFFF, 1, 0, 0 },
	{ CHC5CAMIO_CID_IN_DIVIDER,      "Trigger Divider",      V4L2_CTRL_TYPE_INTEGER, 0, 0xFF,   1, 0, 0 },

	{ CHC5CAMIO_CID_STROBE_ENABLE,   "Strobe Enable",        V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_STROBE_INVERT,   "Invert Strobe Level/Edge", V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_STROBE_SRC,      "Strobe Source",        V4L2_CTRL_TYPE_MENU,    0, 1, 0, 0, 0, chc5_menu_strobe_src },
	{ CHC5CAMIO_CID_STROBE_DELAY,    "Strobe Delay",         V4L2_CTRL_TYPE_INTEGER, 0, 0xFFFF, 1, 0, 0 },
	{ CHC5CAMIO_CID_STROBE_DURATION, "Strobe Duration",      V4L2_CTRL_TYPE_INTEGER, 0, 0xFFFF, 1, 100, 0 },
	{ CHC5CAMIO_CID_STROBE_MINON,    "Strobe Min On",        V4L2_CTRL_TYPE_INTEGER, 0, 0xFFFF, 1, 0, 0 },

	{ CHC5CAMIO_CID_OUT_SOURCE,      "Aux Out Source",       V4L2_CTRL_TYPE_MENU,    0, 7, 0, 0, 0, chc5_menu_out_source },
	{ CHC5CAMIO_CID_OUT_INVERT,      "Aux Out Invert",       V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_OUT_USER_VALUE,  "Aux Out User Value",   V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_NATIVE_LOW,      "Native Active Low",    V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0, NULL, CHC5CAMIO_VER_NATIVE },

	{ CHC5CAMIO_CID_SYNC_ROLE,       "Sync Role",            V4L2_CTRL_TYPE_MENU,    0, 3, 0, 0, 0, chc5_menu_sync_role },
	{ CHC5CAMIO_CID_SYNC_XVS_POL,    "Sync XVS Pol",         V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SYNC_XHS_POL,    "Sync XHS Pol",         V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SYNC_ROLE_BY,    "Sync Role By I2C",     V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SYNC_TRIG_ROUTE, "Sync Trigger Route",   V4L2_CTRL_TYPE_MENU,    0, 2, 0, 0, 0, chc5_menu_trig_route },

	{ CHC5CAMIO_CID_STATUS,          "Aux IO Status",        V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_STATUS_MASK,    1, 0, CHC5CAMIO_RO_FLAGS },
	{ CHC5CAMIO_CID_IN_STATUS,       "Aux In Status",        V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_INSTATUS_MASK,  1, 0, CHC5CAMIO_RO_FLAGS },
	{ CHC5CAMIO_CID_SYNC_STATUS,     "Sync Status",          V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_SYNCSTATUS_MASK, 1, 0, CHC5CAMIO_RO_FLAGS },

	{ CHC5CAMIO_CID_TRIG_SOURCE,     "Trigger Source",       V4L2_CTRL_TYPE_MENU,    0, 2, 0, 0, 0, chc5_menu_trig_source, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_TRIG_SOFTWARE,   "Trigger Software",     V4L2_CTRL_TYPE_BUTTON,  0, 0, 0, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_TRIG_WIDTH_MODE, "Trigger Width Mode",   V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_TRIG_WIDTH_NS,   "Trigger Width ns",     V4L2_CTRL_TYPE_INTEGER, 0, 0x7FFFFFFF, 1, 1000000, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_TRIG_MINPER_NS,  "Trigger Min Period ns", V4L2_CTRL_TYPE_INTEGER, 0, 0x7FFFFFFF, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_IN_FILTER_NS,    "Aux In Filter ns",     V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_NS24_MASK, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_TRIG_COUNT,      "Trigger Count",        V4L2_CTRL_TYPE_INTEGER64, 0, 0xFFFFFFFFLL, 1, 0, CHC5CAMIO_RO_FLAGS, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_LINE_PERIOD_NS,  "Line Period ns",       V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_NS24_MASK, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_FRAME_LINES,     "Frame Lines",          V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_NS24_MASK, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_LINE_XHS,        "Line XHS Enable",      V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_SCHED_PERIOD_PS, "Sched Period ps",      V4L2_CTRL_TYPE_INTEGER64, CHC5CAMIO_SCHED_PS_MIN, CHC5CAMIO_SCHED_PS_MAX, 1, 33333333333LL, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_SCHED_START_NS,  "Sched Start ns",       V4L2_CTRL_TYPE_INTEGER64, 0, S64_MAX, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_SCHED_ENABLE,    "Sched Enable",         V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0, NULL, CHC5CAMIO_VER_TRIG },
	{ CHC5CAMIO_CID_SCHED_STATUS,    "Sched Status",         V4L2_CTRL_TYPE_INTEGER, 0, 0x3FF, 1, 0, CHC5CAMIO_RO_FLAGS, NULL, CHC5CAMIO_VER_TRIG },
};

static int chc5camio_init_controls(struct chc5camio_device *c)
{
	struct v4l2_ctrl_handler *hdl = &c->ctrl_handler;
	unsigned int i;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, ARRAY_SIZE(chc5camio_ctrl_table));
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(chc5camio_ctrl_table); i++) {
		const struct chc5camio_ctrl_desc *d = &chc5camio_ctrl_table[i];
		struct v4l2_ctrl_config cfg = {
			.ops   = &chc5camio_ctrl_ops,
			.id    = d->id,
			.name  = d->name,
			.type  = d->type,
			.min   = d->min,
			.max   = d->max,
			.step  = d->step,
			.def   = d->def,
			.flags = d->flags,
			.qmenu = d->qmenu,
		};

		if (d->min_ver && !chc5camio_has(c, d->min_ver))
			continue;
		if (d->id == CHC5CAMIO_CID_SYNC_ROLE &&
		    !chc5camio_has(c, CHC5CAMIO_VER_PTP))
			cfg.max = 2;
		else if (d->id == CHC5CAMIO_CID_OUT_SOURCE && !chc5camio_ptp_pulses(c))
			cfg.max = 6;
		if (d->id == CHC5CAMIO_CID_SYNC_TRIG_ROUTE && chc5camio_has(c, CHC5CAMIO_VER_TRIG))
			cfg.max = 1;

		v4l2_ctrl_new_custom(hdl, &cfg, NULL);
	}

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	return 0;
}

#define CHC5CAMIO_EVT_WIDTH_TICKS	10
#define CHC5CAMIO_EVT_LAT_NS		20
#define CHC5CAMIO_SERVO_ARM_NS		(50 * NSEC_PER_MSEC)
#define CHC5CAMIO_SERVO_READ_NS		(30 * NSEC_PER_MSEC)
#define CHC5CAMIO_SERVO_STEP_NS		NSEC_PER_MSEC
#define CHC5CAMIO_SERVO_LOCK_NS		500
#define CHC5CAMIO_SERVO_LOCK_N		4
#define CHC5CAMIO_SERVO_UNLOCK_NS	2000
#define CHC5CAMIO_SERVO_QUIET_N		3
#define CHC5CAMIO_SERVO_REARM_N		5
#define CHC5CAMIO_SERVO_KP_MILLI	700
#define CHC5CAMIO_SERVO_KI_MILLI	300
#define CHC5CAMIO_MAX_ADJ_PPB		500000
#define CHC5CAMIO_FIND_DELAY		(2 * HZ)
#define CHC5CAMIO_FOLLOW_MS		125

static bool ptp_servo = true;
module_param(ptp_servo, bool, 0444);
MODULE_PARM_DESC(ptp_servo,
		 "lock the PTP time base to the device tree's Ethernet port (the PHY event pin where wired, else the GEM clock)");

static unsigned int ptp_evt_ms = 1000;
module_param(ptp_evt_ms, uint, 0444);
MODULE_PARM_DESC(ptp_evt_ms, "PTP event pulse period in ms (100-1000)");

static bool chc5camio_ptp_pulses(struct chc5camio_device *c)
{
	struct device_node *np = c->dev->of_node;

	return chc5camio_has(c, CHC5CAMIO_VER_PTP) &&
	       IS_ENABLED(CONFIG_PTP_1588_CLOCK) && ptp_servo &&
	       of_property_read_bool(np, "circuitvalley,ptp-event-pin") &&
	       of_property_present(np, "circuitvalley,ptp-ethernet");
}

static const char * const chc5camio_servo_names[] = {
	[CHC5CAMIO_SERVO_OFF]	= "off",
	[CHC5CAMIO_SERVO_FIND]	= "find",
	[CHC5CAMIO_SERVO_ARM]	= "arm",
	[CHC5CAMIO_SERVO_STEP]	= "step",
	[CHC5CAMIO_SERVO_FREQ]	= "freq",
	[CHC5CAMIO_SERVO_PI]	= "pi",
};

static struct chc5camio_device *ptp_to_chc5camio(struct ptp_clock_info *ptp)
{
	return container_of(ptp, struct chc5camio_device, ptp_info);
}

static u64 chc5camio_ptp_read(struct chc5camio_device *c,
			      struct ptp_system_timestamp *sts)
{
	u32 lo, hi;

	ptp_read_system_prets(sts);
	lo = chc5camio_read(c, CHC5CAMIO_REG_PTP_TIME_LO);
	ptp_read_system_postts(sts);
	hi = chc5camio_read(c, CHC5CAMIO_REG_PTP_TIME_HI);

	return (u64)hi << 32 | lo;
}

static void chc5camio_ptp_load(struct chc5camio_device *c, u64 ns)
{
	chc5camio_write(c, CHC5CAMIO_REG_PTP_TIME_LO, lower_32_bits(ns));
	chc5camio_write(c, CHC5CAMIO_REG_PTP_TIME_HI, upper_32_bits(ns));
	chc5camio_write(c, CHC5CAMIO_REG_PTP_CTRL,
			c->ptp_ctrl | CHC5CAMIO_PTP_LOAD);
}

static void chc5camio_ptp_shift(struct chc5camio_device *c, s64 delta)
{
	unsigned long flags;
	u64 t, k0, k1;

	local_irq_save(flags);
	k0 = ktime_get_raw_fast_ns();
	t = chc5camio_ptp_read(c, NULL);
	k1 = ktime_get_raw_fast_ns();
	chc5camio_ptp_load(c, t + delta + (k1 - k0));
	local_irq_restore(flags);
	chc5camio_sched_resync(c);
}

static void chc5camio_ptp_set_ppb(struct chc5camio_device *c, s64 ppb)
{
	s64 adj;

	ppb = clamp_t(s64, ppb, -CHC5CAMIO_MAX_ADJ_PPB, CHC5CAMIO_MAX_ADJ_PPB);
	adj = div_s64(ppb << 32, (s64)c->clk_hz);
	adj = clamp_t(s64, adj, S32_MIN, S32_MAX);
	chc5camio_write(c, CHC5CAMIO_REG_PTP_RATE_ADJ, (u32)(s32)adj);
}

static bool chc5camio_servo_owns(struct chc5camio_device *c)
{
	return c->servo.state != CHC5CAMIO_SERVO_OFF;
}

static bool chc5camio_on_ptp_time(struct chc5camio_device *c)
{
	if (!c->servo.locked)
		return false;
	if (c->servo.ref == CHC5CAMIO_REF_GEM)
		return c->ndev && macb_zynq_ptp_following(c->ndev);
	return c->servo.ptp_on;
}

#define CHC5CAMIO_SCHED_LEAD_NS		(5 * NSEC_PER_MSEC)

static void chc5camio_sched_program(struct chc5camio_device *c, bool resync)
{
	u64 p_ps = c->sched_period_ps, start = c->sched_start_ns;
	u64 t;
	u32 st, rem_ps;
	u64 p_ns, frac;

	st = chc5camio_read(c, CHC5CAMIO_REG_PTP_STATUS);
	if (!(c->ptp_ctrl & CHC5CAMIO_PTP_SCHED_EN) &&
	    (st & CHC5CAMIO_PTPST_RUNNING)) {
		c->sched_pending = true;
		mod_delayed_work(system_wq, &c->sched_work, 1);
		return;
	}
	c->sched_pending = false;

	t = chc5camio_ptp_read(c, NULL) + CHC5CAMIO_SCHED_LEAD_NS;
	p_ns = div_u64_rem(p_ps, 1000, &rem_ps);
	frac = div_u64((u64)rem_ps << 32, 1000);
	if (st & CHC5CAMIO_PTPST_RUNNING) {
		u32 hi = chc5camio_read(c, CHC5CAMIO_REG_SCHED_PER_HI);

		t += (u64)hi << 32 | chc5camio_read(c, CHC5CAMIO_REG_SCHED_PER_LO);
	}
	if (!start)
		start = mul_u64_u64_div_u64(mul_u64_u64_div_u64(1000, t, p_ps) + 1,
					    p_ps, 1000);
	else if (resync && start < t)
		start += mul_u64_u64_div_u64(mul_u64_u64_div_u64(1000, t - start, p_ps) + 1,
					     p_ps, 1000);

	chc5camio_write(c, CHC5CAMIO_REG_SCHED_NEXT_LO, lower_32_bits(start));
	chc5camio_write(c, CHC5CAMIO_REG_SCHED_NEXT_HI, upper_32_bits(start));
	chc5camio_write(c, CHC5CAMIO_REG_SCHED_PER_LO, lower_32_bits(p_ns));
	chc5camio_write(c, CHC5CAMIO_REG_SCHED_PER_HI, upper_32_bits(p_ns));
	chc5camio_write(c, CHC5CAMIO_REG_SCHED_PER_FRAC, lower_32_bits(frac));
	c->ptp_ctrl |= CHC5CAMIO_PTP_SCHED_EN;
	chc5camio_write(c, CHC5CAMIO_REG_PTP_CTRL, c->ptp_ctrl | CHC5CAMIO_PTP_COMMIT);

	dev_dbg(c->dev, "schedule: start %llu ns, period %llu ps%s\n", start, p_ps,
		resync ? " (re-anchored on its grid)" : "");
}

static void chc5camio_sched_work(struct work_struct *w)
{
	struct chc5camio_device *c = container_of(to_delayed_work(w),
						  struct chc5camio_device,
						  sched_work);

	mutex_lock(&c->ptp_lock);
	if (c->sched_pending && c->sched_on) {
		if (chc5camio_read(c, CHC5CAMIO_REG_PTP_STATUS) &
		    CHC5CAMIO_PTPST_RUNNING)
			mod_delayed_work(system_wq, &c->sched_work, 1);
		else
			chc5camio_sched_program(c, true);
	}
	mutex_unlock(&c->ptp_lock);
}

static void chc5camio_sched_resync(struct chc5camio_device *c)
{
	if (c->sched_on && c->line_period && c->sched_period_ps)
		chc5camio_sched_program(c, true);
}

static int chc5camio_sched_apply(struct chc5camio_device *c)
{
	if (c->sched_on && !c->line_period) {
		dev_warn(c->dev, "schedule needs Line Period ns first\n");
		return -EINVAL;
	}

	mutex_lock(&c->ptp_lock);
	if (!c->sched_on) {
		c->sched_pending = false;
		c->ptp_ctrl &= ~CHC5CAMIO_PTP_SCHED_EN;
		chc5camio_write(c, CHC5CAMIO_REG_PTP_CTRL, c->ptp_ctrl);
	} else {
		chc5camio_sched_program(c, false);
	}
	mutex_unlock(&c->ptp_lock);
	return 0;
}

static int chc5camio_ptp_gettimex64(struct ptp_clock_info *ptp,
				    struct timespec64 *ts,
				    struct ptp_system_timestamp *sts)
{
	struct chc5camio_device *c = ptp_to_chc5camio(ptp);
	u64 ns;

	mutex_lock(&c->ptp_lock);
	ns = chc5camio_ptp_read(c, sts);
	mutex_unlock(&c->ptp_lock);

	*ts = ns_to_timespec64(ns);
	return 0;
}

static int chc5camio_ptp_settime64(struct ptp_clock_info *ptp,
				   const struct timespec64 *ts)
{
	struct chc5camio_device *c = ptp_to_chc5camio(ptp);
	int ret = 0;

	mutex_lock(&c->ptp_lock);
	if (chc5camio_servo_owns(c))
		ret = -EBUSY;
	else {
		chc5camio_ptp_load(c, timespec64_to_ns(ts));
		chc5camio_sched_resync(c);
	}
	mutex_unlock(&c->ptp_lock);

	return ret;
}

static int chc5camio_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct chc5camio_device *c = ptp_to_chc5camio(ptp);
	int ret = 0;

	mutex_lock(&c->ptp_lock);
	if (chc5camio_servo_owns(c))
		ret = -EBUSY;
	else
		chc5camio_ptp_shift(c, delta);
	mutex_unlock(&c->ptp_lock);

	return ret;
}

static int chc5camio_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct chc5camio_device *c = ptp_to_chc5camio(ptp);
	int ret = 0;

	mutex_lock(&c->ptp_lock);
	if (chc5camio_servo_owns(c))
		ret = -EBUSY;
	else
		chc5camio_ptp_set_ppb(c, scaled_ppm_to_ppb(scaled_ppm));
	mutex_unlock(&c->ptp_lock);

	return ret;
}

static bool chc5camio_ptp_find_port(struct chc5camio_device *c)
{
	if (!c->ndev)
		c->ndev = of_find_net_device_by_node(c->eth_np);
	return c->ndev;
}

static struct phy_device *chc5camio_ptp_find_phy(struct chc5camio_device *c)
{
	struct phy_device *phydev = NULL;

	if (!chc5camio_ptp_find_port(c))
		return NULL;
	rtnl_lock();
	if (c->ndev->phydev) {
		phydev = c->ndev->phydev;
		get_device(&phydev->mdio.dev);
	}
	rtnl_unlock();
	return phydev;
}

static void chc5camio_servo_drop_phy(struct chc5camio_device *c)
{
	if (c->phydev) {
		put_device(&c->phydev->mdio.dev);
		c->phydev = NULL;
	}
}

static long chc5camio_servo_until(u64 now_ns, u64 t_ns)
{
	long j = t_ns > now_ns ? (long)nsecs_to_jiffies(t_ns - now_ns) : 0;

	return max(j, 1L);
}

static long chc5camio_servo_next(struct chc5camio_device *c)
{
	struct chc5camio_servo *s = &c->servo;
	u64 now = chc5camio_ptp_read(c, NULL), t = s->t0_ns;

	if (now >= t)
		t += (div_u64(now - t, s->period_ns) + 1) * s->period_ns;

	return chc5camio_servo_until(now, t + CHC5CAMIO_SERVO_READ_NS);
}

static long chc5camio_servo_arm(struct chc5camio_device *c)
{
	struct chc5camio_servo *s = &c->servo;
	u64 now, t0;
	u32 rem;

	now = chc5camio_ptp_read(c, NULL);
	t0 = now + CHC5CAMIO_SERVO_ARM_NS;
	div_u64_rem(t0, s->period_ns, &rem);
	t0 += s->period_ns - rem;

	chc5camio_write(c, CHC5CAMIO_REG_EVT_CFG, CHC5CAMIO_EVT_CFG_EN |
			CHC5CAMIO_EVT_CFG_WIDTH(CHC5CAMIO_EVT_WIDTH_TICKS));
	chc5camio_write(c, CHC5CAMIO_REG_EVT_PERIOD, s->period_ns);
	chc5camio_write(c, CHC5CAMIO_REG_EVT_NEXT_LO, lower_32_bits(t0));
	chc5camio_write(c, CHC5CAMIO_REG_EVT_NEXT_HI, upper_32_bits(t0));
	c->ptp_ctrl |= CHC5CAMIO_PTP_EVT_EN;
	chc5camio_write(c, CHC5CAMIO_REG_PTP_CTRL, c->ptp_ctrl);

	if (chc5camio_read(c, CHC5CAMIO_REG_PTP_STATUS) &
	    CHC5CAMIO_PTPST_EVT_REFUSED) {
		s->refused++;
		return 1;
	}

	s->t0_ns = t0;
	s->arms++;
	s->quiet = 0;
	s->state = s->after_arm;
	return chc5camio_servo_until(now, t0 + CHC5CAMIO_SERVO_READ_NS);
}

static void chc5camio_servo_step(struct chc5camio_device *c, s64 err)
{
	struct chc5camio_servo *s = &c->servo;

	chc5camio_ptp_shift(c, err);
	s->steps++;
	s->good = 0;
	s->locked = false;
	s->freq_have = false;
	s->after_arm = CHC5CAMIO_SERVO_FREQ;
	s->state = s->ref == CHC5CAMIO_REF_GEM ? CHC5CAMIO_SERVO_FREQ
					       : CHC5CAMIO_SERVO_ARM;
}

static void chc5camio_servo_sample(struct chc5camio_device *c, s64 err, u64 t_ns)
{
	struct chc5camio_servo *s = &c->servo;
	s64 rate, out;

	s->samples++;
	s->last_err_ns = err;

	if (s->state == CHC5CAMIO_SERVO_STEP || abs(err) > CHC5CAMIO_SERVO_STEP_NS) {
		chc5camio_servo_step(c, err);
		return;
	}

	if (s->state == CHC5CAMIO_SERVO_FREQ) {
		if (!s->freq_have) {
			s->freq_t_ns = t_ns;
			s->freq_err_ns = err;
			s->freq_have = true;
			return;
		}
		s->freq_ppb += div64_s64((err - s->freq_err_ns) * (s64)NSEC_PER_SEC,
					 (s64)(t_ns - s->freq_t_ns));
		s->freq_ppb = clamp_t(s64, s->freq_ppb, -CHC5CAMIO_MAX_ADJ_PPB,
				      CHC5CAMIO_MAX_ADJ_PPB);
		chc5camio_ptp_set_ppb(c, s->freq_ppb);
		s->freq_have = false;
		s->state = CHC5CAMIO_SERVO_PI;
		return;
	}

	rate = div_s64(err * (s64)NSEC_PER_SEC, s->period_ns);
	s->freq_ppb += div_s64(rate * CHC5CAMIO_SERVO_KI_MILLI, 1000);
	s->freq_ppb = clamp_t(s64, s->freq_ppb, -CHC5CAMIO_MAX_ADJ_PPB,
			      CHC5CAMIO_MAX_ADJ_PPB);
	out = s->freq_ppb + div_s64(rate * CHC5CAMIO_SERVO_KP_MILLI, 1000);
	chc5camio_ptp_set_ppb(c, out);

	if (abs(err) < CHC5CAMIO_SERVO_LOCK_NS) {
		if (s->good < CHC5CAMIO_SERVO_LOCK_N)
			s->good++;
	} else if (!s->locked || abs(err) > CHC5CAMIO_SERVO_UNLOCK_NS) {
		s->good = 0;
	}
	s->locked = s->good >= CHC5CAMIO_SERVO_LOCK_N;
}

static long chc5camio_servo_run(struct chc5camio_device *c)
{
	struct chc5camio_servo *s = &c->servo;
	u32 n1, n2, st;
	u64 phy_ns, t;
	long delay;
	bool ptp_on;
	u8 cnt;
	int ret;

	ptp_on = c->phydev && marvell_phy_ptp_timestamping(c->phydev);

	mutex_lock(&c->ptp_lock);
	s->ptp_on = ptp_on;

	if (s->state == CHC5CAMIO_SERVO_FIND) {
		mutex_unlock(&c->ptp_lock);
		if (!c->phydev)
			c->phydev = chc5camio_ptp_find_phy(c);
		ret = c->phydev ? marvell_phy_ptp_event_enable(c->phydev, true)
				: -ENODEV;
		mutex_lock(&c->ptp_lock);
		if (ret) {
			if (ret != s->last_ret)
				dev_warn(c->dev, "PTP servo: no event input on the %s PHY (%d), retrying\n",
					 c->ndev ? netdev_name(c->ndev) : "ptp-ethernet", ret);
			s->last_ret = ret;
			chc5camio_servo_drop_phy(c);
			mutex_unlock(&c->ptp_lock);
			return CHC5CAMIO_FIND_DELAY;
		}
		dev_info(c->dev, "PTP servo: locking to the %s PHY clock through the PHY event pin\n",
			 netdev_name(c->ndev));
		s->last_ret = 0;
		s->state = CHC5CAMIO_SERVO_ARM;
		s->after_arm = CHC5CAMIO_SERVO_STEP;
	}

	if (s->state == CHC5CAMIO_SERVO_ARM) {
		delay = chc5camio_servo_arm(c);
		mutex_unlock(&c->ptp_lock);
		marvell_phy_ptp_event_read(c->phydev, &phy_ns, &cnt);
		return delay;
	}

	n1 = chc5camio_read(c, CHC5CAMIO_REG_EVT_COUNT);
	mutex_unlock(&c->ptp_lock);
	ret = marvell_phy_ptp_event_read(c->phydev, &phy_ns, &cnt);
	mutex_lock(&c->ptp_lock);
	n2 = chc5camio_read(c, CHC5CAMIO_REG_EVT_COUNT);
	st = chc5camio_read(c, CHC5CAMIO_REG_PTP_STATUS);

	if (ret == -ENODEV) {
		chc5camio_servo_drop_phy(c);
		s->locked = false;
		s->good = 0;
		s->state = CHC5CAMIO_SERVO_FIND;
		mutex_unlock(&c->ptp_lock);
		return CHC5CAMIO_FIND_DELAY;
	}

	if (ret == -EAGAIN || (st & CHC5CAMIO_PTPST_EVT_REFUSED)) {
		s->missed++;
		if (++s->quiet >= CHC5CAMIO_SERVO_QUIET_N) {
			s->locked = false;
			s->good = 0;
		}
		if (s->quiet >= CHC5CAMIO_SERVO_REARM_N ||
		    (st & CHC5CAMIO_PTPST_EVT_REFUSED)) {
			if (s->quiet >= CHC5CAMIO_SERVO_REARM_N)
				dev_warn_ratelimited(c->dev, "PTP servo: no event stamp for %u pulses, re-arming\n",
						     s->quiet);
			s->after_arm = s->state;
			s->state = CHC5CAMIO_SERVO_ARM;
			mutex_unlock(&c->ptp_lock);
			marvell_phy_ptp_event_enable(c->phydev, true);
			return 1;
		}
	} else if (ret) {
		s->errors++;
	} else if (n1 != n2 || !n1) {
		s->rejected++;
	} else {
		s->quiet = 0;
		t = s->t0_ns + (u64)(n1 - 1) * s->period_ns + CHC5CAMIO_EVT_LAT_NS;
		chc5camio_servo_sample(c, (s64)(phy_ns - t), t);
		if (s->state == CHC5CAMIO_SERVO_ARM) {
			mutex_unlock(&c->ptp_lock);
			return 1;
		}
	}

	delay = chc5camio_servo_next(c);
	mutex_unlock(&c->ptp_lock);
	return delay;
}

static int chc5camio_follow_read(struct chc5camio_device *c, s64 *err, u64 *t_ns)
{
	unsigned long flags;
	u64 f0, f1, gem;
	int ret;

	local_irq_save(flags);
	f0 = chc5camio_ptp_read(c, NULL);
	ret = macb_zynq_ptp_now_ns(c->ndev, &gem);
	f1 = chc5camio_ptp_read(c, NULL);
	local_irq_restore(flags);
	if (ret)
		return ret;

	*t_ns = f0 + (f1 - f0) / 2;
	*err = (s64)(gem - *t_ns);
	return 0;
}

static long chc5camio_follow_run(struct chc5camio_device *c)
{
	struct chc5camio_servo *s = &c->servo;
	s64 err;
	u64 t;
	int ret;

	mutex_lock(&c->ptp_lock);
	if (s->state == CHC5CAMIO_SERVO_FIND) {
		if (!chc5camio_ptp_find_port(c)) {
			if (!s->last_ret)
				dev_warn(c->dev, "PTP servo: ptp-ethernet port not registered yet, retrying\n");
			s->last_ret = -ENODEV;
			mutex_unlock(&c->ptp_lock);
			return CHC5CAMIO_FIND_DELAY;
		}
		dev_info(c->dev, "PTP servo: following the %s GEM clock (no PHY event pin)\n",
			 netdev_name(c->ndev));
		s->last_ret = 0;
		s->state = CHC5CAMIO_SERVO_STEP;
	}

	ret = chc5camio_follow_read(c, &err, &t);
	if (ret) {
		s->missed++;
		if (++s->quiet >= CHC5CAMIO_SERVO_QUIET_N) {
			s->locked = false;
			s->good = 0;
		}
		if (ret != s->last_ret)
			dev_info(c->dev, "PTP servo: %s GEM clock not running (%d), holding over\n",
				 netdev_name(c->ndev), ret);
		s->last_ret = ret;
		mutex_unlock(&c->ptp_lock);
		return CHC5CAMIO_FIND_DELAY;
	}
	if (s->last_ret) {
		dev_info(c->dev, "PTP servo: %s GEM clock back\n", netdev_name(c->ndev));
		s->last_ret = 0;
	}
	s->quiet = 0;
	chc5camio_servo_sample(c, err, t);
	mutex_unlock(&c->ptp_lock);

	return msecs_to_jiffies(CHC5CAMIO_FOLLOW_MS);
}

static long chc5camio_ptp_aux_work(struct ptp_clock_info *ptp)
{
	struct chc5camio_device *c = ptp_to_chc5camio(ptp);

	if (c->servo.state == CHC5CAMIO_SERVO_OFF)
		return -1;
	if (c->servo.ref == CHC5CAMIO_REF_GEM)
		return chc5camio_follow_run(c);

	return chc5camio_servo_run(c);
}

static const struct ptp_clock_info chc5camio_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "chc5-camio",
	.max_adj	= CHC5CAMIO_MAX_ADJ_PPB,
	.adjfine	= chc5camio_ptp_adjfine,
	.adjtime	= chc5camio_ptp_adjtime,
	.gettimex64	= chc5camio_ptp_gettimex64,
	.settime64	= chc5camio_ptp_settime64,
	.do_aux_work	= chc5camio_ptp_aux_work,
};

static int chc5camio_ptp_init(struct chc5camio_device *c)
{
	struct chc5camio_servo *s = &c->servo;

	c->clk_hz = c->axi_clk ? clk_get_rate(c->axi_clk) : 0;
	if (!c->clk_hz)
		c->clk_hz = 150000000;

	mutex_lock(&c->ptp_lock);
	chc5camio_write(c, CHC5CAMIO_REG_EVT_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_PTP_RATE_ADJ, 0);
	c->ptp_ctrl = CHC5CAMIO_PTP_TB_EN;
	chc5camio_ptp_load(c, ktime_get_real_ns());
	mutex_unlock(&c->ptp_lock);

	c->ptp_info = chc5camio_ptp_caps;
	c->ptp_clock = ptp_clock_register(&c->ptp_info, c->dev);
	if (IS_ERR(c->ptp_clock)) {
		int ret = PTR_ERR(c->ptp_clock);

		c->ptp_clock = NULL;
		return ret;
	}
	if (!c->ptp_clock)
		return 0;

	if (of_property_read_bool(c->dev->of_node, "circuitvalley,ptp-event-pin")) {
		s->ref = CHC5CAMIO_REF_PIN13;
		s->period_ns = clamp(ptp_evt_ms, 100U, 1000U) * NSEC_PER_MSEC;
	} else {
		s->ref = CHC5CAMIO_REF_GEM;
		s->period_ns = CHC5CAMIO_FOLLOW_MS * NSEC_PER_MSEC;
	}
	c->eth_np = of_parse_phandle(c->dev->of_node, "circuitvalley,ptp-ethernet", 0);
	if (ptp_servo && c->eth_np) {
		s->state = CHC5CAMIO_SERVO_FIND;
		ptp_schedule_worker(c->ptp_clock, 0);
	}

	dev_info(c->dev, "PTP clock %d (time base at %lu Hz), servo %s\n",
		 ptp_clock_index(c->ptp_clock), c->clk_hz,
		 !chc5camio_servo_owns(c) ? "off (no circuitvalley,ptp-ethernet in the device tree)" :
		 s->ref == CHC5CAMIO_REF_GEM ? "on, follows the GEM clock" : "on, PHY event pin");
	return 0;
}

static void chc5camio_ptp_remove(struct chc5camio_device *c)
{
	if (!c->ptp_clock)
		return;

	ptp_clock_unregister(c->ptp_clock);
	c->ptp_clock = NULL;

	mutex_lock(&c->ptp_lock);
	c->servo.state = CHC5CAMIO_SERVO_OFF;
	c->sched_on = false;
	c->sched_pending = false;
	c->ptp_ctrl = 0;
	chc5camio_write(c, CHC5CAMIO_REG_EVT_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_PTP_CTRL, 0);
	mutex_unlock(&c->ptp_lock);

	if (c->phydev)
		marvell_phy_ptp_event_enable(c->phydev, false);
	chc5camio_servo_drop_phy(c);
	if (c->ndev) {
		put_device(&c->ndev->dev);
		c->ndev = NULL;
	}
	of_node_put(c->eth_np);
	c->eth_np = NULL;
}

static ssize_t ptp_servo_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct chc5camio_device *c = dev_get_drvdata(dev);
	struct chc5camio_servo s;
	bool ptp_time;
	int idx;

	if (!c || !c->ptp_clock)
		return sysfs_emit(buf, "no PTP time base\n");

	mutex_lock(&c->ptp_lock);
	s = c->servo;
	ptp_time = chc5camio_on_ptp_time(c);
	idx = ptp_clock_index(c->ptp_clock);
	mutex_unlock(&c->ptp_lock);

	return sysfs_emit(buf,
			  "clock ptp%d ref %s state %s locked %d offset_ns %lld freq_ppb %lld period_ms %u ptp_time %d\n"
			  "samples %u missed %u rejected %u steps %u arms %u refused %u errors %u\n",
			  idx, s.ref == CHC5CAMIO_REF_GEM ? "gem" : "pin13",
			  chc5camio_servo_names[s.state], s.locked,
			  s.last_err_ns, s.freq_ppb, s.period_ns / (u32)NSEC_PER_MSEC,
			  ptp_time, s.samples, s.missed, s.rejected, s.steps, s.arms,
			  s.refused, s.errors);
}
static DEVICE_ATTR_RO(ptp_servo);

static struct attribute *chc5camio_attrs[] = {
	&dev_attr_ptp_servo.attr,
	NULL
};
ATTRIBUTE_GROUPS(chc5camio);

static int chc5camio_log_status(struct v4l2_subdev *sd)
{
	struct chc5camio_device *c = to_chc5camio(sd);
	u32 ver, status, in_status, sync_status;

	mutex_lock(&c->lock);
	ver         = chc5camio_read(c, CHC5CAMIO_REG_ID_VERSION);
	status      = chc5camio_read(c, CHC5CAMIO_REG_STATUS);
	in_status   = chc5camio_read(c, CHC5CAMIO_REG_IN_STATUS);
	sync_status = chc5camio_read(c, CHC5CAMIO_REG_SYNC_STATUS);
	mutex_unlock(&c->lock);

	dev_info(c->dev, "ID_VERSION 0x%08x STATUS 0x%08x IN_STATUS 0x%08x SYNC_STATUS 0x%08x\n",
		 ver, status, in_status, sync_status);
	dev_info(c->dev, "in_cfg 0x%08x strobe_cfg 0x%08x out_cfg 0x%08x sync_cfg 0x%08x\n",
		 c->in_cfg, c->strobe_cfg, c->out_cfg, c->sync_cfg);
	return 0;
}

static const struct v4l2_subdev_core_ops chc5camio_core_ops = {
	.log_status = chc5camio_log_status,
};

static const struct v4l2_subdev_ops chc5camio_subdev_ops = {
	.core = &chc5camio_core_ops,
};

static int chc5camio_probe(struct platform_device *pdev)
{
	struct chc5camio_device *c;
	struct v4l2_subdev *sd;
	struct resource *res;
	u32 ver;
	int ret;

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->dev = &pdev->dev;
	mutex_init(&c->lock);
	mutex_init(&c->ptp_lock);
	INIT_DELAYED_WORK(&c->sched_work, chc5camio_sched_work);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	c->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(c->iomem))
		return PTR_ERR(c->iomem);

	c->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(c->axi_clk))
		return PTR_ERR(c->axi_clk);

	ret = clk_prepare_enable(c->axi_clk);
	if (ret)
		return ret;

	ver = chc5camio_read(c, CHC5CAMIO_REG_ID_VERSION);
	if ((ver >> 16) != CHC5CAMIO_MAGIC)
		dev_warn(&pdev->dev,
			 "unexpected ID_VERSION 0x%08x (magic != 0x%04x) -- wrong or absent IP?\n",
			 ver, CHC5CAMIO_MAGIC);

	c->version = ver;

	chc5camio_reset(c);

	ret = chc5camio_init_controls(c);
	if (ret) {
		dev_err(&pdev->dev, "failed to init controls: %d\n", ret);
		goto err_clk;
	}

	sd = &c->subdev;
	v4l2_subdev_init(sd, &chc5camio_subdev_ops);
	sd->dev = &pdev->dev;
	sd->ctrl_handler = &c->ctrl_handler;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.function = MEDIA_ENT_F_FLASH;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	ret = media_entity_pads_init(&sd->entity, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to init media entity: %d\n", ret);
		goto err_ctrl;
	}

	ret = v4l2_device_register(&pdev->dev, &c->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register v4l2_device: %d\n", ret);
		goto err_entity;
	}

	ret = v4l2_device_register_subdev(&c->v4l2_dev, sd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev: %d\n", ret);
		goto err_v4l2;
	}

	ret = v4l2_device_register_subdev_nodes(&c->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev node: %d\n", ret);
		goto err_subdev;
	}

	platform_set_drvdata(pdev, c);

	if ((ver >> 16) == CHC5CAMIO_MAGIC &&
	    (ver & 0xFFFFu) >= CHC5CAMIO_VER_PTP) {
		ret = chc5camio_ptp_init(c);
		if (ret)
			dev_warn(&pdev->dev, "PTP clock not registered: %d\n", ret);
	}

	dev_info(&pdev->dev, "probed: ID_VERSION=0x%08x (control-only subdev)\n", ver);
	return 0;

err_subdev:
	v4l2_device_unregister_subdev(sd);
err_v4l2:
	v4l2_device_unregister(&c->v4l2_dev);
err_entity:
	media_entity_cleanup(&sd->entity);
err_ctrl:
	v4l2_ctrl_handler_free(&c->ctrl_handler);
err_clk:
	clk_disable_unprepare(c->axi_clk);
	return ret;
}

static int chc5camio_remove(struct platform_device *pdev)
{
	struct chc5camio_device *c = platform_get_drvdata(pdev);

	chc5camio_ptp_remove(c);
	v4l2_device_unregister_subdev(&c->subdev);
	v4l2_device_unregister(&c->v4l2_dev);
	media_entity_cleanup(&c->subdev.entity);
	v4l2_ctrl_handler_free(&c->ctrl_handler);
	cancel_delayed_work_sync(&c->sched_work);
	clk_disable_unprepare(c->axi_clk);
	return 0;
}

static const struct of_device_id chc5camio_of_match[] = {
	{ .compatible = "circuitvalley,chc5-camio-sync-1.0" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5camio_of_match);

static struct platform_driver chc5camio_driver = {
	.probe  = chc5camio_probe,
	.remove = chc5camio_remove,
	.driver = {
		.name           = "chc5-camio",
		.of_match_table = chc5camio_of_match,
		.dev_groups     = chc5camio_groups,
	},
};
module_platform_driver(chc5camio_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 camera I/O and sync");
MODULE_LICENSE("GPL");
