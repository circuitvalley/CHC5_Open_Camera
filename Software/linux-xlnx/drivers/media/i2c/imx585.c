// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX585 sensor driver
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
#define V4L2_CID_IMX585_VMAX		(V4L2_CID_USER_IMX585_BASE + 7)
#define V4L2_CID_IMX585_HMAX		(V4L2_CID_USER_IMX585_BASE + 8)
#define V4L2_CID_IMX585_SHR		(V4L2_CID_USER_IMX585_BASE + 9)

#define IMX585_REG_MODE_SELECT		0x3000
#define IMX585_MODE_STANDBY		0x01
#define IMX585_MODE_STREAMING		0x00
#define IMX585_REG_REGHOLD		0x3001
#define IMX585_REG_XMSTA		0x3002

#define IMX585_REG_INCK_SEL		0x3014
#define IMX585_REG_DATARATE_SEL		0x3015
#define IMX585_REG_LANEMODE		0x3040

#define IMX585_REG_WINMODE		0x3018
#define IMX585_WINMODE_ALL_PIXEL	0x10
#define IMX585_WINMODE_CROP		0x14
#define IMX585_REG_ADDMODE		0x301b
#define IMX585_REG_BIN_MODE		0x3019
#define IMX585_REG_HREVERSE		0x3020
#define IMX585_REG_VREVERSE		0x3021
#define IMX585_REG_ADBIT		0x3022
#define IMX585_REG_MDBIT		0x3023
#define IMX585_REG_PIX_HST		0x303c
#define IMX585_REG_PIX_HWIDTH		0x303e
#define IMX585_REG_PIX_VST		0x3044
#define IMX585_REG_PIX_VWIDTH		0x3046
#define IMX585_REG_DIG_CLP_VSTART	0x30d5

#define IMX585_REG_VMAX			0x3028
#define IMX585_VMAX_MAX			0xfffff
#define IMX585_REG_HMAX			0x302c
#define IMX585_HMAX_MAX			0xffff
#define IMX585_REG_SHR0			0x3050

#define IMX585_HMAX_LINE_OVERHEAD	40
#define IMX585_HMAX_ABS_MIN		280
#define IMX585_BPP_12			12

#define IMX585_VBLANK_MIN		70

#define IMX585_SHR0_MIN			8
#define IMX585_EXPOSURE_OFFSET		IMX585_SHR0_MIN
#define IMX585_EXPOSURE_MIN		2
#define IMX585_EXPOSURE_STEP		1
#define IMX585_EXPOSURE_DEFAULT		1000

#define IMX585_REG_ANALOG_GAIN		0x306c
#define IMX585_GAIN_DB10_PER_CODE	3
#define IMX585_ANA_GAIN_REG_MAX		240
#define IMX585_ANA_GAIN_MIN		0
#define IMX585_ANA_GAIN_MAX		720
#define IMX585_ANA_GAIN_STEP		3
#define IMX585_ANA_GAIN_DEFAULT		0

static unsigned int imx585_gain_code(unsigned int db10)
{
	return db10 / IMX585_GAIN_DB10_PER_CODE;
}

#define IMX585_REG_FDG_SEL0		0x3030
#define IMX585_ANA_GAIN_HCG_LEVEL	51
#define IMX585_ANA_GAIN_HCG_MIN		34
#define IMX585_ANA_GAIN_HCG_THRESHOLD	(IMX585_ANA_GAIN_HCG_MIN + \
					 IMX585_ANA_GAIN_HCG_LEVEL)

#define IMX585_REG_BLKLEVEL		0x30dc
#define IMX585_BLKLEVEL_REG_DEFAULT	0x32
#define IMX585_BLKLEVEL_REG_MAX		0x3ff
#define IMX585_BLKLEVEL_SHIFT		2
#define IMX585_BLKLEVEL_DEFAULT		(IMX585_BLKLEVEL_REG_DEFAULT << \
					 IMX585_BLKLEVEL_SHIFT)
#define IMX585_BLKLEVEL_MAX		(IMX585_BLKLEVEL_REG_MAX << \
					 IMX585_BLKLEVEL_SHIFT)

#define IMX585_REG_TPG_EN_DUOUT		0x30e0
#define IMX585_REG_TPG_PATSEL		0x30e2
#define IMX585_REG_TPG_COLORWIDTH	0x30e4
#define IMX585_REG_TESTCLKEN		0x5300

#define IMX585_INTERNAL_CLOCK		74250000U

#define IMX585_EMBEDDED_LINE_WIDTH	16384
#define IMX585_NUM_EMBEDDED_LINES	1

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

#define IMX585_NATIVE_WIDTH		3856U
#define IMX585_NATIVE_HEIGHT		2180U
#define IMX585_PIXEL_ARRAY_LEFT		8U
#define IMX585_PIXEL_ARRAY_TOP		12U
#define IMX585_PIXEL_ARRAY_WIDTH	3840U
#define IMX585_PIXEL_ARRAY_HEIGHT	2160U

#define IMX585_MIN_WIDTH		64U
#define IMX585_MIN_HEIGHT		240U
#define IMX585_WIDTH_STEP		16U
#define IMX585_HEIGHT_STEP		4U
#define IMX585_CROP_LEFT_STEP		2U
#define IMX585_CROP_TOP_STEP		4U

struct imx585_reg {
	u16 address;
	u8 val;
};

struct imx585_reg_list {
	unsigned int num_of_regs;
	const struct imx585_reg *regs;
};

enum imx585_base_mode {
	IMX585_BASE_MODE_FULL,
	IMX585_BASE_MODE_2X2_BIN,
};

struct imx585_base_config {
	enum imx585_base_mode id;
	bool binning;
	unsigned int max_width;
	unsigned int max_height;
};

enum {
	IMX585_LINK_FREQ_297MHZ,
	IMX585_LINK_FREQ_360MHZ,
	IMX585_LINK_FREQ_445MHZ,
	IMX585_LINK_FREQ_594MHZ,
	IMX585_LINK_FREQ_720MHZ,
	IMX585_LINK_FREQ_891MHZ,
	IMX585_LINK_FREQ_1039MHZ,
	IMX585_LINK_FREQ_1188MHZ,
};

static const s64 link_freqs[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 297000000,
	[IMX585_LINK_FREQ_360MHZ]  = 360000000,
	[IMX585_LINK_FREQ_445MHZ]  = 445500000,
	[IMX585_LINK_FREQ_594MHZ]  = 594000000,
	[IMX585_LINK_FREQ_720MHZ]  = 720000000,
	[IMX585_LINK_FREQ_891MHZ]  = 891000000,
	[IMX585_LINK_FREQ_1039MHZ] = 1039500000,
	[IMX585_LINK_FREQ_1188MHZ] = 1188000000,
};

static const u8 link_freq_datarate_sel[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 0x07,
	[IMX585_LINK_FREQ_360MHZ]  = 0x06,
	[IMX585_LINK_FREQ_445MHZ]  = 0x05,
	[IMX585_LINK_FREQ_594MHZ]  = 0x04,
	[IMX585_LINK_FREQ_720MHZ]  = 0x03,
	[IMX585_LINK_FREQ_891MHZ]  = 0x02,
	[IMX585_LINK_FREQ_1039MHZ] = 0x01,
	[IMX585_LINK_FREQ_1188MHZ] = 0x00,
};

