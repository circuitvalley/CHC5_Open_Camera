// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX678 sensor driver
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

#ifndef V4L2_CID_USER_IMX585_BASE
#define V4L2_CID_USER_IMX585_BASE	(V4L2_CID_USER_BASE + 0x2000)
#endif
#define V4L2_CID_IMX678_HCG_LEVEL	(V4L2_CID_USER_IMX585_BASE + 6)
#define V4L2_CID_IMX678_VMAX		(V4L2_CID_USER_IMX585_BASE + 7)
#define V4L2_CID_IMX678_HMAX		(V4L2_CID_USER_IMX585_BASE + 8)
#define V4L2_CID_IMX678_SHR		(V4L2_CID_USER_IMX585_BASE + 9)

#define IMX678_REG_MODE_SELECT		0x3000
#define IMX678_MODE_STANDBY		0x01
#define IMX678_MODE_STREAMING		0x00
#define IMX678_REG_REGHOLD		0x3001
#define IMX678_REG_XMSTA		0x3002

#define IMX678_REG_INCK_SEL		0x3014
#define IMX678_REG_DATARATE_SEL		0x3015
#define IMX678_REG_LANEMODE		0x3040

#define IMX678_REG_WINMODE		0x3018
#define IMX678_WINMODE_ALL_PIXEL	0x00
#define IMX678_WINMODE_CROP		0x04
#define IMX678_REG_WDMODE		0x301a
#define IMX678_REG_ADDMODE		0x301b
#define IMX678_REG_HREVERSE		0x3020
#define IMX678_REG_VREVERSE		0x3021
#define IMX678_REG_ADBIT		0x3022
#define IMX678_REG_MDBIT		0x3023
#define IMX678_REG_PIX_HST		0x303c
#define IMX678_REG_PIX_HWIDTH		0x303e
#define IMX678_REG_PIX_VST		0x3044
#define IMX678_REG_PIX_VWIDTH		0x3046

#define IMX678_REG_VMAX			0x3028
#define IMX678_VMAX_MAX			0xfffff
#define IMX678_REG_HMAX			0x302c
#define IMX678_HMAX_MAX			0xffff
#define IMX678_REG_SHR0			0x3050

#define IMX678_VBLANK_MIN		70
#define IMX678_VMAX_MIN			1024

#define IMX678_SHR0_MIN			3
#define IMX678_EXPOSURE_OFFSET		IMX678_SHR0_MIN
#define IMX678_EXPOSURE_MIN		1
#define IMX678_EXPOSURE_STEP		1
#define IMX678_EXPOSURE_DEFAULT		1000

#define IMX678_REG_ANALOG_GAIN		0x3070
#define IMX678_GAIN_DB10_PER_CODE	3
#define IMX678_ANA_GAIN_REG_MAX		240
#define IMX678_ANA_GAIN_MIN		0
#define IMX678_ANA_GAIN_MAX		720
#define IMX678_ANA_GAIN_STEP		3
#define IMX678_ANA_GAIN_DEFAULT		0

static unsigned int imx678_gain_code(unsigned int db10)
{
	return db10 / IMX678_GAIN_DB10_PER_CODE;
}

#define IMX678_REG_FDG_SEL0		0x3030
#define IMX678_ANA_GAIN_HCG_LEVEL	29
#define IMX678_ANA_GAIN_HCG_MIN		34

#define IMX678_REG_BLKLEVEL		0x30dc
#define IMX678_BLKLEVEL_REG_DEFAULT	0x32
#define IMX678_BLKLEVEL_REG_MAX		0x3ff
#define IMX678_BLKLEVEL_SHIFT		2
#define IMX678_BLKLEVEL_DEFAULT		(IMX678_BLKLEVEL_REG_DEFAULT << \
					 IMX678_BLKLEVEL_SHIFT)
#define IMX678_BLKLEVEL_MAX		(IMX678_BLKLEVEL_REG_MAX << \
					 IMX678_BLKLEVEL_SHIFT)

#define IMX678_REG_TPG_EN_DUOUT		0x30e0
#define IMX678_REG_TPG_PATSEL		0x30e2
#define IMX678_REG_TPG_COLORWIDTH	0x30e4
#define IMX678_REG_TESTCLKEN		0x5300

#define IMX678_REG_XXS_OUTSEL		0x30a4
#define IMX678_REG_XXS_DRV		0x30a6
#define IMX678_REG_EXTMODE		0x30ce

#define IMX678_INTERNAL_CLOCK		74250000U

#define IMX678_PIXEL_RATE_MULT		9U
#define IMX678_PIXEL_RATE \
	((u64)IMX678_PIXEL_RATE_MULT * IMX678_INTERNAL_CLOCK)

#define IMX678_EMBEDDED_LINE_WIDTH	16384
#define IMX678_NUM_EMBEDDED_LINES	1

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

#define IMX678_NATIVE_WIDTH		3856U
#define IMX678_NATIVE_HEIGHT		2180U
#define IMX678_PIXEL_ARRAY_LEFT		8U
#define IMX678_PIXEL_ARRAY_TOP		12U
#define IMX678_PIXEL_ARRAY_WIDTH	3840U
#define IMX678_PIXEL_ARRAY_HEIGHT	2160U

#define IMX678_MIN_WIDTH		1040U
#define IMX678_MIN_HEIGHT		956U
#define IMX678_HWIDTH_FORBIDDEN		2048U
#define IMX678_WIDTH_STEP		16U
#define IMX678_HEIGHT_STEP		4U
#define IMX678_CROP_LEFT_STEP		2U
#define IMX678_CROP_TOP_STEP		4U

struct imx678_reg {
	u16 address;
	u8 val;
};

struct imx678_reg_list {
	unsigned int num_of_regs;
	const struct imx678_reg *regs;
};

enum imx678_base_mode {
	IMX678_BASE_MODE_FULL,
	IMX678_BASE_MODE_2X2_BIN,
};

struct imx678_base_config {
	enum imx678_base_mode id;
	bool binning;
	unsigned int max_width;
	unsigned int max_height;
};

enum {
	IMX678_LINK_FREQ_297MHZ,
	IMX678_LINK_FREQ_360MHZ,
	IMX678_LINK_FREQ_445MHZ,
	IMX678_LINK_FREQ_594MHZ,
	IMX678_LINK_FREQ_720MHZ,
	IMX678_LINK_FREQ_891MHZ,
	IMX678_LINK_FREQ_1039MHZ,
	IMX678_LINK_FREQ_1188MHZ,
};

static const s64 link_freqs[] = {
	[IMX678_LINK_FREQ_297MHZ]  = 297000000,
	[IMX678_LINK_FREQ_360MHZ]  = 360000000,
	[IMX678_LINK_FREQ_445MHZ]  = 445500000,
	[IMX678_LINK_FREQ_594MHZ]  = 594000000,
	[IMX678_LINK_FREQ_720MHZ]  = 720000000,
	[IMX678_LINK_FREQ_891MHZ]  = 891000000,
	[IMX678_LINK_FREQ_1039MHZ] = 1039500000,
	[IMX678_LINK_FREQ_1188MHZ] = 1188000000,
};

static const u8 link_freq_datarate_sel[] = {
	[IMX678_LINK_FREQ_297MHZ]  = 0x07,
	[IMX678_LINK_FREQ_360MHZ]  = 0x06,
	[IMX678_LINK_FREQ_445MHZ]  = 0x05,
	[IMX678_LINK_FREQ_594MHZ]  = 0x04,
	[IMX678_LINK_FREQ_720MHZ]  = 0x03,
	[IMX678_LINK_FREQ_891MHZ]  = 0x02,
	[IMX678_LINK_FREQ_1039MHZ] = 0x01,
	[IMX678_LINK_FREQ_1188MHZ] = 0x00,
};

static const u16 hmax_min_4lane_12bit[] = {
	[IMX678_LINK_FREQ_297MHZ]  = 2640,
	[IMX678_LINK_FREQ_360MHZ]  = 1320,
	[IMX678_LINK_FREQ_445MHZ]  = 1320,
	[IMX678_LINK_FREQ_594MHZ]  = 1100,
	[IMX678_LINK_FREQ_720MHZ]  =  660,
	[IMX678_LINK_FREQ_891MHZ]  =  550,
	[IMX678_LINK_FREQ_1039MHZ] =  550,
	[IMX678_LINK_FREQ_1188MHZ] =  458,
};

