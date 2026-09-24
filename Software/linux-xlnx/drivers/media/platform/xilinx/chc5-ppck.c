// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 pixel packer
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

#define CHC5_PPCK_REG_FMT		0x00
#define CHC5_PPCK_FMT_MASK		0x07

#define CHC5_PPCK_HW_FMT_RAW8		0
#define CHC5_PPCK_HW_FMT_RAW12_PACKED	1
#define CHC5_PPCK_HW_FMT_RAW12_UNPACKED	2
#define CHC5_PPCK_HW_FMT_RGB565		3
#define CHC5_PPCK_HW_FMT_YUV422		4
#define CHC5_PPCK_HW_FMT_MONO8		5
#define CHC5_PPCK_HW_FMT_XRGB8888	6
#define CHC5_PPCK_HW_FMT_BGR24		7
#define CHC5_PPCK_HW_FMT_RAW10_PACKED	CHC5_PPCK_HW_FMT_RAW12_PACKED
#define CHC5_PPCK_HW_FMT_RAW10_UNPACKED	CHC5_PPCK_HW_FMT_RAW12_UNPACKED
#define CHC5_PPCK_HW_FMT_RAW14_PACKED	CHC5_PPCK_HW_FMT_RAW12_PACKED
#define CHC5_PPCK_HW_FMT_RAW14_UNPACKED	CHC5_PPCK_HW_FMT_RAW12_UNPACKED
#define CHC5_PPCK_HW_FMT_RAW16_PACKED	CHC5_PPCK_HW_FMT_RAW12_PACKED
#define CHC5_PPCK_HW_FMT_RAW16_UNPACKED	CHC5_PPCK_HW_FMT_RAW12_UNPACKED

#define CHC5_PPCK_PAD_SINK		0
#define CHC5_PPCK_PAD_SOURCE		1
#define CHC5_PPCK_NUM_PADS		2

#define CHC5_PPCK_DEF_WIDTH		1920
#define CHC5_PPCK_DEF_HEIGHT		1080
#define CHC5_PPCK_MIN_WIDTH		64
#define CHC5_PPCK_MAX_WIDTH		8192
#define CHC5_PPCK_MIN_HEIGHT		64
#define CHC5_PPCK_MAX_HEIGHT		8192
#define CHC5_PPCK_DEF_MBUS_CODE	MEDIA_BUS_FMT_SRGGB12_1X12

struct chc5_ppck_fmt_entry {
	u32	mbus_code;
	u32	hw_fmt;
	const char *name;
};

