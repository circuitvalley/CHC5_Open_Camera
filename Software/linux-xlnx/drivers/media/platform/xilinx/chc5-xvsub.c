// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 video subsystem
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

#define CHC5_XVSUB_REG_H_IN			0x00
#define CHC5_XVSUB_REG_V_IN			0x04
#define CHC5_XVSUB_REG_H_OUT			0x08
#define CHC5_XVSUB_REG_V_OUT			0x0C
#define CHC5_XVSUB_REG_LEFT			0x10
#define CHC5_XVSUB_REG_TOP			0x14
#define CHC5_XVSUB_REG_FRAME_NUM		0x18
#define CHC5_XVSUB_REG_FRAME_DEN		0x1C

#define CHC5_XVSUB_PAD_SINK			0
#define CHC5_XVSUB_PAD_SOURCE		1
#define CHC5_XVSUB_NUM_PADS			2

#define CHC5_XVSUB_MIN_WIDTH			4
#define CHC5_XVSUB_MAX_WIDTH			8191
#define CHC5_XVSUB_MIN_HEIGHT		4
#define CHC5_XVSUB_MAX_HEIGHT		8191
#define CHC5_XVSUB_DEF_WIDTH			1920
#define CHC5_XVSUB_DEF_HEIGHT		1080
#define CHC5_XVSUB_CROP_ALIGN		4

#define CHC5_XVSUB_FRAME_NUM_MIN		0
#define CHC5_XVSUB_FRAME_NUM_DEF		1
#define CHC5_XVSUB_FRAME_NUM_MAX		255
#define CHC5_XVSUB_FRAME_DEN_MIN		1
#define CHC5_XVSUB_FRAME_DEN_DEF		1
#define CHC5_XVSUB_FRAME_DEN_MAX		255

static const u32 chc5_xvsub_mbus_formats[] = {
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SRGGB14_1X14,
	MEDIA_BUS_FMT_SGRBG14_1X14,
	MEDIA_BUS_FMT_SGBRG14_1X14,
	MEDIA_BUS_FMT_SBGGR14_1X14,
	MEDIA_BUS_FMT_RBG888_1X24,
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_VYYUYY8_1X24,
};

#define CHC5_XVSUB_NUM_FORMATS	ARRAY_SIZE(chc5_xvsub_mbus_formats)

#define CHC5_XVSUB_DEF_MBUS_CODE	MEDIA_BUS_FMT_RBG888_1X24

struct chc5_xvsub_device {
	struct device		*dev;
	void __iomem		*iomem;
	struct clk		*axi_clk;
	u32			ppc;

	struct v4l2_subdev	subdev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_pad	pads[CHC5_XVSUB_NUM_PADS];

	struct v4l2_mbus_framefmt formats[CHC5_XVSUB_NUM_PADS];

	struct v4l2_rect	crop;

	struct v4l2_ctrl	*ctrl_frame_num;
	struct v4l2_ctrl	*ctrl_frame_den;
	u32			frame_num;
	u32			frame_den;

	struct mutex		lock;
};

static inline struct chc5_xvsub_device *to_chc5_xvsub(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5_xvsub_device, subdev);
}

static inline void chc5_xvsub_write(struct chc5_xvsub_device *xv, u32 reg, u32 val)
{
	iowrite32(val, xv->iomem + reg);
}

static inline u32 chc5_xvsub_read(struct chc5_xvsub_device *xv, u32 reg)
{
	return ioread32(xv->iomem + reg);
}