static const u16 hmax_min_4lane_10bit[] = {
	[IMX678_LINK_FREQ_297MHZ]  = 2640,
	[IMX678_LINK_FREQ_360MHZ]  = 1320,
	[IMX678_LINK_FREQ_445MHZ]  = 1100,
	[IMX678_LINK_FREQ_594MHZ]  = 1100,
	[IMX678_LINK_FREQ_720MHZ]  =  550,
	[IMX678_LINK_FREQ_891MHZ]  =  550,
	[IMX678_LINK_FREQ_1039MHZ] =  458,
	[IMX678_LINK_FREQ_1188MHZ] =  458,
};

struct imx678_inck_cfg {
	u32 xclk_hz;
	u8 inck_sel;
};

static const struct imx678_inck_cfg imx678_inck_table[] = {
	{ 74250000, 0x00 },
	{ 37125000, 0x01 },
	{ 72000000, 0x02 },
	{ 27000000, 0x03 },
	{ 24000000, 0x04 },
	{ 36000000, 0x05 },
	{ 18000000, 0x06 },
	{ 13500000, 0x07 },
};

static const struct imx678_reg mode_common_regs[] = {
	{0x301a, 0x00},
	{0x301c, 0x00},
	{0x301e, 0x01},
	{0x3030, 0x00},
	{0x30a4, 0x0a},
	{0x30a6, 0x00},
	{0x30ce, 0x00},
	{0x306b, 0x00},
	{0x3400, 0x01},
	{0x3460, 0x22},
	{0x355a, 0x64},
	{0x3a02, 0x7a},
	{0x3a10, 0xec},
	{0x3a12, 0x71},
	{0x3a14, 0xde},
	{0x3a20, 0x2b},
	{0x3a24, 0x22},
	{0x3a25, 0x25},
	{0x3a26, 0x2a},
	{0x3a27, 0x2c},
	{0x3a28, 0x39},
	{0x3a29, 0x38},
	{0x3a30, 0x04},
	{0x3a31, 0x04},
	{0x3a32, 0x03},
	{0x3a33, 0x03},
	{0x3a34, 0x09},
	{0x3a35, 0x06},
	{0x3a38, 0xcd},
	{0x3a3a, 0x4c},
	{0x3a3c, 0xb9},
	{0x3a3e, 0x30},
	{0x3a40, 0x2c},
	{0x3a42, 0x39},
	{0x3a4e, 0x00},
	{0x3a52, 0x00},
	{0x3a56, 0x00},
	{0x3a5a, 0x00},
	{0x3a5e, 0x00},
	{0x3a62, 0x00},
	{0x3a64, 0x00},
	{0x3a6e, 0xa0},
	{0x3a70, 0x50},
	{0x3a8c, 0x04},
	{0x3a8d, 0x03},
	{0x3a8e, 0x09},
	{0x3a90, 0x38},
	{0x3a91, 0x42},
	{0x3a92, 0x3c},
	{0x3b0e, 0xf3},
	{0x3b12, 0xe5},
	{0x3b27, 0xc0},
	{0x3b2e, 0xef},
	{0x3b30, 0x6a},
	{0x3b32, 0xf6},
	{0x3b36, 0xe1},
	{0x3b3a, 0xe8},
	{0x3b5a, 0x17},
	{0x3b5e, 0xef},
	{0x3b60, 0x6a},
	{0x3b62, 0xf6},
	{0x3b66, 0xe1},
	{0x3b6a, 0xe8},
	{0x3b88, 0xec},
	{0x3b8a, 0xed},
	{0x3b94, 0x71},
	{0x3b96, 0x72},
	{0x3b98, 0xde},
	{0x3b9a, 0xdf},
	{0x3c0f, 0x06},
	{0x3c10, 0x06},
	{0x3c11, 0x06},
	{0x3c12, 0x06},
	{0x3c13, 0x06},
	{0x3c18, 0x20},
	{0x3c37, 0x10},
	{0x3c3a, 0x7a},
	{0x3c40, 0xf4},
	{0x3c48, 0xe6},
	{0x3c54, 0xce},
	{0x3c56, 0xd0},
	{0x3c6c, 0x53},
	{0x3c6e, 0x55},
	{0x3c70, 0xc0},
	{0x3c72, 0xc2},
	{0x3c7e, 0xce},
	{0x3c8c, 0xcf},
	{0x3c8e, 0xeb},
	{0x3c98, 0x54},
	{0x3c9a, 0x70},
	{0x3c9c, 0xc1},
	{0x3c9e, 0xdd},
	{0x3cb0, 0x7a},
	{0x3cb2, 0xba},
	{0x3cc8, 0xbc},
	{0x3cca, 0x7c},
	{0x3cd4, 0xea},
	{0x3cd5, 0x01},
	{0x3cd6, 0x4a},
	{0x3cd8, 0x00},
	{0x3cd9, 0x00},
	{0x3cda, 0xff},
	{0x3cdb, 0x03},
	{0x3cdc, 0x00},
	{0x3cdd, 0x00},
	{0x3cde, 0xff},
	{0x3cdf, 0x03},
	{0x3ce4, 0x4c},
	{0x3ce6, 0xec},
	{0x3ce7, 0x01},
	{0x3ce8, 0xff},
	{0x3ce9, 0x03},
	{0x3cea, 0x00},
	{0x3ceb, 0x00},
	{0x3cec, 0xff},
	{0x3ced, 0x03},
	{0x3cee, 0x00},
	{0x3cef, 0x00},
	{0x3cf2, 0xff},
	{0x3cf3, 0x03},
	{0x3cf4, 0x00},
	{0x3e28, 0x82},
	{0x3e2a, 0x80},
	{0x3e30, 0x85},
	{0x3e32, 0x7d},
	{0x3e5c, 0xce},
	{0x3e5e, 0xd3},
	{0x3e70, 0x53},
	{0x3e72, 0x58},
	{0x3e74, 0xc0},
	{0x3e76, 0xc5},
	{0x3e78, 0xc0},
	{0x3e79, 0x01},
	{0x3e7a, 0xd4},
	{0x3e7b, 0x01},
	{0x3eb4, 0x0b},
	{0x3eb5, 0x02},
	{0x3eb6, 0x4d},
	{0x3eb7, 0x42},
	{0x3eec, 0xf3},
	{0x3eee, 0xe7},
	{0x3f01, 0x01},
	{0x3f24, 0x10},
	{0x3f28, 0x2d},
	{0x3f2a, 0x2d},
	{0x3f2c, 0x2d},
	{0x3f2e, 0x2d},
	{0x3f30, 0x23},
	{0x3f38, 0x2d},
	{0x3f3a, 0x2d},
	{0x3f3c, 0x2d},
	{0x3f3e, 0x28},
	{0x3f40, 0x1e},
	{0x3f48, 0x2d},
	{0x3f4a, 0x2d},
	{0x3f4c, 0x00},
	{0x4004, 0xe4},
	{0x4006, 0xff},
	{0x4018, 0x69},
	{0x401a, 0x84},
	{0x401c, 0xd6},
	{0x401e, 0xf1},
	{0x4038, 0xde},
	{0x403a, 0x00},
	{0x403b, 0x01},
	{0x404c, 0x63},
	{0x404e, 0x85},
	{0x4050, 0xd0},
	{0x4052, 0xf2},
	{0x4108, 0xdd},
	{0x410a, 0xf7},
	{0x411c, 0x62},
	{0x411e, 0x7c},
	{0x4120, 0xcf},
	{0x4122, 0xe9},
	{0x4138, 0xe6},
	{0x413a, 0xf1},
	{0x414c, 0x6b},
	{0x414e, 0x76},
	{0x4150, 0xd8},
	{0x4152, 0xe3},
	{0x417e, 0x03},
	{0x417f, 0x01},
	{0x4186, 0xe0},
	{0x4190, 0xf3},
	{0x4192, 0xf7},
	{0x419c, 0x78},
	{0x419e, 0x7c},
	{0x41a0, 0xe5},
	{0x41a2, 0xe9},
	{0x41c8, 0xe2},
	{0x41ca, 0xfd},
	{0x41dc, 0x67},
	{0x41de, 0x82},
	{0x41e0, 0xd4},
	{0x41e2, 0xef},
	{0x4200, 0xde},
	{0x4202, 0xda},
	{0x4218, 0x63},
	{0x421a, 0x5f},
	{0x421c, 0xd0},
	{0x421e, 0xcc},
	{0x425a, 0x82},
	{0x425c, 0xef},
	{0x4348, 0xfe},
	{0x4349, 0x06},
	{0x4352, 0xce},
	{0x4420, 0x0b},
	{0x4421, 0x02},
	{0x4422, 0x4d},
	{0x4423, 0x0a},
	{0x4426, 0xf5},
	{0x442a, 0xe7},
	{0x4432, 0xf5},
	{0x4436, 0xe7},
	{0x4466, 0xb4},
	{0x446e, 0x32},
	{0x449f, 0x1c},
	{0x44a4, 0x2c},
	{0x44a6, 0x2c},
	{0x44a8, 0x2c},
	{0x44aa, 0x2c},
	{0x44b4, 0x2c},
	{0x44b6, 0x2c},
	{0x44b8, 0x2c},
	{0x44ba, 0x2c},
	{0x44c4, 0x2c},
	{0x44c6, 0x2c},
	{0x44c8, 0x2c},
	{0x4506, 0xf3},
	{0x450e, 0xe5},
	{0x4516, 0xf3},
	{0x4522, 0xe5},
	{0x4524, 0xf3},
	{0x452c, 0xe5},
	{0x453c, 0x22},
	{0x453d, 0x1b},
	{0x453e, 0x1b},
	{0x453f, 0x15},
	{0x4540, 0x15},
	{0x4541, 0x15},
	{0x4542, 0x15},
	{0x4543, 0x15},
	{0x4544, 0x15},
	{0x4548, 0x00},
	{0x4549, 0x01},
	{0x454a, 0x01},
	{0x454b, 0x06},
	{0x454c, 0x06},
	{0x454d, 0x06},
	{0x454e, 0x06},
	{0x454f, 0x06},
	{0x4550, 0x06},
	{0x4554, 0x55},
	{0x4555, 0x02},
	{0x4556, 0x42},
	{0x4557, 0x05},
	{0x4558, 0xfd},
	{0x4559, 0x05},
	{0x455a, 0x94},
	{0x455b, 0x06},
	{0x455d, 0x06},
	{0x455e, 0x49},
	{0x455f, 0x07},
	{0x4560, 0x7f},
	{0x4561, 0x07},
	{0x4562, 0xa5},
	{0x4564, 0x55},
	{0x4565, 0x02},
	{0x4566, 0x42},
	{0x4567, 0x05},
	{0x4568, 0xfd},
	{0x4569, 0x05},
	{0x456a, 0x94},
	{0x456b, 0x06},
	{0x456d, 0x06},
	{0x456e, 0x49},
	{0x456f, 0x07},
	{0x4572, 0xa5},
	{0x460c, 0x7d},
	{0x460e, 0xb1},
	{0x4614, 0xa8},
	{0x4616, 0xb2},
	{0x461c, 0x7e},
	{0x461e, 0xa7},
	{0x4624, 0xa8},
	{0x4626, 0xb2},
	{0x462c, 0x7e},
	{0x462e, 0x8a},
	{0x4630, 0x94},
	{0x4632, 0xa7},
	{0x4634, 0xfb},
	{0x4636, 0x2f},
	{0x4638, 0x81},
	{0x4639, 0x01},
	{0x463a, 0xb5},
	{0x463b, 0x01},
	{0x463c, 0x26},
	{0x463e, 0x30},
	{0x4640, 0xac},
	{0x4641, 0x01},
	{0x4642, 0xb6},
	{0x4643, 0x01},
	{0x4644, 0xfc},
	{0x4646, 0x25},
	{0x4648, 0x82},
	{0x4649, 0x01},
	{0x464a, 0xab},
	{0x464b, 0x01},
	{0x464c, 0x26},
	{0x464e, 0x30},
	{0x4654, 0xfc},
	{0x4656, 0x08},
	{0x4658, 0x12},
	{0x465a, 0x25},
	{0x4662, 0xfc},
	{0x46a2, 0xfb},
	{0x46d6, 0xf3},
	{0x46e6, 0x00},
	{0x46e8, 0xff},
	{0x46e9, 0x03},
	{0x46ec, 0x7a},
	{0x46ee, 0xe5},
	{0x46f4, 0xee},
	{0x46f6, 0xf2},
	{0x470c, 0xff},
	{0x470d, 0x03},
	{0x470e, 0x00},
	{0x4714, 0xe0},
	{0x4716, 0xe4},
	{0x471e, 0xed},
	{0x472e, 0x00},
	{0x4730, 0xff},
	{0x4731, 0x03},
	{0x4734, 0x7b},
	{0x4736, 0xdf},
	{0x4754, 0x7d},
	{0x4756, 0x8b},
	{0x4758, 0x93},
	{0x475a, 0xb1},
	{0x475c, 0xfb},
	{0x475e, 0x09},
	{0x4760, 0x11},
	{0x4762, 0x2f},
	{0x4766, 0xcc},
	{0x4776, 0xcb},
	{0x477e, 0x4a},
	{0x478e, 0x49},
	{0x4794, 0x7c},
	{0x4796, 0x8f},
	{0x4798, 0xb3},
	{0x4799, 0x00},
	{0x479a, 0xcc},
	{0x479c, 0xc1},
	{0x479e, 0xcb},
	{0x47a4, 0x7d},
	{0x47a6, 0x8e},
	{0x47a8, 0xb4},
	{0x47a9, 0x00},
	{0x47aa, 0xc0},
	{0x47ac, 0xfa},
	{0x47ae, 0x0d},
	{0x47b0, 0x31},
	{0x47b1, 0x01},
	{0x47b2, 0x4a},
	{0x47b3, 0x01},
	{0x47b4, 0x3f},
	{0x47b6, 0x49},
	{0x47bc, 0xfb},
	{0x47be, 0x0c},
	{0x47c0, 0x32},
	{0x47c1, 0x01},
	{0x47c2, 0x3e},
	{0x47c3, 0x01},
};

