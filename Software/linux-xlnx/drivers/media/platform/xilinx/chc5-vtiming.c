// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 video timing
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
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define CHC5TC_REG_LINES_PER_FRAME	0x00
#define CHC5TC_LINES_MASK			0x1FFF

#define CHC5TC_PAD_SINK		0
#define CHC5TC_PAD_SOURCE		1
#define CHC5TC_NUM_PADS		2

#define CHC5TC_DEF_WIDTH		1920
#define CHC5TC_DEF_HEIGHT		1080
#define CHC5TC_MIN_WIDTH		64
#define CHC5TC_MAX_WIDTH		8192
#define CHC5TC_MIN_HEIGHT		64
#define CHC5TC_MAX_HEIGHT		8191
#define CHC5TC_DEF_MBUS_CODE	MEDIA_BUS_FMT_UYVY8_1X16

static const u32 chc5tc_supported_formats[] = {
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_VUY8_1X24,
	MEDIA_BUS_FMT_RBG888_1X24,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SRGGB14_1X14,
	MEDIA_BUS_FMT_SGRBG14_1X14,
	MEDIA_BUS_FMT_SGBRG14_1X14,
	MEDIA_BUS_FMT_SBGGR14_1X14,
};

struct chc5tc_device {
	struct device		*dev;
	void __iomem		*iomem;
	struct clk		*axi_clk;
	struct clk		*vid_clk;

	struct v4l2_subdev	subdev;
	struct media_pad	pads[CHC5TC_NUM_PADS];

	struct v4l2_mbus_framefmt formats[CHC5TC_NUM_PADS];

	struct mutex		lock;
};

static inline struct chc5tc_device *to_chc5tc(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5tc_device, subdev);
}

static inline void chc5tc_write(struct chc5tc_device *chc5tc, u32 reg, u32 val)
{
	iowrite32(val, chc5tc->iomem + reg);
}

static inline u32 chc5tc_read(struct chc5tc_device *chc5tc, u32 reg)
{
	return ioread32(chc5tc->iomem + reg);
}

static void chc5tc_set_lines_per_frame(struct chc5tc_device *chc5tc, u32 lines)
{
	chc5tc_write(chc5tc, CHC5TC_REG_LINES_PER_FRAME, lines & CHC5TC_LINES_MASK);
	dev_dbg(chc5tc->dev, "lines_per_frame set to %u\n", lines & CHC5TC_LINES_MASK);
}

static bool chc5tc_is_format_supported(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(chc5tc_supported_formats); i++) {
		if (chc5tc_supported_formats[i] == code)
			return true;
	}
	return false;
}

static void chc5tc_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = CHC5TC_DEF_WIDTH;
	fmt->height = CHC5TC_DEF_HEIGHT;
	fmt->code = CHC5TC_DEF_MBUS_CODE;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
}

static void chc5tc_clamp_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width  = clamp_t(u32, fmt->width,  CHC5TC_MIN_WIDTH,  CHC5TC_MAX_WIDTH);
	fmt->height = clamp_t(u32, fmt->height, CHC5TC_MIN_HEIGHT, CHC5TC_MAX_HEIGHT);

	if (!chc5tc_is_format_supported(fmt->code))
		fmt->code = CHC5TC_DEF_MBUS_CODE;

	fmt->field = V4L2_FIELD_NONE;
}

static int chc5tc_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(chc5tc_supported_formats))
		return -EINVAL;

	code->code = chc5tc_supported_formats[code->index];
	return 0;
}

static int chc5tc_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 0)
		return -EINVAL;

	if (!chc5tc_is_format_supported(fse->code))
		return -EINVAL;

	fse->min_width  = CHC5TC_MIN_WIDTH;
	fse->max_width  = CHC5TC_MAX_WIDTH;
	fse->min_height = CHC5TC_MIN_HEIGHT;
	fse->max_height = CHC5TC_MAX_HEIGHT;

	return 0;
}