static void chc5_xvsub_commit_hw(struct chc5_xvsub_device *xv)
{
	u32 h_in  = xv->formats[CHC5_XVSUB_PAD_SINK].width;
	u32 v_in  = xv->formats[CHC5_XVSUB_PAD_SINK].height;
	u32 h_out = xv->crop.width;
	u32 v_out = xv->crop.height;
	u32 left  = xv->crop.left;
	u32 top   = xv->crop.top;

	chc5_xvsub_write(xv, CHC5_XVSUB_REG_H_IN,      h_in);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_V_IN,      v_in);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_H_OUT,     h_out);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_V_OUT,     v_out);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_LEFT,      left);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_TOP,       top);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_FRAME_NUM, xv->frame_num);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_FRAME_DEN, xv->frame_den);

	dev_dbg(xv->dev,
		"HW commit: %ux%u -> crop(%u,%u)/%ux%u -> %ux%u, decimate %u/%u\n",
		h_in, v_in, left, top, h_out, v_out,
		xv->formats[CHC5_XVSUB_PAD_SOURCE].width,
		xv->formats[CHC5_XVSUB_PAD_SOURCE].height,
		xv->frame_num, xv->frame_den);
}

static bool chc5_xvsub_is_valid_mbus_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < CHC5_XVSUB_NUM_FORMATS; i++) {
		if (chc5_xvsub_mbus_formats[i] == code)
			return true;
	}
	return false;
}

static void chc5_xvsub_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width	= CHC5_XVSUB_DEF_WIDTH;
	fmt->height	= CHC5_XVSUB_DEF_HEIGHT;
	fmt->code	= CHC5_XVSUB_DEF_MBUS_CODE;
	fmt->field	= V4L2_FIELD_NONE;
	fmt->colorspace	= V4L2_COLORSPACE_SRGB;
	fmt->xfer_func	= V4L2_XFER_FUNC_DEFAULT;
	fmt->ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
}

static void chc5_xvsub_init_crop(struct chc5_xvsub_device *xv)
{
	xv->crop.left   = 0;
	xv->crop.top    = 0;
	xv->crop.width  = xv->formats[CHC5_XVSUB_PAD_SINK].width;
	xv->crop.height = xv->formats[CHC5_XVSUB_PAD_SINK].height;
}

static void chc5_xvsub_clamp_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width  = clamp_t(u32, fmt->width,  CHC5_XVSUB_MIN_WIDTH, CHC5_XVSUB_MAX_WIDTH);
	fmt->height = clamp_t(u32, fmt->height, CHC5_XVSUB_MIN_HEIGHT, CHC5_XVSUB_MAX_HEIGHT);
	fmt->width  = rounddown(fmt->width, CHC5_XVSUB_CROP_ALIGN);
	fmt->height = rounddown(fmt->height, CHC5_XVSUB_CROP_ALIGN);

	if (!chc5_xvsub_is_valid_mbus_code(fmt->code))
		fmt->code = CHC5_XVSUB_DEF_MBUS_CODE;

	fmt->field = V4L2_FIELD_NONE;
}

static void chc5_xvsub_clamp_crop(struct chc5_xvsub_device *xv, struct v4l2_rect *crop)
{
	u32 sink_w = xv->formats[CHC5_XVSUB_PAD_SINK].width;
	u32 sink_h = xv->formats[CHC5_XVSUB_PAD_SINK].height;

	if (crop->left < 0)
		crop->left = 0;
	if (crop->top < 0)
		crop->top = 0;

	crop->left   = rounddown(clamp_t(u32, crop->left, 0, sink_w - CHC5_XVSUB_CROP_ALIGN),
				 CHC5_XVSUB_CROP_ALIGN);
	crop->top    = rounddown(clamp_t(u32, crop->top, 0, sink_h - CHC5_XVSUB_CROP_ALIGN),
				 CHC5_XVSUB_CROP_ALIGN);

	crop->width  = clamp_t(u32, crop->width, CHC5_XVSUB_CROP_ALIGN, sink_w - crop->left);
	crop->width  = rounddown(crop->width, CHC5_XVSUB_CROP_ALIGN);

	crop->height = clamp_t(u32, crop->height, CHC5_XVSUB_CROP_ALIGN, sink_h - crop->top);
	crop->height = rounddown(crop->height, CHC5_XVSUB_CROP_ALIGN);
}

