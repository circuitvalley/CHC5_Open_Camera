// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 crop
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <uapi/linux/chc5-v4l2-controls.h>

#define CHC5CROP_REG_ID			0x00
#define CHC5CROP_REG_CTRL		0x04
#define CHC5CROP_REG_X			0x08
#define CHC5CROP_REG_Y			0x0C
#define CHC5CROP_REG_WIDTH		0x10
#define CHC5CROP_REG_HEIGHT		0x14
#define CHC5CROP_REG_ACTIVE_XY		0x18
#define CHC5CROP_REG_ACTIVE_WH		0x1C
#define CHC5CROP_REG_FRAME_GEOM		0x20
#define CHC5CROP_REG_STATUS		0x24
#define CHC5CROP_REG_FRAME_CNT		0x28

#define CHC5CROP_ID_MAGIC		0xCC5C0100u

#define CHC5CROP_CTRL_ENABLE		BIT(0)
#define CHC5CROP_CTRL_BYPASS		BIT(1)
#define CHC5CROP_CTRL_DROP_UNTIL_SOF	BIT(2)
#define CHC5CROP_CTRL_RESET_VAL		((u32)(CHC5CROP_CTRL_ENABLE | \
					       CHC5CROP_CTRL_DROP_UNTIL_SOF))

#define CHC5CROP_STS_SHORT_LINE		BIT(0)
#define CHC5CROP_STS_WIDTH_TRUNCATED	BIT(1)
#define CHC5CROP_STS_WINDOW_OOB		BIT(2)
#define CHC5CROP_STS_SOF_RESYNC		BIT(3)
#define CHC5CROP_STS_MASK		0xFu

#define CHC5CROP_LO(v)			((v) & 0xFFFFu)
#define CHC5CROP_HI(v)			(((v) >> 16) & 0xFFFFu)

#define CHC5CROP_PAD_SINK		0
#define CHC5CROP_PAD_SOURCE		1
#define CHC5CROP_NUM_PADS		2

#define CHC5CROP_DEF_PPC		4
#define CHC5CROP_DEF_MAX_COLS		8192
#define CHC5CROP_DEF_MAX_ROWS		8192

#define CHC5CROP_DEF_WIDTH		1920
#define CHC5CROP_DEF_HEIGHT		1080
#define CHC5CROP_MIN_WIDTH		64
#define CHC5CROP_MIN_HEIGHT		64

static const u32 chc5crop_bayer_codes[][4] = {
	{ MEDIA_BUS_FMT_SRGGB8_1X8,   MEDIA_BUS_FMT_SGRBG8_1X8,
	  MEDIA_BUS_FMT_SGBRG8_1X8,   MEDIA_BUS_FMT_SBGGR8_1X8   },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, MEDIA_BUS_FMT_SGRBG10_1X10,
	  MEDIA_BUS_FMT_SGBRG10_1X10, MEDIA_BUS_FMT_SBGGR10_1X10 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, MEDIA_BUS_FMT_SGRBG12_1X12,
	  MEDIA_BUS_FMT_SGBRG12_1X12, MEDIA_BUS_FMT_SBGGR12_1X12 },
	{ MEDIA_BUS_FMT_SRGGB14_1X14, MEDIA_BUS_FMT_SGRBG14_1X14,
	  MEDIA_BUS_FMT_SGBRG14_1X14, MEDIA_BUS_FMT_SBGGR14_1X14 },
	{ MEDIA_BUS_FMT_SRGGB16_1X16, MEDIA_BUS_FMT_SGRBG16_1X16,
	  MEDIA_BUS_FMT_SGBRG16_1X16, MEDIA_BUS_FMT_SBGGR16_1X16 },
};

#define CHC5CROP_DEF_MBUS_CODE		MEDIA_BUS_FMT_SRGGB12_1X12

struct chc5crop_device {
	struct device		*dev;
	void __iomem		*iomem;
	struct clk		*axi_clk;
	struct clk		*vid_clk;

	u32			ppc;
	u32			max_cols;
	u32			max_rows;

	struct v4l2_subdev	subdev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_pad	pads[CHC5CROP_NUM_PADS];

	struct v4l2_mbus_framefmt formats[CHC5CROP_NUM_PADS];