static const struct chc5_ppck_fmt_entry chc5_ppck_format_table[] = {
	{ MEDIA_BUS_FMT_SRGGB8_1X8,   CHC5_PPCK_HW_FMT_RAW8,           "RAW8"           },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, CHC5_PPCK_HW_FMT_RAW10_PACKED,   "RAW10 packed"   },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, CHC5_PPCK_HW_FMT_RAW10_UNPACKED, "RAW10 unpacked" },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, CHC5_PPCK_HW_FMT_RAW12_PACKED,   "RAW12 packed"   },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, CHC5_PPCK_HW_FMT_RAW12_UNPACKED, "RAW12 unpacked" },
	{ MEDIA_BUS_FMT_SRGGB14_1X14, CHC5_PPCK_HW_FMT_RAW14_PACKED,   "RAW14 packed"   },
	{ MEDIA_BUS_FMT_SGBRG14_1X14, CHC5_PPCK_HW_FMT_RAW14_UNPACKED, "RAW14 unpacked" },
	{ MEDIA_BUS_FMT_SRGGB16_1X16, CHC5_PPCK_HW_FMT_RAW16_PACKED,   "RAW16 packed"   },
	{ MEDIA_BUS_FMT_SGBRG16_1X16, CHC5_PPCK_HW_FMT_RAW16_UNPACKED, "RAW16 unpacked" },
	{ MEDIA_BUS_FMT_RGB565_1X16,  CHC5_PPCK_HW_FMT_RGB565,         "RGB565"         },
	{ MEDIA_BUS_FMT_YUYV8_1X16,   CHC5_PPCK_HW_FMT_YUV422,         "YUV422"         },
	{ MEDIA_BUS_FMT_Y8_1X8,       CHC5_PPCK_HW_FMT_MONO8,          "MONO8"          },
	{ MEDIA_BUS_FMT_Y12_1X12,     CHC5_PPCK_HW_FMT_RAW12_PACKED,   "MONO12 packed"  },
	{ MEDIA_BUS_FMT_Y16_1X16,     CHC5_PPCK_HW_FMT_RAW12_UNPACKED, "MONO12 unpacked"},
	{ MEDIA_BUS_FMT_RGB888_1X32_PADHI, CHC5_PPCK_HW_FMT_XRGB8888,  "XRGB8888"       },
	{ MEDIA_BUS_FMT_BGR888_1X24,  CHC5_PPCK_HW_FMT_BGR24,          "BGR24"          },
	{ MEDIA_BUS_FMT_SBGGR8_1X8,   CHC5_PPCK_HW_FMT_RAW8,           "RAW8 (BGGR)"    },
	{ MEDIA_BUS_FMT_SGRBG8_1X8,   CHC5_PPCK_HW_FMT_RAW8,           "RAW8 (GRBG)"    },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, CHC5_PPCK_HW_FMT_RAW10_PACKED,   "RAW10 p (BGGR)" },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, CHC5_PPCK_HW_FMT_RAW10_UNPACKED, "RAW10 u (GRBG)" },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, CHC5_PPCK_HW_FMT_RAW12_PACKED,   "RAW12 p (BGGR)" },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, CHC5_PPCK_HW_FMT_RAW12_UNPACKED, "RAW12 u (GRBG)" },
	{ MEDIA_BUS_FMT_SBGGR14_1X14, CHC5_PPCK_HW_FMT_RAW14_PACKED,   "RAW14 p (BGGR)" },
	{ MEDIA_BUS_FMT_SGRBG14_1X14, CHC5_PPCK_HW_FMT_RAW14_UNPACKED, "RAW14 u (GRBG)" },
	{ MEDIA_BUS_FMT_SBGGR16_1X16, CHC5_PPCK_HW_FMT_RAW16_PACKED,   "RAW16 p (BGGR)" },
	{ MEDIA_BUS_FMT_SGRBG16_1X16, CHC5_PPCK_HW_FMT_RAW16_UNPACKED, "RAW16 u (GRBG)" },
};

#define CHC5_PPCK_NUM_FORMATS	ARRAY_SIZE(chc5_ppck_format_table)

struct chc5_ppck_device {
	struct device		*dev;
	void __iomem		*iomem;
	struct clk		*axi_clk;

	struct v4l2_subdev	subdev;
	struct media_pad	pads[CHC5_PPCK_NUM_PADS];

	struct v4l2_mbus_framefmt formats[CHC5_PPCK_NUM_PADS];

	struct mutex		lock;
};

static inline struct chc5_ppck_device *to_chc5_ppck(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5_ppck_device, subdev);
}

static inline void chc5_ppck_write(struct chc5_ppck_device *chc5_ppck, u32 reg, u32 val)
{
	iowrite32(val, chc5_ppck->iomem + reg);
}

static inline u32 chc5_ppck_read(struct chc5_ppck_device *chc5_ppck, u32 reg)
{
	return ioread32(chc5_ppck->iomem + reg);
}

static void chc5_ppck_set_hw_fmt(struct chc5_ppck_device *chc5_ppck, u32 hw_fmt)
{
	chc5_ppck_write(chc5_ppck, CHC5_PPCK_REG_FMT, hw_fmt & CHC5_PPCK_FMT_MASK);
	dev_dbg(chc5_ppck->dev, "fmt register set to %u\n", hw_fmt & CHC5_PPCK_FMT_MASK);
}