static const struct imx678_reg base_full_12bit_regs[] = {
	{IMX678_REG_ADBIT, 0x01},
	{IMX678_REG_MDBIT, 0x01},
	{IMX678_REG_ADDMODE, 0x00},
};

static const struct imx678_reg base_full_10bit_regs[] = {
	{IMX678_REG_ADBIT, 0x00},
	{IMX678_REG_MDBIT, 0x00},
	{IMX678_REG_ADDMODE, 0x00},
};

static const struct imx678_reg base_2x2bin_12bit_regs[] = {
	{IMX678_REG_ADBIT, 0x00},
	{IMX678_REG_MDBIT, 0x01},
	{IMX678_REG_ADDMODE, 0x01},
};

static const struct imx678_base_config base_configs[] = {
	[IMX678_BASE_MODE_FULL] = {
		.id = IMX678_BASE_MODE_FULL,
		.binning = false,
		.max_width = IMX678_PIXEL_ARRAY_WIDTH,
		.max_height = IMX678_PIXEL_ARRAY_HEIGHT,
	},
	[IMX678_BASE_MODE_2X2_BIN] = {
		.id = IMX678_BASE_MODE_2X2_BIN,
		.binning = true,
		.max_width = IMX678_PIXEL_ARRAY_WIDTH / 2,
		.max_height = IMX678_PIXEL_ARRAY_HEIGHT / 2,
	},
};

#define IMX678_NUM_BASE_CONFIGS ARRAY_SIZE(base_configs)

static const u32 codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

static const u32 mono_codes[] = {
	MEDIA_BUS_FMT_Y12_1X12,
	MEDIA_BUS_FMT_Y10_1X10,
};