	struct v4l2_rect	crop;

	struct v4l2_ctrl	*ctrl_enable;
	struct v4l2_ctrl	*ctrl_bypass;
	struct v4l2_ctrl	*ctrl_drop_sof;
	struct v4l2_ctrl	*ctrl_status;
	struct v4l2_ctrl	*ctrl_active;

	struct mutex		lock;
};

static inline struct chc5crop_device *to_chc5crop(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5crop_device, subdev);
}

static inline void chc5crop_write(struct chc5crop_device *c, u32 reg, u32 val)
{
	iowrite32(val, c->iomem + reg);
}

static inline u32 chc5crop_read(struct chc5crop_device *c, u32 reg)
{
	return ioread32(c->iomem + reg);
}

static int chc5crop_bayer_row_of(u32 code, unsigned int *phase)
{
	unsigned int i, p;

	for (i = 0; i < ARRAY_SIZE(chc5crop_bayer_codes); i++) {
		for (p = 0; p < 4; p++) {
			if (chc5crop_bayer_codes[i][p] == code) {
				*phase = p;
				return (int)i;
			}
		}
	}
	return -1;
}

static u32 chc5crop_shift_code(u32 code, u32 x, u32 y)
{
	unsigned int phase;
	int row;

	row = chc5crop_bayer_row_of(code, &phase);
	if (row < 0)
		return code;

	phase ^= ((y & 1) << 1) | (x & 1);

	return chc5crop_bayer_codes[row][phase & 0x3];
}

static void chc5crop_probe_params(struct chc5crop_device *c)
{
	u32 dt_ppc = 0;
	u32 k;

	chc5crop_write(c, CHC5CROP_REG_X, 0xFFFFFFFFu);
	c->max_cols = chc5crop_read(c, CHC5CROP_REG_X) + 1;

	chc5crop_write(c, CHC5CROP_REG_HEIGHT, 0xFFFFFFFFu);
	c->max_rows = chc5crop_read(c, CHC5CROP_REG_HEIGHT);

	c->ppc = 1;
	for (k = 1; k <= 8; k <<= 1) {
		chc5crop_write(c, CHC5CROP_REG_WIDTH, k);
		if (chc5crop_read(c, CHC5CROP_REG_WIDTH) == k) {
			c->ppc = k;
			break;
		}
	}

	chc5crop_write(c, CHC5CROP_REG_X, 0);
	chc5crop_write(c, CHC5CROP_REG_Y, 0);
	chc5crop_write(c, CHC5CROP_REG_WIDTH, 0);
	chc5crop_write(c, CHC5CROP_REG_HEIGHT, 0);
	chc5crop_write(c, CHC5CROP_REG_STATUS, CHC5CROP_STS_MASK);

	if (!c->max_cols || c->max_cols > CHC5CROP_DEF_MAX_COLS * 4 ||
	    !c->max_rows || c->max_rows > CHC5CROP_DEF_MAX_ROWS * 4) {
		dev_warn(c->dev,
			 "implausible bounds probed (%ux%u), using defaults\n",
			 c->max_cols, c->max_rows);
		c->max_cols = CHC5CROP_DEF_MAX_COLS;
		c->max_rows = CHC5CROP_DEF_MAX_ROWS;
	}

	if (!of_property_read_u32(c->dev->of_node, "xlnx,ppc", &dt_ppc) &&
	    dt_ppc != c->ppc)
		dev_warn(c->dev,
			 "xlnx,ppc=%u disagrees with the hardware (%u) -- trusting the hardware\n",
			 dt_ppc, c->ppc);
}

static void chc5crop_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width	  = CHC5CROP_DEF_WIDTH;
	fmt->height	  = CHC5CROP_DEF_HEIGHT;
	fmt->code	  = CHC5CROP_DEF_MBUS_CODE;
	fmt->field	  = V4L2_FIELD_NONE;
	fmt->colorspace	  = V4L2_COLORSPACE_SRGB;
	fmt->xfer_func	  = V4L2_XFER_FUNC_DEFAULT;
	fmt->ycbcr_enc	  = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
}

static void chc5crop_init_crop(struct chc5crop_device *c)
{
	c->crop.left   = 0;
	c->crop.top    = 0;
	c->crop.width  = c->formats[CHC5CROP_PAD_SINK].width;
	c->crop.height = c->formats[CHC5CROP_PAD_SINK].height;
}

