// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 black level corrector
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

#define CHC5BLKC_REG_BLACK_LEVEL		0x00
#define CHC5BLKC_BLACK_LEVEL_MASK		0x3FFF

#define CHC5BLKC_PAD_SINK			0
#define CHC5BLKC_PAD_SOURCE_DEMOSAIC		1
#define CHC5BLKC_PAD_SOURCE_STATS		2
#define CHC5BLKC_PAD_SOURCE_ETH			3
#define CHC5BLKC_PAD_SOURCE_USB			4
#define CHC5BLKC_NUM_PADS			5

#define CHC5BLKC_DEF_WIDTH			1920
#define CHC5BLKC_DEF_HEIGHT			1080
#define CHC5BLKC_MIN_WIDTH			64
#define CHC5BLKC_MAX_WIDTH			8192
#define CHC5BLKC_MIN_HEIGHT			64
#define CHC5BLKC_MAX_HEIGHT			8192
#define CHC5BLKC_DEF_MBUS_CODE		MEDIA_BUS_FMT_SRGGB10_1X10

static const u32 chc5blkc_supported_formats[] = {
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
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_Y12_1X12,
	MEDIA_BUS_FMT_Y14_1X14,
};

struct chc5blkc_device {
	struct device		*dev;
	void __iomem		*iomem;
	struct clk		*axi_clk;
	struct clk		*vid_clk;

	struct v4l2_subdev	subdev;
	struct media_pad	pads[CHC5BLKC_NUM_PADS];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*ctrl_black_level;

	struct v4l2_mbus_framefmt formats[CHC5BLKC_NUM_PADS];

	struct mutex		lock;
};

static inline struct chc5blkc_device *to_chc5blkc(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5blkc_device, subdev);
}

static inline void chc5blkc_write(struct chc5blkc_device *chc5blkc, u32 reg, u32 val)
{
	iowrite32(val, chc5blkc->iomem + reg);
}

static inline u32 chc5blkc_read(struct chc5blkc_device *chc5blkc, u32 reg)
{
	return ioread32(chc5blkc->iomem + reg);
}

static void chc5blkc_set_black_level(struct chc5blkc_device *chc5blkc, u32 level)
{
	chc5blkc_write(chc5blkc, CHC5BLKC_REG_BLACK_LEVEL, level & CHC5BLKC_BLACK_LEVEL_MASK);
	dev_dbg(chc5blkc->dev, "black_level set to %u\n", level & CHC5BLKC_BLACK_LEVEL_MASK);
}

static bool chc5blkc_is_format_supported(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(chc5blkc_supported_formats); i++) {
		if (chc5blkc_supported_formats[i] == code)
			return true;
	}
	return false;
}

static void chc5blkc_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = CHC5BLKC_DEF_WIDTH;
	fmt->height = CHC5BLKC_DEF_HEIGHT;
	fmt->code = CHC5BLKC_DEF_MBUS_CODE;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
}

static void chc5blkc_clamp_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width  = clamp_t(u32, fmt->width,  CHC5BLKC_MIN_WIDTH,  CHC5BLKC_MAX_WIDTH);
	fmt->height = clamp_t(u32, fmt->height, CHC5BLKC_MIN_HEIGHT, CHC5BLKC_MAX_HEIGHT);

	if (!chc5blkc_is_format_supported(fmt->code))
		fmt->code = CHC5BLKC_DEF_MBUS_CODE;

	fmt->field = V4L2_FIELD_NONE;
}

static int chc5blkc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5blkc_device *chc5blkc =
		container_of(ctrl->handler, struct chc5blkc_device, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_BLACK_LEVEL:
		mutex_lock(&chc5blkc->lock);
		chc5blkc_set_black_level(chc5blkc, ctrl->val);
		mutex_unlock(&chc5blkc->lock);
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops chc5blkc_ctrl_ops = {
	.s_ctrl = chc5blkc_s_ctrl,
};

static int chc5blkc_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(chc5blkc_supported_formats))
		return -EINVAL;

	code->code = chc5blkc_supported_formats[code->index];
	return 0;
}

static int chc5blkc_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 0)
		return -EINVAL;

	if (!chc5blkc_is_format_supported(fse->code))
		return -EINVAL;

	fse->min_width  = CHC5BLKC_MIN_WIDTH;
	fse->max_width  = CHC5BLKC_MAX_WIDTH;
	fse->min_height = CHC5BLKC_MIN_HEIGHT;
	fse->max_height = CHC5BLKC_MAX_HEIGHT;

	return 0;
}

static int chc5blkc_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *state,
		       struct v4l2_subdev_format *fmt)
{
	struct chc5blkc_device *chc5blkc = to_chc5blkc(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5BLKC_NUM_PADS)
		return -EINVAL;

	mutex_lock(&chc5blkc->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (!mbus_fmt) {
			mutex_unlock(&chc5blkc->lock);
			return -EINVAL;
		}
		fmt->format = *mbus_fmt;
	} else {
		fmt->format = chc5blkc->formats[fmt->pad];
	}