static const char * const imx678_test_pattern_menu[] = {
	"Disabled",
	"All 000h",
	"All FFFh",
	"All 555h",
	"All AAAh",
	"Toggle 555h/AAAh",
	"Toggle AAAh/555h",
	"Toggle 000h/555h",
	"Toggle 555h/000h",
	"Toggle 000h/FFFh",
	"Toggle FFFh/000h",
	"Horizontal color bars",
	"Vertical color bars",
};

static const u8 imx678_test_pattern_val[] = {
	0x00,
	0x00,
	0x01,
	0x02,
	0x03,
	0x04,
	0x05,
	0x06,
	0x07,
	0x08,
	0x09,
	0x0a,
	0x0b,
};

static const char * const imx678_supply_name[] = {
	"VANA",
	"VDIG",
	"VDDL",
};

#define IMX678_NUM_SUPPLIES ARRAY_SIZE(imx678_supply_name)

#define IMX678_XCLR_MIN_DELAY_US	20000
#define IMX678_XCLR_DELAY_RANGE_US	1000
#define IMX678_STREAM_DELAY_US		25000
#define IMX678_STREAM_DELAY_RANGE_US	1000

struct imx678 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	struct clk *xclk;
	u32 xclk_freq;
	u8 inck_sel_val;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX678_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *blklevel;
	struct v4l2_ctrl *gain;

	struct v4l2_ctrl *vmax_ctrl;
	struct v4l2_ctrl *hmax_ctrl;
	struct v4l2_ctrl *shr_ctrl;

	unsigned int hcg_level;

	unsigned int link_freq_idx;
	unsigned int num_lanes;
	bool mono;

	const struct imx678_base_config *base_cfg;

	unsigned int out_width;
	unsigned int out_height;

	struct v4l2_rect crop;
	struct v4l2_rect compose;

	unsigned int hmax_min;
	unsigned int hmax;
	unsigned int vmax;

	struct mutex mutex;

	bool streaming;
	bool common_regs_written;
};

static inline struct imx678 *to_imx678(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx678, sd);
}

static int imx678_read_reg(struct imx678 *imx678, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
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

static int imx678_write_reg(struct imx678 *imx678, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
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

static int imx678_write_regs(struct imx678 *imx678,
			     const struct imx678_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx678_write_reg(imx678, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static void imx678_reghold(struct imx678 *imx678, bool hold)
{
	imx678_write_reg(imx678, IMX678_REG_REGHOLD, 1, hold ? 1 : 0);
}

static bool imx678_code_is_10bit(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_Y10_1X10:
		return true;
	default:
		return false;
	}
}

static u32 imx678_get_format_code(struct imx678 *imx678, u32 code)
{
	unsigned int i;

	if (imx678->mono)
		return imx678_code_is_10bit(code) ? MEDIA_BUS_FMT_Y10_1X10 :
						    MEDIA_BUS_FMT_Y12_1X12;

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == code)
			return codes[i];

	return codes[0];
}

static u32 imx678_code_for_cfg(u32 code, const struct imx678_base_config *cfg)
{
	if (!cfg->binning)
		return code;

	switch (code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return MEDIA_BUS_FMT_SRGGB12_1X12;
	case MEDIA_BUS_FMT_Y10_1X10:
		return MEDIA_BUS_FMT_Y12_1X12;
	default:
		return code;
	}
}

static const struct imx678_base_config *
imx678_binning_from_ratio(const struct v4l2_rect *crop,
			  unsigned int compose_w, unsigned int compose_h)
{
	const struct imx678_base_config *bin =
		&base_configs[IMX678_BASE_MODE_2X2_BIN];

	if (compose_w > bin->max_width || compose_h > bin->max_height)
		return &base_configs[IMX678_BASE_MODE_FULL];

	if (compose_w <= crop->width / 2 && compose_h <= crop->height / 2)
		return bin;

	if (compose_w < IMX678_MIN_WIDTH || compose_h < IMX678_MIN_HEIGHT)
		return bin;

	return &base_configs[IMX678_BASE_MODE_FULL];
}

static void imx678_clamp_align(unsigned int *width, unsigned int *height,
			       const struct imx678_base_config *cfg)
{
	unsigned int scale = cfg->binning ? 2 : 1;
	unsigned int min_w = roundup(IMX678_MIN_WIDTH / scale,
				     IMX678_WIDTH_STEP);
	unsigned int min_h = roundup(IMX678_MIN_HEIGHT / scale,
				     IMX678_HEIGHT_STEP);

	*width = clamp(*width, min_w, cfg->max_width);
	*height = clamp(*height, min_h, cfg->max_height);

	*width = rounddown(*width, IMX678_WIDTH_STEP);
	if (*width < min_w)
		*width = min_w;

	*height = rounddown(*height, IMX678_HEIGHT_STEP);
	if (*height < min_h)
		*height = min_h;

	if (*width * scale == IMX678_HWIDTH_FORBIDDEN)
		*width -= IMX678_WIDTH_STEP;
}

static void imx678_clamp_crop(struct v4l2_rect *crop)
{
	if (crop->left < 0)
		crop->left = 0;
	if (crop->top < 0)
		crop->top = 0;

	crop->left = rounddown(crop->left, IMX678_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX678_CROP_TOP_STEP);

	if (crop->width < IMX678_MIN_WIDTH)
		crop->width = IMX678_MIN_WIDTH;
	if (crop->height < IMX678_MIN_HEIGHT)
		crop->height = IMX678_MIN_HEIGHT;
	if (crop->width > IMX678_PIXEL_ARRAY_WIDTH)
		crop->width = IMX678_PIXEL_ARRAY_WIDTH;
	if (crop->height > IMX678_PIXEL_ARRAY_HEIGHT)
		crop->height = IMX678_PIXEL_ARRAY_HEIGHT;

	crop->width = rounddown(crop->width, IMX678_WIDTH_STEP);
	crop->height = rounddown(crop->height, IMX678_HEIGHT_STEP);

	if (crop->width == IMX678_HWIDTH_FORBIDDEN)
		crop->width -= IMX678_WIDTH_STEP;

	if (crop->left + crop->width > IMX678_PIXEL_ARRAY_WIDTH)
		crop->left = IMX678_PIXEL_ARRAY_WIDTH - crop->width;
	if (crop->top + crop->height > IMX678_PIXEL_ARRAY_HEIGHT)
		crop->top = IMX678_PIXEL_ARRAY_HEIGHT - crop->height;

	crop->left = rounddown(crop->left, IMX678_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX678_CROP_TOP_STEP);
}

static void imx678_tighten_crop(struct imx678 *imx678,
				const struct imx678_base_config *cfg)
{
	if (cfg->binning) {
		imx678->crop.width = imx678->compose.width * 2;
		imx678->crop.height = imx678->compose.height * 2;
	} else {
		imx678->crop.width = imx678->compose.width;
		imx678->crop.height = imx678->compose.height;
	}
	imx678_clamp_crop(&imx678->crop);
}

static const struct imx678_reg_list *
imx678_base_reg_list(const struct imx678_base_config *cfg, u32 fmt_code)
{
	static const struct imx678_reg_list full_12 = {
		ARRAY_SIZE(base_full_12bit_regs), base_full_12bit_regs };
	static const struct imx678_reg_list full_10 = {
		ARRAY_SIZE(base_full_10bit_regs), base_full_10bit_regs };
	static const struct imx678_reg_list bin_12 = {
		ARRAY_SIZE(base_2x2bin_12bit_regs), base_2x2bin_12bit_regs };

	if (cfg->binning)
		return &bin_12;
	return imx678_code_is_10bit(fmt_code) ? &full_10 : &full_12;
}

static int imx678_write_dynamic_regs(struct imx678 *imx678)
{
	const struct v4l2_rect *crop = &imx678->crop;
	int ret;

	imx678_reghold(imx678, true);

	ret = imx678_write_reg(imx678, IMX678_REG_WINMODE, 1,
			       IMX678_WINMODE_CROP);
	if (ret)
		goto release_hold;

	ret = imx678_write_reg(imx678, IMX678_REG_PIX_HST, 2,
			       IMX678_PIXEL_ARRAY_LEFT + crop->left);
	if (ret)
		goto release_hold;

	ret = imx678_write_reg(imx678, IMX678_REG_PIX_HWIDTH, 2, crop->width);
	if (ret)
		goto release_hold;

	ret = imx678_write_reg(imx678, IMX678_REG_PIX_VST, 2,
			       IMX678_PIXEL_ARRAY_TOP + crop->top);
	if (ret)
		goto release_hold;

	ret = imx678_write_reg(imx678, IMX678_REG_PIX_VWIDTH, 2, crop->height);
	if (ret)
		goto release_hold;

	ret = imx678_write_reg(imx678, IMX678_REG_HMAX, 2, imx678->hmax);
	if (ret)
		goto release_hold;

	ret = imx678_write_reg(imx678, IMX678_REG_VMAX, 3, imx678->vmax);

release_hold:
	imx678_reghold(imx678, false);

	return ret;
}

static unsigned int imx678_hmax_min(struct imx678 *imx678, u32 fmt_code)
{
	const u16 *table = imx678_code_is_10bit(fmt_code) ?
			   hmax_min_4lane_10bit : hmax_min_4lane_12bit;

	return table[imx678->link_freq_idx] * (4 / imx678->num_lanes);
}

static void imx678_set_default_format(struct imx678 *imx678)
{
	imx678->base_cfg = &base_configs[IMX678_BASE_MODE_FULL];
	imx678->out_width = IMX678_PIXEL_ARRAY_WIDTH;
	imx678->out_height = IMX678_PIXEL_ARRAY_HEIGHT;
	imx678->fmt_code = imx678->mono ? MEDIA_BUS_FMT_Y12_1X12 :
					  MEDIA_BUS_FMT_SRGGB12_1X12;

	imx678->crop.left = 0;
	imx678->crop.top = 0;
	imx678->crop.width = IMX678_PIXEL_ARRAY_WIDTH;
	imx678->crop.height = IMX678_PIXEL_ARRAY_HEIGHT;

	imx678->compose.left = 0;
	imx678->compose.top = 0;
	imx678->compose.width = IMX678_PIXEL_ARRAY_WIDTH;
	imx678->compose.height = IMX678_PIXEL_ARRAY_HEIGHT;

	imx678->hmax_min = imx678_hmax_min(imx678, imx678->fmt_code);
	imx678->hmax = imx678->hmax_min;
	imx678->vmax = max_t(unsigned int,
			     imx678->crop.height + IMX678_VBLANK_MIN,
			     IMX678_VMAX_MIN) & ~1U;
}

static int imx678_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;
	struct v4l2_rect *try_compose;

	mutex_lock(&imx678->mutex);

	try_fmt_img->width = IMX678_PIXEL_ARRAY_WIDTH;
	try_fmt_img->height = IMX678_PIXEL_ARRAY_HEIGHT;
	try_fmt_img->code = imx678_get_format_code(imx678,
						   MEDIA_BUS_FMT_SRGGB12_1X12);
	try_fmt_img->field = V4L2_FIELD_NONE;

	try_fmt_meta->width = IMX678_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX678_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = IMX678_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX678_PIXEL_ARRAY_HEIGHT;

	try_compose = v4l2_subdev_state_get_compose(fh->state, IMAGE_PAD);
	try_compose->left = 0;
	try_compose->top = 0;
	try_compose->width = IMX678_PIXEL_ARRAY_WIDTH;
	try_compose->height = IMX678_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx678->mutex);

	return 0;
}