static void chc5crop_clamp_format(struct chc5crop_device *c,
				  struct v4l2_mbus_framefmt *fmt)
{
	fmt->width  = clamp_t(u32, fmt->width,
			      CHC5CROP_MIN_WIDTH, c->max_cols);
	fmt->height = clamp_t(u32, fmt->height,
			      CHC5CROP_MIN_HEIGHT, c->max_rows);
	fmt->field  = V4L2_FIELD_NONE;
}

static void chc5crop_clamp_crop(struct chc5crop_device *c,
				struct v4l2_rect *r, u32 flags)
{
	u32 sink_w = c->formats[CHC5CROP_PAD_SINK].width;
	u32 sink_h = c->formats[CHC5CROP_PAD_SINK].height;
	u32 w;

	if (r->left < 0)
		r->left = 0;
	if (r->top < 0)
		r->top = 0;

	r->left = clamp_t(u32, r->left, 0, sink_w - 1);
	r->top  = clamp_t(u32, r->top,  0, sink_h - 1);
	r->left = min_t(u32, r->left, c->max_cols - 1);
	r->top  = min_t(u32, r->top,  c->max_rows - 1);

	if (sink_w >= c->ppc && r->left > sink_w - c->ppc)
		r->left = sink_w - c->ppc;

	w = clamp_t(u32, r->width, c->ppc, sink_w - r->left);
	if (flags & V4L2_SEL_FLAG_GE)
		w = round_up(w, c->ppc);
	else
		w = round_down(w, c->ppc);

	if (r->left + w > sink_w)
		w = round_down(sink_w - r->left, c->ppc);

	r->width  = max_t(u32, w, c->ppc);
	r->height = clamp_t(u32, r->height, 1, sink_h - r->top);
}

static void chc5crop_update_source(struct chc5crop_device *c)
{
	struct v4l2_mbus_framefmt *sink = &c->formats[CHC5CROP_PAD_SINK];
	struct v4l2_mbus_framefmt *src  = &c->formats[CHC5CROP_PAD_SOURCE];

	*src = *sink;
	src->width  = c->crop.width;
	src->height = c->crop.height;
	src->code   = chc5crop_shift_code(sink->code,
					  c->crop.left, c->crop.top);
}

static void chc5crop_write_window(struct chc5crop_device *c)
{
	u32 x, y, w, h;

	chc5crop_write(c, CHC5CROP_REG_STATUS, CHC5CROP_STS_WIDTH_TRUNCATED);

	chc5crop_write(c, CHC5CROP_REG_X,      c->crop.left);
	chc5crop_write(c, CHC5CROP_REG_Y,      c->crop.top);
	chc5crop_write(c, CHC5CROP_REG_WIDTH,  c->crop.width);
	chc5crop_write(c, CHC5CROP_REG_HEIGHT, c->crop.height);

	x = chc5crop_read(c, CHC5CROP_REG_X);
	y = chc5crop_read(c, CHC5CROP_REG_Y);
	w = chc5crop_read(c, CHC5CROP_REG_WIDTH);
	h = chc5crop_read(c, CHC5CROP_REG_HEIGHT);

	if (x != c->crop.left || y != c->crop.top ||
	    w != c->crop.width || h != c->crop.height) {
		dev_warn(c->dev,
			 "window adjusted by HW: asked (%d,%d)/%ux%u, took (%u,%u)/%ux%u\n",
			 c->crop.left, c->crop.top,
			 c->crop.width, c->crop.height, x, y, w, h);

		c->crop.left   = x;
		c->crop.top    = y;
		c->crop.width  = w;
		c->crop.height = h;
		chc5crop_update_source(c);
	}

	if (chc5crop_read(c, CHC5CROP_REG_STATUS) &
	    CHC5CROP_STS_WIDTH_TRUNCATED)
		dev_warn(c->dev,
			 "CROP_WIDTH was masked to a multiple of PPC=%u\n",
			 c->ppc);

	dev_dbg(c->dev, "window (%u,%u)/%ux%u, source code 0x%04x\n",
		x, y, w, h, c->formats[CHC5CROP_PAD_SOURCE].code);
}