static int chc5_xvsub_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5_xvsub_device *xv =
		container_of(ctrl->handler, struct chc5_xvsub_device, ctrl_handler);
	u32 num, den;

	num = xv->ctrl_frame_num->val;
	den = xv->ctrl_frame_den->val;

	if (num > den)
		num = den;

	mutex_lock(&xv->lock);

	xv->frame_num = num;
	xv->frame_den = den;

	chc5_xvsub_write(xv, CHC5_XVSUB_REG_FRAME_NUM, xv->frame_num);
	chc5_xvsub_write(xv, CHC5_XVSUB_REG_FRAME_DEN, xv->frame_den);

	dev_dbg(xv->dev, "ctrl: frame decimation set to %u/%u (written to HW)\n",
		xv->frame_num, xv->frame_den);

	mutex_unlock(&xv->lock);

	if (xv->ctrl_frame_num->val != num)
		xv->ctrl_frame_num->val = num;

	return 0;
}

static const struct v4l2_ctrl_ops chc5_xvsub_ctrl_ops = {
	.s_ctrl = chc5_xvsub_s_ctrl,
};

static const struct v4l2_ctrl_config chc5_xvsub_ctrl_frame_num = {
	.ops	= &chc5_xvsub_ctrl_ops,
	.id	= CHC5_XVSUB_CID_FRAME_NUM,
	.name	= "Frame Decimation Numerator",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= CHC5_XVSUB_FRAME_NUM_MIN,
	.max	= CHC5_XVSUB_FRAME_NUM_MAX,
	.step	= 1,
	.def	= CHC5_XVSUB_FRAME_NUM_DEF,
};

static const struct v4l2_ctrl_config chc5_xvsub_ctrl_frame_den = {
	.ops	= &chc5_xvsub_ctrl_ops,
	.id	= CHC5_XVSUB_CID_FRAME_DEN,
	.name	= "Frame Decimation Denominator",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= CHC5_XVSUB_FRAME_DEN_MIN,
	.max	= CHC5_XVSUB_FRAME_DEN_MAX,
	.step	= 1,
	.def	= CHC5_XVSUB_FRAME_DEN_DEF,
};

static int chc5_xvsub_init_controls(struct chc5_xvsub_device *xv)
{
	struct v4l2_ctrl_handler *hdl = &xv->ctrl_handler;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 2);
	if (ret)
		return ret;

	xv->ctrl_frame_num = v4l2_ctrl_new_custom(hdl,
						   &chc5_xvsub_ctrl_frame_num,
						   NULL);
	xv->ctrl_frame_den = v4l2_ctrl_new_custom(hdl,
						   &chc5_xvsub_ctrl_frame_den,
						   NULL);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	v4l2_ctrl_cluster(2, &xv->ctrl_frame_num);

	xv->subdev.ctrl_handler = hdl;

	return 0;
}

static int chc5_xvsub_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= CHC5_XVSUB_NUM_FORMATS)
		return -EINVAL;

	code->code = chc5_xvsub_mbus_formats[code->index];
	return 0;
}

static int chc5_xvsub_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 0)
		return -EINVAL;

	if (!chc5_xvsub_is_valid_mbus_code(fse->code))
		return -EINVAL;

	fse->min_width  = CHC5_XVSUB_MIN_WIDTH;
	fse->max_width  = CHC5_XVSUB_MAX_WIDTH;
	fse->min_height = CHC5_XVSUB_MIN_HEIGHT;
	fse->max_height = CHC5_XVSUB_MAX_HEIGHT;

	return 0;
}

static int chc5_xvsub_get_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			 struct v4l2_subdev_format *fmt)
{
	struct chc5_xvsub_device *xv = to_chc5_xvsub(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5_XVSUB_NUM_PADS)
		return -EINVAL;

	mutex_lock(&xv->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (!mbus_fmt) {
			mutex_unlock(&xv->lock);
			return -EINVAL;
		}
		fmt->format = *mbus_fmt;
	} else {
		fmt->format = xv->formats[fmt->pad];
	}

	mutex_unlock(&xv->lock);
	return 0;
}