static const u16 hmax_min_4lane_12bit[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 1584,
	[IMX585_LINK_FREQ_360MHZ]  = 1320,
	[IMX585_LINK_FREQ_445MHZ]  = 1100,
	[IMX585_LINK_FREQ_594MHZ]  = 792,
	[IMX585_LINK_FREQ_720MHZ]  = 660,
	[IMX585_LINK_FREQ_891MHZ]  = 550,
	[IMX585_LINK_FREQ_1039MHZ] = 440,
	[IMX585_LINK_FREQ_1188MHZ] = 396,
};

static const u16 hmax_min_4lane_10bit[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 1320,
	[IMX585_LINK_FREQ_360MHZ]  = 1100,
	[IMX585_LINK_FREQ_445MHZ]  = 1100,
	[IMX585_LINK_FREQ_594MHZ]  = 660,
	[IMX585_LINK_FREQ_720MHZ]  = 550,
	[IMX585_LINK_FREQ_891MHZ]  = 550,
	[IMX585_LINK_FREQ_1039MHZ] = 440,
	[IMX585_LINK_FREQ_1188MHZ] = 396,
};

struct imx585_inck_cfg {
	u32 xclk_hz;
	u8 inck_sel;
};

static const struct imx585_inck_cfg imx585_inck_table[] = {
	{ 74250000, 0x00 },
	{ 37125000, 0x01 },
	{ 72000000, 0x02 },
	{ 27000000, 0x03 },
	{ 24000000, 0x04 },
};

static const struct imx585_reg mode_common_regs[] = {
	{0x301a, 0x00},
	{0x3024, 0x00},
	{0x3069, 0x00},
	{0x3074, 0x64},
	{0x3030, 0x00},
	{0x30a4, 0x0a},
	{0x30a6, 0x00},
	{0x3081, 0x00},
	{0x3a4c, 0x39},
	{0x3a4d, 0x01},
	{0x3a50, 0x48},
	{0x3a51, 0x01},
	{0x3e10, 0x10},
	{0x3460, 0x21},
	{0x3478, 0xa1},
	{0x347c, 0x01},
	{0x3480, 0x01},
	{0x3a4e, 0x14},
	{0x3a52, 0x14},
	{0x3a56, 0x00},
	{0x3a5a, 0x00},
	{0x3a5e, 0x00},
	{0x3a62, 0x00},
	{0x3a6a, 0x20},
	{0x3a6c, 0x42},
	{0x3a6e, 0xa0},
	{0x3b2c, 0x0c},
	{0x3b30, 0x1c},
	{0x3b34, 0x0c},
	{0x3b38, 0x1c},
	{0x3ba0, 0x0c},
	{0x3ba4, 0x1c},
	{0x3ba8, 0x0c},
	{0x3bac, 0x1c},
	{0x3d3c, 0x11},
	{0x3d46, 0x0b},
	{0x3de0, 0x3f},
	{0x3de1, 0x08},
	{0x3e14, 0x87},
	{0x3e16, 0x91},
	{0x3e18, 0x91},
	{0x3e1a, 0x87},
	{0x3e1c, 0x78},
	{0x3e1e, 0x50},
	{0x3e20, 0x50},
	{0x3e22, 0x50},
	{0x3e24, 0x87},
	{0x3e26, 0x91},
	{0x3e28, 0x91},
	{0x3e2a, 0x87},
	{0x3e2c, 0x78},
	{0x3e2e, 0x50},
	{0x3e30, 0x50},
	{0x3e32, 0x50},
	{0x3e34, 0x87},
	{0x3e36, 0x91},
	{0x3e38, 0x91},
	{0x3e3a, 0x87},
	{0x3e3c, 0x78},
	{0x3e3e, 0x50},
	{0x3e40, 0x50},
	{0x3e42, 0x50},
	{0x4054, 0x64},
	{0x4148, 0xfe},
	{0x4149, 0x05},
	{0x414a, 0xff},
	{0x414b, 0x05},
	{0x420a, 0x03},
	{0x423d, 0x9c},
	{0x4242, 0xb4},
	{0x4246, 0xb4},
	{0x424e, 0xb4},
	{0x425c, 0xb4},
	{0x425e, 0xb6},
	{0x426c, 0xb4},
	{0x426e, 0xb6},
	{0x428c, 0xb4},
	{0x428e, 0xb6},
	{0x4708, 0x00},
	{0x4709, 0x00},
	{0x470a, 0xff},
	{0x470b, 0x03},
	{0x470c, 0x00},
	{0x470d, 0x00},
	{0x470e, 0xff},
	{0x470f, 0x03},
	{0x47eb, 0x1c},
	{0x47f0, 0xa6},
	{0x47f2, 0xa6},
	{0x47f4, 0xa0},
	{0x47f6, 0x96},
	{0x4808, 0xa6},
	{0x480a, 0xa6},
	{0x480c, 0xa0},
	{0x480e, 0x96},
	{0x492c, 0xb2},
	{0x4930, 0x03},
	{0x4932, 0x03},
	{0x4936, 0x5b},
	{0x4938, 0x82},
	{0x493e, 0x23},
	{0x4ba8, 0x1c},
	{0x4ba9, 0x03},
	{0x4bac, 0x1c},
	{0x4bad, 0x1c},
	{0x4bae, 0x1c},
	{0x4baf, 0x1c},
	{0x4bb0, 0x1c},
	{0x4bb1, 0x1c},
	{0x4bb2, 0x1c},
	{0x4bb3, 0x1c},
	{0x4bb4, 0x1c},
	{0x4bb8, 0x03},
	{0x4bb9, 0x03},
	{0x4bba, 0x03},
	{0x4bbb, 0x03},
	{0x4bbc, 0x03},
	{0x4bbd, 0x03},
	{0x4bbe, 0x03},
	{0x4bbf, 0x03},
	{0x4bc0, 0x03},
	{0x4c14, 0x87},
	{0x4c16, 0x91},
	{0x4c18, 0x91},
	{0x4c1a, 0x87},
	{0x4c1c, 0x78},
	{0x4c1e, 0x50},
	{0x4c20, 0x50},
	{0x4c22, 0x50},
	{0x4c24, 0x87},
	{0x4c26, 0x91},
	{0x4c28, 0x91},
	{0x4c2a, 0x87},
	{0x4c2c, 0x78},
	{0x4c2e, 0x50},
	{0x4c30, 0x50},
	{0x4c32, 0x50},
	{0x4c34, 0x87},
	{0x4c36, 0x91},
	{0x4c38, 0x91},
	{0x4c3a, 0x87},
	{0x4c3c, 0x78},
	{0x4c3e, 0x50},
	{0x4c40, 0x50},
	{0x4c42, 0x50},
	{0x4d12, 0x1f},
	{0x4d13, 0x1e},
	{0x4d26, 0x33},
	{0x4e0e, 0x59},
	{0x4e14, 0x55},
	{0x4e16, 0x59},
	{0x4e1e, 0x3b},
	{0x4e20, 0x47},
	{0x4e22, 0x54},
	{0x4e26, 0x81},
	{0x4e2c, 0x7d},
	{0x4e2e, 0x81},
	{0x4e36, 0x63},
	{0x4e38, 0x6f},
	{0x4e3a, 0x7c},
	{0x4f3a, 0x3c},
	{0x4f3c, 0x46},
	{0x4f3e, 0x59},
	{0x4f42, 0x64},
	{0x4f44, 0x6e},
	{0x4f46, 0x81},
	{0x4f4a, 0x82},
	{0x4f5a, 0x81},
	{0x4f62, 0xaa},
	{0x4f72, 0xa9},
	{0x4f78, 0x36},
	{0x4f7a, 0x41},
	{0x4f7c, 0x61},
	{0x4f7d, 0x01},
	{0x4f7e, 0x7c},
	{0x4f7f, 0x01},
	{0x4f80, 0x77},
	{0x4f82, 0x7b},
	{0x4f88, 0x37},
	{0x4f8a, 0x40},
	{0x4f8c, 0x62},
	{0x4f8d, 0x01},
	{0x4f8e, 0x76},
	{0x4f8f, 0x01},
	{0x4f90, 0x5e},
	{0x4f91, 0x02},
	{0x4f92, 0x69},
	{0x4f93, 0x02},
	{0x4f94, 0x89},
	{0x4f95, 0x02},
	{0x4f96, 0xa4},
	{0x4f97, 0x02},
	{0x4f98, 0x9f},
	{0x4f99, 0x02},
	{0x4f9a, 0xa3},
	{0x4f9b, 0x02},
	{0x4fa0, 0x5f},
	{0x4fa1, 0x02},
	{0x4fa2, 0x68},
	{0x4fa3, 0x02},
	{0x4fa4, 0x8a},
	{0x4fa5, 0x02},
	{0x4fa6, 0x9e},
	{0x4fa7, 0x02},
	{0x519e, 0x79},
	{0x51a6, 0xa1},
	{0x51f0, 0xac},
	{0x51f2, 0xaa},
	{0x51f4, 0xa5},
	{0x51f6, 0xa0},
	{0x5200, 0x9b},
	{0x5202, 0x91},
	{0x5204, 0x87},
	{0x5206, 0x82},
	{0x5208, 0xac},
	{0x520a, 0xaa},
	{0x520c, 0xa5},
	{0x520e, 0xa0},
	{0x5210, 0x9b},
	{0x5212, 0x91},
	{0x5214, 0x87},
	{0x5216, 0x82},
	{0x5218, 0xac},
	{0x521a, 0xaa},
	{0x521c, 0xa5},
	{0x521e, 0xa0},
	{0x5220, 0x9b},
	{0x5222, 0x91},
	{0x5224, 0x87},
	{0x5226, 0x82},
};