	mutex_unlock(&chc5blkc->lock);
	return 0;
}

static int chc5blkc_set_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *state,
		       struct v4l2_subdev_format *fmt)
{
	struct chc5blkc_device *chc5blkc = to_chc5blkc(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;
	unsigned int i;

	if (fmt->pad >= CHC5BLKC_NUM_PADS)
		return -EINVAL;

	chc5blkc_clamp_format(&fmt->format);

	mutex_lock(&chc5blkc->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (mbus_fmt)
			*mbus_fmt = fmt->format;

		if (fmt->pad == CHC5BLKC_PAD_SINK) {
			for (i = CHC5BLKC_PAD_SINK + 1; i < CHC5BLKC_NUM_PADS; i++) {
				mbus_fmt = v4l2_subdev_get_try_format(sd, state, i);
				if (mbus_fmt)
					*mbus_fmt = fmt->format;
			}
		}
	} else {
		chc5blkc->formats[fmt->pad] = fmt->format;

		if (fmt->pad == CHC5BLKC_PAD_SINK) {
			for (i = CHC5BLKC_PAD_SINK + 1; i < CHC5BLKC_NUM_PADS; i++)
				chc5blkc->formats[i] = fmt->format;
		}
	}

	mutex_unlock(&chc5blkc->lock);
	return 0;
}

static int chc5blkc_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;
	unsigned int i;

	for (i = 0; i < CHC5BLKC_NUM_PADS; i++) {
		fmt = v4l2_subdev_get_try_format(sd, fh->state, i);
		chc5blkc_init_format(fmt);
	}

	return 0;
}

static int chc5blkc_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct chc5blkc_device *chc5blkc = to_chc5blkc(sd);

	mutex_lock(&chc5blkc->lock);

	if (enable) {
		chc5blkc_set_black_level(chc5blkc, chc5blkc->ctrl_black_level->val);
		dev_dbg(chc5blkc->dev,
			"stream on: sink=%ux%u(0x%04x) black_level=%d\n",
			chc5blkc->formats[CHC5BLKC_PAD_SINK].width,
			chc5blkc->formats[CHC5BLKC_PAD_SINK].height,
			chc5blkc->formats[CHC5BLKC_PAD_SINK].code,
			chc5blkc->ctrl_black_level->val);
	} else {
		dev_dbg(chc5blkc->dev, "stream off\n");
	}

	mutex_unlock(&chc5blkc->lock);
	return 0;
}

static const char * const chc5blkc_pad_names[CHC5BLKC_NUM_PADS] = {
	[CHC5BLKC_PAD_SINK]            = "Sink",
	[CHC5BLKC_PAD_SOURCE_DEMOSAIC] = "Source wb_gain / gamma",
	[CHC5BLKC_PAD_SOURCE_STATS]    = "Source ae_stats (raw tap)",
	[CHC5BLKC_PAD_SOURCE_ETH]      = "Source pixel packer eth",
	[CHC5BLKC_PAD_SOURCE_USB]      = "Source pixel packer usb",
};

static int chc5blkc_log_status(struct v4l2_subdev *sd)
{
	struct chc5blkc_device *chc5blkc = to_chc5blkc(sd);
	unsigned int i;
	u32 reg;

	mutex_lock(&chc5blkc->lock);
	reg = chc5blkc_read(chc5blkc, CHC5BLKC_REG_BLACK_LEVEL);
	mutex_unlock(&chc5blkc->lock);

	dev_info(chc5blkc->dev, "HW black_level: %u\n", reg & CHC5BLKC_BLACK_LEVEL_MASK);
	dev_info(chc5blkc->dev, "Ctrl black_level: %d\n", chc5blkc->ctrl_black_level->val);

	for (i = 0; i < CHC5BLKC_NUM_PADS; i++)
		dev_info(chc5blkc->dev, "%-16s: %ux%u code=0x%04x\n",
			 chc5blkc_pad_names[i],
			 chc5blkc->formats[i].width,
			 chc5blkc->formats[i].height,
			 chc5blkc->formats[i].code);

	return 0;
}

static const struct v4l2_subdev_core_ops chc5blkc_core_ops = {
	.log_status = chc5blkc_log_status,
};

static const struct v4l2_subdev_video_ops chc5blkc_video_ops = {
	.s_stream = chc5blkc_s_stream,
};

static const struct v4l2_subdev_pad_ops chc5blkc_pad_ops = {
	.enum_mbus_code  = chc5blkc_enum_mbus_code,
	.enum_frame_size = chc5blkc_enum_frame_size,
	.get_fmt         = chc5blkc_get_fmt,
	.set_fmt         = chc5blkc_set_fmt,
};

static const struct v4l2_subdev_ops chc5blkc_subdev_ops = {
	.core  = &chc5blkc_core_ops,
	.video = &chc5blkc_video_ops,
	.pad   = &chc5blkc_pad_ops,
};

