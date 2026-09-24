// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX565 sensor driver
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
#include <asm/unaligned.h>
#else
#include <linux/unaligned.h>
#endif
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include <uapi/linux/chc5-v4l2-controls.h>

#define IMX565_REG_STANDBY		0x3000
#define IMX565_STANDBY_POR		0x01
#define IMX565_HMAX_POR			0x0081
#define IMX565_REG_XMSTA		0x3010

#define IMX565_REG_INCKSEL_ST0		0x3014
#define IMX565_REG_INCKSEL_ST1		0x3015
#define IMX565_REG_INCKSEL_ST2		0x3016
#define IMX565_REG_INCKSEL_ST3		0x3018
#define IMX565_REG_INCKSEL_ST4		0x3019
#define IMX565_REG_INCKSEL_ST5		0x301b
#define IMX565_REG_INCKSEL_D2		0x3226
#define IMX565_REG_INCKSEL_D3		0x3227

#define IMX565_REG_REGHOLD		0x3034
#define IMX565_REG_HVMODE		0x303c

#define IMX565_REG_VOPB_VBLK_HWID	0x30d0
#define IMX565_REG_FINFO_HWIDTH		0x30d2

#define IMX565_REG_VMAX			0x30d4
#define IMX565_VMAX_MAX			0xfffff
#define IMX565_REG_HMAX			0x30d8
#define IMX565_HMAX_MAX			0xffff
#define IMX565_INTERNAL_CLOCK		74250000U

#define IMX565_REG_GMRWT		0x30e2
#define IMX565_REG_GMTWT		0x30e3
#define IMX565_REG_GAINDLY		0x30e5
#define IMX565_REG_GSDLY		0x30e6

#define IMX565_REG_ROI_MODE		0x3100
#define IMX565_REG_FID0_ROI		0x3104
#define IMX565_FID0_ROI_OFF		0x00
#define IMX565_FID0_ROI_AREA1		0x03
#define IMX565_REG_FID0_ROIPH1		0x3120
#define IMX565_REG_FID0_ROIPV1		0x3122
#define IMX565_REG_FID0_ROIWH1		0x3124
#define IMX565_REG_FID0_ROIWV1		0x3126

#define IMX565_REG_ADBIT		0x3200
#define IMX565_REG_ODBIT		0x3430
#define IMX565_REG_HVREVERSE		0x3204

#define IMX565_TOFFSET_NS		2470
#define IMX565_REG_SHS			0x3240
#define IMX565_SHS_OFFSET		4
#define IMX565_EXPOSURE_MIN		1
#define IMX565_EXPOSURE_STEP		1
#define IMX565_EXPOSURE_DEFAULT		1000

#define IMX565_REG_SYNCSEL		0x343c

#define IMX565_REG_GAIN			0x3514
#define IMX565_GAIN_DB10_PER_CODE	1
#define IMX565_ANA_GAIN_MIN		0
#define IMX565_ANA_GAIN_MAX		480
#define IMX565_ANA_GAIN_STEP		1
#define IMX565_ANA_GAIN_DEFAULT		0

static unsigned int imx565_gain_code(unsigned int db10)
{
	return db10 / IMX565_GAIN_DB10_PER_CODE;
}

#define IMX565_REG_BLKLEVEL		0x35b4
#define IMX565_BLKLEVEL_DEFAULT		0xf0
#define IMX565_BLKLEVEL_MAX		0xfff

#define IMX565_REG_TPG_EN		0x3550
#define IMX565_TPG_EN_ON		0x07
#define IMX565_TPG_EN_OFF		0x06
#define IMX565_REG_TPG_PATSEL		0x3551

static const char * const imx565_test_pattern_menu[] = {
	"Disabled",
	"Sequence Pattern 1",
	"Sequence Pattern 2",
	"Gradation Pattern",
};

#define IMX565_REG_LANESEL		0x3904

#define IMX565_EMBEDDED_LINE_WIDTH	16384
#define IMX565_NUM_EMBEDDED_LINES	1

enum pad_types {
	IMAGE_PAD,
	METADATA_PAD,
	NUM_PADS
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
static inline struct v4l2_mbus_framefmt *
v4l2_subdev_state_get_format(struct v4l2_subdev_state *state, unsigned int pad)
{
	if (WARN_ON(!state))
		return NULL;
	if (WARN_ON(pad >= NUM_PADS))
		pad = 0;
	return &state->pads[pad].try_fmt;
}

static inline struct v4l2_rect *
v4l2_subdev_state_get_crop(struct v4l2_subdev_state *state, unsigned int pad)
{
	if (WARN_ON(!state))
		return NULL;
	if (WARN_ON(pad >= NUM_PADS))
		pad = 0;
	return &state->pads[pad].try_crop;
}

static inline struct v4l2_rect *
v4l2_subdev_state_get_compose(struct v4l2_subdev_state *state, unsigned int pad)
{
	if (WARN_ON(!state))
		return NULL;
	if (WARN_ON(pad >= NUM_PADS))
		pad = 0;
	return &state->pads[pad].try_compose;
}
#endif

#define IMX565_NATIVE_WIDTH		4128U
#define IMX565_NATIVE_HEIGHT		3008U
#define IMX565_PIXEL_ARRAY_WIDTH	4128U
#define IMX565_PIXEL_ARRAY_HEIGHT	3008U

#define IMX565_MIN_WIDTH		608U
#define IMX565_MIN_WIDTH_8BIT		1200U
#define IMX565_MIN_HEIGHT		8U
#define IMX565_WIDTH_STEP		16U
#define IMX565_HEIGHT_STEP		8U
#define IMX565_CROP_LEFT_STEP		8U
#define IMX565_CROP_TOP_STEP		8U

#define IMX565_VBLANK_OVERHEAD		86

#define IMX565_EBD_LINES		4
#define IMX565_VOB_LINES		64
#define IMX565_VOB_LINES_SUB		32

static_assert(IMX565_VBLANK_OVERHEAD ==
	      6 + 1 + IMX565_EBD_LINES + IMX565_VOB_LINES + 2 + 2 + 7,
	      "VBLANK overhead and VOB line count have drifted apart");

#define IMX565_SUB_WIDTH		2064U
#define IMX565_SUB_HEIGHT		1504U
#define IMX565_SUB_TRIM_TOP		3U
#define IMX565_SUB_TRIM_BOTTOM		5U
#define IMX565_SUB_IMAGE_HEIGHT		(IMX565_SUB_HEIGHT - IMX565_SUB_TRIM_TOP - \
					 IMX565_SUB_TRIM_BOTTOM)

static_assert(IMX565_SUB_TRIM_TOP % 2 == 1,
	      "an even top trim leaves the subsampled mosaic GBRG");
static_assert(IMX565_SUB_IMAGE_HEIGHT % 8 == 0,
	      "binned image height must be a multiple of 8");
#define IMX565_VBLANK_OVERHEAD_SUB	50

#define IMX565_HVMODE_ALLPIX		0x00
#define IMX565_HVMODE_SUBSAMPLE		0x08

#define IMX565_REG_LLBLANK		0x323c
#define IMX565_REG_VINT_EN		0x323e
#define IMX565_LLBLANK_ALLPIX		0x19
#define IMX565_LLBLANK_SUB		0x11
#define IMX565_VINT_EN_ALLPIX		0x33
#define IMX565_VINT_EN_SUB		0x23