static const struct imx585_reg base_full_12bit_regs[] = {
	{IMX585_REG_ADBIT, 0x02},
	{IMX585_REG_MDBIT, 0x01},
	{0x3930, 0x0c},
	{0x3931, 0x01},
	{0x4231, 0x08},
	{0x4940, 0x23},
	{IMX585_REG_ADDMODE, 0x00},
	{IMX585_REG_DIG_CLP_VSTART, 0x04},
};

static const struct imx585_reg base_full_10bit_regs[] = {
	{IMX585_REG_ADBIT, 0x00},
	{IMX585_REG_MDBIT, 0x00},
	{0x3930, 0x66},
	{0x3931, 0x00},
	{0x4231, 0x18},
	{0x493c, 0x23},
	{IMX585_REG_ADDMODE, 0x00},
	{IMX585_REG_DIG_CLP_VSTART, 0x04},
};

static const struct imx585_reg base_2x2bin_12bit_regs[] = {
	{IMX585_REG_ADBIT, 0x00},
	{IMX585_REG_MDBIT, 0x01},
	{0x3930, 0x66},
	{0x3931, 0x00},
	{0x4231, 0x18},
	{IMX585_REG_ADDMODE, 0x01},
	{IMX585_REG_DIG_CLP_VSTART, 0x02},
};

static const struct imx585_reg base_2x2bin_10bit_regs[] = {
	{IMX585_REG_ADBIT, 0x00},
	{IMX585_REG_MDBIT, 0x00},
	{0x3930, 0x66},
	{0x3931, 0x00},
	{0x4231, 0x18},
	{0x493c, 0x23},
	{IMX585_REG_ADDMODE, 0x01},
	{IMX585_REG_DIG_CLP_VSTART, 0x02},
};

static const struct imx585_base_config base_configs[] = {
	[IMX585_BASE_MODE_FULL] = {
		.id = IMX585_BASE_MODE_FULL,
		.binning = false,
		.max_width = IMX585_PIXEL_ARRAY_WIDTH,
		.max_height = IMX585_PIXEL_ARRAY_HEIGHT,
	},
	[IMX585_BASE_MODE_2X2_BIN] = {
		.id = IMX585_BASE_MODE_2X2_BIN,
		.binning = true,
		.max_width = IMX585_PIXEL_ARRAY_WIDTH / 2,
		.max_height = IMX585_PIXEL_ARRAY_HEIGHT / 2,
	},
};

#define IMX585_NUM_BASE_CONFIGS ARRAY_SIZE(base_configs)

static const u32 codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
};

static const u32 mono_codes[] = {
	MEDIA_BUS_FMT_Y12_1X12,
	MEDIA_BUS_FMT_Y10_1X10,
};

static const char * const imx585_test_pattern_menu[] = {
	"Disabled",
	"All 000h",
	"All FFFh",
	"All 555h",
	"All AAAh",
	"Horizontal Color Bars",
	"Vertical Color Bars",
};

static const u8 imx585_test_pattern_val[] = {
	0x00,
	0x00,
	0x01,
	0x02,
	0x03,
	0x0a,
	0x0b,
};

static const char * const imx585_supply_name[] = {
	"VANA",
	"VDIG",
	"VDDL",
};

#define IMX585_NUM_SUPPLIES ARRAY_SIZE(imx585_supply_name)

#define IMX585_XCLR_MIN_DELAY_US	20000
#define IMX585_XCLR_DELAY_RANGE_US	1000
#define IMX585_STREAM_DELAY_US		25000
#define IMX585_STREAM_DELAY_RANGE_US	1000

struct imx585 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	struct clk *xclk;
	u32 xclk_freq;
	u8 inck_sel_val;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX585_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *blklevel;

	struct v4l2_ctrl *vmax_ctrl;
	struct v4l2_ctrl *hmax_ctrl;
	struct v4l2_ctrl *shr_ctrl;

	unsigned int link_freq_idx;
	unsigned int num_lanes;
	bool mono;

	const struct imx585_base_config *base_cfg;

	unsigned int out_width;
	unsigned int out_height;

	struct v4l2_rect crop;
	struct v4l2_rect compose;

	unsigned int hmax_min;
	unsigned int hmax;
	unsigned int vmax;
	u64 pix_rate;

	struct mutex mutex;

	bool streaming;
	bool common_regs_written;
};

static inline struct imx585 *to_imx585(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx585, sd);
}

