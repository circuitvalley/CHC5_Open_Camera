// SPDX-License-Identifier: GPL-2.0-only
/*
 * OmniVision OX08B40 sensor driver
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
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include <uapi/linux/chc5-v4l2-controls.h>

#define OX08B40_REG_MODE_SELECT		CCI_REG8(0x0100)
#define OX08B40_MODE_STANDBY		0x00
#define OX08B40_MODE_STREAMING		0x01

#define OX08B40_REG_SOFTWARE_RST	CCI_REG8(0x0103)
#define OX08B40_SOFTWARE_RST		0x01

#define OX08B40_REG_CHIP_ID		CCI_REG24(0x300a)
#define OX08B40_CHIP_ID			0x580841
#define OX08B40_REG_SILICON_REV		CCI_REG8(0x302a)

#define OX08B40_REG_GROUP_ACCESS	CCI_REG8(0x3208)
#define OX08B40_GROUP_HOLD_START	0x00
#define OX08B40_GROUP_HOLD_END		0x10
#define OX08B40_GROUP_LAUNCH_VBLANK	0xa0

#define OX08B40_REG_EXP_DCG		CCI_REG16(0x3501)
#define OX08B40_REG_AGAIN_HCG		CCI_REG16(0x3508)
#define OX08B40_REG_DGAIN_HCG		CCI_REG24(0x350a)
#define OX08B40_REG_EXP_SPD		CCI_REG16(0x3541)
#define OX08B40_REG_AGAIN_SPD		CCI_REG16(0x3548)
#define OX08B40_REG_DGAIN_SPD		CCI_REG24(0x354a)
#define OX08B40_REG_AGAIN_LCG		CCI_REG16(0x3588)
#define OX08B40_REG_DGAIN_LCG		CCI_REG24(0x358a)
#define OX08B40_REG_EXP_VS		CCI_REG16(0x35c1)
#define OX08B40_REG_AGAIN_VS		CCI_REG16(0x35c8)
#define OX08B40_REG_DGAIN_VS		CCI_REG24(0x35ca)

#define OX08B40_REG_HTS_DCG		CCI_REG16(0x380c)
#define OX08B40_REG_VTS			CCI_REG16(0x380e)
#define OX08B40_REG_TIMING_CTRL20	CCI_REG8(0x3820)
#define OX08B40_MIRROR_MASK		BIT(5)
#define OX08B40_FLIP_MASK		BIT(2)

#define OX08B40_REG_UID_LATE_CTRL0	CCI_REG8(0x4640)
#define OX08B40_UID_MIRROR		BIT(0)
#define OX08B40_UID_VFLIP		BIT(1)

#define OX08B40_REG_EMB_CTRL		CCI_REG8(0x4317)
#define OX08B40_EMB_FRONT_EN		BIT(5)
#define OX08B40_EMB_END_EN		BIT(4)

#define OX08B40_REG_PRE_ISP_HCG		CCI_REG8(0x5240)
#define OX08B40_REG_PRE_ISP_LCG		CCI_REG8(0x5440)
#define OX08B40_REG_PRE_ISP_SPD		CCI_REG8(0x5640)
#define OX08B40_REG_PRE_ISP_VS		CCI_REG8(0x5840)
#define OX08B40_TPAT_EN			BIT(0)
#define OX08B40_TPAT_STYLE_SHIFT	4

#define OX08B40_XCLK_FREQ		24000000
#define OX08B40_LINK_FREQ		480000000ULL
#define OX08B40_DATA_LANES		4

#define OX08B40_PIXEL_RATE		320000000ULL

#define OX08B40_REG_X_ADDR_START	CCI_REG16(0x3800)
#define OX08B40_REG_Y_ADDR_START	CCI_REG16(0x3802)
#define OX08B40_REG_X_ADDR_END		CCI_REG16(0x3804)
#define OX08B40_REG_Y_ADDR_END		CCI_REG16(0x3806)
#define OX08B40_REG_X_OUTPUT_SIZE	CCI_REG16(0x3808)
#define OX08B40_REG_Y_OUTPUT_SIZE	CCI_REG16(0x380a)
#define OX08B40_REG_ISP_X_WIN_TRUNC	CCI_REG16(0x3810)

#define OX08B40_REG_HTS_DCG		CCI_REG16(0x380c)
#define OX08B40_HTS_DCG_BASE		1536U

#define OX08B40_HTS_SPD_FIXED		592U
#define OX08B40_HTS_VS_FIXED		592U
#define OX08B40_HTS_SUM_MAX		0xFFFFU
#define OX08B40_HTS_DCG_MAX		(OX08B40_HTS_SUM_MAX - \
					 OX08B40_HTS_SPD_FIXED - \
					 OX08B40_HTS_VS_FIXED)
#define OX08B40_HBLANK_PX_PER_HTS_X1000	2056U
#define OX08B40_HBLANK_STEP		2U

#define OX08B40_ISP_X_TRUNC		8U
#define OX08B40_ISP_Y_TRUNC		4U
#define OX08B40_ISP_X_MARGIN_R		24U
#define OX08B40_ISP_Y_MARGIN_B		4U

#define OX08B40_TAIL_CUT_WIDTH		3584U

static u32 ox08b40_tail_cut(u32 width)
{
	if (!width || width >= OX08B40_TAIL_CUT_WIDTH)
		return 0;
	return 2 * (DIV_ROUND_UP(OX08B40_TAIL_CUT_WIDTH, width) - 1);
}

#define OX08B40_OB_TOP			2U
#define OX08B40_OB_BOTTOM		4U

#define OX08B40_WIDTH_ALIGN		16U

#define OX08B40_HEIGHT_ALIGN		2U
#define OX08B40_OFFSET_ALIGN		8U

#define OX08B40_MIN_WIDTH		608U
#define OX08B40_MIN_HEIGHT		472U

#define OX08B40_NATIVE_WIDTH		3856U
#define OX08B40_NATIVE_HEIGHT		2176U
#define OX08B40_PIXEL_ARRAY_LEFT	8U
#define OX08B40_PIXEL_ARRAY_TOP		8U
#define OX08B40_PIXEL_ARRAY_WIDTH	3840U
#define OX08B40_PIXEL_ARRAY_HEIGHT	2160U

#define OX08B40_LINE_LENGTH		5593U

#define OX08B40_FRAME_LENGTH_DEFAULT	2256U
#define OX08B40_FRAME_LENGTH_MAX	(0xffffU * 2U)

#define OX08B40_VS_EXPOSURE_MAX		35U
#define OX08B40_EXPOSURE_MARGIN		((OX08B40_VS_EXPOSURE_MAX + 13U) * 2U)

#define OX08B40_VBLANK_MIN		(OX08B40_EXPOSURE_MARGIN - \
					 OX08B40_OB_TOP - OX08B40_OB_BOTTOM)
#define OX08B40_EXPOSURE_MIN		4U
#define OX08B40_EXPOSURE_STEP		2U
#define OX08B40_EXPOSURE_DEFAULT	1856U

#define OX08B40_DCG_VS_RATIO		186U

enum ox08b40_capture {
	OX08B40_CAP_HCG,
	OX08B40_CAP_LCG,
	OX08B40_CAP_SPD,
	OX08B40_CAP_VS,
	OX08B40_NUM_CAPTURES,
};

static const u32 ox08b40_dgain_reg[OX08B40_NUM_CAPTURES] = {
	[OX08B40_CAP_HCG] = OX08B40_REG_DGAIN_HCG,
	[OX08B40_CAP_LCG] = OX08B40_REG_DGAIN_LCG,
	[OX08B40_CAP_SPD] = OX08B40_REG_DGAIN_SPD,
	[OX08B40_CAP_VS]  = OX08B40_REG_DGAIN_VS,
};

#define OX08B40_ANA_GAIN_MIN		0
#define OX08B40_ANA_GAIN_MAX		238
#define OX08B40_ANA_GAIN_STEP		1
#define OX08B40_ANA_GAIN_DEFAULT	0

static const u16 ox08b40_gain_q10[OX08B40_ANA_GAIN_MAX + 1] = {
	  1024,  1036,  1048,  1060,  1072,  1085,  1097,  1110,
	  1123,  1136,  1149,  1162,  1176,  1189,  1203,  1217,
	  1231,  1245,  1260,  1274,  1289,  1304,  1319,  1334,
	  1350,  1366,  1381,  1397,  1414,  1430,  1446,  1463,
	  1480,  1497,  1515,  1532,  1550,  1568,  1586,  1604,
	  1623,  1642,  1661,  1680,  1699,  1719,  1739,  1759,
	  1780,  1800,  1821,  1842,  1863,  1885,  1907,  1929,
	  1951,  1974,  1997,  2020,  2043,  2067,  2091,  2115,
	  2139,  2164,  2189,  2215,  2240,  2266,  2292,  2319,
	  2346,  2373,  2400,  2428,  2456,  2485,  2514,  2543,
	  2572,  2602,  2632,  2663,  2693,  2725,  2756,  2788,
	  2820,  2853,  2886,  2919,  2953,  2987,  3022,  3057,
	  3092,  3128,  3164,  3201,  3238,  3276,  3314,  3352,
	  3391,  3430,  3470,  3510,  3551,  3592,  3633,  3675,
	  3718,  3761,  3805,  3849,  3893,  3938,  3984,  4030,
	  4077,  4124,  4172,  4220,  4269,  4318,  4368,  4419,
	  4470,  4522,  4574,  4627,  4681,  4735,  4790,  4845,
	  4901,  4958,  5015,  5073,  5132,  5192,  5252,  5313,
	  5374,  5436,  5499,  5563,  5627,  5692,  5758,  5825,
	  5893,  5961,  6030,  6100,  6170,  6242,  6314,  6387,
	  6461,  6536,  6611,  6688,  6766,  6844,  6923,  7003,
	  7084,  7166,  7249,  7333,  7418,  7504,  7591,  7679,
	  7768,  7858,  7949,  8041,  8134,  8228,  8323,  8420,
	  8517,  8616,  8716,  8817,  8919,  9022,  9126,  9232,
	  9339,  9447,  9557,  9667,  9779,  9892, 10007, 10123,
	 10240, 10359, 10479, 10600, 10723, 10847, 10972, 11099,
	 11228, 11358, 11489, 11623, 11757, 11893, 12031, 12170,
	 12311, 12454, 12598, 12744, 12891, 13041, 13192, 13344,
	 13499, 13655, 13813, 13973, 14135, 14299, 14464, 14632,
	 14801, 14973, 15146, 15321, 15499, 15678, 15860,
};

static unsigned int ox08b40_gain_factor(unsigned int db10)
{
	if (db10 > OX08B40_ANA_GAIN_MAX)
		db10 = OX08B40_ANA_GAIN_MAX;

	return ox08b40_gain_q10[db10];
}

#define OX08B40_BLACK_LEVEL		64

#define OX08B40_REG_PWL_CURVE_FIRST	0x5e00
#define OX08B40_PWL_CURVE_LEN		CHC5SENSOR_PWL_CURVE_LEN

#define OX08B40_REG_FORMAT_1F		CCI_REG8(0x431f)
#define OX08B40_PWL0_EN			BIT(5)
#define OX08B40_PWL0_BITS_MASK		GENMASK(4, 3)
#define OX08B40_PWL0_BITS_SHIFT		3

#define OX08B40_DGAIN_MIN		0x0400
#define OX08B40_DGAIN_MAX		0x3fff

#define OX08B40_EMBEDDED_LINE_WIDTH	16384
#define OX08B40_NUM_EMBEDDED_LINES	1

#include "ox08b40_mode_3840x2160.h"

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

struct ox08b40_reg_list {
	unsigned int num_of_regs;
	const struct cci_reg_sequence *regs;
};

struct ox08b40_mode {
	unsigned int width;
	unsigned int height;
	struct v4l2_rect crop;
	unsigned int frm_length_default;
	struct ox08b40_reg_list reg_list;
	u16 dgain_base[OX08B40_NUM_CAPTURES];
};

static const struct ox08b40_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.crop = {
			.left = OX08B40_PIXEL_ARRAY_LEFT,
			.top = OX08B40_PIXEL_ARRAY_TOP,
			.width = 3840,
			.height = 2160,
		},
		.frm_length_default = OX08B40_FRAME_LENGTH_DEFAULT,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(ox08b40_mode_3840x2160_regs),
			.regs = ox08b40_mode_3840x2160_regs,
		},
		.dgain_base = {
			[OX08B40_CAP_HCG] = 0x0400,
			[OX08B40_CAP_LCG] = 0x0414,
			[OX08B40_CAP_SPD] = 0x0400,
			[OX08B40_CAP_VS]  = 0x0414,
		},
	},
};

static const char * const ox08b40_test_pattern_menu[] = {
	"Disabled",
	"Colour Bar Type 1",
	"Colour Bar Type 2",
	"Colour Bar Type 3",
	"Colour Bar Type 4",
};

static const char * const ox08b40_supply_name[] = {
	"avdd",
	"dovdd",
	"dvdd",
};

#define OX08B40_NUM_SUPPLIES ARRAY_SIZE(ox08b40_supply_name)

static const s64 ox08b40_link_freq_menu[] = {
	OX08B40_LINK_FREQ,
};

struct ox08b40 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	struct clk *xclk;
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[OX08B40_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *black_level;

	unsigned long group_launch_deadline;

	const struct ox08b40_mode *mode;

	struct v4l2_rect crop;

	struct mutex mutex;

	bool streaming;
};

static inline struct ox08b40 *to_ox08b40(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct ox08b40, sd);
}

static u32 ox08b40_get_format_code(struct ox08b40 *ox08b40)
{
	if (ox08b40->vflip && ox08b40->vflip->val)
		return MEDIA_BUS_FMT_SGRBG12_1X12;

	return MEDIA_BUS_FMT_SBGGR12_1X12;
}

static int ox08b40_read(struct ox08b40 *ox08b40, u32 reg, u64 *val, int *err)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	unsigned int len = CCI_REG_WIDTH_BYTES(reg);
	u8 addr_buf[2], data_buf[4] = { 0 };
	struct i2c_msg msgs[2];
	unsigned int i;
	u64 v = 0;
	int ret;

	if (err && *err)
		return *err;

	if (len > sizeof(data_buf)) {
		ret = -EINVAL;
		goto out;
	}

	put_unaligned_be16(CCI_REG_ADDR(reg), addr_buf);

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err_ratelimited(&client->dev,
				    "failed to read reg 0x%04x: %d\n",
				    (u16)CCI_REG_ADDR(reg), ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	for (i = 0; i < len; i++)
		v = (v << 8) | data_buf[i];

	*val = v;
	ret = 0;

out:
	if (err)
		*err = ret;

	return ret;
}

static int ox08b40_write(struct ox08b40 *ox08b40, u32 reg, u64 val, int *err)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	unsigned int len = CCI_REG_WIDTH_BYTES(reg);
	u8 buf[2 + 4];
	unsigned int i;
	int ret;

	if (err && *err)
		return *err;

	if (len > 4) {
		ret = -EINVAL;
		goto out;
	}

	put_unaligned_be16(CCI_REG_ADDR(reg), buf);
	for (i = 0; i < len; i++)
		buf[2 + i] = val >> (8 * (len - 1 - i));

	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2) {
		dev_err_ratelimited(&client->dev,
				    "failed to write reg 0x%04x: %d\n",
				    (u16)CCI_REG_ADDR(reg), ret);
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	ret = 0;

out:
	if (err)
		*err = ret;

	return ret;
}

static int ox08b40_write_regs(struct ox08b40 *ox08b40,
			      const struct cci_reg_sequence *regs,
			      unsigned int num, int *err)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < num; i++) {
		ret = ox08b40_write(ox08b40, regs[i].reg, regs[i].val, err);
		if (ret)
			return ret;
	}

	return ret;
}

static unsigned int ox08b40_wire_height(const struct ox08b40 *ox08b40)
{
	return ox08b40->crop.height + OX08B40_OB_TOP + OX08B40_OB_BOTTOM;
}

static int ox08b40_set_frame_length(struct ox08b40 *ox08b40, unsigned int lines)
{
	int ret = 0;

	ox08b40_write(ox08b40, OX08B40_REG_VTS, lines / 2, &ret);

	return ret;
}

static int ox08b40_set_exposure(struct ox08b40 *ox08b40, unsigned int lines)
{
	unsigned int dcg = lines / 2;
	unsigned int vs;
	int ret = 0;

	vs = clamp((dcg + OX08B40_DCG_VS_RATIO / 2) / OX08B40_DCG_VS_RATIO,
		   2U, OX08B40_VS_EXPOSURE_MAX);

	ox08b40_write(ox08b40, OX08B40_REG_EXP_DCG, dcg, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_EXP_SPD, dcg, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_EXP_VS, vs, &ret);

	return ret;
}

static int ox08b40_set_analogue_gain(struct ox08b40 *ox08b40, u32 db10)
{
	u32 factor = ox08b40_gain_factor(db10);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < OX08B40_NUM_CAPTURES; i++) {
		u32 gain = ((u32)ox08b40->mode->dgain_base[i] * factor) >> 10;

		gain = clamp(gain, (u32)OX08B40_DGAIN_MIN,
			     (u32)OX08B40_DGAIN_MAX);

		ox08b40_write(ox08b40, ox08b40_dgain_reg[i], gain << 6, &ret);
	}

	return ret;
}

static void ox08b40_clamp_crop(struct v4l2_rect *r)
{
	u32 w, h, l, t;

	w = clamp_t(u32, ALIGN_DOWN((u32)r->width, OX08B40_WIDTH_ALIGN),
		    OX08B40_MIN_WIDTH, OX08B40_PIXEL_ARRAY_WIDTH);
	h = clamp_t(u32, ALIGN_DOWN((u32)r->height, OX08B40_HEIGHT_ALIGN),
		    OX08B40_MIN_HEIGHT, OX08B40_PIXEL_ARRAY_HEIGHT);

	l = clamp_t(u32, ALIGN_DOWN((u32)r->left, OX08B40_OFFSET_ALIGN),
		    OX08B40_PIXEL_ARRAY_LEFT,
		    OX08B40_PIXEL_ARRAY_LEFT + OX08B40_PIXEL_ARRAY_WIDTH - w);
	t = clamp_t(u32, ALIGN_DOWN((u32)r->top, OX08B40_OFFSET_ALIGN),
		    OX08B40_PIXEL_ARRAY_TOP,
		    OX08B40_PIXEL_ARRAY_TOP + OX08B40_PIXEL_ARRAY_HEIGHT - h);

	r->left = l;
	r->top = t;
	r->width = w;
	r->height = h;
}

#define OX08B40_BAD_PHASE_PERIOD	256U
#define OX08B40_BAD_PHASE_FIRST		224U
#define OX08B40_SAFE_PHASE		216U

static u32 ox08b40_x_trunc_for(u32 left)
{
	u32 trunc = OX08B40_ISP_X_TRUNC;
	u32 phase = (left - trunc) % OX08B40_BAD_PHASE_PERIOD;

	if (phase >= OX08B40_BAD_PHASE_FIRST)
		trunc += phase - OX08B40_SAFE_PHASE;

	return trunc;
}

static int ox08b40_program_window(struct ox08b40 *ox08b40)
{
	const struct v4l2_rect *c = &ox08b40->crop;
	u32 cut = ox08b40_tail_cut((u32)c->width);
	u32 x_trunc = ox08b40_x_trunc_for((u32)c->left);
	u32 x_start = (u32)c->left - x_trunc;
	u32 y_start = (u32)c->top - OX08B40_ISP_Y_TRUNC;
	u32 x_end = x_start + x_trunc + (u32)c->width +
		    OX08B40_ISP_X_MARGIN_R - 1;
	u32 y_end = y_start + OX08B40_ISP_Y_TRUNC + (u32)c->height + cut +
		    OX08B40_ISP_Y_MARGIN_B - 1;
	int ret = 0;

	if (y_end > OX08B40_NATIVE_HEIGHT - 1) {
		u32 over = y_end - (OX08B40_NATIVE_HEIGHT - 1);
		u32 margin = min(over, OX08B40_ISP_Y_MARGIN_B);

		y_end -= margin;
		over -= margin;
		over = min(over, y_start);
		y_start -= over;
		y_end -= over;
	}

	ox08b40_write(ox08b40, OX08B40_REG_ISP_X_WIN_TRUNC, x_trunc, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_X_ADDR_START, x_start, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_Y_ADDR_START, y_start, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_X_ADDR_END, x_end, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_Y_ADDR_END, y_end, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_X_OUTPUT_SIZE, c->width, &ret);

	ox08b40_write(ox08b40, OX08B40_REG_Y_OUTPUT_SIZE, (u32)c->height + cut,
		      &ret);

	return ret;
}

static int ox08b40_set_hblank(struct ox08b40 *ox08b40, u32 hblank)
{
	u32 base = OX08B40_LINE_LENGTH - (u32)ox08b40->crop.width;
	u32 extra = (hblank > base) ? (hblank - base) : 0;
	u32 hts = OX08B40_HTS_DCG_BASE +
		  (extra * 1000 + OX08B40_HBLANK_PX_PER_HTS_X1000 / 2) /
		  OX08B40_HBLANK_PX_PER_HTS_X1000;
	int ret = 0;

	if (hts > OX08B40_HTS_DCG_MAX)
		hts = OX08B40_HTS_DCG_MAX;

	ox08b40_write(ox08b40, OX08B40_REG_HTS_DCG, hts, &ret);

	return ret;
}

static int ox08b40_set_orientation(struct ox08b40 *ox08b40)
{
	u64 uid;
	u8 val = 0;
	int ret = 0;

	if (ox08b40->hflip->val)
		val |= OX08B40_MIRROR_MASK;
	if (ox08b40->vflip->val)
		val |= OX08B40_FLIP_MASK;

	ret = ox08b40_read(ox08b40, OX08B40_REG_UID_LATE_CTRL0, &uid, NULL);
	if (ret)
		return ret;

	uid &= ~(OX08B40_UID_MIRROR | OX08B40_UID_VFLIP);
	if (ox08b40->hflip->val)
		uid |= OX08B40_UID_MIRROR;
	if (ox08b40->vflip->val)
		uid |= OX08B40_UID_VFLIP;

	ox08b40_write(ox08b40, OX08B40_REG_TIMING_CTRL20, val, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_UID_LATE_CTRL0, uid, &ret);

	return ret;
}

static int ox08b40_set_test_pattern(struct ox08b40 *ox08b40, u32 pattern)
{
	static const u32 chans[] = {
		OX08B40_REG_PRE_ISP_HCG, OX08B40_REG_PRE_ISP_LCG,
		OX08B40_REG_PRE_ISP_SPD, OX08B40_REG_PRE_ISP_VS,
	};
	unsigned int i;
	u8 val = 0;
	int ret = 0;

	if (pattern)
		val = ((pattern - 1) << OX08B40_TPAT_STYLE_SHIFT) |
		      OX08B40_TPAT_EN;

	for (i = 0; i < ARRAY_SIZE(chans); i++)
		ox08b40_write(ox08b40, chans[i], val, &ret);

	return ret;
}

static unsigned int ox08b40_frame_period_ms(struct ox08b40 *ox08b40)
{
	u64 lines = ox08b40_wire_height(ox08b40) + ox08b40->vblank->cur.val;
	u64 pixels = lines * (ox08b40->crop.width + ox08b40->hblank->cur.val);

	return (unsigned int)div_u64(pixels * 1000, OX08B40_PIXEL_RATE) + 1;
}

static void ox08b40_group_wait_launched(struct ox08b40 *ox08b40)
{
	if (ox08b40->group_launch_deadline &&
	    time_before(jiffies, ox08b40->group_launch_deadline))
		msleep(jiffies_to_msecs(ox08b40->group_launch_deadline - jiffies) + 1);
}

static void ox08b40_group_launched(struct ox08b40 *ox08b40)
{
	ox08b40->group_launch_deadline =
		jiffies + msecs_to_jiffies(ox08b40_frame_period_ms(ox08b40));
}

static int ox08b40_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ox08b40 *ox08b40 =
		container_of(ctrl->handler, struct ox08b40, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		int exposure_max, exposure_def;

		exposure_max = OX08B40_FRAME_LENGTH_MAX - OX08B40_EXPOSURE_MARGIN;
		exposure_def = min_t(int, exposure_max,
				     OX08B40_EXPOSURE_DEFAULT);
		__v4l2_ctrl_modify_range(ox08b40->exposure,
					 ox08b40->exposure->minimum,
					 exposure_max,
					 ox08b40->exposure->step,
					 exposure_def);
	}

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	if (ctrl->id == V4L2_CID_HFLIP || ctrl->id == V4L2_CID_VFLIP) {
		ret = ox08b40_set_orientation(ox08b40);
		pm_runtime_put(&client->dev);
		return ret;
	}

	if (ctrl->id == V4L2_CID_HBLANK) {
		ret = ox08b40_set_hblank(ox08b40, ctrl->val);
		pm_runtime_put(&client->dev);
		return ret;
	}

	ox08b40_group_wait_launched(ox08b40);
	ox08b40_write(ox08b40, OX08B40_REG_GROUP_ACCESS,
		  OX08B40_GROUP_HOLD_START, &ret);

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (ox08b40->exposure->is_new)
			ret = ox08b40_set_exposure(ox08b40,
				min_t(int, ox08b40->exposure->val,
				      ox08b40_wire_height(ox08b40) +
				      ox08b40->vblank->val -
				      OX08B40_EXPOSURE_MARGIN));
		if (!ret && ox08b40->gain->is_new)
			ret = ox08b40_set_analogue_gain(ox08b40,
							ox08b40->gain->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ox08b40_set_frame_length(ox08b40,
					       ox08b40_wire_height(ox08b40) +
					       ctrl->val);
		if (!ret && ox08b40->exposure->cur.val >
			    ox08b40_wire_height(ox08b40) + ctrl->val -
			    OX08B40_EXPOSURE_MARGIN)
			ret = ox08b40_set_exposure(ox08b40,
				ox08b40_wire_height(ox08b40) + ctrl->val -
				OX08B40_EXPOSURE_MARGIN);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ox08b40_set_test_pattern(ox08b40, ctrl->val);
		break;
	case V4L2_CID_BLACK_LEVEL:
		break;
	default:
		dev_info(&client->dev, "ctrl(id:0x%x, val:0x%x) not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	ox08b40_write(ox08b40, OX08B40_REG_GROUP_ACCESS,
		  OX08B40_GROUP_HOLD_END, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_GROUP_ACCESS,
		  OX08B40_GROUP_LAUNCH_VBLANK, &ret);
	ox08b40_group_launched(ox08b40);

	pm_runtime_put(&client->dev);

	return ret;
}

static int ox08b40_read_pwl_curve(struct v4l2_ctrl *ctrl)
{
	struct ox08b40 *ox08b40 =
		container_of(ctrl->handler, struct ox08b40, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	u8 *out = ctrl->p_new.p_u8;
	unsigned int i;
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < OX08B40_PWL_CURVE_LEN; i++) {
		u64 v;

		ret = ox08b40_read(ox08b40,
				   CCI_REG8(OX08B40_REG_PWL_CURVE_FIRST + i),
				   &v, NULL);
		if (ret) {
			dev_err(&client->dev,
				"PWL curve read failed at 0x%04x: %d\n",
				OX08B40_REG_PWL_CURVE_FIRST + i, ret);
			goto out;
		}
		out[i] = (u8)v;
	}

out:
	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);
	return ret;
}

static int ox08b40_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case CHC5SENSOR_CID_OB_TOP:
		ctrl->val = OX08B40_OB_TOP;
		return 0;
	case CHC5SENSOR_CID_OB_BOTTOM:
		ctrl->val = OX08B40_OB_BOTTOM;
		return 0;
	case CHC5SENSOR_CID_OB_ON_IMAGE_DT:
		ctrl->val = 1;
		return 0;
	case CHC5SENSOR_CID_OB_LEFT:
	case CHC5SENSOR_CID_OB_RIGHT:
		ctrl->val = 0;
		return 0;
	case CHC5SENSOR_CID_PWL_CURVE:
		return ox08b40_read_pwl_curve(ctrl);
	}

	return -EINVAL;
}

#define OX08B40_OB_CTRL(_id, _name)					\
	{								\
		.ops	= &ox08b40_ctrl_ops,				\
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

static const struct v4l2_ctrl_ops ox08b40_ctrl_ops = {
	.s_ctrl = ox08b40_set_ctrl,
	.g_volatile_ctrl = ox08b40_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config ox08b40_ob_ctrls[] = {
	OX08B40_OB_CTRL(CHC5SENSOR_CID_OB_LEFT,   "Optical Black Left"),
	OX08B40_OB_CTRL(CHC5SENSOR_CID_OB_TOP,    "Optical Black Top"),
	OX08B40_OB_CTRL(CHC5SENSOR_CID_OB_RIGHT,  "Optical Black Right"),
	OX08B40_OB_CTRL(CHC5SENSOR_CID_OB_BOTTOM, "Optical Black Bottom"),
	OX08B40_OB_CTRL(CHC5SENSOR_CID_OB_ON_IMAGE_DT, "Optical Black On Image DT"),
	{
		.ops	= &ox08b40_ctrl_ops,
		.id	= CHC5SENSOR_CID_PWL_CURVE,
		.name	= "PWL Curve Registers",
		.type	= V4L2_CTRL_TYPE_U8,
		.min	= 0,
		.max	= 0xFF,
		.step	= 1,
		.def	= 0,
		.dims	= { OX08B40_PWL_CURVE_LEN },
		.flags	= V4L2_CTRL_FLAG_READ_ONLY |
			  V4L2_CTRL_FLAG_VOLATILE,
	},
};

static int ox08b40_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (code->index > 0)
			return -EINVAL;
		code->code = ox08b40_get_format_code(to_ox08b40(sd));
	} else {
		if (code->index > 0)
			return -EINVAL;
		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int ox08b40_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index > 0 ||
		    fse->code != ox08b40_get_format_code(to_ox08b40(sd)))
			return -EINVAL;

		fse->min_width = OX08B40_MIN_WIDTH;
		fse->max_width = OX08B40_PIXEL_ARRAY_WIDTH;
		fse->min_height = OX08B40_MIN_HEIGHT;
		fse->max_height = OX08B40_PIXEL_ARRAY_HEIGHT;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = OX08B40_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = OX08B40_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void ox08b40_set_framing_limits(struct ox08b40 *ox08b40);

static void ox08b40_update_image_pad_format(struct ox08b40 *ox08b40,
					    u32 width, u32 height,
					    struct v4l2_subdev_format *fmt)
{
	fmt->format.width = width;
	fmt->format.height = height + OX08B40_OB_TOP + OX08B40_OB_BOTTOM;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.code = ox08b40_get_format_code(ox08b40);
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;
}

static void ox08b40_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = OX08B40_EMBEDDED_LINE_WIDTH;
	fmt->format.height = OX08B40_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int ox08b40_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct ox08b40 *ox08b40 = to_ox08b40(sd);
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;
	struct v4l2_subdev_format sdfmt = { 0 };

	ox08b40_update_image_pad_format(ox08b40, supported_modes[0].width,
					supported_modes[0].height, &sdfmt);
	fmt = v4l2_subdev_state_get_format(state, IMAGE_PAD);
	*fmt = sdfmt.format;

	memset(&sdfmt, 0, sizeof(sdfmt));
	ox08b40_update_metadata_pad_format(&sdfmt);
	fmt = v4l2_subdev_state_get_format(state, METADATA_PAD);
	*fmt = sdfmt.format;

	crop = v4l2_subdev_state_get_crop(state, IMAGE_PAD);
	*crop = supported_modes[0].crop;

	return 0;
}

static int ox08b40_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct ox08b40 *ox08b40 = to_ox08b40(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&ox08b40->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state, fmt->pad);
	else if (fmt->pad == IMAGE_PAD)
		ox08b40_update_image_pad_format(ox08b40, ox08b40->crop.width,
						ox08b40->crop.height, fmt);
	else
		ox08b40_update_metadata_pad_format(fmt);

	mutex_unlock(&ox08b40->mutex);

	return 0;
}

static int ox08b40_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct ox08b40 *ox08b40 = to_ox08b40(sd);
	struct v4l2_mbus_framefmt *framefmt;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&ox08b40->mutex);

	if (fmt->pad == IMAGE_PAD) {
		struct v4l2_rect r;

		{
		const struct v4l2_rect *cur =
			(fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
				? &ox08b40->crop
				: v4l2_subdev_state_get_crop(sd_state, IMAGE_PAD);

		r.width = fmt->format.width;
		r.height = fmt->format.height;
		r.left = cur ? cur->left : OX08B40_PIXEL_ARRAY_LEFT;
		r.top  = cur ? cur->top  : OX08B40_PIXEL_ARRAY_TOP;
		ox08b40_clamp_crop(&r);

		if (!cur || cur->width != r.width || cur->height != r.height) {
			r.left = OX08B40_PIXEL_ARRAY_LEFT +
				 ((OX08B40_PIXEL_ARRAY_WIDTH - r.width) / 2 & ~1U);
			r.top = OX08B40_PIXEL_ARRAY_TOP +
				((OX08B40_PIXEL_ARRAY_HEIGHT - r.height) / 2 & ~1U);
		}
		}

		ox08b40_update_image_pad_format(ox08b40, r.width, r.height, fmt);

		*v4l2_subdev_state_get_crop(sd_state, IMAGE_PAD) = r;

		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			ox08b40->crop = r;
			ox08b40_set_framing_limits(ox08b40);
		}
	} else {
		ox08b40_update_metadata_pad_format(fmt);
	}

	framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	*framefmt = fmt->format;

	mutex_unlock(&ox08b40->mutex);

	return 0;
}

static int ox08b40_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_selection *sel)
{
	struct ox08b40 *ox08b40 = to_ox08b40(sd);

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			mutex_lock(&ox08b40->mutex);
			sel->r = ox08b40->crop;
			mutex_unlock(&ox08b40->mutex);
		} else {
			sel->r = *v4l2_subdev_state_get_crop(sd_state,
							     IMAGE_PAD);
		}
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = OX08B40_NATIVE_WIDTH;
		sel->r.height = OX08B40_NATIVE_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = OX08B40_PIXEL_ARRAY_LEFT;
		sel->r.top = OX08B40_PIXEL_ARRAY_TOP;
		sel->r.width = OX08B40_PIXEL_ARRAY_WIDTH;
		sel->r.height = OX08B40_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int ox08b40_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_selection *sel)
{
	struct ox08b40 *ox08b40 = to_ox08b40(sd);
	struct v4l2_mbus_framefmt *framefmt;
	struct v4l2_rect r;

	if (sel->pad != IMAGE_PAD || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	r = sel->r;
	ox08b40_clamp_crop(&r);

	mutex_lock(&ox08b40->mutex);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE && ox08b40->streaming) {
		mutex_unlock(&ox08b40->mutex);
		return -EBUSY;
	}

	*v4l2_subdev_state_get_crop(sd_state, IMAGE_PAD) = r;

	framefmt = v4l2_subdev_state_get_format(sd_state, IMAGE_PAD);
	framefmt->width = r.width;
	framefmt->height = r.height;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ox08b40->crop = r;
		ox08b40_set_framing_limits(ox08b40);
	}

	mutex_unlock(&ox08b40->mutex);

	sel->r = r;
	return 0;
}

static void ox08b40_report_companding(struct ox08b40 *ox08b40)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	static const char * const pwl_bits[] = { "12", "14", "16", "20" };
	u64 fmt;

	if (ox08b40_read(ox08b40, OX08B40_REG_FORMAT_1F, &fmt, NULL))
		return;

	if (!(fmt & OX08B40_PWL0_EN)) {
		dev_info(&client->dev,
			 "PWL disabled: output is uncompanded (linear) 24-bit\n");
		return;
	}

	dev_info(&client->dev,
		 "PWL enabled: 24-bit HDR companded to %s-bit, output is NOT linear\n",
		 pwl_bits[(fmt & OX08B40_PWL0_BITS_MASK) >> OX08B40_PWL0_BITS_SHIFT]);
}

static int ox08b40_start_streaming(struct ox08b40 *ox08b40)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	const struct ox08b40_reg_list *reg_list = &ox08b40->mode->reg_list;
	int ret;

	ret = ox08b40_write_regs(ox08b40, reg_list->regs,
				 reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	ret = ox08b40_program_window(ox08b40);
	if (ret) {
		dev_err(&client->dev, "%s failed to set window\n", __func__);
		return ret;
	}

	ox08b40_report_companding(ox08b40);

	ret = ox08b40_write(ox08b40, OX08B40_REG_EMB_CTRL,
			OX08B40_EMB_FRONT_EN | OX08B40_EMB_END_EN, NULL);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_handler_setup(ox08b40->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ox08b40_write(ox08b40, OX08B40_REG_MODE_SELECT,
			    OX08B40_MODE_STREAMING, NULL);
	if (ret)
		return ret;

	ret = 0;
	ox08b40_write(ox08b40, OX08B40_REG_GROUP_ACCESS,
		      OX08B40_GROUP_HOLD_START, &ret);
	if (!ret)
		ret = ox08b40_set_frame_length(ox08b40,
					       ox08b40_wire_height(ox08b40) +
					       ox08b40->vblank->cur.val);
	if (!ret)
		ret = ox08b40_set_exposure(ox08b40, ox08b40->exposure->cur.val);
	if (!ret)
		ret = ox08b40_set_analogue_gain(ox08b40, ox08b40->gain->cur.val);
	ox08b40_write(ox08b40, OX08B40_REG_GROUP_ACCESS,
		      OX08B40_GROUP_HOLD_END, &ret);
	ox08b40_write(ox08b40, OX08B40_REG_GROUP_ACCESS,
		      OX08B40_GROUP_LAUNCH_VBLANK, &ret);
	ox08b40_group_launched(ox08b40);
	return ret;
}

static void ox08b40_stop_streaming(struct ox08b40 *ox08b40)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	int ret;

	ret = ox08b40_write(ox08b40, OX08B40_REG_MODE_SELECT,
			OX08B40_MODE_STANDBY, NULL);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int ox08b40_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ox08b40 *ox08b40 = to_ox08b40(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ox08b40->mutex);
	if (ox08b40->streaming == enable) {
		mutex_unlock(&ox08b40->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = ox08b40_start_streaming(ox08b40);
		if (ret)
			goto err_rpm_put;
	} else {
		ox08b40_stop_streaming(ox08b40);
		pm_runtime_put(&client->dev);
	}

	ox08b40->streaming = enable;

	__v4l2_ctrl_grab(ox08b40->vflip, enable);
	__v4l2_ctrl_grab(ox08b40->hflip, enable);

	mutex_unlock(&ox08b40->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&ox08b40->mutex);

	return ret;
}

static int ox08b40_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ox08b40 *ox08b40 = to_ox08b40(sd);
	int ret;

	ret = regulator_bulk_enable(OX08B40_NUM_SUPPLIES, ox08b40->supplies);
	if (ret) {
		dev_err(dev, "%s: failed to enable regulators\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(ox08b40->xclk);
	if (ret) {
		dev_err(dev, "%s: failed to enable clock\n", __func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(ox08b40->reset_gpio, 0);

	fsleep(7000);

	return 0;

reg_off:
	regulator_bulk_disable(OX08B40_NUM_SUPPLIES, ox08b40->supplies);
	return ret;
}

static int ox08b40_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ox08b40 *ox08b40 = to_ox08b40(sd);

	gpiod_set_value_cansleep(ox08b40->reset_gpio, 1);
	clk_disable_unprepare(ox08b40->xclk);
	regulator_bulk_disable(OX08B40_NUM_SUPPLIES, ox08b40->supplies);

	return 0;
}

static int ox08b40_identify_module(struct ox08b40 *ox08b40)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	u64 chip_id, rev;
	int ret;

	ret = ox08b40_read(ox08b40, OX08B40_REG_CHIP_ID, &chip_id, NULL);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to read chip id\n");

	if (chip_id != OX08B40_CHIP_ID)
		return dev_err_probe(&client->dev, -EIO,
				     "chip id mismatch: got 0x%06llx, expected 0x%06x\n",
				     chip_id, OX08B40_CHIP_ID);

	if (!ox08b40_read(ox08b40, OX08B40_REG_SILICON_REV, &rev, NULL))
		dev_info(&client->dev,
			 "OX08B40 detected (chip id 0x%06llx, silicon rev 0x%02llx)\n",
			 chip_id, rev);

	return 0;
}

static const struct v4l2_subdev_core_ops ox08b40_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ox08b40_video_ops = {
	.s_stream = ox08b40_set_stream,
};

static const struct v4l2_subdev_pad_ops ox08b40_pad_ops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	.init_cfg = ox08b40_init_state,
#endif
	.enum_mbus_code = ox08b40_enum_mbus_code,
	.get_fmt = ox08b40_get_pad_format,
	.set_fmt = ox08b40_set_pad_format,
	.get_selection = ox08b40_get_selection,
	.set_selection = ox08b40_set_selection,
	.enum_frame_size = ox08b40_enum_frame_size,
};

static const struct v4l2_subdev_ops ox08b40_subdev_ops = {
	.core = &ox08b40_core_ops,
	.video = &ox08b40_video_ops,
	.pad = &ox08b40_pad_ops,
};

static const struct v4l2_subdev_internal_ops ox08b40_internal_ops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	.init_state = ox08b40_init_state,
#endif
};

static void ox08b40_set_framing_limits(struct ox08b40 *ox08b40)
{
	const struct ox08b40_mode *mode = ox08b40->mode;
	unsigned int height = ox08b40_wire_height(ox08b40);
	unsigned int width = ox08b40->crop.width;
	unsigned int hblank;

	__v4l2_ctrl_modify_range(ox08b40->vblank,
				 OX08B40_VBLANK_MIN,
				 OX08B40_FRAME_LENGTH_MAX - height, 2,
				 mode->frm_length_default - height);
	__v4l2_ctrl_s_ctrl(ox08b40->vblank,
			   mode->frm_length_default - height);

	hblank = OX08B40_LINE_LENGTH - width;
	__v4l2_ctrl_modify_range(ox08b40->hblank, hblank,
				 hblank + (OX08B40_HTS_DCG_MAX -
					   OX08B40_HTS_DCG_BASE) *
					  OX08B40_HBLANK_PX_PER_HTS_X1000 / 1000,
				 OX08B40_HBLANK_STEP, hblank);
	__v4l2_ctrl_s_ctrl(ox08b40->hblank, hblank);
}

static int ox08b40_init_controls(struct ox08b40 *ox08b40)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ox08b40->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr = &ox08b40->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	unsigned int i;
	int ret;

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 17);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &ox08b40->mutex;

	ox08b40->pixel_rate =
		v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
				  V4L2_CID_PIXEL_RATE, OX08B40_PIXEL_RATE,
				  OX08B40_PIXEL_RATE, 1, OX08B40_PIXEL_RATE);

	ox08b40->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &ox08b40_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(ox08b40_link_freq_menu) - 1,
				       0, ox08b40_link_freq_menu);
	if (ox08b40->link_freq)
		ox08b40->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ox08b40->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
					    V4L2_CID_VBLANK, 0, 0xffff, 2, 0);
	ox08b40->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
					    V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	ox08b40->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OX08B40_EXPOSURE_MIN, 0xffff,
					      OX08B40_EXPOSURE_STEP,
					      OX08B40_EXPOSURE_DEFAULT);

	ox08b40->gain = v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
					  V4L2_CID_ANALOGUE_GAIN,
					  OX08B40_ANA_GAIN_MIN, OX08B40_ANA_GAIN_MAX,
					  OX08B40_ANA_GAIN_STEP,
					  OX08B40_ANA_GAIN_DEFAULT);
	v4l2_ctrl_cluster(2, &ox08b40->exposure);

	ox08b40->black_level =
		v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
				  V4L2_CID_BLACK_LEVEL, OX08B40_BLACK_LEVEL,
				  OX08B40_BLACK_LEVEL, 1, OX08B40_BLACK_LEVEL);
	if (ox08b40->black_level)
		ox08b40->black_level->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ox08b40->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
					   V4L2_CID_HFLIP, 0, 1, 1, 0);
	ox08b40->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &ox08b40_ctrl_ops,
					   V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ox08b40_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ox08b40_test_pattern_menu) - 1,
				     0, 0, ox08b40_test_pattern_menu);

	for (i = 0; i < ARRAY_SIZE(ox08b40_ob_ctrls); i++)
		v4l2_ctrl_new_custom(ctrl_hdlr, &ox08b40_ob_ctrls[i], NULL);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ox08b40_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	ox08b40->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&ox08b40->mutex);
	ox08b40_set_framing_limits(ox08b40);
	mutex_unlock(&ox08b40->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void ox08b40_free_controls(struct ox08b40 *ox08b40)
{
	v4l2_ctrl_handler_free(ox08b40->sd.ctrl_handler);
	mutex_destroy(&ox08b40->mutex);
}

static int ox08b40_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	if (ep_cfg.bus.mipi_csi2.num_data_lanes != OX08B40_DATA_LANES) {
		dev_err(dev, "only %u data lanes are supported\n",
			OX08B40_DATA_LANES);
		goto error_out;
	}

	if (!ep_cfg.nr_of_link_frequencies ||
	    ep_cfg.link_frequencies[0] != OX08B40_LINK_FREQ) {
		dev_err(dev, "link-frequencies must be %llu\n",
			OX08B40_LINK_FREQ);
		goto error_out;
	}

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int ox08b40_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ox08b40 *ox08b40;
	unsigned int i;
	int ret;

	ox08b40 = devm_kzalloc(dev, sizeof(*ox08b40), GFP_KERNEL);
	if (!ox08b40)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&ox08b40->sd, client, &ox08b40_subdev_ops);
	ox08b40->sd.internal_ops = &ox08b40_internal_ops;

	if (ox08b40_check_hwcfg(dev))
		return -EINVAL;

	ox08b40->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(ox08b40->xclk))
		return dev_err_probe(dev, PTR_ERR(ox08b40->xclk),
				     "failed to get xclk\n");

	ox08b40->xclk_freq = clk_get_rate(ox08b40->xclk);
	if (ox08b40->xclk_freq != OX08B40_XCLK_FREQ)
		return dev_err_probe(dev, -EINVAL,
				     "xclk frequency %u not supported, need %u\n",
				     ox08b40->xclk_freq, OX08B40_XCLK_FREQ);

	for (i = 0; i < OX08B40_NUM_SUPPLIES; i++)
		ox08b40->supplies[i].supply = ox08b40_supply_name[i];

	ret = devm_regulator_bulk_get(dev, OX08B40_NUM_SUPPLIES,
				      ox08b40->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	ox08b40->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(ox08b40->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ox08b40->reset_gpio),
				     "failed to get reset gpio\n");

	mutex_init(&ox08b40->mutex);

	ret = ox08b40_power_on(dev);
	if (ret)
		goto error_mutex;

	ret = ox08b40_identify_module(ox08b40);
	if (ret)
		goto error_power_off;

	ox08b40->mode = &supported_modes[0];
	ox08b40->crop = supported_modes[0].crop;

	ret = ox08b40_write(ox08b40, OX08B40_REG_SOFTWARE_RST,
			OX08B40_SOFTWARE_RST, NULL);
	if (ret)
		goto error_power_off;
	usleep_range(10000, 11000);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = ox08b40_init_controls(ox08b40);
	if (ret)
		goto error_pm_mutex;

	ox08b40->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			     V4L2_SUBDEV_FL_HAS_EVENTS;
	ox08b40->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ox08b40->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	ox08b40->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ox08b40->sd.entity, NUM_PADS,
				     ox08b40->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&ox08b40->sd);
	if (ret < 0) {
		dev_err(dev, "subdev init error: %d\n", ret);
		goto error_media_entity;
	}

	ret = v4l2_async_register_subdev_sensor(&ox08b40->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_subdev_cleanup;
	}

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&ox08b40->sd);
error_media_entity:
	media_entity_cleanup(&ox08b40->sd.entity);
error_handler_free:
	ox08b40_free_controls(ox08b40);
	goto error_pm;

error_pm_mutex:
	mutex_destroy(&ox08b40->mutex);
error_pm:
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		ox08b40_power_off(dev);
	pm_runtime_set_suspended(dev);

	return ret;

error_power_off:
	ox08b40_power_off(dev);
error_mutex:
	mutex_destroy(&ox08b40->mutex);

	return ret;
}

static void ox08b40_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ox08b40 *ox08b40 = to_ox08b40(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	ox08b40_free_controls(ox08b40);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ox08b40_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id ox08b40_dt_ids[] = {
	{ .compatible = "ovti,ox08b40" },
	{  }
};
MODULE_DEVICE_TABLE(of, ox08b40_dt_ids);

static const struct dev_pm_ops ox08b40_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(ox08b40_power_off, ox08b40_power_on, NULL)
};

static struct i2c_driver ox08b40_i2c_driver = {
	.driver = {
		.name = "ox08b40",
		.of_match_table	= ox08b40_dt_ids,
		.pm = &ox08b40_pm_ops,
	},
	.probe = ox08b40_probe,
	.remove = ox08b40_remove,
};

module_i2c_driver(ox08b40_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("OmniVision OX08B40 sensor driver");
MODULE_LICENSE("GPL");