static int chc5_xvsub_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			 struct v4l2_subdev_format *fmt)
{
	struct chc5_xvsub_device *xv = to_chc5_xvsub(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5_XVSUB_NUM_PADS)
		return -EINVAL;

	chc5_xvsub_clamp_format(&fmt->format);

	mutex_lock(&xv->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (mbus_fmt)
			*mbus_fmt = fmt->format;
	} else {
		xv->formats[fmt->pad] = fmt->format;

		if (fmt->pad == CHC5_XVSUB_PAD_SINK) {
			chc5_xvsub_init_crop(xv);
			xv->formats[CHC5_XVSUB_PAD_SOURCE].width  = fmt->format.width;
			xv->formats[CHC5_XVSUB_PAD_SOURCE].height = fmt->format.height;
		}
	}

	mutex_unlock(&xv->lock);
	return 0;
}

static int chc5_xvsub_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_selection *sel)
{
	struct chc5_xvsub_device *xv = to_chc5_xvsub(sd);

	if (sel->pad != CHC5_XVSUB_PAD_SINK)
		return -EINVAL;

	mutex_lock(&xv->lock);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = xv->crop;
		break;

	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left   = 0;
		sel->r.top    = 0;
		sel->r.width  = xv->formats[CHC5_XVSUB_PAD_SINK].width;
		sel->r.height = xv->formats[CHC5_XVSUB_PAD_SINK].height;
		break;

	default:
		mutex_unlock(&xv->lock);
		return -EINVAL;
	}

	mutex_unlock(&xv->lock);
	return 0;
}

static int chc5_xvsub_set_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_selection *sel)
{
	struct chc5_xvsub_device *xv = to_chc5_xvsub(sd);

	if (sel->pad != CHC5_XVSUB_PAD_SINK)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	mutex_lock(&xv->lock);

	chc5_xvsub_clamp_crop(xv, &sel->r);

	xv->crop = sel->r;

	xv->formats[CHC5_XVSUB_PAD_SOURCE].width  = sel->r.width;
	xv->formats[CHC5_XVSUB_PAD_SOURCE].height = sel->r.height;

	mutex_unlock(&xv->lock);
	return 0;
}

static int chc5_xvsub_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5_XVSUB_PAD_SINK);
	chc5_xvsub_init_format(fmt);

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5_XVSUB_PAD_SOURCE);
	chc5_xvsub_init_format(fmt);

	return 0;
}

static int chc5_xvsub_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct chc5_xvsub_device *xv = to_chc5_xvsub(sd);

	mutex_lock(&xv->lock);

	if (enable) {
		if (xv->frame_num > xv->frame_den)
			xv->frame_num = xv->frame_den;

		chc5_xvsub_commit_hw(xv);

		dev_dbg(xv->dev,
			"stream on: sink=%ux%u source=%ux%u crop=(%u,%u)/%ux%u decimate=%u/%u\n",
			xv->formats[CHC5_XVSUB_PAD_SINK].width,
			xv->formats[CHC5_XVSUB_PAD_SINK].height,
			xv->formats[CHC5_XVSUB_PAD_SOURCE].width,
			xv->formats[CHC5_XVSUB_PAD_SOURCE].height,
			xv->crop.left, xv->crop.top,
			xv->crop.width, xv->crop.height,
			xv->frame_num, xv->frame_den);
	} else {
		dev_dbg(xv->dev, "stream off\n");
	}

	mutex_unlock(&xv->lock);
	return 0;
}