static void imx678_adjust_exposure_range(struct imx678 *imx678)
{
	int exposure_max, exposure_def;

	exposure_max = imx678->vmax - IMX678_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx678->exposure->val);
	__v4l2_ctrl_modify_range(imx678->exposure, imx678->exposure->minimum,
				 exposure_max, imx678->exposure->step,
				 exposure_def);
}

static int imx678_set_exposure(struct imx678 *imx678, unsigned int exp_lines)
{
	u32 shr0;

	if (exp_lines > imx678->vmax)
		exp_lines = imx678->vmax;

	shr0 = imx678->vmax - exp_lines;
	shr0 = clamp_t(u32, shr0, IMX678_SHR0_MIN,
		       imx678->vmax - IMX678_EXPOSURE_MIN);

	return imx678_write_reg(imx678, IMX678_REG_SHR0, 3, shr0);
}

static void imx678_gain_split(struct imx678 *imx678, unsigned int code,
			      unsigned int *reg, bool *hcg)
{
	unsigned int level = imx678->hcg_level;

	if (level && code >= IMX678_ANA_GAIN_HCG_MIN + level) {
		*hcg = true;
		*reg = code - level;
	} else {
		*hcg = false;
		*reg = code;
	}
}

static unsigned int imx678_hblank_to_hmax(struct imx678 *imx678,
					  unsigned int hblank)
{
	unsigned int hmax = (imx678->out_width + hblank) /
			    IMX678_PIXEL_RATE_MULT;

	return clamp_t(unsigned int, hmax, imx678->hmax_min, IMX678_HMAX_MAX);
}