static int imx585_read_reg(struct imx585 *imx585, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
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

static int imx585_write_reg(struct imx585 *imx585, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
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

static int imx585_write_regs(struct imx585 *imx585,
			     const struct imx585_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx585_write_reg(imx585, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static void imx585_reghold(struct imx585 *imx585, bool hold)
{
	imx585_write_reg(imx585, IMX585_REG_REGHOLD, 1, hold ? 1 : 0);
}

static bool imx585_code_is_10bit(u32 code)
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

static u32 imx585_get_format_code(struct imx585 *imx585, u32 code)
{
	unsigned int i;

	lockdep_assert_held(&imx585->mutex);

	if (imx585->mono)
		return imx585_code_is_10bit(code) ? MEDIA_BUS_FMT_Y10_1X10 :
						    MEDIA_BUS_FMT_Y12_1X12;

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == code)
			break;

	if (i >= ARRAY_SIZE(codes))
		i = 0;

	i = (i & ~3) | (imx585->vflip->val ? 2 : 0) |
	    (imx585->hflip->val ? 1 : 0);

	return codes[i];
}

static const struct imx585_base_config *
imx585_binning_from_ratio(const struct v4l2_rect *crop,
			  unsigned int compose_w, unsigned int compose_h)
{
	if (compose_w <= crop->width / 2 &&
	    compose_h <= crop->height / 2 &&
	    compose_w <= base_configs[IMX585_BASE_MODE_2X2_BIN].max_width &&
	    compose_h <= base_configs[IMX585_BASE_MODE_2X2_BIN].max_height)
		return &base_configs[IMX585_BASE_MODE_2X2_BIN];

	return &base_configs[IMX585_BASE_MODE_FULL];
}

static void imx585_clamp_align(unsigned int *width, unsigned int *height,
			       const struct imx585_base_config *cfg)
{
	unsigned int min_h = cfg->binning ? IMX585_MIN_HEIGHT / 2
					  : IMX585_MIN_HEIGHT;

	*width = clamp(*width, IMX585_MIN_WIDTH, cfg->max_width);
	*height = clamp(*height, min_h, cfg->max_height);

	*width = rounddown(*width, IMX585_WIDTH_STEP);
	if (*width < IMX585_MIN_WIDTH)
		*width = IMX585_MIN_WIDTH;

	*height = rounddown(*height, IMX585_HEIGHT_STEP);
	if (*height < min_h)
		*height = min_h;
}

static void imx585_clamp_crop(struct v4l2_rect *crop)
{
	if (crop->left < 0)
		crop->left = 0;
	if (crop->top < 0)
		crop->top = 0;

	crop->left = rounddown(crop->left, IMX585_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX585_CROP_TOP_STEP);

	if (crop->width < IMX585_MIN_WIDTH)
		crop->width = IMX585_MIN_WIDTH;
	if (crop->height < IMX585_MIN_HEIGHT)
		crop->height = IMX585_MIN_HEIGHT;
	if (crop->width > IMX585_PIXEL_ARRAY_WIDTH)
		crop->width = IMX585_PIXEL_ARRAY_WIDTH;
	if (crop->height > IMX585_PIXEL_ARRAY_HEIGHT)
		crop->height = IMX585_PIXEL_ARRAY_HEIGHT;

	crop->width = rounddown(crop->width, IMX585_WIDTH_STEP);
	crop->height = rounddown(crop->height, IMX585_HEIGHT_STEP);

	if (crop->left + crop->width > IMX585_PIXEL_ARRAY_WIDTH)
		crop->left = IMX585_PIXEL_ARRAY_WIDTH - crop->width;
	if (crop->top + crop->height > IMX585_PIXEL_ARRAY_HEIGHT)
		crop->top = IMX585_PIXEL_ARRAY_HEIGHT - crop->height;

	crop->left = rounddown(crop->left, IMX585_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX585_CROP_TOP_STEP);
}

static void imx585_tighten_crop(struct imx585 *imx585,
				const struct imx585_base_config *cfg)
{
	if (cfg->binning) {
		imx585->crop.width = imx585->compose.width * 2;
		imx585->crop.height = imx585->compose.height * 2;
	} else {
		imx585->crop.width = imx585->compose.width;
		imx585->crop.height = imx585->compose.height;
	}
	imx585_clamp_crop(&imx585->crop);
}

static const struct imx585_reg_list *
imx585_base_reg_list(const struct imx585_base_config *cfg, u32 fmt_code)
{
	static const struct imx585_reg_list full_12 = {
		ARRAY_SIZE(base_full_12bit_regs), base_full_12bit_regs };
	static const struct imx585_reg_list full_10 = {
		ARRAY_SIZE(base_full_10bit_regs), base_full_10bit_regs };
	static const struct imx585_reg_list bin_12 = {
		ARRAY_SIZE(base_2x2bin_12bit_regs), base_2x2bin_12bit_regs };
	static const struct imx585_reg_list bin_10 = {
		ARRAY_SIZE(base_2x2bin_10bit_regs), base_2x2bin_10bit_regs };

	if (cfg->binning)
		return imx585_code_is_10bit(fmt_code) ? &bin_10 : &bin_12;
	return imx585_code_is_10bit(fmt_code) ? &full_10 : &full_12;
}

static int imx585_write_dynamic_regs(struct imx585 *imx585)
{
	const struct v4l2_rect *crop = &imx585->crop;
	int ret;

	imx585_reghold(imx585, true);

	ret = imx585_write_reg(imx585, IMX585_REG_WINMODE, 1,
			       IMX585_WINMODE_CROP);
	if (ret)
		goto release_hold;

	ret = imx585_write_reg(imx585, IMX585_REG_PIX_HST, 2,
			       IMX585_PIXEL_ARRAY_LEFT + crop->left);
	if (ret)
		goto release_hold;

	ret = imx585_write_reg(imx585, IMX585_REG_PIX_HWIDTH, 2, crop->width);
	if (ret)
		goto release_hold;

	ret = imx585_write_reg(imx585, IMX585_REG_PIX_VST, 2,
			       IMX585_PIXEL_ARRAY_TOP + crop->top);
	if (ret)
		goto release_hold;

	ret = imx585_write_reg(imx585, IMX585_REG_PIX_VWIDTH, 2, crop->height);
	if (ret)
		goto release_hold;

	ret = imx585_write_reg(imx585, IMX585_REG_HMAX, 2, imx585->hmax);
	if (ret)
		goto release_hold;

	ret = imx585_write_reg(imx585, IMX585_REG_VMAX, 3, imx585->vmax);

release_hold:
	imx585_reghold(imx585, false);

	return ret;
}

static unsigned int imx585_hmax_min(struct imx585 *imx585, u32 fmt_code)
{
	const u16 *table = imx585_code_is_10bit(fmt_code) ?
			   hmax_min_4lane_10bit : hmax_min_4lane_12bit;
	unsigned int table_min = table[imx585->link_freq_idx] *
				 (4 / imx585->num_lanes);
	unsigned int hmax;
	u64 payload;

	if (imx585_code_is_10bit(fmt_code))
		return table_min;

	payload = div64_u64((u64)imx585->crop.width * IMX585_BPP_12 *
			    IMX585_INTERNAL_CLOCK,
			    2ULL * link_freqs[imx585->link_freq_idx] *
			    imx585->num_lanes);

	hmax = (unsigned int)payload + IMX585_HMAX_LINE_OVERHEAD;

	return max(hmax, (unsigned int)IMX585_HMAX_ABS_MIN);
}

static void imx585_set_default_format(struct imx585 *imx585)
{
	imx585->base_cfg = &base_configs[IMX585_BASE_MODE_FULL];
	imx585->out_width = IMX585_PIXEL_ARRAY_WIDTH;
	imx585->out_height = IMX585_PIXEL_ARRAY_HEIGHT;
	imx585->fmt_code = imx585->mono ? MEDIA_BUS_FMT_Y12_1X12 :
					  MEDIA_BUS_FMT_SRGGB12_1X12;

	imx585->crop.left = 0;
	imx585->crop.top = 0;
	imx585->crop.width = IMX585_PIXEL_ARRAY_WIDTH;
	imx585->crop.height = IMX585_PIXEL_ARRAY_HEIGHT;

	imx585->compose.left = 0;
	imx585->compose.top = 0;
	imx585->compose.width = IMX585_PIXEL_ARRAY_WIDTH;
	imx585->compose.height = IMX585_PIXEL_ARRAY_HEIGHT;

	imx585->hmax_min = imx585_hmax_min(imx585, imx585->fmt_code);
	imx585->hmax = imx585->hmax_min;
	imx585->vmax = imx585->crop.height + IMX585_VBLANK_MIN;
}

static int imx585_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx585 *imx585 = to_imx585(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;
	struct v4l2_rect *try_compose;

	mutex_lock(&imx585->mutex);

	try_fmt_img->width = IMX585_PIXEL_ARRAY_WIDTH;
	try_fmt_img->height = IMX585_PIXEL_ARRAY_HEIGHT;
	try_fmt_img->code = imx585_get_format_code(imx585,
						   MEDIA_BUS_FMT_SRGGB12_1X12);
	try_fmt_img->field = V4L2_FIELD_NONE;

	try_fmt_meta->width = IMX585_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX585_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = IMX585_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX585_PIXEL_ARRAY_HEIGHT;

	try_compose = v4l2_subdev_state_get_compose(fh->state, IMAGE_PAD);
	try_compose->left = 0;
	try_compose->top = 0;
	try_compose->width = IMX585_PIXEL_ARRAY_WIDTH;
	try_compose->height = IMX585_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx585->mutex);

	return 0;
}

static void imx585_adjust_exposure_range(struct imx585 *imx585)
{
	int exposure_max, exposure_def;

	exposure_max = imx585->vmax - IMX585_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx585->exposure->val);
	__v4l2_ctrl_modify_range(imx585->exposure, imx585->exposure->minimum,
				 exposure_max, imx585->exposure->step,
				 exposure_def);
}

static int imx585_set_exposure(struct imx585 *imx585, unsigned int exp_lines)
{
	u32 shr0;

	if (exp_lines > imx585->vmax)
		exp_lines = imx585->vmax;

	shr0 = imx585->vmax - exp_lines;
	shr0 &= ~1U;
	shr0 = clamp_t(u32, shr0, IMX585_SHR0_MIN,
		       imx585->vmax - IMX585_EXPOSURE_MIN);
	shr0 &= ~1U;

	return imx585_write_reg(imx585, IMX585_REG_SHR0, 3, shr0);
}

static unsigned int imx585_hblank_to_hmax(struct imx585 *imx585,
					  unsigned int hblank)
{
	u64 hmax = (u64)(imx585->out_width + hblank) * imx585->hmax_min;

	do_div(hmax, imx585->out_width);
	return min_t(u64, hmax, IMX585_HMAX_MAX);
}

static int imx585_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx585 *imx585 =
		container_of(ctrl->handler, struct imx585, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		imx585->vmax = (imx585->out_height + ctrl->val) & ~1U;
		imx585_adjust_exposure_range(imx585);
	}

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN: {
		unsigned int gain = imx585_gain_code(ctrl->val);
		bool hcg = false;

		if (gain >= IMX585_ANA_GAIN_HCG_THRESHOLD) {
			hcg = true;
			gain -= IMX585_ANA_GAIN_HCG_LEVEL;
		}

		imx585_reghold(imx585, true);
		ret = imx585_write_reg(imx585, IMX585_REG_ANALOG_GAIN, 2, gain);
		if (!ret)
			ret = imx585_write_reg(imx585, IMX585_REG_FDG_SEL0, 1,
					       hcg ? 0x01 : 0x00);
		imx585_reghold(imx585, false);
		break;
	}
	case V4L2_CID_EXPOSURE:
		ret = imx585_set_exposure(imx585, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		imx585_reghold(imx585, true);
		ret = imx585_write_reg(imx585, IMX585_REG_VMAX, 3,
				       imx585->vmax);
		if (!ret)
			ret = imx585_set_exposure(imx585,
						  imx585->exposure->val);
		imx585_reghold(imx585, false);
		break;
	case V4L2_CID_HBLANK:
		imx585->hmax = imx585_hblank_to_hmax(imx585, ctrl->val);
		ret = imx585_write_reg(imx585, IMX585_REG_HMAX, 2,
				       imx585->hmax);
		break;
	case V4L2_CID_HFLIP:
		ret = imx585_write_reg(imx585, IMX585_REG_HREVERSE, 1,
				       ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = imx585_write_reg(imx585, IMX585_REG_VREVERSE, 1,
				       ctrl->val);
		break;
	case V4L2_CID_BLACK_LEVEL:
		ret = imx585_write_reg(imx585, IMX585_REG_BLKLEVEL, 2,
				       ctrl->val >> IMX585_BLKLEVEL_SHIFT);
		break;
	case V4L2_CID_IMX585_VMAX:
		if (!ctrl->val)
			break;
		ret = imx585_write_reg(imx585, IMX585_REG_VMAX, 3, ctrl->val);
		break;
	case V4L2_CID_IMX585_HMAX:
		if (!ctrl->val)
			break;
		ret = imx585_write_reg(imx585, IMX585_REG_HMAX, 2, ctrl->val);
		break;
	case V4L2_CID_IMX585_SHR:
		if (!ctrl->val)
			break;
		ret = imx585_write_reg(imx585, IMX585_REG_SHR0, 3, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			imx585_write_reg(imx585, IMX585_REG_BLKLEVEL, 2, 0);
			imx585_write_reg(imx585, IMX585_REG_TPG_PATSEL, 1,
					 imx585_test_pattern_val[ctrl->val]);
			imx585_write_reg(imx585, IMX585_REG_TPG_COLORWIDTH, 1,
					 0x00);
			imx585_write_reg(imx585, IMX585_REG_TESTCLKEN, 1,
					 0x0a);
			ret = imx585_write_reg(imx585,
					       IMX585_REG_TPG_EN_DUOUT, 1, 1);
		} else {
			imx585_write_reg(imx585, IMX585_REG_TPG_EN_DUOUT, 1,
					 0);
			imx585_write_reg(imx585, IMX585_REG_TESTCLKEN, 1,
					 0x02);
			ret = imx585_write_reg(imx585, IMX585_REG_BLKLEVEL, 2,
					       imx585->blklevel->val >>
					       IMX585_BLKLEVEL_SHIFT);
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

#define IMX585_VOB_LINES		20
#define IMX585_VOB_LINES_BIN	10

static int imx585_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx585 *imx585 =
		container_of(ctrl->handler, struct imx585, ctrl_handler);

	switch (ctrl->id) {
	case CHC5SENSOR_CID_OB_TOP:
		ctrl->val = imx585->base_cfg && imx585->base_cfg->binning ?
			    IMX585_VOB_LINES_BIN : IMX585_VOB_LINES;
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

static const struct v4l2_ctrl_ops imx585_ctrl_ops = {
	.s_ctrl		 = imx585_set_ctrl,
	.g_volatile_ctrl = imx585_g_volatile_ctrl,
};

#define IMX585_OB_CTRL(_id, _name)					\
	{								\
		.ops	= &imx585_ctrl_ops,				\
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

static const struct v4l2_ctrl_config imx585_ob_ctrls[] = {
	IMX585_OB_CTRL(CHC5SENSOR_CID_OB_LEFT,   "Optical Black Left"),
	IMX585_OB_CTRL(CHC5SENSOR_CID_OB_TOP,    "Optical Black Top"),
	IMX585_OB_CTRL(CHC5SENSOR_CID_OB_RIGHT,  "Optical Black Right"),
	IMX585_OB_CTRL(CHC5SENSOR_CID_OB_BOTTOM, "Optical Black Bottom"),
	IMX585_OB_CTRL(CHC5SENSOR_CID_OB_ON_IMAGE_DT, "Optical Black On Image DT"),
};

static const struct v4l2_ctrl_config imx585_cfg_vmax = {
	.ops  = &imx585_ctrl_ops,
	.id   = V4L2_CID_IMX585_VMAX,
	.name = "VMAX",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX585_VMAX_MAX,
	.step = 1,
};

static const struct v4l2_ctrl_config imx585_cfg_hmax = {
	.ops  = &imx585_ctrl_ops,
	.id   = V4L2_CID_IMX585_HMAX,
	.name = "HMAX",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX585_HMAX_MAX,
	.step = 1,
};

static const struct v4l2_ctrl_config imx585_cfg_shr = {
	.ops  = &imx585_ctrl_ops,
	.id   = V4L2_CID_IMX585_SHR,
	.name = "SHR",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = 0,
	.max  = IMX585_VMAX_MAX,
	.step = 1,
};

static int imx585_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx585 *imx585 = to_imx585(sd);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (imx585->mono) {
			if (code->index >= ARRAY_SIZE(mono_codes))
				return -EINVAL;

			code->code = mono_codes[code->index];
			return 0;
		}

		if (code->index >= (ARRAY_SIZE(codes) / 4))
			return -EINVAL;

		code->code = imx585_get_format_code(imx585,
						    codes[code->index * 4]);
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx585_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx585 *imx585 = to_imx585(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index > 0)
			return -EINVAL;

		if (fse->code != imx585_get_format_code(imx585, fse->code))
			return -EINVAL;

		fse->min_width = IMX585_MIN_WIDTH;
		fse->max_width = IMX585_PIXEL_ARRAY_WIDTH;
		fse->min_height = IMX585_MIN_HEIGHT;
		fse->max_height = IMX585_PIXEL_ARRAY_HEIGHT;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX585_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX585_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx585_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx585_update_image_pad_format(struct imx585 *imx585,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = imx585->out_width;
	fmt->format.height = imx585->out_height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx585_reset_colorspace(&fmt->format);
}

static void imx585_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX585_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX585_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx585_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx585 *imx585 = to_imx585(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx585->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx585_get_format_code(imx585, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx585_update_image_pad_format(imx585, fmt);
			fmt->format.code =
			       imx585_get_format_code(imx585, imx585->fmt_code);
		} else {
			imx585_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx585->mutex);
	return 0;
}

static void imx585_set_framing_limits(struct imx585 *imx585)
{
	unsigned int out_w = imx585->out_width;
	unsigned int out_h = imx585->out_height;
	unsigned int frm_length_min, frm_length_default;
	unsigned int vblank_min, vblank_max, vblank_def;
	unsigned int hblank_max;
	u64 pix_rate;

	imx585->hmax_min = imx585_hmax_min(imx585, imx585->fmt_code);
	imx585->hmax = imx585->hmax_min;

	pix_rate = (u64)out_w * IMX585_INTERNAL_CLOCK;
	do_div(pix_rate, imx585->hmax_min);
	imx585->pix_rate = pix_rate;

	__v4l2_ctrl_modify_range(imx585->pixel_rate, pix_rate, pix_rate, 1,
				 pix_rate);

	hblank_max = div_u64((u64)(IMX585_HMAX_MAX - imx585->hmax_min) *
			     out_w, imx585->hmax_min);
	__v4l2_ctrl_modify_range(imx585->hblank, 0, hblank_max, 1, 0);
	__v4l2_ctrl_s_ctrl(imx585->hblank, 0);

	frm_length_min = imx585->crop.height + IMX585_VBLANK_MIN;

	frm_length_default = div_u64((u64)IMX585_INTERNAL_CLOCK,
				     imx585->hmax * 30);
	if (frm_length_default < frm_length_min)
		frm_length_default = frm_length_min;
	if (frm_length_default > IMX585_VMAX_MAX)
		frm_length_default = IMX585_VMAX_MAX;

	vblank_min = round_up(frm_length_min - out_h, 2);
	vblank_max = round_down(IMX585_VMAX_MAX - out_h, 2);
	vblank_def = clamp(round_up(frm_length_default - out_h, 2),
			   vblank_min, vblank_max);

	__v4l2_ctrl_modify_range(imx585->vblank, vblank_min, vblank_max,
				 2, vblank_def);
	__v4l2_ctrl_s_ctrl(imx585->vblank, vblank_def);

	imx585->vmax = (out_h + imx585->vblank->val) & ~1U;

	imx585_adjust_exposure_range(imx585);
}

static int imx585_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx585_base_config *cfg;
	struct imx585 *imx585 = to_imx585(sd);
	unsigned int req_width, req_height;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx585->mutex);

	if (fmt->pad == IMAGE_PAD) {
		fmt->format.code = imx585_get_format_code(imx585,
							  fmt->format.code);

		req_width = fmt->format.width;
		req_height = fmt->format.height;

		cfg = imx585_binning_from_ratio(&imx585->crop,
						req_width, req_height);

		imx585_clamp_align(&req_width, &req_height, cfg);

		fmt->format.width = req_width;
		fmt->format.height = req_height;
		fmt->format.field = V4L2_FIELD_NONE;
		imx585_reset_colorspace(&fmt->format);

		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx585->base_cfg = cfg;
			imx585->out_width = req_width;
			imx585->out_height = req_height;
			imx585->compose.width = req_width;
			imx585->compose.height = req_height;
			imx585->fmt_code = fmt->format.code;

			imx585_tighten_crop(imx585, cfg);

			imx585_set_framing_limits(imx585);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx585_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx585->mutex);

	return 0;
}

static int imx585_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx585 *imx585 = to_imx585(sd);

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&imx585->mutex);
		sel->r = imx585->crop;
		mutex_unlock(&imx585->mutex);
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		mutex_lock(&imx585->mutex);
		sel->r = imx585->compose;
		mutex_unlock(&imx585->mutex);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX585_NATIVE_WIDTH;
		sel->r.height = IMX585_NATIVE_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX585_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX585_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int imx585_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx585 *imx585 = to_imx585(sd);
	const struct imx585_base_config *cfg;

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&imx585->mutex);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_rect crop = sel->r;
		unsigned int compose_w, compose_h;

		imx585_clamp_crop(&crop);

		cfg = imx585_binning_from_ratio(&crop, imx585->compose.width,
						imx585->compose.height);

		compose_w = cfg->binning ? crop.width / 2 : crop.width;
		compose_h = cfg->binning ? crop.height / 2 : crop.height;

		imx585_clamp_align(&compose_w, &compose_h, cfg);

		if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
			*v4l2_subdev_state_get_crop(sd_state, sel->pad) = crop;
		} else {
			imx585->crop = crop;
			imx585->compose.width = compose_w;
			imx585->compose.height = compose_h;
			imx585->base_cfg = cfg;
			imx585->out_width = compose_w;
			imx585->out_height = compose_h;
			imx585_set_framing_limits(imx585);
		}

		sel->r = crop;

	} else {
		unsigned int compose_w = sel->r.width;
		unsigned int compose_h = sel->r.height;

		cfg = imx585_binning_from_ratio(&imx585->crop,
						compose_w, compose_h);

		imx585_clamp_align(&compose_w, &compose_h, cfg);

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY) {
			imx585->compose.left = 0;
			imx585->compose.top = 0;
			imx585->compose.width = compose_w;
			imx585->compose.height = compose_h;
			imx585->base_cfg = cfg;
			imx585->out_width = compose_w;
			imx585->out_height = compose_h;

			imx585_tighten_crop(imx585, cfg);

			cfg = imx585_binning_from_ratio(&imx585->crop,
							compose_w, compose_h);
			imx585->base_cfg = cfg;

			imx585_set_framing_limits(imx585);
		}

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = compose_w;
		sel->r.height = compose_h;
	}

	mutex_unlock(&imx585->mutex);

	return 0;
}