static void chc5crop_check_status(struct chc5crop_device *c)
{
	u32 st = chc5crop_read(c, CHC5CROP_REG_STATUS);

	if (!st)
		return;

	chc5crop_write(c, CHC5CROP_REG_STATUS, st);

	if (st & CHC5CROP_STS_SHORT_LINE)
		dev_err(c->dev,
			"SHORT_LINE: CROP_X + CROP_WIDTH exceeds the line length\n");
	if (st & CHC5CROP_STS_WINDOW_OOB)
		dev_err(c->dev,
			"WINDOW_OOB: CROP_Y + CROP_HEIGHT exceeds the frame height\n");
	if (st & CHC5CROP_STS_SOF_RESYNC)
		dev_warn(c->dev,
			 "SOF_RESYNC: start of frame arrived mid-line, link glitch upstream\n");
}

static int chc5crop_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5crop_device *c =
		container_of(ctrl->handler, struct chc5crop_device, ctrl_handler);
	u32 xy, wh;

	switch (ctrl->id) {
	case CHC5CROP_CID_STATUS:
		mutex_lock(&c->lock);
		ctrl->val = chc5crop_read(c, CHC5CROP_REG_STATUS) &
			    CHC5CROP_STS_MASK;
		if (ctrl->val)
			chc5crop_write(c, CHC5CROP_REG_STATUS, ctrl->val);
		mutex_unlock(&c->lock);
		return 0;

	case CHC5CROP_CID_ACTIVE_WINDOW:
		mutex_lock(&c->lock);
		xy = chc5crop_read(c, CHC5CROP_REG_ACTIVE_XY);
		wh = chc5crop_read(c, CHC5CROP_REG_ACTIVE_WH);
		mutex_unlock(&c->lock);
		ctrl->p_new.p_u32[0] = CHC5CROP_LO(xy);
		ctrl->p_new.p_u32[1] = CHC5CROP_HI(xy);
		ctrl->p_new.p_u32[2] = CHC5CROP_LO(wh);
		ctrl->p_new.p_u32[3] = CHC5CROP_HI(wh);
		return 0;
	}

	return -EINVAL;
}