struct imx565_reg {
	u16 address;
	u8 val;
};

struct imx565_reg_list {
	unsigned int num_of_regs;
	const struct imx565_reg *regs;
};

enum {
	IMX565_LINK_FREQ_594MHZ,
	IMX565_LINK_FREQ_445MHZ,
	IMX565_LINK_FREQ_297MHZ,
	IMX565_NUM_LINK_FREQS
};

static const s64 link_freqs[] = {
	[IMX565_LINK_FREQ_594MHZ] = 594000000,
	[IMX565_LINK_FREQ_445MHZ] = 445500000,
	[IMX565_LINK_FREQ_297MHZ] = 297000000,
};

enum imx565_depth {
	IMX565_DEPTH_12 = 0,
	IMX565_DEPTH_10 = 1,
	IMX565_DEPTH_8 = 2,
	IMX565_NUM_DEPTHS
};

static const u16 hmax_min_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{
		{ 812, 1078, 1610 },
		{ 1586, 2108, 3154 },
	},
	{
		{ 684, 907, 1353 },
		{ 1328, 1764, 2638 },
	},
	{
		{ 555, 736, 1096 },
		{ 1070, 1422, 2125 },
	},
};

static const u8 gmrwt_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{ { 0x04, 0x02, 0x02 }, { 0x02, 0x02, 0x02 } },
	{ { 0x04, 0x04, 0x02 }, { 0x02, 0x02, 0x02 } },
	{ { 0x04, 0x04, 0x02 }, { 0x02, 0x02, 0x02 } },
};

static const u8 gmtwt_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{ { 0x12, 0x0e, 0x0a }, { 0x0a, 0x08, 0x06 } },
	{ { 0x16, 0x10, 0x0c }, { 0x0c, 0x0a, 0x06 } },
	{ { 0x1a, 0x14, 0x0e }, { 0x0e, 0x0a, 0x08 } },
};

static const u8 gsdly_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{ { 0x08, 0x06, 0x04 }, { 0x04, 0x04, 0x02 } },
	{ { 0x0a, 0x08, 0x06 }, { 0x06, 0x04, 0x04 } },
	{ { 0x0c, 0x0a, 0x06 }, { 0x06, 0x06, 0x04 } },
};

static const u16 hmax_min_sub_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{
		{ 425, 562, 836 },
		{ 812, 1076, 1606 },
	},
	{
		{ 361, 477, 708 },
		{ 683, 904, 1348 },
	},
	{
		{ 297, 392, 580 },
		{ 554, 734, 1093 },
	},
};

static const u8 gmrwt_sub_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{ { 0x06, 0x04, 0x04 }, { 0x04, 0x02, 0x02 } },
	{ { 0x06, 0x06, 0x04 }, { 0x04, 0x04, 0x02 } },
	{ { 0x08, 0x06, 0x04 }, { 0x04, 0x04, 0x02 } },
};

static const u8 gmtwt_sub_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{ { 0x22, 0x1a, 0x12 }, { 0x12, 0x0e, 0x0a } },
	{ { 0x28, 0x1e, 0x14 }, { 0x16, 0x10, 0x0c } },
	{ { 0x30, 0x26, 0x1a }, { 0x1a, 0x14, 0x0e } },
};

static const u8 gsdly_sub_table[IMX565_NUM_DEPTHS][2][IMX565_NUM_LINK_FREQS] = {
	{ { 0x10, 0x0c, 0x08 }, { 0x08, 0x06, 0x04 } },
	{ { 0x12, 0x0e, 0x0a }, { 0x0a, 0x08, 0x06 } },
	{ { 0x16, 0x10, 0x0c }, { 0x0c, 0x0a, 0x06 } },
};

#define IMX565_GAINDLY_VAL		0x02

static const struct imx565_reg mode_common_regs[] = {
	{0x3014, 0x05},
	{0x3015, 0x91},
	{0x3016, 0x50},
	{0x3018, 0x20},
	{0x3019, 0x02},
	{0x301b, 0x1d},
	{0x321c, 0x40},
	{0x321d, 0x05},
	{0x321e, 0xe0},
	{0x321f, 0x00},
	{0x3220, 0x40},
	{0x3221, 0x05},
	{0x3222, 0xe0},
	{0x3223, 0x00},
	{0x3224, 0x40},
	{0x3225, 0x14},
	{0x3226, 0x80},
	{0x3227, 0x80},
	{0x323c, 0x19},
	{0x323d, 0x00},
	{0x323e, 0x33},
	{0x303c, 0x00},
	{0x3502, 0x09},
	{0x343c, 0xc0},
	{0x3ca4, 0x80},
	{0x3ca5, 0x09},
	{0x3942, 0x03},
	{0x3004, 0xa8},
	{0x3005, 0x02},
	{0x322b, 0x06},
	{0x3233, 0x10},
	{0x3521, 0x51},
	{0x3522, 0xb1},
	{0x3535, 0x00},
	{0x3542, 0x27},
	{0x3546, 0x54},
	{0x354a, 0x20},
	{0x359c, 0x0f},
	{0x35a5, 0x12},
	{0x35a9, 0x42},
	{0x35b6, 0x02},
	{0x35ce, 0x0e},
	{0x35ed, 0x12},
	{0x35f0, 0xfb},
	{0x35f1, 0x0b},
	{0x35f2, 0xfb},
	{0x35f3, 0x0b},
	{0x362e, 0x24},
	{0x3642, 0x10},
	{0x3656, 0x44},
	{0x366a, 0x2e},
	{0x3670, 0xc3},
	{0x3672, 0x05},
	{0x3674, 0xb6},
	{0x3675, 0x01},
	{0x3676, 0x05},
	{0x367e, 0x24},
	{0x3692, 0x10},
	{0x36f5, 0x0f},
	{0x3797, 0x20},
	{0x3e2e, 0x07},
	{0x3e30, 0x4e},
	{0x3e6e, 0x07},
	{0x3e70, 0x35},
	{0x3e96, 0x01},
	{0x3e9e, 0x38},
	{0x3ea0, 0x4c},
	{0x3f3a, 0x04},
	{0x4056, 0x23},
	{0x4096, 0x23},
	{0x4182, 0x00},
	{0x41a2, 0x03},
	{0x4232, 0x3c},
	{0x4235, 0x22},
	{0x4306, 0x00},
	{0x4307, 0x00},
	{0x4308, 0x00},
	{0x4309, 0x00},
	{0x4310, 0x04},
	{0x4311, 0x04},
	{0x4312, 0x04},
	{0x4313, 0x04},
	{0x431e, 0x16},
	{0x431f, 0x16},
	{0x433c, 0x8a},
	{0x433d, 0x02},
	{0x433e, 0xe8},
	{0x433f, 0x05},
	{0x4340, 0x9e},
	{0x4341, 0x0c},
	{0x446a, 0x4c},
	{0x446e, 0x51},
	{0x4472, 0x57},
	{0x4476, 0x79},
	{0x448a, 0x4c},
	{0x448e, 0x51},
	{0x4492, 0x57},
	{0x4496, 0x79},
	{0x44ec, 0x3f},
	{0x44f0, 0x44},
	{0x44f4, 0x4a},
	{0x4510, 0x3f},
	{0x4514, 0x44},
	{0x4518, 0x4a},
	{0x4576, 0xbe},
	{0x457a, 0xb1},
	{0x4580, 0xbc},
	{0x4584, 0xaf},
	{0x473c, 0x06},
	{0x473d, 0x06},
	{0x473e, 0x06},
	{0x473f, 0x06},
	{0x4749, 0x9f},
	{0x474a, 0x99},
	{0x474b, 0x09},
	{0x4753, 0x90},
	{0x4754, 0x99},
	{0x4755, 0x09},
	{0x4788, 0x04},
	{0x35a5, 0x00},
	{0x35a9, 0x00},
	{0x35ed, 0x00},
	{0x4864, 0xdc},
	{0x4868, 0xdc},
	{0x486c, 0xdc},
	{0x4874, 0xdc},
	{0x4878, 0xdc},
	{0x487c, 0xdc},
	{0x48a4, 0xf4},
	{0x48a8, 0xf4},
	{0x48ac, 0xf4},
	{0x48b4, 0xf4},
	{0x48b8, 0xf4},
	{0x48bc, 0xf4},
	{0x4901, 0x0a},
	{0x4902, 0x01},
	{0x4916, 0x00},
	{0x4917, 0x00},
	{0x4918, 0xff},
	{0x4919, 0x0f},
	{0x491e, 0xff},
	{0x491f, 0x0f},
	{0x4920, 0x00},
	{0x4921, 0x00},
	{0x4926, 0xff},
	{0x4927, 0x0f},
	{0x4928, 0x00},
	{0x4929, 0x00},
	{0x4a34, 0x0a},
};