static int imx585_start_streaming(struct imx585 *imx585)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	const struct imx585_reg_list *reg_list;
	int ret;

	if (!imx585->common_regs_written) {
		ret = imx585_write_regs(imx585, mode_common_regs,
					ARRAY_SIZE(mode_common_regs));
		if (!ret)
			ret = imx585_write_reg(imx585, IMX585_REG_INCK_SEL, 1,
					       imx585->inck_sel_val);
		if (!ret)
			ret = imx585_write_reg(imx585, IMX585_REG_DATARATE_SEL,
					       1,
					       link_freq_datarate_sel[imx585->link_freq_idx]);
		if (!ret)
			ret = imx585_write_reg(imx585, IMX585_REG_LANEMODE, 1,
					       imx585->num_lanes == 2 ? 0x01 :
									0x03);
		if (!ret)
			ret = imx585_write_reg(imx585, IMX585_REG_BIN_MODE, 1,
					       imx585->mono ? 0x01 : 0x00);
		if (!ret)
			ret = imx585_write_reg(imx585, IMX585_REG_BLKLEVEL, 2,
					       IMX585_BLKLEVEL_REG_DEFAULT);
		if (ret) {
			dev_err(&client->dev,
				"%s failed to set common settings\n",
				__func__);
			return ret;
		}

		imx585->common_regs_written = true;
	}

	reg_list = imx585_base_reg_list(imx585->base_cfg, imx585->fmt_code);
	ret = imx585_write_regs(imx585, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set base mode\n",
			__func__);
		return ret;
	}

	ret = imx585_write_dynamic_regs(imx585);
	if (ret) {
		dev_err(&client->dev, "%s failed to set dynamic regs\n",
			__func__);
		return ret;
	}

	__v4l2_ctrl_s_ctrl(imx585->vmax_ctrl, 0);
	__v4l2_ctrl_s_ctrl(imx585->hmax_ctrl, 0);
	__v4l2_ctrl_s_ctrl(imx585->shr_ctrl, 0);

	ret = __v4l2_ctrl_handler_setup(imx585->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = imx585_write_reg(imx585, IMX585_REG_MODE_SELECT, 1,
			       IMX585_MODE_STREAMING);
	if (ret)
		return ret;

	usleep_range(IMX585_STREAM_DELAY_US,
		     IMX585_STREAM_DELAY_US + IMX585_STREAM_DELAY_RANGE_US);

	return imx585_write_reg(imx585, IMX585_REG_XMSTA, 1, 0x00);
}