static int chc5_xvsub_log_status(struct v4l2_subdev *sd)
{
	struct chc5_xvsub_device *xv = to_chc5_xvsub(sd);
	u32 h_in, v_in, h_out, v_out, left, top, fnum, fden;

	mutex_lock(&xv->lock);
	h_in  = chc5_xvsub_read(xv, CHC5_XVSUB_REG_H_IN)  & 0x1FFF;
	v_in  = chc5_xvsub_read(xv, CHC5_XVSUB_REG_V_IN)  & 0x1FFF;
	h_out = chc5_xvsub_read(xv, CHC5_XVSUB_REG_H_OUT) & 0x1FFF;
	v_out = chc5_xvsub_read(xv, CHC5_XVSUB_REG_V_OUT) & 0x1FFF;
	left  = chc5_xvsub_read(xv, CHC5_XVSUB_REG_LEFT)  & 0x1FFF;
	top   = chc5_xvsub_read(xv, CHC5_XVSUB_REG_TOP)   & 0x1FFF;
	fnum  = chc5_xvsub_read(xv, CHC5_XVSUB_REG_FRAME_NUM) & 0xFF;
	fden  = chc5_xvsub_read(xv, CHC5_XVSUB_REG_FRAME_DEN) & 0xFF;
	mutex_unlock(&xv->lock);

	dev_info(xv->dev, "HW regs: H_IN=%u V_IN=%u H_OUT=%u V_OUT=%u\n",
		 h_in, v_in, h_out, v_out);
	dev_info(xv->dev, "HW regs: LEFT=%u TOP=%u FRAME_NUM=%u FRAME_DEN=%u\n",
		 left, top, fnum, fden);
	dev_info(xv->dev, "Sink:   %ux%u code=0x%04x\n",
		 xv->formats[CHC5_XVSUB_PAD_SINK].width,
		 xv->formats[CHC5_XVSUB_PAD_SINK].height,
		 xv->formats[CHC5_XVSUB_PAD_SINK].code);
	dev_info(xv->dev, "Source: %ux%u code=0x%04x\n",
		 xv->formats[CHC5_XVSUB_PAD_SOURCE].width,
		 xv->formats[CHC5_XVSUB_PAD_SOURCE].height,
		 xv->formats[CHC5_XVSUB_PAD_SOURCE].code);
	dev_info(xv->dev, "Crop:   (%u,%u)/%ux%u  decimate %u/%u\n",
		 xv->crop.left, xv->crop.top,
		 xv->crop.width, xv->crop.height,
		 xv->frame_num, xv->frame_den);

	return 0;
}

static const struct v4l2_subdev_core_ops chc5_xvsub_core_ops = {
	.log_status	= chc5_xvsub_log_status,
};

static const struct v4l2_subdev_video_ops chc5_xvsub_video_ops = {
	.s_stream	= chc5_xvsub_s_stream,
};

static const struct v4l2_subdev_pad_ops chc5_xvsub_pad_ops = {
	.enum_mbus_code		= chc5_xvsub_enum_mbus_code,
	.enum_frame_size	= chc5_xvsub_enum_frame_size,
	.get_fmt		= chc5_xvsub_get_fmt,
	.set_fmt		= chc5_xvsub_set_fmt,
	.get_selection		= chc5_xvsub_get_selection,
	.set_selection		= chc5_xvsub_set_selection,
};

static const struct v4l2_subdev_ops chc5_xvsub_subdev_ops = {
	.core	= &chc5_xvsub_core_ops,
	.video	= &chc5_xvsub_video_ops,
	.pad	= &chc5_xvsub_pad_ops,
};

static const struct v4l2_subdev_internal_ops chc5_xvsub_internal_ops = {
	.open	= chc5_xvsub_open,
};