static const struct imx565_reg rate_1188_regs[] = {
	{0x3226, 0x80}, {0x3227, 0x80},
	{0x3cb4, 0x4f}, {0x3cb5, 0x00},
	{0x3cb6, 0x8f}, {0x3cb7, 0x00},
	{0x3cb8, 0x4f}, {0x3cb9, 0x00},
	{0x3cba, 0x87}, {0x3cbb, 0x00},
	{0x3cbc, 0x4f}, {0x3cbd, 0x00},
	{0x3cbe, 0x47}, {0x3cbf, 0x00},
	{0x3cc0, 0x3f}, {0x3cc1, 0x00},
	{0x3cc2, 0x37}, {0x3cc3, 0x01},
	{0x3cc4, 0x0f}, {0x3cc5, 0x00},
	{0x3cc6, 0x7f}, {0x3cc7, 0x00},
};

static const struct imx565_reg rate_891_regs[] = {
	{0x3226, 0xc0}, {0x3227, 0xd0},
	{0x3cb4, 0x3f}, {0x3cb5, 0x00},
	{0x3cb6, 0x7f}, {0x3cb7, 0x00},
	{0x3cb8, 0x3f}, {0x3cb9, 0x00},
	{0x3cba, 0x6f}, {0x3cbb, 0x00},
	{0x3cbc, 0x37}, {0x3cbd, 0x00},
	{0x3cbe, 0x37}, {0x3cbf, 0x00},
	{0x3cc0, 0x2f}, {0x3cc1, 0x00},
	{0x3cc2, 0xf7}, {0x3cc3, 0x00},
	{0x3cc4, 0x0f}, {0x3cc5, 0x00},
	{0x3cc6, 0x5f}, {0x3cc7, 0x00},
};

static const struct imx565_reg rate_594_regs[] = {
	{0x3226, 0x80}, {0x3227, 0x90},
	{0x3cb4, 0x2f}, {0x3cb5, 0x00},
	{0x3cb6, 0x67}, {0x3cb7, 0x00},
	{0x3cb8, 0x2f}, {0x3cb9, 0x00},
	{0x3cba, 0x4f}, {0x3cbb, 0x00},
	{0x3cbc, 0x27}, {0x3cbd, 0x00},
	{0x3cbe, 0x27}, {0x3cbf, 0x00},
	{0x3cc0, 0x27}, {0x3cc1, 0x00},
	{0x3cc2, 0xb7}, {0x3cc3, 0x00},
	{0x3cc4, 0x0f}, {0x3cc5, 0x00},
	{0x3cc6, 0x47}, {0x3cc7, 0x00},
};

static const struct imx565_reg_list rate_reg_lists[] = {
	[IMX565_LINK_FREQ_594MHZ] = {
		ARRAY_SIZE(rate_1188_regs), rate_1188_regs },
	[IMX565_LINK_FREQ_445MHZ] = {
		ARRAY_SIZE(rate_891_regs), rate_891_regs },
	[IMX565_LINK_FREQ_297MHZ] = {
		ARRAY_SIZE(rate_594_regs), rate_594_regs },
};

static const struct imx565_reg depth_12bit_regs[] = {
	{0x35a4, 0x08},
	{0x35a8, 0x08},
	{0x35ec, 0x08},
	{0x36e8, 0x13},
	{0x4460, 0x6e},
	{0x4728, 0xfb},
	{0x4729, 0x07},
	{0x472e, 0x06},
	{0x472f, 0x06},
	{0x4730, 0x06},
	{0x4731, 0x06},
	{0x4900, 0x6c},
	{0x4908, 0x68},
	{IMX565_REG_ADBIT, 0x15},
	{IMX565_REG_ODBIT, 0x01},
};

static const struct imx565_reg depth_10bit_regs[] = {
	{0x35a4, 0x1c},
	{0x35a8, 0x1c},
	{0x35ec, 0x1c},
	{0x36e8, 0x11},
	{0x4460, 0x6c},
	{0x4728, 0xd4},
	{0x4729, 0x0e},
	{0x472e, 0x05},
	{0x472f, 0x04},
	{0x4730, 0x04},
	{0x4731, 0x04},
	{0x4900, 0x64},
	{0x4908, 0x6e},
	{IMX565_REG_ADBIT, 0x05},
	{IMX565_REG_ODBIT, 0x00},
};

static const struct imx565_reg depth_8bit_regs[] = {
	{0x35a4, 0x08},
	{0x35a8, 0x08},
	{0x35ec, 0x08},
	{0x36e8, 0x11},
	{0x4460, 0x6c},
	{0x4728, 0xfb},
	{0x4729, 0x07},
	{0x472e, 0x06},
	{0x472f, 0x06},
	{0x4730, 0x06},
	{0x4731, 0x06},
	{0x4900, 0x44},
	{0x4908, 0x6e},
	{IMX565_REG_ADBIT, 0x25},
	{IMX565_REG_ODBIT, 0x02},
};

static const u32 codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SBGGR8_1X8,
};

static const char * const imx565_supply_name[] = {
	"VANA",
	"VDIG",
	"VDDL",
};

#define IMX565_NUM_SUPPLIES ARRAY_SIZE(imx565_supply_name)

#define IMX565_XCLR_ASSERT_US		10
#define IMX565_XCLR_MIN_DELAY_US	10000
#define IMX565_XCLR_DELAY_RANGE_US	1000

struct imx565 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	struct clk *xclk;
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX565_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	unsigned int link_freq_idx;
	unsigned int num_lanes;

	unsigned int out_width;
	unsigned int out_height;

	struct v4l2_rect crop;
	struct v4l2_rect compose;

	bool subsample;

	unsigned int hmax_min;
	unsigned int hmax;
	unsigned int vmax;
	u64 pix_rate;

	struct mutex mutex;

	bool streaming;
	bool hblank_live_logged;
};

static inline struct imx565 *to_imx565(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx565, sd);
}