static int chc5tc_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct chc5tc_device *chc5tc = to_chc5tc(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5TC_NUM_PADS)
		return -EINVAL;

	mutex_lock(&chc5tc->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (!mbus_fmt) {
			mutex_unlock(&chc5tc->lock);
			return -EINVAL;
		}
		fmt->format = *mbus_fmt;
	} else {
		fmt->format = chc5tc->formats[fmt->pad];
	}

	mutex_unlock(&chc5tc->lock);
	return 0;
}

static int chc5tc_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct chc5tc_device *chc5tc = to_chc5tc(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5TC_NUM_PADS)
		return -EINVAL;

	chc5tc_clamp_format(&fmt->format);

	mutex_lock(&chc5tc->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (mbus_fmt)
			*mbus_fmt = fmt->format;
	} else {
		chc5tc->formats[fmt->pad] = fmt->format;

		if (fmt->pad == CHC5TC_PAD_SOURCE)
			chc5tc_set_lines_per_frame(chc5tc, fmt->format.height);
	}

	mutex_unlock(&chc5tc->lock);
	return 0;
}

static int chc5tc_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5TC_PAD_SINK);
	chc5tc_init_format(fmt);

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5TC_PAD_SOURCE);
	chc5tc_init_format(fmt);

	return 0;
}

static int chc5tc_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct chc5tc_device *chc5tc = to_chc5tc(sd);

	mutex_lock(&chc5tc->lock);

	if (enable) {
		chc5tc_set_lines_per_frame(chc5tc,
					 chc5tc->formats[CHC5TC_PAD_SOURCE].height);
		dev_dbg(chc5tc->dev,
			"stream on: sink=%ux%u(0x%04x) source=%ux%u(0x%04x)\n",
			chc5tc->formats[CHC5TC_PAD_SINK].width,
			chc5tc->formats[CHC5TC_PAD_SINK].height,
			chc5tc->formats[CHC5TC_PAD_SINK].code,
			chc5tc->formats[CHC5TC_PAD_SOURCE].width,
			chc5tc->formats[CHC5TC_PAD_SOURCE].height,
			chc5tc->formats[CHC5TC_PAD_SOURCE].code);
	} else {
		dev_dbg(chc5tc->dev, "stream off\n");
	}

	mutex_unlock(&chc5tc->lock);
	return 0;
}

static int chc5tc_log_status(struct v4l2_subdev *sd)
{
	struct chc5tc_device *chc5tc = to_chc5tc(sd);
	u32 reg;

	mutex_lock(&chc5tc->lock);
	reg = chc5tc_read(chc5tc, CHC5TC_REG_LINES_PER_FRAME);
	mutex_unlock(&chc5tc->lock);

	dev_info(chc5tc->dev, "HW lines_per_frame: %u\n", reg & CHC5TC_LINES_MASK);
	dev_info(chc5tc->dev, "Sink:   %ux%u code=0x%04x\n",
		 chc5tc->formats[CHC5TC_PAD_SINK].width,
		 chc5tc->formats[CHC5TC_PAD_SINK].height,
		 chc5tc->formats[CHC5TC_PAD_SINK].code);
	dev_info(chc5tc->dev, "Source: %ux%u code=0x%04x\n",
		 chc5tc->formats[CHC5TC_PAD_SOURCE].width,
		 chc5tc->formats[CHC5TC_PAD_SOURCE].height,
		 chc5tc->formats[CHC5TC_PAD_SOURCE].code);

	return 0;
}

static const struct v4l2_subdev_core_ops chc5tc_core_ops = {
	.log_status	= chc5tc_log_status,
};

static const struct v4l2_subdev_video_ops chc5tc_video_ops = {
	.s_stream	= chc5tc_s_stream,
};

static const struct v4l2_subdev_pad_ops chc5tc_pad_ops = {
	.enum_mbus_code		= chc5tc_enum_mbus_code,
	.enum_frame_size	= chc5tc_enum_frame_size,
	.get_fmt		= chc5tc_get_fmt,
	.set_fmt		= chc5tc_set_fmt,
};

static const struct v4l2_subdev_ops chc5tc_subdev_ops = {
	.core	= &chc5tc_core_ops,
	.video	= &chc5tc_video_ops,
	.pad	= &chc5tc_pad_ops,
};