static int imx678_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx678 *imx678 =
		container_of(ctrl->handler, struct imx678, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		imx678->vmax = (imx678->out_height + ctrl->val) & ~1U;
		imx678_adjust_exposure_range(imx678);
	}

	if (ctrl->id == V4L2_CID_IMX678_HCG_LEVEL)
		imx678->hcg_level = ctrl->val;

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_IMX678_HCG_LEVEL:
		ctrl = imx678->gain;
		fallthrough;
	case V4L2_CID_ANALOGUE_GAIN: {
		unsigned int gain;
		bool hcg;

		imx678_gain_split(imx678, imx678_gain_code(ctrl->val),
				  &gain, &hcg);

		imx678_reghold(imx678, true);
		ret = imx678_write_reg(imx678, IMX678_REG_ANALOG_GAIN, 2, gain);
		if (!ret)
			ret = imx678_write_reg(imx678, IMX678_REG_FDG_SEL0, 1,
					       hcg ? 0x01 : 0x00);
		imx678_reghold(imx678, false);
		break;
	}
	case V4L2_CID_EXPOSURE:
		ret = imx678_set_exposure(imx678, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		imx678_reghold(imx678, true);
		ret = imx678_write_reg(imx678, IMX678_REG_VMAX, 3,
				       imx678->vmax);
		if (!ret)
			ret = imx678_set_exposure(imx678,
						  imx678->exposure->val);
		imx678_reghold(imx678, false);
		break;
	case V4L2_CID_HBLANK:
		imx678->hmax = imx678_hblank_to_hmax(imx678, ctrl->val);
		imx678_reghold(imx678, true);
		ret = imx678_write_reg(imx678, IMX678_REG_HMAX, 2,
				       imx678->hmax);
		imx678_reghold(imx678, false);
		break;
	case V4L2_CID_HFLIP:
		ret = imx678_write_reg(imx678, IMX678_REG_HREVERSE, 1,
				       ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = imx678_write_reg(imx678, IMX678_REG_VREVERSE, 1,
				       ctrl->val);
		break;
	case V4L2_CID_BLACK_LEVEL:
		ret = imx678_write_reg(imx678, IMX678_REG_BLKLEVEL, 2,
				       ctrl->val >> IMX678_BLKLEVEL_SHIFT);
		break;
	case V4L2_CID_IMX678_VMAX:
		if (!ctrl->val)
			break;
		ret = imx678_write_reg(imx678, IMX678_REG_VMAX, 3, ctrl->val);
		break;
	case V4L2_CID_IMX678_HMAX:
		if (!ctrl->val)
			break;
		ret = imx678_write_reg(imx678, IMX678_REG_HMAX, 2, ctrl->val);
		break;
	case V4L2_CID_IMX678_SHR:
		if (!ctrl->val)
			break;
		ret = imx678_write_reg(imx678, IMX678_REG_SHR0, 3, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			imx678_write_reg(imx678, IMX678_REG_BLKLEVEL, 2, 0);
			imx678_write_reg(imx678, IMX678_REG_TPG_PATSEL, 1,
					 imx678_test_pattern_val[ctrl->val]);
			imx678_write_reg(imx678, IMX678_REG_TPG_COLORWIDTH, 1,
					 0x00);
			imx678_write_reg(imx678, IMX678_REG_TESTCLKEN, 1,
					 0x0a);
			ret = imx678_write_reg(imx678,
					       IMX678_REG_TPG_EN_DUOUT, 1, 1);
		} else {
			imx678_write_reg(imx678, IMX678_REG_TPG_EN_DUOUT, 1,
					 0);
			imx678_write_reg(imx678, IMX678_REG_TESTCLKEN, 1,
					 0x02);
			ret = imx678_write_reg(imx678, IMX678_REG_BLKLEVEL, 2,
					       imx678->blklevel->val >>
					       IMX678_BLKLEVEL_SHIFT);
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

#define IMX678_VOB_LINES		20
#define IMX678_VOB_LINES_BIN	10

static int imx678_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx678 *imx678 =
		container_of(ctrl->handler, struct imx678, ctrl_handler);

	switch (ctrl->id) {
	case CHC5SENSOR_CID_OB_TOP:
		ctrl->val = imx678->base_cfg && imx678->base_cfg->binning ?
			    IMX678_VOB_LINES_BIN : IMX678_VOB_LINES;
		return 0;
	case CHC5SENSOR_CID_OB_ON_IMAGE_DT:
		ctrl->val = 0;
		return 0;
	case CHC5SENSOR_CID_OB_LEFT:
	case CHC5SENSOR_CID_OB_RIGHT:
	case CHC5SENSOR_CID_OB_BOTTOM:
		ctrl->val = 0;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops imx678_ctrl_ops = {
	.s_ctrl		 = imx678_set_ctrl,
	.g_volatile_ctrl = imx678_g_volatile_ctrl,
};

#define IMX678_OB_CTRL(_id, _name)					\
	{								\
		.ops	= &imx678_ctrl_ops,				\
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

static const struct v4l2_ctrl_config imx678_ob_ctrls[] = {
	IMX678_OB_CTRL(CHC5SENSOR_CID_OB_LEFT,   "Optical Black Left"),
	IMX678_OB_CTRL(CHC5SENSOR_CID_OB_TOP,    "Optical Black Top"),
	IMX678_OB_CTRL(CHC5SENSOR_CID_OB_RIGHT,  "Optical Black Right"),
	IMX678_OB_CTRL(CHC5SENSOR_CID_OB_BOTTOM, "Optical Black Bottom"),
	IMX678_OB_CTRL(CHC5SENSOR_CID_OB_ON_IMAGE_DT, "Optical Black On Image DT"),
};

static const struct v4l2_ctrl_config imx678_cfg_hcg_level = {
	.ops  = &imx678_ctrl_ops,
	.id   = V4L2_CID_IMX678_HCG_LEVEL,
	.name = "HCG Level",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX678_ANA_GAIN_REG_MAX - IMX678_ANA_GAIN_HCG_MIN,
	.step = 1,
	.def  = IMX678_ANA_GAIN_HCG_LEVEL,
};

static const struct v4l2_ctrl_config imx678_cfg_vmax = {
	.ops  = &imx678_ctrl_ops,
	.id   = V4L2_CID_IMX678_VMAX,
	.name = "VMAX",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX678_VMAX_MAX,
	.step = 1,
};

static const struct v4l2_ctrl_config imx678_cfg_hmax = {
	.ops  = &imx678_ctrl_ops,
	.id   = V4L2_CID_IMX678_HMAX,
	.name = "HMAX",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX678_HMAX_MAX,
	.step = 1,
};

static const struct v4l2_ctrl_config imx678_cfg_shr = {
	.ops  = &imx678_ctrl_ops,
	.id   = V4L2_CID_IMX678_SHR,
	.name = "SHR",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX678_VMAX_MAX,
	.step = 1,
};

static int imx678_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (imx678->mono) {
			if (code->index >= ARRAY_SIZE(mono_codes))
				return -EINVAL;

			code->code = mono_codes[code->index];
			return 0;
		}

		if (code->index >= ARRAY_SIZE(codes))
			return -EINVAL;

		code->code = imx678_get_format_code(imx678,
						    codes[code->index]);
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx678_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index > 0)
			return -EINVAL;

		if (fse->code != imx678_get_format_code(imx678, fse->code))
			return -EINVAL;

		fse->min_width = roundup(IMX678_MIN_WIDTH / 2,
					 IMX678_WIDTH_STEP);
		fse->max_width = IMX678_PIXEL_ARRAY_WIDTH;
		fse->min_height = roundup(IMX678_MIN_HEIGHT / 2,
					  IMX678_HEIGHT_STEP);
		fse->max_height = IMX678_PIXEL_ARRAY_HEIGHT;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX678_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX678_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx678_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx678_update_image_pad_format(struct imx678 *imx678,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = imx678->out_width;
	fmt->format.height = imx678->out_height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx678_reset_colorspace(&fmt->format);
}

static void imx678_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX678_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX678_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx678_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx678->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx678_get_format_code(imx678, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx678_update_image_pad_format(imx678, fmt);
			fmt->format.code =
			       imx678_get_format_code(imx678, imx678->fmt_code);
		} else {
			imx678_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx678->mutex);
	return 0;
}

static void imx678_set_framing_limits(struct imx678 *imx678)
{
	unsigned int out_w = imx678->out_width;
	unsigned int out_h = imx678->out_height;
	unsigned int frm_length_min, frm_length_default;
	unsigned int vblank_min, vblank_max, vblank_def;
	unsigned int hblank_min, hblank_max;

	imx678->hmax_min = imx678_hmax_min(imx678, imx678->fmt_code);
	imx678->hmax = imx678->hmax_min;

	hblank_min = imx678->hmax_min * IMX678_PIXEL_RATE_MULT;
	hblank_min = hblank_min > out_w ? hblank_min - out_w : 0;
	hblank_max = IMX678_HMAX_MAX * IMX678_PIXEL_RATE_MULT - out_w;

	__v4l2_ctrl_modify_range(imx678->hblank, hblank_min, hblank_max,
				 IMX678_PIXEL_RATE_MULT, hblank_min);
	__v4l2_ctrl_s_ctrl(imx678->hblank, hblank_min);

	frm_length_min = max_t(unsigned int,
			       imx678->crop.height + IMX678_VBLANK_MIN,
			       IMX678_VMAX_MIN);

	frm_length_default = div_u64((u64)IMX678_INTERNAL_CLOCK,
				     imx678->hmax * 30);
	if (frm_length_default < frm_length_min)
		frm_length_default = frm_length_min;
	if (frm_length_default > IMX678_VMAX_MAX)
		frm_length_default = IMX678_VMAX_MAX;

	vblank_min = round_up(frm_length_min - out_h, 2);
	vblank_max = round_down(IMX678_VMAX_MAX - out_h, 2);
	vblank_def = clamp(round_up(frm_length_default - out_h, 2),
			   vblank_min, vblank_max);

	__v4l2_ctrl_modify_range(imx678->vblank, vblank_min, vblank_max,
				 2, vblank_def);
	__v4l2_ctrl_s_ctrl(imx678->vblank, vblank_def);

	imx678->vmax = (out_h + imx678->vblank->val) & ~1U;

	imx678_adjust_exposure_range(imx678);
}

static int imx678_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx678_base_config *cfg;
	struct imx678 *imx678 = to_imx678(sd);
	unsigned int req_width, req_height;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx678->mutex);

	if (fmt->pad == IMAGE_PAD) {
		fmt->format.code = imx678_get_format_code(imx678,
							  fmt->format.code);

		req_width = fmt->format.width;
		req_height = fmt->format.height;

		cfg = imx678_binning_from_ratio(&imx678->crop,
						req_width, req_height);

		imx678_clamp_align(&req_width, &req_height, cfg);

		fmt->format.code = imx678_code_for_cfg(fmt->format.code, cfg);
		fmt->format.width = req_width;
		fmt->format.height = req_height;
		fmt->format.field = V4L2_FIELD_NONE;
		imx678_reset_colorspace(&fmt->format);

		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx678->base_cfg = cfg;
			imx678->out_width = req_width;
			imx678->out_height = req_height;
			imx678->compose.width = req_width;
			imx678->compose.height = req_height;
			imx678->fmt_code = fmt->format.code;

			imx678_tighten_crop(imx678, cfg);

			imx678_set_framing_limits(imx678);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx678_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx678->mutex);

	return 0;
}

static int imx678_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&imx678->mutex);
		sel->r = imx678->crop;
		mutex_unlock(&imx678->mutex);
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		mutex_lock(&imx678->mutex);
		sel->r = imx678->compose;
		mutex_unlock(&imx678->mutex);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX678_NATIVE_WIDTH;
		sel->r.height = IMX678_NATIVE_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX678_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX678_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int imx678_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_base_config *cfg;

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&imx678->mutex);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_rect crop = sel->r;
		unsigned int compose_w, compose_h;

		imx678_clamp_crop(&crop);

		cfg = imx678_binning_from_ratio(&crop, imx678->compose.width,
						imx678->compose.height);

		compose_w = cfg->binning ? crop.width / 2 : crop.width;
		compose_h = cfg->binning ? crop.height / 2 : crop.height;

		imx678_clamp_align(&compose_w, &compose_h, cfg);

		if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
			*v4l2_subdev_state_get_crop(sd_state, sel->pad) = crop;
		} else {
			imx678->crop = crop;
			imx678->compose.width = compose_w;
			imx678->compose.height = compose_h;
			imx678->base_cfg = cfg;
			imx678->out_width = compose_w;
			imx678->out_height = compose_h;
			imx678->fmt_code = imx678_code_for_cfg(imx678->fmt_code,
							       cfg);
			imx678_set_framing_limits(imx678);
		}

		sel->r = crop;

	} else {
		unsigned int compose_w = sel->r.width;
		unsigned int compose_h = sel->r.height;

		cfg = imx678_binning_from_ratio(&imx678->crop,
						compose_w, compose_h);

		imx678_clamp_align(&compose_w, &compose_h, cfg);

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY) {
			imx678->compose.left = 0;
			imx678->compose.top = 0;
			imx678->compose.width = compose_w;
			imx678->compose.height = compose_h;
			imx678->base_cfg = cfg;
			imx678->out_width = compose_w;
			imx678->out_height = compose_h;

			imx678_tighten_crop(imx678, cfg);

			cfg = imx678_binning_from_ratio(&imx678->crop,
							compose_w, compose_h);
			imx678->base_cfg = cfg;
			imx678->fmt_code = imx678_code_for_cfg(imx678->fmt_code,
							       cfg);

			imx678_set_framing_limits(imx678);
		}

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = compose_w;
		sel->r.height = compose_h;
	}

	mutex_unlock(&imx678->mutex);

	return 0;
}

