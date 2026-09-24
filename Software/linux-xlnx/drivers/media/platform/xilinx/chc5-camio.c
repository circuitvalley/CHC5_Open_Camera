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
#include <linux/marvell_phy_ptp.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

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

#define CHC5CAMIO_MAGIC			0xCC5Au
#define CHC5CAMIO_VER_PTP		0x0200u

#define CHC5CAMIO_PTP_TB_EN		BIT(0)
#define CHC5CAMIO_PTP_EVT_EN		BIT(1)
#define CHC5CAMIO_PTP_LOAD		BIT(3)

#define CHC5CAMIO_PTPST_EVT_REFUSED	BIT(6)

#define CHC5CAMIO_EVT_CFG_EN		BIT(0)
#define CHC5CAMIO_EVT_CFG_WIDTH(t)	(((t) & 0xFFu) << 8)

#define CHC5CAMIO_CTRL_ENABLE		BIT(0)
#define CHC5CAMIO_CTRL_SOFT_RESET	BIT(1)

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

#define CHC5CAMIO_SYNC_ROLE_SHIFT	0
#define CHC5CAMIO_SYNC_ROLE_MASK	0x3u
#define CHC5CAMIO_SYNC_XVS_DIR		BIT(2)
#define CHC5CAMIO_SYNC_XHS_DIR		BIT(3)
#define CHC5CAMIO_SYNC_XVS_POL		BIT(4)
#define CHC5CAMIO_SYNC_XHS_POL		BIT(5)
#define CHC5CAMIO_SYNC_ROLE_BY_I2C	BIT(6)

#define CHC5CAMIO_TIME16_MASK		0xFFFFu
#define CHC5CAMIO_DIVIDER_MASK		0xFFu
#define CHC5CAMIO_STATUS_MASK		0xFFu
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

struct chc5camio_servo {
	enum chc5camio_servo_state state;
	enum chc5camio_servo_state after_arm;
	u64	t0_ns;
	u32	period_ns;
	s64	freq_ppb;
	s64	last_err_ns;
	u64	freq_t_ns;
	s64	freq_err_ns;
	bool	freq_have;
	bool	locked;
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

	struct mutex		lock;
	u32			version;

	struct ptp_clock_info	ptp_info;
	struct ptp_clock	*ptp_clock;
	struct mutex		ptp_lock;
	u32			ptp_ctrl;
	unsigned long		clk_hz;
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

	chc5camio_write(c, CHC5CAMIO_REG_CONTROL, 0);
	chc5camio_write(c, CHC5CAMIO_REG_IN_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_IN_DELAY, 0);
	chc5camio_write(c, CHC5CAMIO_REG_IN_DIVIDER, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_DELAY, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_DUR, 0);
	chc5camio_write(c, CHC5CAMIO_REG_STROBE_MINON, 0);
	chc5camio_write(c, CHC5CAMIO_REG_OUT_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_SYNC_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_SYNC_TRIG_ROUTE, 0);
}

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
	case CHC5CAMIO_CID_IN_DELAY:
		chc5camio_write(c, CHC5CAMIO_REG_IN_DELAY, v & CHC5CAMIO_TIME16_MASK);
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