static const struct chc5_ppck_fmt_entry *chc5_ppck_find_format_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < CHC5_PPCK_NUM_FORMATS; i++) {
		if (chc5_ppck_format_table[i].mbus_code == code)
			return &chc5_ppck_format_table[i];
	}
	return NULL;
}

static const struct chc5_ppck_fmt_entry *chc5_ppck_find_format_by_hw(u32 hw_fmt)
{
	unsigned int i;

	for (i = 0; i < CHC5_PPCK_NUM_FORMATS; i++) {
		if (chc5_ppck_format_table[i].hw_fmt == hw_fmt)
			return &chc5_ppck_format_table[i];
	}
	return NULL;
}

static void chc5_ppck_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = CHC5_PPCK_DEF_WIDTH;
	fmt->height = CHC5_PPCK_DEF_HEIGHT;
	fmt->code = CHC5_PPCK_DEF_MBUS_CODE;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
}

static void chc5_ppck_clamp_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width  = clamp_t(u32, fmt->width,  CHC5_PPCK_MIN_WIDTH,  CHC5_PPCK_MAX_WIDTH);
	fmt->height = clamp_t(u32, fmt->height, CHC5_PPCK_MIN_HEIGHT, CHC5_PPCK_MAX_HEIGHT);

	if (!chc5_ppck_find_format_by_code(fmt->code))
		fmt->code = CHC5_PPCK_DEF_MBUS_CODE;

	fmt->field = V4L2_FIELD_NONE;
}

static int chc5_ppck_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= CHC5_PPCK_NUM_FORMATS)
		return -EINVAL;

	code->code = chc5_ppck_format_table[code->index].mbus_code;
	return 0;
}

static int chc5_ppck_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 0)
		return -EINVAL;

	if (!chc5_ppck_find_format_by_code(fse->code))
		return -EINVAL;

	fse->min_width  = CHC5_PPCK_MIN_WIDTH;
	fse->max_width  = CHC5_PPCK_MAX_WIDTH;
	fse->min_height = CHC5_PPCK_MIN_HEIGHT;
	fse->max_height = CHC5_PPCK_MAX_HEIGHT;

	return 0;
}

static int chc5_ppck_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct chc5_ppck_device *chc5_ppck = to_chc5_ppck(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5_PPCK_NUM_PADS)
		return -EINVAL;

	mutex_lock(&chc5_ppck->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (!mbus_fmt) {
			mutex_unlock(&chc5_ppck->lock);
			return -EINVAL;
		}
		fmt->format = *mbus_fmt;
	} else {
		fmt->format = chc5_ppck->formats[fmt->pad];
	}

	mutex_unlock(&chc5_ppck->lock);
	return 0;
}

static int chc5_ppck_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct chc5_ppck_device *chc5_ppck = to_chc5_ppck(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;
	const struct chc5_ppck_fmt_entry *entry;

	if (fmt->pad >= CHC5_PPCK_NUM_PADS)
		return -EINVAL;

	chc5_ppck_clamp_format(&fmt->format);

	mutex_lock(&chc5_ppck->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (mbus_fmt)
			*mbus_fmt = fmt->format;
	} else {
		chc5_ppck->formats[fmt->pad] = fmt->format;

		if (fmt->pad == CHC5_PPCK_PAD_SOURCE) {
			entry = chc5_ppck_find_format_by_code(fmt->format.code);
			if (entry)
				chc5_ppck_set_hw_fmt(chc5_ppck, entry->hw_fmt);
		}
	}

	mutex_unlock(&chc5_ppck->lock);
	return 0;
}

static int chc5_ppck_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5_PPCK_PAD_SINK);
	chc5_ppck_init_format(fmt);

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5_PPCK_PAD_SOURCE);
	chc5_ppck_init_format(fmt);

	return 0;
}