static int imx678_start_streaming(struct imx678 *imx678)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	const struct imx678_reg_list *reg_list;
	int ret;

	if (!imx678->common_regs_written) {
		ret = imx678_write_regs(imx678, mode_common_regs,
					ARRAY_SIZE(mode_common_regs));
		if (!ret)
			ret = imx678_write_reg(imx678, IMX678_REG_INCK_SEL, 1,
					       imx678->inck_sel_val);
		if (!ret)
			ret = imx678_write_reg(imx678, IMX678_REG_DATARATE_SEL,
					       1,
					       link_freq_datarate_sel[imx678->link_freq_idx]);
		if (!ret)
			ret = imx678_write_reg(imx678, IMX678_REG_LANEMODE, 1,
					       imx678->num_lanes == 2 ? 0x01 :
									0x03);
		if (!ret)
			ret = imx678_write_reg(imx678, IMX678_REG_BLKLEVEL, 2,
					       IMX678_BLKLEVEL_REG_DEFAULT);
		if (ret) {
			dev_err(&client->dev,
				"%s failed to set common settings\n",
				__func__);
			return ret;
		}

		imx678->common_regs_written = true;
	}

	reg_list = imx678_base_reg_list(imx678->base_cfg, imx678->fmt_code);
	ret = imx678_write_regs(imx678, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set base mode\n",
			__func__);
		return ret;
	}

	ret = imx678_write_dynamic_regs(imx678);
	if (ret) {
		dev_err(&client->dev, "%s failed to set dynamic regs\n",
			__func__);
		return ret;
	}

	__v4l2_ctrl_s_ctrl(imx678->vmax_ctrl, 0);
	__v4l2_ctrl_s_ctrl(imx678->hmax_ctrl, 0);
	__v4l2_ctrl_s_ctrl(imx678->shr_ctrl, 0);

	ret = __v4l2_ctrl_handler_setup(imx678->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = imx678_write_reg(imx678, IMX678_REG_MODE_SELECT, 1,
			       IMX678_MODE_STREAMING);
	if (ret)
		return ret;

	usleep_range(IMX678_STREAM_DELAY_US,
		     IMX678_STREAM_DELAY_US + IMX678_STREAM_DELAY_RANGE_US);

	return imx678_write_reg(imx678, IMX678_REG_XMSTA, 1, 0x00);
}

static void imx678_stop_streaming(struct imx678 *imx678)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	int ret;

	ret = imx678_write_reg(imx678, IMX678_REG_XMSTA, 1, 0x01);
	if (!ret)
		ret = imx678_write_reg(imx678, IMX678_REG_MODE_SELECT, 1,
				       IMX678_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int imx678_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx678->mutex);
	if (imx678->streaming == enable) {
		mutex_unlock(&imx678->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = imx678_start_streaming(imx678);
		if (ret)
			goto err_rpm_put;
	} else {
		imx678_stop_streaming(imx678);
		pm_runtime_put(&client->dev);
	}

	imx678->streaming = enable;

	__v4l2_ctrl_grab(imx678->vflip, enable);
	__v4l2_ctrl_grab(imx678->hflip, enable);

	mutex_unlock(&imx678->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx678->mutex);

	return ret;
}

static int imx678_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);
	int ret;

	ret = regulator_bulk_enable(IMX678_NUM_SUPPLIES,
				    imx678->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx678->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx678->reset_gpio, 1);
	usleep_range(IMX678_XCLR_MIN_DELAY_US,
		     IMX678_XCLR_MIN_DELAY_US + IMX678_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(IMX678_NUM_SUPPLIES, imx678->supplies);
	return ret;
}

static int imx678_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	gpiod_set_value_cansleep(imx678->reset_gpio, 0);
	regulator_bulk_disable(IMX678_NUM_SUPPLIES, imx678->supplies);
	clk_disable_unprepare(imx678->xclk);

	imx678->common_regs_written = false;

	return 0;
}

static int __maybe_unused imx678_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	if (imx678->streaming)
		imx678_stop_streaming(imx678);

	return 0;
}

static int __maybe_unused imx678_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);
	int ret;

	if (imx678->streaming) {
		ret = imx678_start_streaming(imx678);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx678_stop_streaming(imx678);
	imx678->streaming = 0;
	return ret;
}

static int imx678_get_regulators(struct imx678 *imx678)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	unsigned int i;

	for (i = 0; i < IMX678_NUM_SUPPLIES; i++)
		imx678->supplies[i].supply = imx678_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX678_NUM_SUPPLIES,
				       imx678->supplies);
}

static int imx678_identify_module(struct imx678 *imx678)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	int ret;
	u32 val;

	ret = imx678_read_reg(imx678, IMX678_REG_BLKLEVEL, 2, &val);
	if (ret) {
		dev_err(&client->dev,
			"failed to read sensor (blklevel probe), error %d\n",
			ret);
		return ret;
	}

	if ((val & IMX678_BLKLEVEL_REG_MAX) != IMX678_BLKLEVEL_REG_DEFAULT)
		dev_warn(&client->dev,
			 "unexpected BLKLEVEL default 0x%x (expected 0x%x)\n",
			 val, IMX678_BLKLEVEL_REG_DEFAULT);

	dev_info(&client->dev, "IMX678 found on %s\n",
		 dev_name(&client->adapter->dev));

	return 0;
}

static const struct v4l2_subdev_core_ops imx678_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx678_video_ops = {
	.s_stream = imx678_set_stream,
};