static int imx565_read_reg(struct imx565 *imx565, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	unsigned int i;
	int ret;

	if (len > 4)
		return -EINVAL;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = 0;
	for (i = 0; i < len; i++)
		*val |= (u32)data_buf[i] << (8 * i);

	return 0;
}

static int imx565_write_reg(struct imx565 *imx565, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	u8 buf[6];
	unsigned int i;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	for (i = 0; i < len; i++)
		buf[2 + i] = val >> (8 * i);

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int imx565_write_regs(struct imx565 *imx565,
			     const struct imx565_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx565_write_reg(imx565, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static void imx565_reghold(struct imx565 *imx565, bool hold)
{
	imx565_write_reg(imx565, IMX565_REG_REGHOLD, 1, hold ? 1 : 0);
}

static enum imx565_depth imx565_code_depth(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		return IMX565_DEPTH_10;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		return IMX565_DEPTH_8;
	default:
		return IMX565_DEPTH_12;
	}
}

static unsigned int imx565_blklevel_shift(u32 code)
{
	switch (imx565_code_depth(code)) {
	case IMX565_DEPTH_10:
		return 2;
	case IMX565_DEPTH_8:
		return 4;
	default:
		return 0;
	}
}

static u32 imx565_min_width(u32 code)
{
	return imx565_code_depth(code) == IMX565_DEPTH_8 ?
	       IMX565_MIN_WIDTH_8BIT : IMX565_MIN_WIDTH;
}

static u32 imx565_get_format_code(struct imx565 *imx565, u32 code)
{
	unsigned int i;

	lockdep_assert_held(&imx565->mutex);

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == code)
			break;

	if (i >= ARRAY_SIZE(codes))
		i = 0;

	i = (i & ~3) | (imx565->vflip->val ? 2 : 0) |
	    (imx565->hflip->val ? 1 : 0);

	return codes[i];
}

static unsigned int imx565_depth_idx(struct imx565 *imx565)
{
	return imx565_code_depth(imx565->fmt_code);
}

static unsigned int imx565_lane_idx(struct imx565 *imx565)
{
	return imx565->num_lanes == 2 ? 1 : 0;
}

static u8 imx565_gmtwt(struct imx565 *imx565)
{
	unsigned int d = imx565_depth_idx(imx565);
	unsigned int l = imx565_lane_idx(imx565);
	unsigned int r = imx565->link_freq_idx;

	return imx565->subsample ? gmtwt_sub_table[d][l][r]
				 : gmtwt_table[d][l][r];
}

static u8 imx565_gmrwt(struct imx565 *imx565)
{
	unsigned int d = imx565_depth_idx(imx565);
	unsigned int l = imx565_lane_idx(imx565);
	unsigned int r = imx565->link_freq_idx;

	return imx565->subsample ? gmrwt_sub_table[d][l][r]
				 : gmrwt_table[d][l][r];
}

static u8 imx565_gsdly(struct imx565 *imx565)
{
	unsigned int d = imx565_depth_idx(imx565);
	unsigned int l = imx565_lane_idx(imx565);
	unsigned int r = imx565->link_freq_idx;

	return imx565->subsample ? gsdly_sub_table[d][l][r]
				 : gsdly_table[d][l][r];
}

static u16 imx565_hmax_min_lookup(struct imx565 *imx565)
{
	unsigned int d = imx565_depth_idx(imx565);
	unsigned int l = imx565_lane_idx(imx565);
	unsigned int r = imx565->link_freq_idx;

	return imx565->subsample ? hmax_min_sub_table[d][l][r]
				 : hmax_min_table[d][l][r];
}

static unsigned int imx565_shs_min(struct imx565 *imx565)
{
	return imx565_gmtwt(imx565) + IMX565_SHS_OFFSET;
}

static unsigned int imx565_vblank_min(struct imx565 *imx565)
{
	unsigned int overhead = imx565->subsample ? IMX565_VBLANK_OVERHEAD_SUB
						  : IMX565_VBLANK_OVERHEAD;

	return overhead + imx565_gmrwt(imx565) + imx565_gmtwt(imx565) +
	       imx565_gsdly(imx565);
}

static void imx565_clamp_align(unsigned int *width, unsigned int *height,
			       u32 min_w)
{
	*width = clamp(*width, min_w, IMX565_PIXEL_ARRAY_WIDTH);
	*height = clamp(*height, IMX565_MIN_HEIGHT, IMX565_PIXEL_ARRAY_HEIGHT);

	*width = rounddown(*width, IMX565_WIDTH_STEP);
	if (*width < min_w)
		*width = min_w;

	*height = rounddown(*height, IMX565_HEIGHT_STEP);
	if (*height < IMX565_MIN_HEIGHT)
		*height = IMX565_MIN_HEIGHT;
}

static void imx565_clamp_crop(struct v4l2_rect *crop, u32 min_w)
{
	unsigned int w = crop->width;
	unsigned int h = crop->height;

	imx565_clamp_align(&w, &h, min_w);
	crop->width = w;
	crop->height = h;

	if (crop->left < 0)
		crop->left = 0;
	if (crop->top < 0)
		crop->top = 0;

	crop->left = rounddown(crop->left, IMX565_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX565_CROP_TOP_STEP);

	if (crop->left + crop->width > IMX565_PIXEL_ARRAY_WIDTH)
		crop->left = IMX565_PIXEL_ARRAY_WIDTH - crop->width;
	if (crop->top + crop->height > IMX565_PIXEL_ARRAY_HEIGHT)
		crop->top = IMX565_PIXEL_ARRAY_HEIGHT - crop->height;

	crop->left = rounddown(crop->left, IMX565_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX565_CROP_TOP_STEP);
}

static int imx565_write_dynamic_regs(struct imx565 *imx565)
{
	const struct v4l2_rect *crop = &imx565->crop;
	bool sub = imx565->subsample;
	bool full = sub || (crop->width == IMX565_PIXEL_ARRAY_WIDTH &&
			    crop->height == IMX565_PIXEL_ARRAY_HEIGHT);
	int ret = 0;

	imx565_reghold(imx565, true);

	ret = imx565_write_reg(imx565, IMX565_REG_HVMODE, 1,
			       sub ? IMX565_HVMODE_SUBSAMPLE :
				     IMX565_HVMODE_ALLPIX);
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_LLBLANK, 2,
				       sub ? IMX565_LLBLANK_SUB :
					     IMX565_LLBLANK_ALLPIX);
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_VINT_EN, 1,
				       sub ? IMX565_VINT_EN_SUB :
					     IMX565_VINT_EN_ALLPIX);
	if (!ret)
		ret = imx565_write_reg(imx565, 0x3521, 1, sub ? 0xa9 : 0x51);
	if (!ret)
		ret = imx565_write_reg(imx565, 0x3522, 1, sub ? 0xb0 : 0xb1);
	if (!ret)
		ret = imx565_write_reg(imx565, 0x3546, 1, sub ? 0x2a : 0x54);
	if (ret)
		goto out;

	ret = imx565_write_reg(imx565, IMX565_REG_HVREVERSE, 1,
			       (imx565->vflip->val ? 0x01 : 0x00) |
			       (imx565->hflip->val ? 0x02 : 0x00));
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_FID0_ROI, 1,
				       full ? IMX565_FID0_ROI_OFF :
					      IMX565_FID0_ROI_AREA1);
	if (!ret && !full) {
		ret = imx565_write_reg(imx565, IMX565_REG_FID0_ROIPH1, 2,
				       crop->left);
		if (!ret)
			ret = imx565_write_reg(imx565, IMX565_REG_FID0_ROIPV1,
					       2, crop->top);
		if (!ret)
			ret = imx565_write_reg(imx565, IMX565_REG_FID0_ROIWH1,
					       2, crop->width);
		if (!ret)
			ret = imx565_write_reg(imx565, IMX565_REG_FID0_ROIWV1,
					       2, crop->height);
	}
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_VOPB_VBLK_HWID, 2,
				       imx565->out_width);
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_FINFO_HWIDTH, 2,
				       imx565->out_width);
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_GMRWT, 1,
				       imx565_gmrwt(imx565));
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_GMTWT, 1,
				       imx565_gmtwt(imx565));
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_GAINDLY, 1,
				       IMX565_GAINDLY_VAL);
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_GSDLY, 1,
				       imx565_gsdly(imx565));
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_HMAX, 2,
				       imx565->hmax);
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_VMAX, 3,
				       imx565->vmax);