static int chc5_ppck_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct chc5_ppck_device *chc5_ppck = to_chc5_ppck(sd);
	const struct chc5_ppck_fmt_entry *entry;

	mutex_lock(&chc5_ppck->lock);

	if (enable) {
		entry = chc5_ppck_find_format_by_code(
				chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].code);
		if (entry)
			chc5_ppck_set_hw_fmt(chc5_ppck, entry->hw_fmt);

		dev_dbg(chc5_ppck->dev,
			"stream on: sink=%ux%u(0x%04x) source=%ux%u(0x%04x) hw_fmt=%s\n",
			chc5_ppck->formats[CHC5_PPCK_PAD_SINK].width,
			chc5_ppck->formats[CHC5_PPCK_PAD_SINK].height,
			chc5_ppck->formats[CHC5_PPCK_PAD_SINK].code,
			chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].width,
			chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].height,
			chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].code,
			entry ? entry->name : "unknown");
	} else {
		dev_dbg(chc5_ppck->dev, "stream off\n");
	}

	mutex_unlock(&chc5_ppck->lock);
	return 0;
}

static int chc5_ppck_log_status(struct v4l2_subdev *sd)
{
	struct chc5_ppck_device *chc5_ppck = to_chc5_ppck(sd);
	const struct chc5_ppck_fmt_entry *entry;
	u32 reg;

	mutex_lock(&chc5_ppck->lock);
	reg = chc5_ppck_read(chc5_ppck, CHC5_PPCK_REG_FMT) & CHC5_PPCK_FMT_MASK;
	mutex_unlock(&chc5_ppck->lock);

	entry = chc5_ppck_find_format_by_hw(reg);

	dev_info(chc5_ppck->dev, "HW fmt register: %u (%s)\n",
		 reg, entry ? entry->name : "unknown");
	dev_info(chc5_ppck->dev, "Sink:   %ux%u code=0x%04x\n",
		 chc5_ppck->formats[CHC5_PPCK_PAD_SINK].width,
		 chc5_ppck->formats[CHC5_PPCK_PAD_SINK].height,
		 chc5_ppck->formats[CHC5_PPCK_PAD_SINK].code);
	dev_info(chc5_ppck->dev, "Source: %ux%u code=0x%04x\n",
		 chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].width,
		 chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].height,
		 chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].code);

	return 0;
}

static const struct v4l2_subdev_core_ops chc5_ppck_core_ops = {
	.log_status	= chc5_ppck_log_status,
};

static const struct v4l2_subdev_video_ops chc5_ppck_video_ops = {
	.s_stream	= chc5_ppck_s_stream,
};

static const struct v4l2_subdev_pad_ops chc5_ppck_pad_ops = {
	.enum_mbus_code		= chc5_ppck_enum_mbus_code,
	.enum_frame_size	= chc5_ppck_enum_frame_size,
	.get_fmt		= chc5_ppck_get_fmt,
	.set_fmt		= chc5_ppck_set_fmt,
};

static const struct v4l2_subdev_ops chc5_ppck_subdev_ops = {
	.core	= &chc5_ppck_core_ops,
	.video	= &chc5_ppck_video_ops,
	.pad	= &chc5_ppck_pad_ops,
};

static const struct v4l2_subdev_internal_ops chc5_ppck_internal_ops = {
	.open	= chc5_ppck_open,
};