static int chc5crop_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5crop_device *c =
		container_of(ctrl->handler, struct chc5crop_device, ctrl_handler);
	u32 reg;

	switch (ctrl->id) {
	case CHC5CROP_CID_ENABLE:
	case CHC5CROP_CID_BYPASS:
	case CHC5CROP_CID_DROP_UNTIL_SOF:
		mutex_lock(&c->lock);
		reg = chc5crop_read(c, CHC5CROP_REG_CTRL);

		if (ctrl->id == CHC5CROP_CID_ENABLE)
			reg = ctrl->val ? (reg | CHC5CROP_CTRL_ENABLE)
					: (reg & ~CHC5CROP_CTRL_ENABLE);
		else if (ctrl->id == CHC5CROP_CID_BYPASS)
			reg = ctrl->val ? (reg | CHC5CROP_CTRL_BYPASS)
					: (reg & ~CHC5CROP_CTRL_BYPASS);
		else
			reg = ctrl->val ? (reg | CHC5CROP_CTRL_DROP_UNTIL_SOF)
					: (reg & ~CHC5CROP_CTRL_DROP_UNTIL_SOF);

		chc5crop_write(c, CHC5CROP_REG_CTRL, reg);
		mutex_unlock(&c->lock);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops chc5crop_ctrl_ops = {
	.s_ctrl		 = chc5crop_s_ctrl,
	.g_volatile_ctrl = chc5crop_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config chc5crop_ctrl_enable = {
	.ops	= &chc5crop_ctrl_ops,
	.id	= CHC5CROP_CID_ENABLE,
	.name	= "Crop Enable",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 1,
};

static const struct v4l2_ctrl_config chc5crop_ctrl_bypass = {
	.ops	= &chc5crop_ctrl_ops,
	.id	= CHC5CROP_CID_BYPASS,
	.name	= "Crop Bypass",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 0,
};

static const struct v4l2_ctrl_config chc5crop_ctrl_drop_sof = {
	.ops	= &chc5crop_ctrl_ops,
	.id	= CHC5CROP_CID_DROP_UNTIL_SOF,
	.name	= "Crop Drop Until SOF",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 1,
};

static const struct v4l2_ctrl_config chc5crop_ctrl_status = {
	.ops	= &chc5crop_ctrl_ops,
	.id	= CHC5CROP_CID_STATUS,
	.name	= "Crop Status Flags",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= CHC5CROP_STS_MASK,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
};

static const struct v4l2_ctrl_config chc5crop_ctrl_active = {
	.ops	= &chc5crop_ctrl_ops,
	.id	= CHC5CROP_CID_ACTIVE_WINDOW,
	.name	= "Crop Active Window (x,y,w,h)",
	.type	= V4L2_CTRL_TYPE_U32,
	.min	= 0,
	.max	= 0xFFFF,
	.step	= 1,
	.def	= 0,
	.dims	= { 4 },
	.flags	= V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
};

static int chc5crop_init_controls(struct chc5crop_device *c)
{
	struct v4l2_ctrl_handler *hdl = &c->ctrl_handler;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 5);
	if (ret)
		return ret;

	c->ctrl_enable   = v4l2_ctrl_new_custom(hdl, &chc5crop_ctrl_enable, NULL);
	c->ctrl_bypass   = v4l2_ctrl_new_custom(hdl, &chc5crop_ctrl_bypass, NULL);
	c->ctrl_drop_sof = v4l2_ctrl_new_custom(hdl, &chc5crop_ctrl_drop_sof, NULL);
	c->ctrl_status   = v4l2_ctrl_new_custom(hdl, &chc5crop_ctrl_status, NULL);
	c->ctrl_active   = v4l2_ctrl_new_custom(hdl, &chc5crop_ctrl_active, NULL);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	return 0;
}

static int chc5crop_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int row, phase;

	if (code->pad >= CHC5CROP_NUM_PADS)
		return -EINVAL;

	if (code->index >= ARRAY_SIZE(chc5crop_bayer_codes) * 4)
		return -EINVAL;

	row   = code->index / 4;
	phase = code->index % 4;
	code->code = chc5crop_bayer_codes[row][phase];

	return 0;
}

static int chc5crop_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct chc5crop_device *c = to_chc5crop(sd);

	if (fse->index || fse->pad >= CHC5CROP_NUM_PADS)
		return -EINVAL;

	fse->min_width  = CHC5CROP_MIN_WIDTH;
	fse->max_width  = c->max_cols;
	fse->min_height = CHC5CROP_MIN_HEIGHT;
	fse->max_height = c->max_rows;

	return 0;
}

static int chc5crop_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	struct chc5crop_device *c = to_chc5crop(sd);
	struct v4l2_mbus_framefmt *mf;

	if (fmt->pad >= CHC5CROP_NUM_PADS)
		return -EINVAL;

	mutex_lock(&c->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (!mf) {
			mutex_unlock(&c->lock);
			return -EINVAL;
		}
		fmt->format = *mf;
	} else {
		fmt->format = c->formats[fmt->pad];
	}

	mutex_unlock(&c->lock);
	return 0;
}

static int chc5crop_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	struct chc5crop_device *c = to_chc5crop(sd);
	struct v4l2_mbus_framefmt *sink, *src;

	if (fmt->pad >= CHC5CROP_NUM_PADS)
		return -EINVAL;

	mutex_lock(&c->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		sink = v4l2_subdev_get_try_format(sd, state, CHC5CROP_PAD_SINK);
		src  = v4l2_subdev_get_try_format(sd, state, CHC5CROP_PAD_SOURCE);
		if (!sink || !src) {
			mutex_unlock(&c->lock);
			return -EINVAL;
		}

		if (fmt->pad == CHC5CROP_PAD_SINK) {
			chc5crop_clamp_format(c, &fmt->format);
			*sink = fmt->format;
		}

		*src = *sink;
		fmt->format = (fmt->pad == CHC5CROP_PAD_SINK) ? *sink : *src;

		mutex_unlock(&c->lock);
		return 0;
	}

	if (fmt->pad == CHC5CROP_PAD_SINK) {
		chc5crop_clamp_format(c, &fmt->format);
		c->formats[CHC5CROP_PAD_SINK] = fmt->format;

		chc5crop_clamp_crop(c, &c->crop, 0);
		chc5crop_write_window(c);
	}

	chc5crop_update_source(c);
	fmt->format = c->formats[fmt->pad];

	mutex_unlock(&c->lock);
	return 0;
}