static const struct v4l2_subdev_pad_ops imx678_pad_ops = {
	.enum_mbus_code = imx678_enum_mbus_code,
	.get_fmt = imx678_get_pad_format,
	.set_fmt = imx678_set_pad_format,
	.get_selection = imx678_get_selection,
	.set_selection = imx678_set_selection,
	.enum_frame_size = imx678_enum_frame_size,
};

static const struct v4l2_subdev_ops imx678_subdev_ops = {
	.core = &imx678_core_ops,
	.video = &imx678_video_ops,
	.pad = &imx678_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx678_internal_ops = {
	.open = imx678_open,
};

static int imx678_init_controls(struct imx678 *imx678)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx678->sd);
	struct v4l2_fwnode_device_properties props;
	int ret;

	ctrl_hdlr = &imx678->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 25);
	if (ret)
		return ret;

	mutex_init(&imx678->mutex);
	ctrl_hdlr->lock = &imx678->mutex;

	imx678->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       IMX678_PIXEL_RATE,
					       IMX678_PIXEL_RATE, 1,
					       IMX678_PIXEL_RATE);
	if (imx678->pixel_rate)
		imx678->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx678->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx678_ctrl_ops,
				       V4L2_CID_LINK_FREQ, 0, 0,
				       &link_freqs[imx678->link_freq_idx]);
	if (imx678->link_freq)
		imx678->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx678->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					   V4L2_CID_VBLANK, IMX678_VBLANK_MIN,
					   IMX678_VMAX_MAX - 1, 2,
					   IMX678_VBLANK_MIN);
	imx678->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					   V4L2_CID_HBLANK, 0,
					   IMX678_HMAX_MAX *
						IMX678_PIXEL_RATE_MULT,
					   IMX678_PIXEL_RATE_MULT, 0);

	imx678->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX678_EXPOSURE_MIN,
					     IMX678_VMAX_MAX -
						IMX678_EXPOSURE_OFFSET,
					     IMX678_EXPOSURE_STEP,
					     IMX678_EXPOSURE_DEFAULT);

	imx678->gain = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					 V4L2_CID_ANALOGUE_GAIN,
					 IMX678_ANA_GAIN_MIN,
					 IMX678_ANA_GAIN_MAX,
					 IMX678_ANA_GAIN_STEP,
					 IMX678_ANA_GAIN_DEFAULT);

	imx678->blklevel = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					     V4L2_CID_BLACK_LEVEL, 0,
					     IMX678_BLKLEVEL_MAX, 1,
					     IMX678_BLKLEVEL_DEFAULT);

	imx678->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);

	imx678->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx678_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx678_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx678_test_pattern_menu) - 1,
				     0, 0, imx678_test_pattern_menu);

	{
		struct v4l2_ctrl_config cfg = imx678_cfg_hcg_level;

		cfg.def = imx678->hcg_level;
		v4l2_ctrl_new_custom(ctrl_hdlr, &cfg, NULL);
	}
	imx678->vmax_ctrl = v4l2_ctrl_new_custom(ctrl_hdlr, &imx678_cfg_vmax,
						 NULL);
	imx678->hmax_ctrl = v4l2_ctrl_new_custom(ctrl_hdlr, &imx678_cfg_hmax,
						 NULL);
	imx678->shr_ctrl = v4l2_ctrl_new_custom(ctrl_hdlr, &imx678_cfg_shr,
						NULL);

	{
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(imx678_ob_ctrls); i++)
			v4l2_ctrl_new_custom(ctrl_hdlr, &imx678_ob_ctrls[i], NULL);
	}

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx678_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx678->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&imx678->mutex);
	imx678_set_framing_limits(imx678);
	mutex_unlock(&imx678->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx678->mutex);

	return ret;
}

static void imx678_free_controls(struct imx678 *imx678)
{
	v4l2_ctrl_handler_free(imx678->sd.ctrl_handler);
	mutex_destroy(&imx678->mutex);
}

static int imx678_check_hwcfg(struct device *dev, struct imx678 *imx678)
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
	imx678->num_lanes = ep_cfg.bus.mipi_csi2.num_data_lanes;

	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	for (i = 0; i < ARRAY_SIZE(link_freqs); i++) {
		if (link_freqs[i] == ep_cfg.link_frequencies[0]) {
			imx678->link_freq_idx = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(link_freqs)) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
			ret = -EINVAL;
			goto error_out;
	}

	if (imx678->link_freq_idx >= IMX678_LINK_FREQ_1039MHZ) {
		dev_err(dev,
			"Link frequency %lld unsupported: HMAX floor unvalidated above 1782 Mbps/lane\n",
			ep_cfg.link_frequencies[0]);
		ret = -EINVAL;
		goto error_out;
	}

	if (imx678->link_freq_idx > IMX678_LINK_FREQ_445MHZ)
		dev_warn(dev,
			 "Link frequency %lld not supported\n",
			 ep_cfg.link_frequencies[0]);

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static const struct of_device_id imx678_dt_ids[] = {
	{ .compatible = "sony,imx678" },
	{  }
};

static int imx678_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx678 *imx678;
	unsigned int i;
	int ret;

	imx678 = devm_kzalloc(&client->dev, sizeof(*imx678), GFP_KERNEL);
	if (!imx678)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx678->sd, client, &imx678_subdev_ops);

	if (imx678_check_hwcfg(dev, imx678))
		return -EINVAL;

	{
		u32 lvl;

		imx678->hcg_level = IMX678_ANA_GAIN_HCG_LEVEL;
		if (!of_property_read_u32(dev->of_node, "sony,hcg-level", &lvl)) {
			if (lvl <= IMX678_ANA_GAIN_REG_MAX - IMX678_ANA_GAIN_HCG_MIN) {
				imx678->hcg_level = lvl;
				dev_info(dev, "hcg-level %u from DT (default %u)\n",
					 lvl, IMX678_ANA_GAIN_HCG_LEVEL);
			} else {
				dev_warn(dev, "sony,hcg-level %u out of range, using %u\n",
					 lvl, IMX678_ANA_GAIN_HCG_LEVEL);
			}
		}
	}

	imx678->mono = of_property_read_bool(dev->of_node, "mono-mode");
	if (imx678->mono)
		dev_info(dev, "configured for the mono sensor variant\n");

	imx678->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx678->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx678->xclk);
	}

	imx678->xclk_freq = clk_get_rate(imx678->xclk);
	for (i = 0; i < ARRAY_SIZE(imx678_inck_table); i++) {
		if (imx678_inck_table[i].xclk_hz == imx678->xclk_freq) {
			imx678->inck_sel_val = imx678_inck_table[i].inck_sel;
			break;
		}
	}
	if (i == ARRAY_SIZE(imx678_inck_table)) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx678->xclk_freq);
		return -EINVAL;
	}

	ret = imx678_get_regulators(imx678);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	imx678->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	ret = imx678_power_on(dev);
	if (ret)
		return ret;

	ret = imx678_identify_module(imx678);
	if (ret)
		goto error_power_off;

	imx678_set_default_format(imx678);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = imx678_init_controls(imx678);
	if (ret)
		goto error_power_off;

	imx678->sd.internal_ops = &imx678_internal_ops;
	imx678->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx678->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx678->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx678->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx678->sd.entity, NUM_PADS, imx678->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx678->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx678->sd.entity);

error_handler_free:
	imx678_free_controls(imx678);

error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	imx678_power_off(&client->dev);

	return ret;
}

static void imx678_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx678_free_controls(imx678);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx678_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

MODULE_DEVICE_TABLE(of, imx678_dt_ids);

static const struct dev_pm_ops imx678_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx678_suspend, imx678_resume)
	SET_RUNTIME_PM_OPS(imx678_power_off, imx678_power_on, NULL)
};

static struct i2c_driver imx678_i2c_driver = {
	.driver = {
		.name = "imx678",
		.of_match_table	= imx678_dt_ids,
		.pm = &imx678_pm_ops,
	},
	.probe = imx678_probe,
	.remove = imx678_remove,
};

module_i2c_driver(imx678_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("Sony IMX678 sensor driver");
MODULE_LICENSE("GPL");