out:
	imx565_reghold(imx565, false);

	return ret;
}

static void imx565_set_default_format(struct imx565 *imx565)
{
	imx565->fmt_code = MEDIA_BUS_FMT_SRGGB12_1X12;
	imx565->subsample = false;
	imx565->out_width = IMX565_PIXEL_ARRAY_WIDTH;
	imx565->out_height = IMX565_PIXEL_ARRAY_HEIGHT;

	imx565->crop.left = 0;
	imx565->crop.top = 0;
	imx565->crop.width = IMX565_PIXEL_ARRAY_WIDTH;
	imx565->crop.height = IMX565_PIXEL_ARRAY_HEIGHT;

	imx565->compose = imx565->crop;

	imx565->hmax_min = imx565_hmax_min_lookup(imx565);
	imx565->hmax = imx565->hmax_min;
	imx565->vmax = imx565->out_height + imx565_vblank_min(imx565);
}

static int imx565_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx565 *imx565 = to_imx565(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;
	struct v4l2_rect *try_compose;

	mutex_lock(&imx565->mutex);

	try_fmt_img->width = IMX565_PIXEL_ARRAY_WIDTH;
	try_fmt_img->height = IMX565_PIXEL_ARRAY_HEIGHT;
	try_fmt_img->code = imx565_get_format_code(imx565,
						   MEDIA_BUS_FMT_SRGGB12_1X12);
	try_fmt_img->field = V4L2_FIELD_NONE;

	try_fmt_meta->width = IMX565_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX565_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = IMX565_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX565_PIXEL_ARRAY_HEIGHT;

	try_compose = v4l2_subdev_state_get_compose(fh->state, IMAGE_PAD);
	*try_compose = *try_crop;

	mutex_unlock(&imx565->mutex);

	return 0;
}

static void imx565_adjust_exposure_range(struct imx565 *imx565)
{
	int exposure_max, exposure_def;

	exposure_max = imx565->vmax - imx565_shs_min(imx565);
	exposure_def = min(exposure_max, imx565->exposure->val);
	__v4l2_ctrl_modify_range(imx565->exposure, imx565->exposure->minimum,
				 exposure_max, imx565->exposure->step,
				 exposure_def);
}

static int imx565_set_exposure(struct imx565 *imx565, unsigned int exp_lines)
{
	unsigned int shs_min = imx565_shs_min(imx565);
	u32 shs;

	exp_lines = clamp_t(u32, exp_lines, IMX565_EXPOSURE_MIN,
			    imx565->vmax - shs_min);
	shs = imx565->vmax - exp_lines;

	return imx565_write_reg(imx565, IMX565_REG_SHS, 3, shs);
}

static unsigned int imx565_hblank_to_hmax(struct imx565 *imx565,
					  unsigned int hblank)
{
	u64 hmax = (u64)(imx565->out_width + hblank) * imx565->hmax_min;

	do_div(hmax, imx565->out_width);
	return min_t(u64, hmax, IMX565_HMAX_MAX);
}

static int imx565_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx565 *imx565 =
		container_of(ctrl->handler, struct imx565, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		imx565->vmax = imx565->out_height + ctrl->val;
		imx565_adjust_exposure_range(imx565);
	}
	if (ctrl->id == V4L2_CID_HBLANK)
		imx565->hmax = imx565_hblank_to_hmax(imx565, ctrl->val);

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		imx565_reghold(imx565, true);
		ret = imx565_write_reg(imx565, IMX565_REG_GAIN, 2,
				      imx565_gain_code(ctrl->val));
		imx565_reghold(imx565, false);
		break;
	case V4L2_CID_EXPOSURE:
		imx565_reghold(imx565, true);
		ret = imx565_set_exposure(imx565, ctrl->val);
		imx565_reghold(imx565, false);
		break;
	case V4L2_CID_VBLANK:
		imx565_reghold(imx565, true);
		ret = imx565_write_reg(imx565, IMX565_REG_VMAX, 3,
				       imx565->vmax);
		if (!ret)
			ret = imx565_set_exposure(imx565,
						  imx565->exposure->val);
		imx565_reghold(imx565, false);
		break;
	case V4L2_CID_HBLANK:
		if (imx565->streaming && !imx565->hblank_live_logged) {
			dev_info(&client->dev,
				 "HBLANK written while streaming: HMAX %u applies at the next stream start\n",
				 imx565->hmax);
			imx565->hblank_live_logged = true;
		}
		ret = imx565_write_reg(imx565, IMX565_REG_HMAX, 2,
				       imx565->hmax);
		break;
	case V4L2_CID_BLACK_LEVEL:
		imx565_reghold(imx565, true);
		ret = imx565_write_reg(imx565, IMX565_REG_BLKLEVEL, 2,
				       ctrl->val >>
				       imx565_blklevel_shift(imx565->fmt_code));
		imx565_reghold(imx565, false);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = imx565_write_reg(imx565, IMX565_REG_HVREVERSE, 1,
				       (imx565->vflip->val ? 0x01 : 0x00) |
				       (imx565->hflip->val ? 0x02 : 0x00));
		break;
	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			ret = imx565_write_reg(imx565, IMX565_REG_TPG_PATSEL, 1,
					       ctrl->val);
			if (!ret)
				ret = imx565_write_reg(imx565, IMX565_REG_TPG_EN,
						       1, IMX565_TPG_EN_ON);
		} else {
			ret = imx565_write_reg(imx565, IMX565_REG_TPG_EN, 1,
					       IMX565_TPG_EN_OFF);
		}
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static int imx565_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx565 *imx565 =
		container_of(ctrl->handler, struct imx565, ctrl_handler);

	switch (ctrl->id) {
	case CHC5SENSOR_CID_OB_TOP:
		ctrl->val = imx565->subsample ? IMX565_SUB_TRIM_TOP
					      : IMX565_VOB_LINES;
		return 0;
	case CHC5SENSOR_CID_OB_BOTTOM:
		ctrl->val = imx565->subsample ? IMX565_SUB_TRIM_BOTTOM : 0;
		return 0;
	case CHC5SENSOR_CID_OB_ON_IMAGE_DT:
		ctrl->val = imx565->subsample ? 1 : 0;
		return 0;
	case CHC5SENSOR_CID_OB_LEFT:
	case CHC5SENSOR_CID_OB_RIGHT:
		ctrl->val = 0;
		return 0;
	}

	return -EINVAL;
}