static int chc5crop_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct chc5crop_device *c = to_chc5crop(sd);
	u32 geom;

	if (sel->pad != CHC5CROP_PAD_SINK)
		return -EINVAL;

	mutex_lock(&c->lock);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = c->crop;
		break;

	case V4L2_SEL_TGT_CROP_BOUNDS:
		geom = chc5crop_read(c, CHC5CROP_REG_FRAME_GEOM);
		sel->r.left = 0;
		sel->r.top  = 0;
		if (CHC5CROP_LO(geom) && CHC5CROP_HI(geom)) {
			sel->r.width  = CHC5CROP_LO(geom);
			sel->r.height = CHC5CROP_HI(geom);
		} else {
			sel->r.width  = c->formats[CHC5CROP_PAD_SINK].width;
			sel->r.height = c->formats[CHC5CROP_PAD_SINK].height;
		}
		break;

	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left   = 0;
		sel->r.top    = 0;
		sel->r.width  = c->formats[CHC5CROP_PAD_SINK].width;
		sel->r.height = c->formats[CHC5CROP_PAD_SINK].height;
		break;

	default:
		mutex_unlock(&c->lock);
		return -EINVAL;
	}

	mutex_unlock(&c->lock);
	return 0;
}

static int chc5crop_set_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct chc5crop_device *c = to_chc5crop(sd);
	struct v4l2_rect r = sel->r;

	if (sel->pad != CHC5CROP_PAD_SINK)
		return -EINVAL;
	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	mutex_lock(&c->lock);

	chc5crop_clamp_crop(c, &r, sel->flags);

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		sel->r = r;
		mutex_unlock(&c->lock);
		return 0;
	}

	c->crop = r;
	chc5crop_update_source(c);
	chc5crop_write_window(c);

	sel->r = c->crop;

	mutex_unlock(&c->lock);
	return 0;
}

static int chc5crop_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5CROP_PAD_SINK);
	chc5crop_init_format(fmt);

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5CROP_PAD_SOURCE);
	chc5crop_init_format(fmt);

	return 0;
}

static int chc5crop_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct chc5crop_device *c = to_chc5crop(sd);

	mutex_lock(&c->lock);

	if (enable) {
		chc5crop_write_window(c);
		chc5crop_check_status(c);

		dev_dbg(c->dev,
			"stream on: sink %ux%u/0x%04x -> window (%u,%u)/%ux%u -> source %ux%u/0x%04x, ppc=%u\n",
			c->formats[CHC5CROP_PAD_SINK].width,
			c->formats[CHC5CROP_PAD_SINK].height,
			c->formats[CHC5CROP_PAD_SINK].code,
			c->crop.left, c->crop.top,
			c->crop.width, c->crop.height,
			c->formats[CHC5CROP_PAD_SOURCE].width,
			c->formats[CHC5CROP_PAD_SOURCE].height,
			c->formats[CHC5CROP_PAD_SOURCE].code,
			c->ppc);
	} else {
		chc5crop_check_status(c);
		dev_dbg(c->dev, "stream off\n");
	}

	mutex_unlock(&c->lock);
	return 0;
}