static const struct media_entity_operations chc5_ppck_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int chc5_ppck_probe(struct platform_device *pdev)
{
	struct chc5_ppck_device *chc5_ppck;
	struct v4l2_subdev *sd;
	struct resource *res;
	u32 fmt_init;
	int ret;

	chc5_ppck = devm_kzalloc(&pdev->dev, sizeof(*chc5_ppck), GFP_KERNEL);
	if (!chc5_ppck)
		return -ENOMEM;

	chc5_ppck->dev = &pdev->dev;
	mutex_init(&chc5_ppck->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chc5_ppck->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chc5_ppck->iomem))
		return PTR_ERR(chc5_ppck->iomem);

	chc5_ppck->axi_clk = devm_clk_get_optional(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(chc5_ppck->axi_clk))
		return PTR_ERR(chc5_ppck->axi_clk);

	ret = clk_prepare_enable(chc5_ppck->axi_clk);
	if (ret)
		return ret;

	if (of_property_read_u32(pdev->dev.of_node, "xlnx,fmt", &fmt_init))
		fmt_init = CHC5_PPCK_HW_FMT_RAW12_PACKED;

	if (fmt_init > 7) {
		dev_warn(&pdev->dev,
			 "xlnx,fmt=%u out of range, defaulting to %u\n",
			 fmt_init, CHC5_PPCK_HW_FMT_RAW12_PACKED);
		fmt_init = CHC5_PPCK_HW_FMT_RAW12_PACKED;
	}

	chc5_ppck_init_format(&chc5_ppck->formats[CHC5_PPCK_PAD_SINK]);
	chc5_ppck_init_format(&chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE]);

	{
		const struct chc5_ppck_fmt_entry *entry;

		entry = chc5_ppck_find_format_by_hw(fmt_init);
		if (entry)
			chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].code = entry->mbus_code;
	}

	chc5_ppck_set_hw_fmt(chc5_ppck, fmt_init);

	sd = &chc5_ppck->subdev;
	v4l2_subdev_init(sd, &chc5_ppck_subdev_ops);
	sd->internal_ops = &chc5_ppck_internal_ops;
	sd->dev = &pdev->dev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	chc5_ppck->pads[CHC5_PPCK_PAD_SINK].flags   = MEDIA_PAD_FL_SINK;
	chc5_ppck->pads[CHC5_PPCK_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV;
	sd->entity.ops = &chc5_ppck_media_ops;

	ret = media_entity_pads_init(&sd->entity, CHC5_PPCK_NUM_PADS, chc5_ppck->pads);
	if (ret) {
		dev_err(&pdev->dev, "failed to init media pads: %d\n", ret);
		goto err_clk;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register subdev: %d\n", ret);
		goto err_entity;
	}

	platform_set_drvdata(pdev, chc5_ppck);

	{
		const struct chc5_ppck_fmt_entry *entry;

		entry = chc5_ppck_find_format_by_hw(fmt_init);
		dev_info(&pdev->dev,
			 "probed: fmt=%u (%s), sink=%ux%u, source=%ux%u\n",
			 fmt_init, entry ? entry->name : "unknown",
			 chc5_ppck->formats[CHC5_PPCK_PAD_SINK].width,
			 chc5_ppck->formats[CHC5_PPCK_PAD_SINK].height,
			 chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].width,
			 chc5_ppck->formats[CHC5_PPCK_PAD_SOURCE].height);
	}

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_clk:
	clk_disable_unprepare(chc5_ppck->axi_clk);
	return ret;
}

static int chc5_ppck_remove(struct platform_device *pdev)
{
	struct chc5_ppck_device *chc5_ppck = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&chc5_ppck->subdev);
	media_entity_cleanup(&chc5_ppck->subdev.entity);
	clk_disable_unprepare(chc5_ppck->axi_clk);

	return 0;
}

static const struct of_device_id chc5_ppck_of_match[] = {
	{ .compatible = "circuitvalley,chc5-pixel-packer-1.0" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5_ppck_of_match);

static struct platform_driver chc5_ppck_driver = {
	.probe	= chc5_ppck_probe,
	.remove	= chc5_ppck_remove,
	.driver	= {
		.name		= "chc5-pixel-packer",
		.of_match_table	= chc5_ppck_of_match,
	},
};
module_platform_driver(chc5_ppck_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 pixel packer");
MODULE_LICENSE("GPL");