#define IMX565_OB_CTRL(_id, _name)					\
	{								\
		.ops	= &imx565_ctrl_ops,				\
		.id	= (_id),					\
		.name	= (_name),					\
		.type	= V4L2_CTRL_TYPE_INTEGER,			\
		.min	= 0,						\
		.max	= 0xFFFF,					\
		.step	= 1,						\
		.def	= 0,						\
		.flags	= V4L2_CTRL_FLAG_READ_ONLY |			\
			  V4L2_CTRL_FLAG_VOLATILE,			\
	}

static const struct v4l2_ctrl_ops imx565_ctrl_ops = {
	.s_ctrl		 = imx565_set_ctrl,
	.g_volatile_ctrl = imx565_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config imx565_ob_ctrls[] = {
	IMX565_OB_CTRL(CHC5SENSOR_CID_OB_LEFT,   "Optical Black Left"),
	IMX565_OB_CTRL(CHC5SENSOR_CID_OB_TOP,    "Optical Black Top"),
	IMX565_OB_CTRL(CHC5SENSOR_CID_OB_RIGHT,  "Optical Black Right"),
	IMX565_OB_CTRL(CHC5SENSOR_CID_OB_BOTTOM, "Optical Black Bottom"),
	IMX565_OB_CTRL(CHC5SENSOR_CID_OB_ON_IMAGE_DT, "Optical Black On Image DT"),
};

static int imx565_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		struct imx565 *imx565 = to_imx565(sd);

		if (code->index >= (ARRAY_SIZE(codes) / 4))
			return -EINVAL;

		mutex_lock(&imx565->mutex);
		code->code = imx565_get_format_code(imx565,
						    codes[code->index * 4]);
		mutex_unlock(&imx565->mutex);
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx565_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx565 *imx565 = to_imx565(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index > 0)
			return -EINVAL;

		if (fse->code != imx565_get_format_code(imx565, fse->code))
			return -EINVAL;

		fse->min_width = imx565_min_width(fse->code);
		fse->max_width = IMX565_PIXEL_ARRAY_WIDTH;
		fse->min_height = IMX565_MIN_HEIGHT;
		fse->max_height = IMX565_PIXEL_ARRAY_HEIGHT;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX565_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX565_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx565_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx565_update_image_pad_format(struct imx565 *imx565,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = imx565->out_width;
	fmt->format.height = imx565->out_height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx565_reset_colorspace(&fmt->format);
}

static void imx565_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX565_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX565_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx565_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx565 *imx565 = to_imx565(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx565->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx565_get_format_code(imx565, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx565_update_image_pad_format(imx565, fmt);
			fmt->format.code =
			       imx565_get_format_code(imx565, imx565->fmt_code);
		} else {
			imx565_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx565->mutex);
	return 0;
}

static void imx565_set_framing_limits(struct imx565 *imx565)
{
	unsigned int out_w = imx565->out_width;
	unsigned int out_h = imx565->out_height;
	unsigned int frm_length_min, frm_length_default;
	unsigned int hblank_max;
	u64 pix_rate;

	imx565->hmax_min = imx565_hmax_min_lookup(imx565);
	imx565->hmax = imx565->hmax_min;

	pix_rate = (u64)out_w * IMX565_INTERNAL_CLOCK;
	do_div(pix_rate, imx565->hmax_min);
	imx565->pix_rate = pix_rate;

	__v4l2_ctrl_modify_range(imx565->pixel_rate, pix_rate, pix_rate, 1,
				 pix_rate);

	hblank_max = div_u64((u64)(IMX565_HMAX_MAX - imx565->hmax_min) *
			     out_w, imx565->hmax_min);
	__v4l2_ctrl_modify_range(imx565->hblank, 0, hblank_max, 1, 0);
	__v4l2_ctrl_s_ctrl(imx565->hblank, 0);

	frm_length_min = out_h + imx565_vblank_min(imx565);

	frm_length_default = div_u64((u64)IMX565_INTERNAL_CLOCK,
				     imx565->hmax * 30);
	if (frm_length_default < frm_length_min)
		frm_length_default = frm_length_min;
	if (frm_length_default > IMX565_VMAX_MAX)
		frm_length_default = IMX565_VMAX_MAX;

	__v4l2_ctrl_modify_range(imx565->vblank,
				 frm_length_min - out_h,
				 IMX565_VMAX_MAX - out_h,
				 1, frm_length_default - out_h);
	__v4l2_ctrl_s_ctrl(imx565->vblank, frm_length_default - out_h);

	imx565->vmax = out_h + imx565->vblank->val;

	imx565_adjust_exposure_range(imx565);
}

static int imx565_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	struct imx565 *imx565 = to_imx565(sd);
	unsigned int req_width, req_height;
	bool sub;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx565->mutex);

	if (fmt->pad == IMAGE_PAD) {
		fmt->format.code = imx565_get_format_code(imx565,
							  fmt->format.code);

		req_width = fmt->format.width;
		req_height = fmt->format.height;

		sub = imx565->subsample &&
		      req_width == IMX565_SUB_WIDTH &&
		      (req_height == IMX565_SUB_HEIGHT ||
		       req_height == IMX565_SUB_IMAGE_HEIGHT);

		if (sub) {
			req_width = IMX565_SUB_WIDTH;
			req_height = IMX565_SUB_HEIGHT;
		} else {
			imx565_clamp_align(&req_width, &req_height,
					   imx565_min_width(fmt->format.code));
		}

		fmt->format.width = req_width;
		fmt->format.height = req_height;
		fmt->format.field = V4L2_FIELD_NONE;
		imx565_reset_colorspace(&fmt->format);

		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx565->subsample = sub;
			imx565->out_width = req_width;
			imx565->out_height = req_height;
			imx565->compose.width = req_width;
			imx565->compose.height = req_height;
			imx565->fmt_code = fmt->format.code;

			if (sub) {
				imx565->crop.left = 0;
				imx565->crop.top = 0;
				imx565->crop.width = IMX565_PIXEL_ARRAY_WIDTH;
				imx565->crop.height = IMX565_PIXEL_ARRAY_HEIGHT;
			} else {
				imx565->crop.width = req_width;
				imx565->crop.height = req_height;
				imx565_clamp_crop(&imx565->crop,
						  imx565_min_width(imx565->fmt_code));
			}

			imx565_set_framing_limits(imx565);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx565_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx565->mutex);

	return 0;
}