static int chc5crop_log_status(struct v4l2_subdev *sd)
{
	struct chc5crop_device *c = to_chc5crop(sd);
	u32 ctrl, xy, wh, geom, st, cnt;

	mutex_lock(&c->lock);
	ctrl = chc5crop_read(c, CHC5CROP_REG_CTRL);
	xy   = chc5crop_read(c, CHC5CROP_REG_ACTIVE_XY);
	wh   = chc5crop_read(c, CHC5CROP_REG_ACTIVE_WH);
	geom = chc5crop_read(c, CHC5CROP_REG_FRAME_GEOM);
	st   = chc5crop_read(c, CHC5CROP_REG_STATUS);
	cnt  = chc5crop_read(c, CHC5CROP_REG_FRAME_CNT);
	mutex_unlock(&c->lock);

	v4l2_info(sd, "build: PPC=%u MAX_COLS=%u MAX_ROWS=%u\n",
		  c->ppc, c->max_cols, c->max_rows);
	v4l2_info(sd, "CTRL: enable=%u bypass=%u drop_until_sof=%u\n",
		  !!(ctrl & CHC5CROP_CTRL_ENABLE),
		  !!(ctrl & CHC5CROP_CTRL_BYPASS),
		  !!(ctrl & CHC5CROP_CTRL_DROP_UNTIL_SOF));
	v4l2_info(sd, "requested window: (%d,%d)/%ux%u\n",
		  c->crop.left, c->crop.top, c->crop.width, c->crop.height);
	v4l2_info(sd, "active window:    (%u,%u)/%ux%u\n",
		  CHC5CROP_LO(xy), CHC5CROP_HI(xy),
		  CHC5CROP_LO(wh), CHC5CROP_HI(wh));
	v4l2_info(sd, "measured input:   %ux%u (one frame late)\n",
		  CHC5CROP_LO(geom), CHC5CROP_HI(geom));
	v4l2_info(sd, "frames seen: %u\n", cnt);
	v4l2_info(sd, "status: 0x%x%s%s%s%s\n", st & CHC5CROP_STS_MASK,
		  (st & CHC5CROP_STS_SHORT_LINE)      ? " SHORT_LINE" : "",
		  (st & CHC5CROP_STS_WIDTH_TRUNCATED) ? " WIDTH_TRUNCATED" : "",
		  (st & CHC5CROP_STS_WINDOW_OOB)      ? " WINDOW_OOB" : "",
		  (st & CHC5CROP_STS_SOF_RESYNC)      ? " SOF_RESYNC" : "");
	v4l2_info(sd, "sink %ux%u/0x%04x -> source %ux%u/0x%04x\n",
		  c->formats[CHC5CROP_PAD_SINK].width,
		  c->formats[CHC5CROP_PAD_SINK].height,
		  c->formats[CHC5CROP_PAD_SINK].code,
		  c->formats[CHC5CROP_PAD_SOURCE].width,
		  c->formats[CHC5CROP_PAD_SOURCE].height,
		  c->formats[CHC5CROP_PAD_SOURCE].code);

	return 0;
}

static const struct v4l2_subdev_core_ops chc5crop_core_ops = {
	.log_status = chc5crop_log_status,
};

static const struct v4l2_subdev_video_ops chc5crop_video_ops = {
	.s_stream = chc5crop_s_stream,
};

static const struct v4l2_subdev_pad_ops chc5crop_pad_ops = {
	.enum_mbus_code	 = chc5crop_enum_mbus_code,
	.enum_frame_size = chc5crop_enum_frame_size,
	.get_fmt	 = chc5crop_get_fmt,
	.set_fmt	 = chc5crop_set_fmt,
	.get_selection	 = chc5crop_get_selection,
	.set_selection	 = chc5crop_set_selection,
};

static const struct v4l2_subdev_ops chc5crop_subdev_ops = {
	.core  = &chc5crop_core_ops,
	.video = &chc5crop_video_ops,
	.pad   = &chc5crop_pad_ops,
};

static const struct v4l2_subdev_internal_ops chc5crop_internal_ops = {
	.open = chc5crop_open,
};