static const struct media_entity_operations chc5_xvsub_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int chc5_xvsub_probe(struct platform_device *pdev)
{
	struct chc5_xvsub_device *xv;
	struct v4l2_subdev *sd;
	struct resource *res;
	u32 ppc;
	int ret;

	xv = devm_kzalloc(&pdev->dev, sizeof(*xv), GFP_KERNEL);
	if (!xv)
		return -ENOMEM;

	xv->dev = &pdev->dev;
	mutex_init(&xv->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xv->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xv->iomem))
		return PTR_ERR(xv->iomem);

	xv->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(xv->axi_clk))
		return PTR_ERR(xv->axi_clk);

	ret = clk_prepare_enable(xv->axi_clk);
	if (ret)
		return ret;

	if (of_property_read_u32(pdev->dev.of_node, "xlnx,ppc", &ppc))
		ppc = 2;
	if (ppc != 2 && ppc != 4) {
		dev_warn(&pdev->dev, "xlnx,ppc=%u invalid, defaulting to 2\n", ppc);
		ppc = 2;
	}
	xv->ppc = ppc;

	chc5_xvsub_init_format(&xv->formats[CHC5_XVSUB_PAD_SINK]);
	chc5_xvsub_init_format(&xv->formats[CHC5_XVSUB_PAD_SOURCE]);

	chc5_xvsub_init_crop(xv);

	xv->frame_num = CHC5_XVSUB_FRAME_NUM_DEF;
	xv->frame_den = CHC5_XVSUB_FRAME_DEN_DEF;

	sd = &xv->subdev;
	v4l2_subdev_init(sd, &chc5_xvsub_subdev_ops);
	sd->internal_ops = &chc5_xvsub_internal_ops;
	sd->dev = &pdev->dev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	ret = chc5_xvsub_init_controls(xv);
	if (ret) {
		dev_err(&pdev->dev, "failed to init controls: %d\n", ret);
		goto err_clk;
	}

	xv->pads[CHC5_XVSUB_PAD_SINK].flags   = MEDIA_PAD_FL_SINK;
	xv->pads[CHC5_XVSUB_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	sd->entity.ops = &chc5_xvsub_media_ops;

	ret = media_entity_pads_init(&sd->entity, CHC5_XVSUB_NUM_PADS, xv->pads);
	if (ret) {
		dev_err(&pdev->dev, "failed to init media pads: %d\n", ret);
		goto err_ctrl;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev: %d\n", ret);
		goto err_entity;
	}

	platform_set_drvdata(pdev, xv);

	dev_info(&pdev->dev,
		 "probed: ppc=%u, sink=%ux%u, source=%ux%u, crop=(%u,%u)/%ux%u\n",
		 xv->ppc,
		 xv->formats[CHC5_XVSUB_PAD_SINK].width,
		 xv->formats[CHC5_XVSUB_PAD_SINK].height,
		 xv->formats[CHC5_XVSUB_PAD_SOURCE].width,
		 xv->formats[CHC5_XVSUB_PAD_SOURCE].height,
		 xv->crop.left, xv->crop.top,
		 xv->crop.width, xv->crop.height);

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_ctrl:
	v4l2_ctrl_handler_free(&xv->ctrl_handler);
err_clk:
	clk_disable_unprepare(xv->axi_clk);
	return ret;
}

static int chc5_xvsub_remove(struct platform_device *pdev)
{
	struct chc5_xvsub_device *xv = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&xv->subdev);
	media_entity_cleanup(&xv->subdev.entity);
	v4l2_ctrl_handler_free(&xv->ctrl_handler);
	mutex_destroy(&xv->lock);
	clk_disable_unprepare(xv->axi_clk);

	return 0;
}

static const struct of_device_id chc5_xvsub_of_match[] = {
	{ .compatible = "circuitvalley,chc5-xvsub-1.0" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5_xvsub_of_match);

static struct platform_driver chc5_xvsub_driver = {
	.probe	= chc5_xvsub_probe,
	.remove	= chc5_xvsub_remove,
	.driver	= {
		.name		= "chc5-xvsub",
		.of_match_table	= chc5_xvsub_of_match,
	},
};
module_platform_driver(chc5_xvsub_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 video subsystem");
MODULE_LICENSE("GPL");