static const struct chc5camio_ctrl_desc chc5camio_ctrl_table[] = {
	{ CHC5CAMIO_CID_ENABLE,          "Aux IO Enable",        V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SOFT_RESET,      "Aux IO Soft Reset",    V4L2_CTRL_TYPE_BUTTON,  0, 0, 0, 0, 0 },

	{ CHC5CAMIO_CID_IN_FUNCTION,     "Aux In Function",      V4L2_CTRL_TYPE_MENU,    0, 2, 0, 0, 0, chc5_menu_in_function },
	{ CHC5CAMIO_CID_IN_ACTIVATION,   "Aux In Activation",    V4L2_CTRL_TYPE_MENU,    0, 3, 0, 0, 0, chc5_menu_in_activation },
	{ CHC5CAMIO_CID_IN_INVERT,       "Aux In Invert",        V4L2_CTRL_TYPE_BOOLEAN, 0, 1,   1, 0, 0 },
	{ CHC5CAMIO_CID_IN_DELAY,        "Trigger Delay",        V4L2_CTRL_TYPE_INTEGER, 0, 0xFFFF, 1, 0, 0 },
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

	{ CHC5CAMIO_CID_SYNC_ROLE,       "Sync Role",            V4L2_CTRL_TYPE_MENU,    0, 3, 0, 0, 0, chc5_menu_sync_role },
	{ CHC5CAMIO_CID_SYNC_XVS_POL,    "Sync XVS Pol",         V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SYNC_XHS_POL,    "Sync XHS Pol",         V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SYNC_ROLE_BY,    "Sync Role By I2C",     V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0, 0 },
	{ CHC5CAMIO_CID_SYNC_TRIG_ROUTE, "Sync Trigger Route",   V4L2_CTRL_TYPE_MENU,    0, 2, 0, 0, 0, chc5_menu_trig_route },

	{ CHC5CAMIO_CID_STATUS,          "Aux IO Status",        V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_STATUS_MASK,    1, 0, CHC5CAMIO_RO_FLAGS },
	{ CHC5CAMIO_CID_IN_STATUS,       "Aux In Status",        V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_INSTATUS_MASK,  1, 0, CHC5CAMIO_RO_FLAGS },
	{ CHC5CAMIO_CID_SYNC_STATUS,     "Sync Status",          V4L2_CTRL_TYPE_INTEGER, 0, CHC5CAMIO_SYNCSTATUS_MASK, 1, 0, CHC5CAMIO_RO_FLAGS },
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

		if ((c->version >> 16) != CHC5CAMIO_MAGIC ||
		    (c->version & 0xFFFFu) < CHC5CAMIO_VER_PTP) {
			if (d->id == CHC5CAMIO_CID_SYNC_ROLE)
				cfg.max = 2;
			else if (d->id == CHC5CAMIO_CID_OUT_SOURCE)
				cfg.max = 6;
		}

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
#define CHC5CAMIO_SERVO_QUIET_N		3
#define CHC5CAMIO_SERVO_REARM_N		5
#define CHC5CAMIO_SERVO_KP_MILLI	700
#define CHC5CAMIO_SERVO_KI_MILLI	300
#define CHC5CAMIO_MAX_ADJ_PPB		500000
#define CHC5CAMIO_FIND_DELAY		(2 * HZ)

static bool ptp_servo = true;
module_param(ptp_servo, bool, 0444);
MODULE_PARM_DESC(ptp_servo,
		 "lock the PTP time base to the Ethernet PHY clock");

static char *ptp_ifname = "eth0";
module_param(ptp_ifname, charp, 0444);
MODULE_PARM_DESC(ptp_ifname, "interface whose PHY the time base locks to");

static unsigned int ptp_evt_ms = 1000;
module_param(ptp_evt_ms, uint, 0444);
MODULE_PARM_DESC(ptp_evt_ms, "PTP event pulse period in ms (100-1000)");

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
	else
		chc5camio_ptp_load(c, timespec64_to_ns(ts));
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

static struct phy_device *chc5camio_ptp_find_phy(void)
{
	struct phy_device *phydev = NULL;
#if IS_ENABLED(CONFIG_NET)
	struct net_device *ndev;

	rtnl_lock();
	ndev = __dev_get_by_name(&init_net, ptp_ifname);
	if (ndev && ndev->phydev) {
		phydev = ndev->phydev;
		get_device(&phydev->mdio.dev);
	}
	rtnl_unlock();
#endif
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
	s->state = CHC5CAMIO_SERVO_ARM;
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
	} else {
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
	u8 cnt;
	int ret;

	mutex_lock(&c->ptp_lock);

	if (s->state == CHC5CAMIO_SERVO_FIND) {
		mutex_unlock(&c->ptp_lock);
		if (!c->phydev)
			c->phydev = chc5camio_ptp_find_phy();
		ret = c->phydev ? marvell_phy_ptp_event_enable(c->phydev, true)
				: -ENODEV;
		mutex_lock(&c->ptp_lock);
		if (ret) {
			if (ret != s->last_ret)
				dev_warn(c->dev, "PTP servo: no event input on the %s PHY (%d), retrying\n",
					 ptp_ifname, ret);
			s->last_ret = ret;
			chc5camio_servo_drop_phy(c);
			mutex_unlock(&c->ptp_lock);
			return CHC5CAMIO_FIND_DELAY;
		}
		dev_info(c->dev, "PTP servo: locking to the %s PHY clock\n",
			 ptp_ifname);
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

static long chc5camio_ptp_aux_work(struct ptp_clock_info *ptp)
{
	struct chc5camio_device *c = ptp_to_chc5camio(ptp);

	if (c->servo.state == CHC5CAMIO_SERVO_OFF)
		return -1;

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

	s->period_ns = clamp(ptp_evt_ms, 100U, 1000U) * NSEC_PER_MSEC;
	if (ptp_servo && of_property_read_bool(c->dev->of_node,
					       "circuitvalley,ptp-event-pin")) {
		s->state = CHC5CAMIO_SERVO_FIND;
		ptp_schedule_worker(c->ptp_clock, 0);
	}

	dev_info(c->dev, "PTP clock %d (time base at %lu Hz), servo %s\n",
		 ptp_clock_index(c->ptp_clock), c->clk_hz,
		 chc5camio_servo_owns(c) ? "on" : "off (no event input in the device tree)");
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
	c->ptp_ctrl = 0;
	chc5camio_write(c, CHC5CAMIO_REG_EVT_CFG, 0);
	chc5camio_write(c, CHC5CAMIO_REG_PTP_CTRL, 0);
	mutex_unlock(&c->ptp_lock);

	if (c->phydev)
		marvell_phy_ptp_event_enable(c->phydev, false);
	chc5camio_servo_drop_phy(c);
}

static ssize_t ptp_servo_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct chc5camio_device *c = dev_get_drvdata(dev);
	struct chc5camio_servo s;
	int idx;

	if (!c || !c->ptp_clock)
		return sysfs_emit(buf, "no PTP time base\n");

	mutex_lock(&c->ptp_lock);
	s = c->servo;
	idx = ptp_clock_index(c->ptp_clock);
	mutex_unlock(&c->ptp_lock);

	return sysfs_emit(buf,
			  "clock ptp%d state %s locked %d offset_ns %lld freq_ppb %lld period_ms %u\n"
			  "samples %u missed %u rejected %u steps %u arms %u refused %u errors %u\n",
			  idx, chc5camio_servo_names[s.state], s.locked,
			  s.last_err_ns, s.freq_ppb, s.period_ns / (u32)NSEC_PER_MSEC,
			  s.samples, s.missed, s.rejected, s.steps, s.arms,
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