static void imx585_stop_streaming(struct imx585 *imx585)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	int ret;

	ret = imx585_write_reg(imx585, IMX585_REG_XMSTA, 1, 0x01);
	if (!ret)
		ret = imx585_write_reg(imx585, IMX585_REG_MODE_SELECT, 1,
				       IMX585_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int imx585_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx585 *imx585 = to_imx585(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx585->mutex);
	if (imx585->streaming == enable) {
		mutex_unlock(&imx585->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = imx585_start_streaming(imx585);
		if (ret)
			goto err_rpm_put;
	} else {
		imx585_stop_streaming(imx585);
		pm_runtime_put(&client->dev);
	}

	imx585->streaming = enable;

	__v4l2_ctrl_grab(imx585->vflip, enable);
	__v4l2_ctrl_grab(imx585->hflip, enable);

	mutex_unlock(&imx585->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx585->mutex);

	return ret;
}

static int imx585_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx585 *imx585 = to_imx585(sd);
	int ret;

	ret = regulator_bulk_enable(IMX585_NUM_SUPPLIES,
				    imx585->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx585->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx585->reset_gpio, 1);
	usleep_range(IMX585_XCLR_MIN_DELAY_US,
		     IMX585_XCLR_MIN_DELAY_US + IMX585_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(IMX585_NUM_SUPPLIES, imx585->supplies);
	return ret;
}

static int imx585_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx585 *imx585 = to_imx585(sd);

	gpiod_set_value_cansleep(imx585->reset_gpio, 0);
	regulator_bulk_disable(IMX585_NUM_SUPPLIES, imx585->supplies);
	clk_disable_unprepare(imx585->xclk);

	imx585->common_regs_written = false;

	return 0;
}