static int imx565_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx565 *imx565 = to_imx565(sd);

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&imx565->mutex);
		sel->r = imx565->crop;
		mutex_unlock(&imx565->mutex);
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		mutex_lock(&imx565->mutex);
		sel->r = imx565->compose;
		mutex_unlock(&imx565->mutex);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX565_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX565_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int imx565_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx565 *imx565 = to_imx565(sd);

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&imx565->mutex);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_rect crop = sel->r;

		imx565_clamp_crop(&crop, imx565_min_width(imx565->fmt_code));

		if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
			*v4l2_subdev_state_get_crop(sd_state, sel->pad) = crop;
		} else {
			imx565->subsample = false;
			imx565->crop = crop;
			imx565->compose.width = crop.width;
			imx565->compose.height = crop.height;
			imx565->out_width = crop.width;
			imx565->out_height = crop.height;
			imx565_set_framing_limits(imx565);
		}

		sel->r = crop;

	} else {
		unsigned int compose_w = sel->r.width;
		unsigned int compose_h = sel->r.height;
		bool image, sub;

		image = compose_w == IMX565_SUB_WIDTH &&
			compose_h == IMX565_SUB_IMAGE_HEIGHT;
		sub = compose_w <= IMX565_PIXEL_ARRAY_WIDTH / 2 &&
		      compose_h <= IMX565_PIXEL_ARRAY_HEIGHT / 2 &&
		      imx565->crop.width == IMX565_PIXEL_ARRAY_WIDTH &&
		      (imx565->crop.height == IMX565_PIXEL_ARRAY_HEIGHT ||
		       (image &&
			imx565->crop.height >= 2 * IMX565_SUB_IMAGE_HEIGHT));

		if (sub) {
			compose_w = IMX565_SUB_WIDTH;
			compose_h = IMX565_SUB_HEIGHT;
		} else {
			imx565_clamp_align(&compose_w, &compose_h,
					   imx565_min_width(imx565->fmt_code));
		}

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY) {
			imx565->subsample = sub;
			imx565->compose.left = 0;
			imx565->compose.top = 0;
			imx565->compose.width = compose_w;
			imx565->compose.height = compose_h;
			imx565->out_width = compose_w;
			imx565->out_height = compose_h;

			if (sub) {
				imx565->crop.left = 0;
				imx565->crop.top = 0;
				imx565->crop.width = IMX565_PIXEL_ARRAY_WIDTH;
				imx565->crop.height = IMX565_PIXEL_ARRAY_HEIGHT;
			} else {
				imx565->crop.width = compose_w;
				imx565->crop.height = compose_h;
				imx565_clamp_crop(&imx565->crop,
						  imx565_min_width(imx565->fmt_code));
			}

			imx565_set_framing_limits(imx565);
		}

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = compose_w;
		sel->r.height = sub && image ? IMX565_SUB_IMAGE_HEIGHT : compose_h;
	}

	mutex_unlock(&imx565->mutex);

	return 0;
}

static int imx565_start_streaming(struct imx565 *imx565)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	const struct imx565_reg_list *rate_list;
	int ret;

	ret = imx565_write_regs(imx565, mode_common_regs,
				ARRAY_SIZE(mode_common_regs));
	if (ret) {
		dev_err(&client->dev, "%s failed to set common settings\n",
			__func__);
		return ret;
	}

	rate_list = &rate_reg_lists[imx565->link_freq_idx];
	ret = imx565_write_regs(imx565, rate_list->regs,
				rate_list->num_of_regs);
	if (!ret) {
		const struct imx565_reg *depth_regs;
		unsigned int depth_len;

		switch (imx565_code_depth(imx565->fmt_code)) {
		case IMX565_DEPTH_10:
			depth_regs = depth_10bit_regs;
			depth_len = ARRAY_SIZE(depth_10bit_regs);
			break;
		case IMX565_DEPTH_8:
			depth_regs = depth_8bit_regs;
			depth_len = ARRAY_SIZE(depth_8bit_regs);
			break;
		default:
			depth_regs = depth_12bit_regs;
			depth_len = ARRAY_SIZE(depth_12bit_regs);
			break;
		}
		ret = imx565_write_regs(imx565, depth_regs, depth_len);
	}
	if (!ret)
		ret = imx565_write_reg(imx565, IMX565_REG_LANESEL, 1,
				       imx565->num_lanes == 2 ? 0x03 : 0x02);
	if (ret) {
		dev_err(&client->dev, "%s failed to set rate/depth/lanes\n",
			__func__);
		return ret;
	}

	ret = imx565_write_dynamic_regs(imx565);
	if (ret) {
		dev_err(&client->dev, "%s failed to set dynamic regs\n",
			__func__);
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(imx565->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = imx565_write_reg(imx565, IMX565_REG_STANDBY, 1, 0x00);
	if (ret)
		return ret;

	msleep(30);

	return imx565_write_reg(imx565, IMX565_REG_XMSTA, 1, 0x00);
}

static void imx565_stop_streaming(struct imx565 *imx565)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	int ret;

	ret = imx565_write_reg(imx565, IMX565_REG_STANDBY, 1, 0x01);
	if (!ret) {
		usleep_range(10000, 11000);
		ret = imx565_write_reg(imx565, IMX565_REG_XMSTA, 1, 0x01);
	}
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int imx565_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx565 *imx565 = to_imx565(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx565->mutex);
	if (imx565->streaming == enable) {
		mutex_unlock(&imx565->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = imx565_start_streaming(imx565);
		if (ret)
			goto err_rpm_put;
	} else {
		imx565_stop_streaming(imx565);
		pm_runtime_put(&client->dev);
	}

	imx565->streaming = enable;
	imx565->hblank_live_logged = false;

	__v4l2_ctrl_grab(imx565->vflip, enable);
	__v4l2_ctrl_grab(imx565->hflip, enable);

	mutex_unlock(&imx565->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx565->mutex);

	return ret;
}

static int imx565_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx565 *imx565 = to_imx565(sd);
	int ret;

	ret = regulator_bulk_enable(IMX565_NUM_SUPPLIES,
				    imx565->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	gpiod_set_value_cansleep(imx565->reset_gpio, 0);
	usleep_range(IMX565_XCLR_ASSERT_US, IMX565_XCLR_ASSERT_US * 2);

	ret = clk_prepare_enable(imx565->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx565->reset_gpio, 1);
	usleep_range(IMX565_XCLR_MIN_DELAY_US,
		     IMX565_XCLR_MIN_DELAY_US + IMX565_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	gpiod_set_value_cansleep(imx565->reset_gpio, 0);
	regulator_bulk_disable(IMX565_NUM_SUPPLIES, imx565->supplies);
	return ret;
}

static int imx565_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx565 *imx565 = to_imx565(sd);

	gpiod_set_value_cansleep(imx565->reset_gpio, 0);
	clk_disable_unprepare(imx565->xclk);
	regulator_bulk_disable(IMX565_NUM_SUPPLIES, imx565->supplies);

	return 0;
}

static int __maybe_unused imx565_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx565 *imx565 = to_imx565(sd);

	if (imx565->streaming)
		imx565_stop_streaming(imx565);

	return 0;
}

static int __maybe_unused imx565_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx565 *imx565 = to_imx565(sd);
	int ret;

	if (imx565->streaming) {
		ret = imx565_start_streaming(imx565);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx565_stop_streaming(imx565);
	imx565->streaming = 0;
	return ret;
}

static int imx565_get_regulators(struct imx565 *imx565)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	unsigned int i;

	for (i = 0; i < IMX565_NUM_SUPPLIES; i++)
		imx565->supplies[i].supply = imx565_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX565_NUM_SUPPLIES,
				       imx565->supplies);
}