static const struct v4l2_subdev_internal_ops chc5blkc_internal_ops = {
	.open = chc5blkc_open,
};

static const struct media_entity_operations chc5blkc_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int chc5blkc_probe(struct platform_device *pdev)
{
	struct chc5blkc_device *chc5blkc;
	struct v4l2_subdev *sd;
	struct resource *res;
	u32 bl_init;
	unsigned int i;
	int ret;

	chc5blkc = devm_kzalloc(&pdev->dev, sizeof(*chc5blkc), GFP_KERNEL);
	if (!chc5blkc)
		return -ENOMEM;

	chc5blkc->dev = &pdev->dev;
	mutex_init(&chc5blkc->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chc5blkc->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chc5blkc->iomem))
		return PTR_ERR(chc5blkc->iomem);

	chc5blkc->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(chc5blkc->axi_clk))
		return PTR_ERR(chc5blkc->axi_clk);

	chc5blkc->vid_clk = devm_clk_get_optional(&pdev->dev, "aclk");
	if (IS_ERR(chc5blkc->vid_clk))
		return PTR_ERR(chc5blkc->vid_clk);

	ret = clk_prepare_enable(chc5blkc->axi_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(chc5blkc->vid_clk);
	if (ret) {
		clk_disable_unprepare(chc5blkc->axi_clk);
		return ret;
	}

	if (of_property_read_u32(pdev->dev.of_node, "xlnx,black-level",
				 &bl_init))
		bl_init = 0;

	for (i = 0; i < CHC5BLKC_NUM_PADS; i++)
		chc5blkc_init_format(&chc5blkc->formats[i]);

	chc5blkc_set_black_level(chc5blkc, bl_init);

	v4l2_ctrl_handler_init(&chc5blkc->ctrl_handler, 1);

	chc5blkc->ctrl_black_level = v4l2_ctrl_new_std(
		&chc5blkc->ctrl_handler, &chc5blkc_ctrl_ops,
		V4L2_CID_BLACK_LEVEL,
		0,
		CHC5BLKC_BLACK_LEVEL_MASK,
		1,
		bl_init);

	if (chc5blkc->ctrl_handler.error) {
		ret = chc5blkc->ctrl_handler.error;
		dev_err(&pdev->dev, "failed to init controls: %d\n", ret);
		goto err_clk;
	}

	sd = &chc5blkc->subdev;
	v4l2_subdev_init(sd, &chc5blkc_subdev_ops);
	sd->internal_ops = &chc5blkc_internal_ops;
	sd->dev = &pdev->dev;
	sd->ctrl_handler = &chc5blkc->ctrl_handler;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	chc5blkc->pads[CHC5BLKC_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = CHC5BLKC_PAD_SINK + 1; i < CHC5BLKC_NUM_PADS; i++)
		chc5blkc->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV;
	sd->entity.ops = &chc5blkc_media_ops;

	ret = media_entity_pads_init(&sd->entity, CHC5BLKC_NUM_PADS, chc5blkc->pads);
	if (ret) {
		dev_err(&pdev->dev, "failed to init media pads: %d\n", ret);
		goto err_ctrl;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev: %d\n", ret);
		goto err_entity;
	}

	platform_set_drvdata(pdev, chc5blkc);
	dev_info(&pdev->dev,
		 "probed: black_level=%u, format=%ux%u, pads=1sink+%usource\n",
		 bl_init,
		 chc5blkc->formats[CHC5BLKC_PAD_SINK].width,
		 chc5blkc->formats[CHC5BLKC_PAD_SINK].height,
		 CHC5BLKC_NUM_PADS - 1);

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_ctrl:
	v4l2_ctrl_handler_free(&chc5blkc->ctrl_handler);
err_clk:
	clk_disable_unprepare(chc5blkc->vid_clk);
	clk_disable_unprepare(chc5blkc->axi_clk);
	return ret;
}

static int chc5blkc_remove(struct platform_device *pdev)
{
	struct chc5blkc_device *chc5blkc = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&chc5blkc->subdev);
	media_entity_cleanup(&chc5blkc->subdev.entity);
	v4l2_ctrl_handler_free(&chc5blkc->ctrl_handler);
	clk_disable_unprepare(chc5blkc->vid_clk);
	clk_disable_unprepare(chc5blkc->axi_clk);

	return 0;
}

static const struct of_device_id chc5blkc_of_match[] = {
	{ .compatible = "circuitvalley,chc5-blkc-1.0" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5blkc_of_match);

static struct platform_driver chc5blkc_driver = {
	.probe  = chc5blkc_probe,
	.remove = chc5blkc_remove,
	.driver = {
		.name           = "chc5-blkc",
		.of_match_table = chc5blkc_of_match,
	},
};
module_platform_driver(chc5blkc_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 black level corrector");
MODULE_LICENSE("GPL");