static int __maybe_unused imx585_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx585 *imx585 = to_imx585(sd);

	if (imx585->streaming)
		imx585_stop_streaming(imx585);

	return 0;
}

static int __maybe_unused imx585_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx585 *imx585 = to_imx585(sd);
	int ret;

	if (imx585->streaming) {
		ret = imx585_start_streaming(imx585);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx585_stop_streaming(imx585);
	imx585->streaming = 0;
	return ret;
}

static int imx585_get_regulators(struct imx585 *imx585)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	unsigned int i;

	for (i = 0; i < IMX585_NUM_SUPPLIES; i++)
		imx585->supplies[i].supply = imx585_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX585_NUM_SUPPLIES,
				       imx585->supplies);
}

static int imx585_identify_module(struct imx585 *imx585)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	int ret;
	u32 val;

	ret = imx585_read_reg(imx585, IMX585_REG_BLKLEVEL, 2, &val);
	if (ret) {
		dev_err(&client->dev,
			"failed to read sensor (blklevel probe), error %d\n",
			ret);
		return ret;
	}

	if ((val & IMX585_BLKLEVEL_REG_MAX) != IMX585_BLKLEVEL_REG_DEFAULT)
		dev_warn(&client->dev,
			 "unexpected BLKLEVEL default 0x%x (expected 0x%x)\n",
			 val, IMX585_BLKLEVEL_REG_DEFAULT);

	dev_info(&client->dev, "IMX585 found on %s\n",
		 dev_name(&client->adapter->dev));

	return 0;
}