static const struct v4l2_subdev_internal_ops chc5tc_internal_ops = {
	.open	= chc5tc_open,
};

static const struct media_entity_operations chc5tc_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int chc5tc_probe(struct platform_device *pdev)
{
	struct chc5tc_device *chc5tc;
	struct v4l2_subdev *sd;
	struct resource *res;
	u32 lines_init;
	int ret;

	chc5tc = devm_kzalloc(&pdev->dev, sizeof(*chc5tc), GFP_KERNEL);
	if (!chc5tc)
		return -ENOMEM;

	chc5tc->dev = &pdev->dev;
	mutex_init(&chc5tc->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chc5tc->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chc5tc->iomem))
		return PTR_ERR(chc5tc->iomem);

	chc5tc->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(chc5tc->axi_clk))
		return PTR_ERR(chc5tc->axi_clk);

	chc5tc->vid_clk = devm_clk_get_optional(&pdev->dev, "clk_in");
	if (IS_ERR(chc5tc->vid_clk))
		return PTR_ERR(chc5tc->vid_clk);

	ret = clk_prepare_enable(chc5tc->axi_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(chc5tc->vid_clk);
	if (ret) {
		clk_disable_unprepare(chc5tc->axi_clk);
		return ret;
	}

	if (of_property_read_u32(pdev->dev.of_node, "xlnx,lines-per-frame",
				 &lines_init))
		lines_init = CHC5TC_DEF_HEIGHT;

	chc5tc_init_format(&chc5tc->formats[CHC5TC_PAD_SINK]);
	chc5tc_init_format(&chc5tc->formats[CHC5TC_PAD_SOURCE]);
	chc5tc->formats[CHC5TC_PAD_SOURCE].height = lines_init;

	chc5tc_set_lines_per_frame(chc5tc, lines_init);

	sd = &chc5tc->subdev;
	v4l2_subdev_init(sd, &chc5tc_subdev_ops);
	sd->internal_ops = &chc5tc_internal_ops;
	sd->dev = &pdev->dev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	chc5tc->pads[CHC5TC_PAD_SINK].flags   = MEDIA_PAD_FL_SINK;
	chc5tc->pads[CHC5TC_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV;
	sd->entity.ops = &chc5tc_media_ops;

	ret = media_entity_pads_init(&sd->entity, CHC5TC_NUM_PADS, chc5tc->pads);
	if (ret) {
		dev_err(&pdev->dev, "failed to init media pads: %d\n", ret);
		goto err_clk;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev: %d\n", ret);
		goto err_entity;
	}

	platform_set_drvdata(pdev, chc5tc);
	dev_info(&pdev->dev,
		 "probed: lines_per_frame=%u, sink=%ux%u, source=%ux%u\n",
		 lines_init,
		 chc5tc->formats[CHC5TC_PAD_SINK].width,
		 chc5tc->formats[CHC5TC_PAD_SINK].height,
		 chc5tc->formats[CHC5TC_PAD_SOURCE].width,
		 chc5tc->formats[CHC5TC_PAD_SOURCE].height);

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_clk:
	clk_disable_unprepare(chc5tc->vid_clk);
	clk_disable_unprepare(chc5tc->axi_clk);
	return ret;
}

static int chc5tc_remove(struct platform_device *pdev)
{
	struct chc5tc_device *chc5tc = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&chc5tc->subdev);
	media_entity_cleanup(&chc5tc->subdev.entity);
	clk_disable_unprepare(chc5tc->vid_clk);
	clk_disable_unprepare(chc5tc->axi_clk);

	return 0;
}

static const struct of_device_id chc5tc_of_match[] = {
	{ .compatible = "circuitvalley,chc5-vtiming-1.0" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5tc_of_match);

static struct platform_driver chc5tc_driver = {
	.probe	= chc5tc_probe,
	.remove	= chc5tc_remove,
	.driver	= {
		.name		= "chc5-vtiming",
		.of_match_table	= chc5tc_of_match,
	},
};
module_platform_driver(chc5tc_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 video timing");
MODULE_LICENSE("GPL");