static int imx565_identify_module(struct imx565 *imx565)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	int ret;
	u32 standby, hmax;

	ret = imx565_read_reg(imx565, IMX565_REG_STANDBY, 1, &standby);
	if (!ret)
		ret = imx565_read_reg(imx565, IMX565_REG_HMAX, 2, &hmax);
	if (ret) {
		dev_err(&client->dev,
			"failed to read sensor (POR probe), error %d\n", ret);
		return ret;
	}

	if (standby != IMX565_STANDBY_POR || hmax != IMX565_HMAX_POR) {
		if (imx565->reset_gpio)
			dev_warn(&client->dev,
				 "unexpected POR state 0x%x/0x%x (expected 0x%x/0x%x) after XCLR reset\n",
				 standby, hmax,
				 IMX565_STANDBY_POR, IMX565_HMAX_POR);
		else
			dev_info(&client->dev,
				 "sensor answered, not at POR (0x%x/0x%x); no reset line to assert, so this is expected on a warm re-probe\n",
				 standby, hmax);
	}

	dev_info(&client->dev, "IMX565 found on %s\n",
		 dev_name(&client->adapter->dev));

	return 0;
}

static const struct v4l2_subdev_core_ops imx565_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx565_video_ops = {
	.s_stream = imx565_set_stream,
};

static const struct v4l2_subdev_pad_ops imx565_pad_ops = {
	.enum_mbus_code = imx565_enum_mbus_code,
	.get_fmt = imx565_get_pad_format,
	.set_fmt = imx565_set_pad_format,
	.get_selection = imx565_get_selection,
	.set_selection = imx565_set_selection,
	.enum_frame_size = imx565_enum_frame_size,
};

static const struct v4l2_subdev_ops imx565_subdev_ops = {
	.core = &imx565_core_ops,
	.video = &imx565_video_ops,
	.pad = &imx565_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx565_internal_ops = {
	.open = imx565_open,
};

static int imx565_init_controls(struct imx565 *imx565)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx565->sd);
	struct v4l2_fwnode_device_properties props;
	unsigned int i;
	int ret;

	ctrl_hdlr = &imx565->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 17);
	if (ret)
		return ret;

	mutex_init(&imx565->mutex);
	ctrl_hdlr->lock = &imx565->mutex;

	imx565->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       1, INT_MAX, 1,
					       IMX565_INTERNAL_CLOCK);
	if (imx565->pixel_rate)
		imx565->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx565->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx565_ctrl_ops,
				       V4L2_CID_LINK_FREQ, 0, 0,
				       &link_freqs[imx565->link_freq_idx]);
	if (imx565->link_freq)
		imx565->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx565->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops,
					   V4L2_CID_VBLANK,
					   IMX565_VBLANK_OVERHEAD,
					   IMX565_VMAX_MAX, 1,
					   IMX565_VBLANK_OVERHEAD);
	imx565->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	imx565->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX565_EXPOSURE_MIN,
					     IMX565_VMAX_MAX - IMX565_SHS_OFFSET,
					     IMX565_EXPOSURE_STEP,
					     IMX565_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX565_ANA_GAIN_MIN, IMX565_ANA_GAIN_MAX,
			  IMX565_ANA_GAIN_STEP, IMX565_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops, V4L2_CID_BLACK_LEVEL,
			  0, IMX565_BLKLEVEL_MAX, 1, IMX565_BLKLEVEL_DEFAULT);

	imx565->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx565->hflip)
		imx565->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx565->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx565_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx565->vflip)
		imx565->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx565_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx565_test_pattern_menu) - 1,
				     0, 0, imx565_test_pattern_menu);

	for (i = 0; i < ARRAY_SIZE(imx565_ob_ctrls); i++)
		v4l2_ctrl_new_custom(ctrl_hdlr, &imx565_ob_ctrls[i], NULL);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx565_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx565->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&imx565->mutex);
	imx565_set_framing_limits(imx565);
	mutex_unlock(&imx565->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx565->mutex);

	return ret;
}

static void imx565_free_controls(struct imx565 *imx565)
{
	v4l2_ctrl_handler_free(imx565->sd.ctrl_handler);
	mutex_destroy(&imx565->mutex);
}

static int imx565_check_hwcfg(struct device *dev, struct imx565 *imx565)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;
	int i;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2 &&
	    ep_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(dev, "2 or 4 data lanes supported, got %d\n",
			ep_cfg.bus.mipi_csi2.num_data_lanes);
		goto error_out;
	}
	imx565->num_lanes = ep_cfg.bus.mipi_csi2.num_data_lanes;

	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	for (i = 0; i < ARRAY_SIZE(link_freqs); i++) {
		if (link_freqs[i] == ep_cfg.link_frequencies[0]) {
			imx565->link_freq_idx = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(link_freqs)) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
		goto error_out;
	}

	if (ep_cfg.nr_of_link_frequencies > 1)
		dev_warn(dev,
			 "DT lists %u link frequencies; only the first (%lld Hz) is used\n",
			 ep_cfg.nr_of_link_frequencies,
			 ep_cfg.link_frequencies[0]);

	dev_info(dev, "%u lanes @ %lld Hz = %llu Mbps/lane, %llu Mbps aggregate\n",
		 imx565->num_lanes, link_freqs[imx565->link_freq_idx],
		 div_u64(link_freqs[imx565->link_freq_idx] * 2, 1000000),
		 div_u64(link_freqs[imx565->link_freq_idx] * 2 *
			 imx565->num_lanes, 1000000));

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static const struct of_device_id imx565_dt_ids[] = {
	{ .compatible = "sony,imx565" },
	{  }
};

static int imx565_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx565 *imx565;
	int ret;

	imx565 = devm_kzalloc(&client->dev, sizeof(*imx565), GFP_KERNEL);
	if (!imx565)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx565->sd, client, &imx565_subdev_ops);

	if (imx565_check_hwcfg(dev, imx565))
		return -EINVAL;

	imx565->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx565->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx565->xclk);
	}

	imx565->xclk_freq = clk_get_rate(imx565->xclk);
	if (imx565->xclk_freq != 37125000) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx565->xclk_freq);
		return -EINVAL;
	}

	ret = imx565_get_regulators(imx565);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	imx565->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx565->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx565->reset_gpio),
				     "failed to get reset GPIO\n");
	if (!imx565->reset_gpio)
		dev_warn(dev,
			 "no reset-gpios in DT: XCLR is never asserted, so register state at probe is undefined and a wedged sensor needs a power cycle\n");

	ret = imx565_power_on(dev);
	if (ret)
		return ret;

	ret = imx565_identify_module(imx565);
	if (ret)
		goto error_power_off;

	imx565_set_default_format(imx565);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = imx565_init_controls(imx565);
	if (ret)
		goto error_pm;

	imx565->sd.internal_ops = &imx565_internal_ops;
	imx565->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx565->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx565->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx565->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx565->sd.entity, NUM_PADS, imx565->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx565->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx565->sd.entity);

error_handler_free:
	imx565_free_controls(imx565);

error_pm:
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx565_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return ret;

error_power_off:
	imx565_power_off(&client->dev);

	return ret;
}

static void imx565_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx565 *imx565 = to_imx565(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx565_free_controls(imx565);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx565_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

MODULE_DEVICE_TABLE(of, imx565_dt_ids);

static const struct dev_pm_ops imx565_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx565_suspend, imx565_resume)
	SET_RUNTIME_PM_OPS(imx565_power_off, imx565_power_on, NULL)
};

static struct i2c_driver imx565_i2c_driver = {
	.driver = {
		.name = "imx565",
		.of_match_table	= imx565_dt_ids,
		.pm = &imx565_pm_ops,
	},
	.probe = imx565_probe,
	.remove = imx565_remove,
};

module_i2c_driver(imx565_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("Sony IMX565 sensor driver");
MODULE_LICENSE("GPL");