static const struct v4l2_subdev_core_ops imx585_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx585_video_ops = {
	.s_stream = imx585_set_stream,
};

static const struct v4l2_subdev_pad_ops imx585_pad_ops = {
	.enum_mbus_code = imx585_enum_mbus_code,
	.get_fmt = imx585_get_pad_format,
	.set_fmt = imx585_set_pad_format,
	.get_selection = imx585_get_selection,
	.set_selection = imx585_set_selection,
	.enum_frame_size = imx585_enum_frame_size,
};

static const struct v4l2_subdev_ops imx585_subdev_ops = {
	.core = &imx585_core_ops,
	.video = &imx585_video_ops,
	.pad = &imx585_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx585_internal_ops = {
	.open = imx585_open,
};

static int imx585_init_controls(struct imx585 *imx585)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx585->sd);
	struct v4l2_fwnode_device_properties props;
	int ret;

	ctrl_hdlr = &imx585->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 25);
	if (ret)
		return ret;

	mutex_init(&imx585->mutex);
	ctrl_hdlr->lock = &imx585->mutex;

	imx585->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       1, INT_MAX, 1,
					       IMX585_INTERNAL_CLOCK);
	if (imx585->pixel_rate)
		imx585->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx585->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx585_ctrl_ops,
				       V4L2_CID_LINK_FREQ, 0, 0,
				       &link_freqs[imx585->link_freq_idx]);
	if (imx585->link_freq)
		imx585->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx585->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					   V4L2_CID_VBLANK, IMX585_VBLANK_MIN,
					   IMX585_VMAX_MAX - 1, 2,
					   IMX585_VBLANK_MIN);
	imx585->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	imx585->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX585_EXPOSURE_MIN,
					     IMX585_VMAX_MAX -
						IMX585_EXPOSURE_OFFSET,
					     IMX585_EXPOSURE_STEP,
					     IMX585_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX585_ANA_GAIN_MIN, IMX585_ANA_GAIN_MAX,
			  IMX585_ANA_GAIN_STEP, IMX585_ANA_GAIN_DEFAULT);

	imx585->blklevel = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					     V4L2_CID_BLACK_LEVEL, 0,
					     IMX585_BLKLEVEL_MAX, 1,
					     IMX585_BLKLEVEL_DEFAULT);

	imx585->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx585->hflip)
		imx585->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx585->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx585_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx585->vflip)
		imx585->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx585_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx585_test_pattern_menu) - 1,
				     0, 0, imx585_test_pattern_menu);

	imx585->vmax_ctrl = v4l2_ctrl_new_custom(ctrl_hdlr, &imx585_cfg_vmax,
						 NULL);
	imx585->hmax_ctrl = v4l2_ctrl_new_custom(ctrl_hdlr, &imx585_cfg_hmax,
						 NULL);
	imx585->shr_ctrl = v4l2_ctrl_new_custom(ctrl_hdlr, &imx585_cfg_shr,
						NULL);

	{
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(imx585_ob_ctrls); i++)
			v4l2_ctrl_new_custom(ctrl_hdlr, &imx585_ob_ctrls[i], NULL);
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

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx585_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx585->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&imx585->mutex);
	imx585_set_framing_limits(imx585);
	mutex_unlock(&imx585->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx585->mutex);

	return ret;
}

static void imx585_free_controls(struct imx585 *imx585)
{
	v4l2_ctrl_handler_free(imx585->sd.ctrl_handler);
	mutex_destroy(&imx585->mutex);
}

static int imx585_check_hwcfg(struct device *dev, struct imx585 *imx585)
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
	imx585->num_lanes = ep_cfg.bus.mipi_csi2.num_data_lanes;

	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	for (i = 0; i < ARRAY_SIZE(link_freqs); i++) {
		if (link_freqs[i] == ep_cfg.link_frequencies[0]) {
			imx585->link_freq_idx = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(link_freqs)) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
			ret = -EINVAL;
			goto error_out;
	}

	if (imx585->link_freq_idx >= IMX585_LINK_FREQ_1039MHZ) {
		dev_err(dev,
			"Link frequency %lld unsupported: HMAX floor unvalidated above 1782 Mbps/lane\n",
			ep_cfg.link_frequencies[0]);
		ret = -EINVAL;
		goto error_out;
	}

	if (imx585->link_freq_idx > IMX585_LINK_FREQ_445MHZ)
		dev_warn(dev,
			 "Link frequency %lld not supported\n",
			 ep_cfg.link_frequencies[0]);

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static const struct of_device_id imx585_dt_ids[] = {
	{ .compatible = "sony,imx585" },
	{  }
};

static int imx585_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx585 *imx585;
	unsigned int i;
	int ret;

	imx585 = devm_kzalloc(&client->dev, sizeof(*imx585), GFP_KERNEL);
	if (!imx585)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx585->sd, client, &imx585_subdev_ops);

	if (imx585_check_hwcfg(dev, imx585))
		return -EINVAL;

	imx585->mono = of_property_read_bool(dev->of_node, "mono-mode");
	if (imx585->mono)
		dev_info(dev, "configured for the mono sensor variant\n");

	imx585->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx585->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx585->xclk);
	}

	imx585->xclk_freq = clk_get_rate(imx585->xclk);
	for (i = 0; i < ARRAY_SIZE(imx585_inck_table); i++) {
		if (imx585_inck_table[i].xclk_hz == imx585->xclk_freq) {
			imx585->inck_sel_val = imx585_inck_table[i].inck_sel;
			break;
		}
	}
	if (i == ARRAY_SIZE(imx585_inck_table)) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx585->xclk_freq);
		return -EINVAL;
	}

	ret = imx585_get_regulators(imx585);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	imx585->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	ret = imx585_power_on(dev);
	if (ret)
		return ret;

	ret = imx585_identify_module(imx585);
	if (ret)
		goto error_power_off;

	imx585_set_default_format(imx585);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = imx585_init_controls(imx585);
	if (ret)
		goto error_power_off;

	imx585->sd.internal_ops = &imx585_internal_ops;
	imx585->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx585->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx585->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx585->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx585->sd.entity, NUM_PADS, imx585->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx585->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx585->sd.entity);

error_handler_free:
	imx585_free_controls(imx585);

error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	imx585_power_off(&client->dev);

	return ret;
}

static void imx585_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx585 *imx585 = to_imx585(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx585_free_controls(imx585);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx585_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

MODULE_DEVICE_TABLE(of, imx585_dt_ids);

static const struct dev_pm_ops imx585_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx585_suspend, imx585_resume)
	SET_RUNTIME_PM_OPS(imx585_power_off, imx585_power_on, NULL)
};

static struct i2c_driver imx585_i2c_driver = {
	.driver = {
		.name = "imx585",
		.of_match_table	= imx585_dt_ids,
		.pm = &imx585_pm_ops,
	},
	.probe = imx585_probe,
	.remove = imx585_remove,
};

module_i2c_driver(imx585_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("Sony IMX585 sensor driver");
MODULE_LICENSE("GPL");