static const struct media_entity_operations chc5crop_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int chc5crop_probe(struct platform_device *pdev)
{
	struct chc5crop_device *c;
	struct v4l2_subdev *sd;
	struct resource *res;
	unsigned int i;
	u32 id, ctrl;
	int ret;

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->dev = &pdev->dev;
	c->ppc      = CHC5CROP_DEF_PPC;
	c->max_cols = CHC5CROP_DEF_MAX_COLS;
	c->max_rows = CHC5CROP_DEF_MAX_ROWS;
	mutex_init(&c->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	c->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(c->iomem))
		return PTR_ERR(c->iomem);

	c->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(c->axi_clk))
		return PTR_ERR(c->axi_clk);

	c->vid_clk = devm_clk_get_optional(&pdev->dev, "aclk");
	if (IS_ERR(c->vid_clk))
		return PTR_ERR(c->vid_clk);

	ret = clk_prepare_enable(c->axi_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(c->vid_clk);
	if (ret) {
		clk_disable_unprepare(c->axi_clk);
		return ret;
	}

	id = chc5crop_read(c, CHC5CROP_REG_ID);
	if (id != CHC5CROP_ID_MAGIC) {
		dev_err(&pdev->dev,
			"ID reads 0x%08x, expected 0x%08x -- wrong base address or the block is not in this bitstream; refusing to probe\n",
			id, CHC5CROP_ID_MAGIC);
		ret = -ENODEV;
		goto err_clk;
	}

	ctrl = chc5crop_read(c, CHC5CROP_REG_CTRL);
	if (ctrl != CHC5CROP_CTRL_RESET_VAL)
		dev_warn(&pdev->dev,
			 "CTRL reads 0x%x, expected the reset value 0x%x -- the block was already configured\n",
			 ctrl, CHC5CROP_CTRL_RESET_VAL);

	chc5crop_probe_params(c);

	for (i = 0; i < CHC5CROP_NUM_PADS; i++)
		chc5crop_init_format(&c->formats[i]);
	chc5crop_init_crop(c);
	chc5crop_update_source(c);

	chc5crop_write(c, CHC5CROP_REG_CTRL, CHC5CROP_CTRL_RESET_VAL);

	ret = chc5crop_init_controls(c);
	if (ret) {
		dev_err(&pdev->dev, "failed to init controls: %d\n", ret);
		goto err_clk;
	}

	ret = v4l2_ctrl_handler_setup(&c->ctrl_handler);
	if (ret) {
		dev_err(&pdev->dev, "failed to apply control defaults: %d\n", ret);
		goto err_ctrl;
	}

	sd = &c->subdev;
	v4l2_subdev_init(sd, &chc5crop_subdev_ops);
	sd->internal_ops = &chc5crop_internal_ops;
	sd->dev = &pdev->dev;
	sd->ctrl_handler = &c->ctrl_handler;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	c->pads[CHC5CROP_PAD_SINK].flags   = MEDIA_PAD_FL_SINK;
	c->pads[CHC5CROP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	sd->entity.ops = &chc5crop_media_ops;

	ret = media_entity_pads_init(&sd->entity, CHC5CROP_NUM_PADS, c->pads);
	if (ret) {
		dev_err(&pdev->dev, "failed to init media pads: %d\n", ret);
		goto err_ctrl;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev: %d\n", ret);
		goto err_entity;
	}

	platform_set_drvdata(pdev, c);
	dev_info(&pdev->dev,
		 "probed: ID=0x%08x PPC=%u MAX=%ux%u, format=%ux%u, pads=1sink+1source\n",
		 id, c->ppc, c->max_cols, c->max_rows,
		 c->formats[CHC5CROP_PAD_SINK].width,
		 c->formats[CHC5CROP_PAD_SINK].height);

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_ctrl:
	v4l2_ctrl_handler_free(&c->ctrl_handler);
err_clk:
	clk_disable_unprepare(c->vid_clk);
	clk_disable_unprepare(c->axi_clk);
	return ret;
}

static int chc5crop_remove(struct platform_device *pdev)
{
	struct chc5crop_device *c = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &c->subdev;

	chc5crop_write(c, CHC5CROP_REG_X, 0);
	chc5crop_write(c, CHC5CROP_REG_Y, 0);
	chc5crop_write(c, CHC5CROP_REG_WIDTH, 0);
	chc5crop_write(c, CHC5CROP_REG_HEIGHT, 0);
	chc5crop_write(c, CHC5CROP_REG_CTRL, CHC5CROP_CTRL_RESET_VAL);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&c->ctrl_handler);
	clk_disable_unprepare(c->vid_clk);
	clk_disable_unprepare(c->axi_clk);
	mutex_destroy(&c->lock);

	return 0;
}

static const struct of_device_id chc5crop_of_match[] = {
	{ .compatible = "circuitvalley,chc5-crop-1.0" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5crop_of_match);

static struct platform_driver chc5crop_driver = {
	.driver = {
		.name		= "chc5-crop",
		.of_match_table	= chc5crop_of_match,
	},
	.probe	= chc5crop_probe,
	.remove	= chc5crop_remove,
};
module_platform_driver(chc5crop_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 crop");
MODULE_LICENSE("GPL");
