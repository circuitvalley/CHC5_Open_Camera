// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX283 sensor driver
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

#define IMX283_REG_STANDBY		0x3000
#define IMX283_STANDBY_CONFIG		0x0a
#define IMX283_STANDBY_RUN		0x00
#define IMX283_STANDBY_STOP		0x0f
#define IMX283_POR_STANDBY_VAL		0x0b

#define IMX283_REG_CLAMP		0x3001
#define IMX283_CLPSQRST			0x10

#define IMX283_REG_PLSTMG08		0x3003
#define IMX283_PLSTMG08_VAL		0x77
#define IMX283_REG_PLSTMG02		0x36aa
#define IMX283_PLSTMG02_VAL		0x00
#define IMX283_REG_STBPL		0x320b
#define IMX283_STBPL_NORMAL		0x00

#define IMX283_REG_PLRD1		0x36c1
#define IMX283_REG_PLRD2		0x36c2
#define IMX283_REG_PLRD3		0x36f7
#define IMX283_REG_PLRD4		0x36f8

#define IMX283_REG_MDSEL1		0x3004
#define IMX283_REG_MDSEL2		0x3005
#define IMX283_REG_MDSEL3		0x3006
#define IMX283_REG_MDSEL4		0x3007
#define IMX283_REG_SMD			0x3008
#define IMX283_REG_SVR			0x3009
#define IMX283_REG_MDSEL7		0x3013
#define IMX283_REG_MDSEL18		0x30f6

#define IMX283_REG_HTRIM_MDVREV		0x300b
#define IMX283_HTRIM_EN_BITS		0x30
#define IMX283_REG_VWINPOS		0x300f
#define IMX283_REG_VWIDCUT		0x3011
#define IMX283_REG_Y_OUT_SIZE		0x302f
#define IMX283_REG_WRITE_VSIZE		0x3031
#define IMX283_REG_OB_SIZE_V		0x3033
#define IMX283_REG_HTRIMMING_START	0x3058
#define IMX283_REG_HTRIMMING_END	0x305a

#define IMX283_INTERNAL_CLOCK		72000000U
#define IMX283_REG_HMAX			0x3036
#define IMX283_HMAX_MAX			0xffff

#define IMX283_PIXELS_PER_CLOCK		8U
#define IMX283_PIXEL_RATE		(IMX283_INTERNAL_CLOCK * \
					 IMX283_PIXELS_PER_CLOCK)
#define IMX283_REG_VMAX			0x3038
#define IMX283_VMAX_MAX			0xfffff
#define IMX283_REG_SHR			0x303b
#define IMX283_SHR_MAX			0xffff
#define IMX283_REG_REGHOLD		0x303f

#define IMX283_EXPOSURE_MIN		4
#define IMX283_EXPOSURE_STEP		1
#define IMX283_EXPOSURE_DEFAULT		1000

#define IMX283_REG_ANALOG_GAIN		0x3042

#define IMX283_DGTL_GAIN_DB10_STEP	60
#define IMX283_DGTL_GAIN_MAX_CODE	3
#define IMX283_ANA_GAIN_PGC_MAX		270
#define IMX283_ANA_GAIN_MIN		0
#define IMX283_ANA_GAIN_MAX		450
#define IMX283_ANA_GAIN_STEP		1
#define IMX283_ANA_GAIN_DEFAULT		0
#define IMX283_REG_DIGITAL_GAIN		0x3044

static const u16 imx283_pgc_code[IMX283_ANA_GAIN_PGC_MAX + 1] = {
	   0,   23,   47,   70,   92,  115,  137,  159,
	 180,  202,  223,  244,  264,  285,  305,  325,
	 345,  364,  383,  402,  421,  440,  458,  476,
	 494,  512,  530,  547,  564,  581,  598,  615,
	 631,  647,  663,  679,  695,  710,  726,  741,
	 756,  771,  785,  800,  814,  828,  842,  856,
	 869,  883,  896,  910,  923,  935,  948,  961,
	 973,  985,  998, 1010, 1022, 1033, 1045, 1056,
	1068, 1079, 1090, 1101, 1112, 1123, 1133, 1144,
	1154, 1164, 1174, 1184, 1194, 1204, 1214, 1223,
	1233, 1242, 1251, 1260, 1269, 1278, 1287, 1296,
	1304, 1313, 1321, 1330, 1338, 1346, 1354, 1362,
	1370, 1378, 1385, 1393, 1400, 1408, 1415, 1422,
	1430, 1437, 1444, 1451, 1457, 1464, 1471, 1477,
	1484, 1490, 1497, 1503, 1509, 1515, 1522, 1528,
	1534, 1539, 1545, 1551, 1557, 1562, 1568, 1573,
	1579, 1584, 1590, 1595, 1600, 1605, 1610, 1615,
	1620, 1625, 1630, 1635, 1639, 1644, 1649, 1653,
	1658, 1662, 1667, 1671, 1675, 1680, 1684, 1688,
	1692, 1696, 1700, 1704, 1708, 1712, 1716, 1720,
	1723, 1727, 1731, 1734, 1738, 1742, 1745, 1749,
	1752, 1755, 1759, 1762, 1765, 1769, 1772, 1775,
	1778, 1781, 1784, 1787, 1790, 1793, 1796, 1799,
	1802, 1805, 1807, 1810, 1813, 1816, 1818, 1821,
	1823, 1826, 1829, 1831, 1834, 1836, 1838, 1841,
	1843, 1846, 1848, 1850, 1852, 1855, 1857, 1859,
	1861, 1863, 1865, 1868, 1870, 1872, 1874, 1876,
	1878, 1880, 1882, 1883, 1885, 1887, 1889, 1891,
	1893, 1894, 1896, 1898, 1900, 1901, 1903, 1905,
	1906, 1908, 1910, 1911, 1913, 1914, 1916, 1917,
	1919, 1920, 1922, 1923, 1925, 1926, 1927, 1929,
	1930, 1931, 1933, 1934, 1935, 1937, 1938, 1939,
	1941, 1942, 1943, 1944, 1945, 1947, 1948, 1949,
	1950, 1951, 1952, 1953, 1954, 1955, 1957,
};

static unsigned int imx283_gain_code(unsigned int db10)
{
	if (db10 > IMX283_ANA_GAIN_PGC_MAX)
		db10 = IMX283_ANA_GAIN_PGC_MAX;

	return imx283_pgc_code[db10];
}

#define IMX283_REG_BLKLEVEL		0x3047
#define IMX283_BLKLEVEL_REG_DEFAULT	50
#define IMX283_BLKLEVEL_REG_MAX		0xff
#define IMX283_BLKLEVEL_SHIFT		2
#define IMX283_BLKLEVEL_DEFAULT		(IMX283_BLKLEVEL_REG_DEFAULT << \
					 IMX283_BLKLEVEL_SHIFT)
#define IMX283_BLKLEVEL_MAX		(IMX283_BLKLEVEL_REG_MAX << \
					 IMX283_BLKLEVEL_SHIFT)

#define IMX283_REG_XMSTA		0x3105
#define IMX283_REG_SYNCDRV		0x3107
#define IMX283_SYNCDRV_XHS_XVS		0xa2

#define IMX283_REG_TPG_CTRL		0x3156
#define IMX283_TPG_CTRL_ENABLE		0x11
#define IMX283_REG_TPG_PATSEL		0x3157

#define IMX283_REG_EBD_X_OUT_SIZE	0x3a54

#define IMX283_EMBEDDED_LINE_WIDTH	16384
#define IMX283_NUM_EMBEDDED_LINES	1

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

#define IMX283_NATIVE_WIDTH		5592U
#define IMX283_NATIVE_HEIGHT		3710U
#define IMX283_PIXEL_ARRAY_WIDTH	5472U
#define IMX283_PIXEL_ARRAY_HEIGHT	3648U

#define IMX283_HTRIM_HOST		120U
#define IMX283_HTRIM_RECORDING_START	(IMX283_HTRIM_HOST + 12U)

#define IMX283_MIN_WIDTH		240U
#define IMX283_WIDTH_STEP		12U
#define IMX283_HEIGHT_STEP		4U
#define IMX283_CROP_LEFT_STEP		12U
#define IMX283_CROP_TOP_STEP		4U

#define IMX283_MIN_CROP_HEIGHT		1848U

enum imx283_base_mode {
	IMX283_BASE_MODE_FULL_12,
	IMX283_BASE_MODE_FULL_10,
	IMX283_BASE_MODE_2X2_BIN,
};

struct imx283_base_config {
	bool binning;
	bool ten_bit;

	u8 mdsel1;
	u8 mdsel2;
	u8 mdsel3;
	u8 mdsel4;

	unsigned int max_width;
	unsigned int max_height;
	unsigned int min_height;
	unsigned int min_width;

	unsigned int hmax_min;
	unsigned int vmax_min_full;
	unsigned int vmax_crop_scale;

	unsigned int vst, vct, veff;
	unsigned int v_offset;
	unsigned int v_offset_rev;

	unsigned int h_step;

	unsigned int shr_min;
	unsigned int ob_size_v;
	unsigned int ob_size_h;
};

static const struct imx283_base_config base_configs[] = {
	[IMX283_BASE_MODE_FULL_12] = {
		.binning = false,
		.ten_bit = false,
		.mdsel1 = 0x04, .mdsel2 = 0x03,
		.mdsel3 = 0x30, .mdsel4 = 0x50,
		.max_width = IMX283_PIXEL_ARRAY_WIDTH,
		.max_height = IMX283_PIXEL_ARRAY_HEIGHT,
		.min_height = 1848,
		.min_width = 240,
		.hmax_min = 887,
		.vmax_min_full = 3793,
		.vmax_crop_scale = 2,
		.vst = 0, .vct = 0, .veff = 3694,
		.v_offset = 24,
		.v_offset_rev = 20,
		.h_step = 4,
		.shr_min = 11,
		.ob_size_v = 16,
		.ob_size_h = 96,
	},
	[IMX283_BASE_MODE_FULL_10] = {
		.binning = false,
		.ten_bit = true,
		.mdsel1 = 0x04, .mdsel2 = 0x01,
		.mdsel3 = 0x20, .mdsel4 = 0x50,
		.max_width = IMX283_PIXEL_ARRAY_WIDTH,
		.max_height = IMX283_PIXEL_ARRAY_HEIGHT,
		.min_height = 1848,
		.min_width = 240,
		.hmax_min = 745,
		.vmax_min_full = 3793,
		.vmax_crop_scale = 2,
		.vst = 0, .vct = 0, .veff = 3694,
		.v_offset = 24,
		.v_offset_rev = 20,
		.h_step = 4,
		.shr_min = 10,
		.ob_size_v = 16,
		.ob_size_h = 96,
	},
	[IMX283_BASE_MODE_2X2_BIN] = {
		.binning = true,
		.ten_bit = false,
		.mdsel1 = 0x0d, .mdsel2 = 0x11,
		.mdsel3 = 0x70, .mdsel4 = 0x50,
		.max_width = IMX283_PIXEL_ARRAY_WIDTH / 2,
		.max_height = IMX283_PIXEL_ARRAY_HEIGHT / 2,
		.min_height = 924,
		.min_width = 360,
		.hmax_min = 362,
		.vmax_min_full = 3840,
		.vmax_crop_scale = 4,
		.vst = 0, .vct = 0, .veff = 1842,
		.v_offset = 10,
		.v_offset_rev = 10,
		.h_step = 12,
		.shr_min = 12,
		.ob_size_v = 4,
		.ob_size_h = 48,
	},
};

enum {
	IMX283_LINK_FREQ_1440MBPS,
	IMX283_LINK_FREQ_720MBPS,
};

static const s64 link_freqs[] = {
	[IMX283_LINK_FREQ_1440MBPS] = 720000000,
	[IMX283_LINK_FREQ_720MBPS] = 360000000,
};

#define IMX283_PLATFORM_MAX_LINK_FREQ	450000000LL

static const struct {
	u16 addr;
	u8 val;
} imx283_regs_720mbps[] = {
	{ 0x36c5, 0x01 },
	{ 0x3ac4, 0x01 },
	{ 0x320b, 0x00 },
	{ 0x3018, 0x77 },
	{ 0x301a, 0x37 },
	{ 0x301c, 0x67 },
	{ 0x301e, 0x37 },
	{ 0x3020, 0x37 },
	{ 0x3022, 0x37 },
	{ 0x3024, 0xdf },
	{ 0x3025, 0x00 },
	{ 0x3026, 0x2f },
	{ 0x3028, 0x47 },
	{ 0x302a, 0x0f },
	{ 0x3104, 0x02 },
};

struct imx283_inck_cfg {
	u32 xclk_hz;
	u8 plrd1;
	u16 plrd2;
	u8 plrd3;
	u8 plrd4;
};

static const struct imx283_inck_cfg imx283_inck_table[] = {
	{  6000000, 0x00, 0x00f0, 0x00, 0xc0 },
	{ 12000000, 0x01, 0x00f0, 0x01, 0xc0 },
	{ 18000000, 0x01, 0x00a0, 0x01, 0x80 },
	{ 24000000, 0x02, 0x00f0, 0x02, 0xc0 },
};

static const u32 codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
};

static const char * const imx283_test_pattern_menu[] = {
	"Disabled",
	"All 000h",
	"All FFFh",
	"All 555h",
	"All AAAh",
	"Horizontal Color Bars",
	"Vertical Color Bars",
};

static const u8 imx283_test_pattern_val[] = {
	0x00,
	0x00,
	0x01,
	0x02,
	0x03,
	0x0a,
	0x0b,
};

static const char * const imx283_supply_name[] = {
	"VANA",
	"VDIG",
	"VDDL",
};

#define IMX283_NUM_SUPPLIES ARRAY_SIZE(imx283_supply_name)

#define IMX283_XCLR_MIN_DELAY_US	10000
#define IMX283_XCLR_DELAY_RANGE_US	1000

struct imx283 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	struct clk *xclk;
	u32 xclk_freq;
	const struct imx283_inck_cfg *inck;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX283_NUM_SUPPLIES];

	unsigned int link_freq_idx;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *black_level;

	const struct imx283_base_config *base_cfg;

	unsigned int out_width;
	unsigned int out_height;

	struct v4l2_rect crop;
	struct v4l2_rect crop_req;
	struct v4l2_rect compose;

	unsigned int hmax_min;
	unsigned int hmax;
	unsigned int vmax;
	unsigned int vmax_min;

	struct mutex mutex;

	bool streaming;
};

static inline struct imx283 *to_imx283(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx283, sd);
}

static int imx283_read_reg(struct imx283 *imx283, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
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

static int imx283_write_reg(struct imx283 *imx283, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
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

static void imx283_reghold(struct imx283 *imx283, bool hold)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	int ret;

	ret = imx283_write_reg(imx283, IMX283_REG_REGHOLD, 1, hold ? 1 : 0);
	if (!ret)
		return;

	dev_err(&client->dev, "REGHOLD %s failed (%d)%s\n",
		hold ? "set" : "release", ret,
		hold ? "" : " - frame-reflected registers are now frozen");
}

static bool imx283_code_is_10bit(u32 code)
{
	return code == MEDIA_BUS_FMT_SRGGB10_1X10 ||
	       code == MEDIA_BUS_FMT_SGBRG10_1X10;
}

static unsigned int imx283_wire_height(const struct imx283 *imx283)
{
	return imx283->out_height + imx283->base_cfg->ob_size_v;
}

static unsigned int imx283_wire_width(const struct imx283 *imx283)
{
	return imx283->out_width + imx283->base_cfg->ob_size_h;
}

static unsigned int imx283_cfg_hmax_min(const struct imx283 *imx283,
					const struct imx283_base_config *cfg)
{
	return cfg->hmax_min *
	       (imx283->link_freq_idx == IMX283_LINK_FREQ_720MBPS ? 2 : 1);
}

static u32 imx283_get_format_code(struct imx283 *imx283, u32 code)
{
	unsigned int i;

	lockdep_assert_held(&imx283->mutex);

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == code)
			break;

	if (i >= ARRAY_SIZE(codes))
		i = 0;

	i = (i & ~1) | (imx283->vflip->val ? 1 : 0);

	return codes[i];
}

static const struct imx283_base_config *
imx283_config_from_ratio(const struct v4l2_rect *crop,
			 unsigned int compose_w, unsigned int compose_h,
			 u32 code)
{
	if (compose_w <= crop->width / 2 &&
	    compose_h <= crop->height / 2 &&
	    compose_w <= base_configs[IMX283_BASE_MODE_2X2_BIN].max_width &&
	    compose_h <= base_configs[IMX283_BASE_MODE_2X2_BIN].max_height)
		return &base_configs[IMX283_BASE_MODE_2X2_BIN];

	return imx283_code_is_10bit(code) ?
	       &base_configs[IMX283_BASE_MODE_FULL_10] :
	       &base_configs[IMX283_BASE_MODE_FULL_12];
}

static void imx283_sync_code_to_config(struct imx283 *imx283,
				       const struct imx283_base_config *cfg)
{
	lockdep_assert_held(&imx283->mutex);

	if (imx283_code_is_10bit(imx283->fmt_code) == cfg->ten_bit)
		return;

	imx283->fmt_code = imx283_get_format_code(imx283,
						  cfg->ten_bit ?
						  MEDIA_BUS_FMT_SRGGB10_1X10 :
						  MEDIA_BUS_FMT_SRGGB12_1X12);
}

static void imx283_clamp_align(unsigned int *width, unsigned int *height,
			       const struct imx283_base_config *cfg)
{
	*width = clamp(*width, cfg->min_width, cfg->max_width);
	*height = clamp(*height, cfg->min_height, cfg->max_height);

	*width = rounddown(*width, IMX283_WIDTH_STEP);
	if (*width < cfg->min_width)
		*width = cfg->min_width;

	*height = rounddown(*height, IMX283_HEIGHT_STEP);
	if (*height < cfg->min_height)
		*height = cfg->min_height;
}

static void imx283_clamp_crop(struct v4l2_rect *crop)
{
	if (crop->left < 0)
		crop->left = 0;
	if (crop->top < 0)
		crop->top = 0;

	crop->left = rounddown(crop->left, IMX283_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX283_CROP_TOP_STEP);

	if (crop->width < IMX283_MIN_WIDTH)
		crop->width = IMX283_MIN_WIDTH;
	if (crop->height < IMX283_MIN_CROP_HEIGHT)
		crop->height = IMX283_MIN_CROP_HEIGHT;
	if (crop->width > IMX283_PIXEL_ARRAY_WIDTH)
		crop->width = IMX283_PIXEL_ARRAY_WIDTH;
	if (crop->height > IMX283_PIXEL_ARRAY_HEIGHT)
		crop->height = IMX283_PIXEL_ARRAY_HEIGHT;

	crop->width = rounddown(crop->width, IMX283_WIDTH_STEP);
	crop->height = rounddown(crop->height, IMX283_HEIGHT_STEP);

	if (crop->left + crop->width > IMX283_PIXEL_ARRAY_WIDTH)
		crop->left = IMX283_PIXEL_ARRAY_WIDTH - crop->width;
	if (crop->top + crop->height > IMX283_PIXEL_ARRAY_HEIGHT)
		crop->top = IMX283_PIXEL_ARRAY_HEIGHT - crop->height;

	crop->left = rounddown(crop->left, IMX283_CROP_LEFT_STEP);
	crop->top = rounddown(crop->top, IMX283_CROP_TOP_STEP);
}

static void imx283_tighten_crop(struct imx283 *imx283,
				const struct imx283_base_config *cfg)
{
	if (cfg->binning) {
		imx283->crop.width = imx283->compose.width * 2;
		imx283->crop.height = imx283->compose.height * 2;
	} else {
		imx283->crop.width = imx283->compose.width;
		imx283->crop.height = imx283->compose.height;
	}
	imx283_clamp_crop(&imx283->crop);
}

static void imx283_vcrop_regs(struct imx283 *imx283, u32 *vwinpos,
			      u32 *vwidcut)
{
	const struct imx283_base_config *cfg = imx283->base_cfg;
	unsigned int top = imx283->crop.top;
	unsigned int start;
	u32 pos;

	if (cfg->binning)
		top /= 2;

	start = cfg->v_offset + top;

	if (imx283->vflip->val) {
		unsigned int far = cfg->veff - cfg->v_offset_rev -
				   imx283->out_height - top;

		pos = (cfg->vst - far / 2) & 0xfff;
	} else {
		pos = start / 2 + cfg->vst;
	}

	*vwinpos = pos;
	*vwidcut = (cfg->veff - imx283->out_height) / 2 + cfg->vct;
}

static unsigned int imx283_vmax_min(const struct imx283_base_config *cfg,
				    unsigned int out_height)
{
	unsigned int vwidcut = (cfg->veff - out_height) / 2 + cfg->vct;

	return cfg->vmax_min_full - (vwidcut - cfg->vct) * cfg->vmax_crop_scale;
}

static int imx283_write_dynamic_regs(struct imx283 *imx283)
{
	const struct imx283_base_config *cfg = imx283->base_cfg;
	const struct v4l2_rect *crop = &imx283->crop;
	unsigned int h_start, h_width;
	u32 vwinpos, vwidcut;
	int ret = 0;

	imx283_vcrop_regs(imx283, &vwinpos, &vwidcut);

	h_start = IMX283_HTRIM_RECORDING_START + crop->left;
	h_width = crop->width;

	imx283_reghold(imx283, true);

	ret = imx283_write_reg(imx283, IMX283_REG_MDSEL1, 1, cfg->mdsel1);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_MDSEL2, 1,
				       cfg->mdsel2);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_MDSEL3, 1,
				       cfg->mdsel3);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_MDSEL4, 1,
				       cfg->mdsel4);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_HTRIM_MDVREV, 1,
				       IMX283_HTRIM_EN_BITS |
				       (imx283->vflip->val ? 1 : 0));
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_VWINPOS, 2, vwinpos);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_VWIDCUT, 2, vwidcut);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_Y_OUT_SIZE, 2,
				       imx283->out_height);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_WRITE_VSIZE, 2,
				       imx283->out_height + cfg->ob_size_v);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_OB_SIZE_V, 1,
				       cfg->ob_size_v);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_HTRIMMING_START, 2,
				       h_start);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_HTRIMMING_END, 2,
				       h_start + h_width);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_HMAX, 2,
				       imx283->hmax);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_VMAX, 3,
				       imx283->vmax);

	imx283_reghold(imx283, false);

	return ret;
}

static void imx283_set_default_format(struct imx283 *imx283)
{
	imx283->base_cfg = &base_configs[IMX283_BASE_MODE_FULL_12];
	imx283->out_width = IMX283_PIXEL_ARRAY_WIDTH;
	imx283->out_height = IMX283_PIXEL_ARRAY_HEIGHT;
	imx283->fmt_code = MEDIA_BUS_FMT_SRGGB12_1X12;

	imx283->crop.left = 0;
	imx283->crop.top = 0;
	imx283->crop.width = IMX283_PIXEL_ARRAY_WIDTH;
	imx283->crop.height = IMX283_PIXEL_ARRAY_HEIGHT;
	imx283->crop_req = imx283->crop;

	imx283->compose.left = 0;
	imx283->compose.top = 0;
	imx283->compose.width = IMX283_PIXEL_ARRAY_WIDTH;
	imx283->compose.height = IMX283_PIXEL_ARRAY_HEIGHT;

	imx283->hmax_min = imx283_cfg_hmax_min(imx283, imx283->base_cfg);
	imx283->hmax = imx283->hmax_min;
	imx283->vmax_min = imx283_vmax_min(imx283->base_cfg,
					   imx283->out_height);
	imx283->vmax = imx283->vmax_min;
}

static int imx283_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx283 *imx283 = to_imx283(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;
	struct v4l2_rect *try_compose;

	mutex_lock(&imx283->mutex);

	try_fmt_img->width = IMX283_PIXEL_ARRAY_WIDTH +
			     base_configs[IMX283_BASE_MODE_FULL_12].ob_size_h;
	try_fmt_img->height = IMX283_PIXEL_ARRAY_HEIGHT +
			      base_configs[IMX283_BASE_MODE_FULL_12].ob_size_v;
	try_fmt_img->code = imx283_get_format_code(imx283,
						   MEDIA_BUS_FMT_SRGGB12_1X12);
	try_fmt_img->field = V4L2_FIELD_NONE;

	try_fmt_meta->width = IMX283_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX283_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = IMX283_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX283_PIXEL_ARRAY_HEIGHT;

	try_compose = v4l2_subdev_state_get_compose(fh->state, IMAGE_PAD);
	try_compose->left = 0;
	try_compose->top = 0;
	try_compose->width = IMX283_PIXEL_ARRAY_WIDTH;
	try_compose->height = IMX283_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx283->mutex);

	return 0;
}

static u32 imx283_shr_max(const struct imx283 *imx283)
{
	return min_t(u32, imx283->vmax - IMX283_EXPOSURE_MIN, IMX283_SHR_MAX);
}

static void imx283_adjust_exposure_range(struct imx283 *imx283)
{
	int exposure_min, exposure_max, exposure_def;

	exposure_max = imx283->vmax - imx283->base_cfg->shr_min;
	exposure_min = imx283->vmax - imx283_shr_max(imx283);
	exposure_def = clamp(imx283->exposure->val, exposure_min, exposure_max);
	__v4l2_ctrl_modify_range(imx283->exposure, exposure_min, exposure_max,
				 imx283->exposure->step, exposure_def);
}

static int imx283_set_exposure(struct imx283 *imx283, unsigned int exp_lines)
{
	u32 shr;

	if (exp_lines > imx283->vmax)
		exp_lines = imx283->vmax;

	shr = imx283->vmax - exp_lines;
	shr = clamp_t(u32, shr, imx283->base_cfg->shr_min,
		      imx283_shr_max(imx283));

	return imx283_write_reg(imx283, IMX283_REG_SHR, 2, shr);
}

static unsigned int imx283_hblank_min(const struct imx283 *imx283)
{
	unsigned int line_min = imx283->hmax_min * IMX283_PIXELS_PER_CLOCK;
	unsigned int wire_w = imx283_wire_width(imx283);

	return line_min > wire_w ? line_min - wire_w : 0;
}

static unsigned int imx283_hblank_to_hmax(struct imx283 *imx283,
					  unsigned int hblank)
{
	unsigned int hmax = (imx283_wire_width(imx283) + hblank) /
			    IMX283_PIXELS_PER_CLOCK;

	return clamp(hmax, imx283->hmax_min, (unsigned int)IMX283_HMAX_MAX);
}

static int imx283_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx283 *imx283 =
		container_of(ctrl->handler, struct imx283, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		imx283->vmax = imx283_wire_height(imx283) + ctrl->val;
		imx283_adjust_exposure_range(imx283);
	} else if (ctrl->id == V4L2_CID_HBLANK) {
		imx283->hmax = imx283_hblank_to_hmax(imx283, ctrl->val);
	}

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		{
			unsigned int db10 = ctrl->val;
			unsigned int dg   = 0;

			while (db10 - dg * IMX283_DGTL_GAIN_DB10_STEP >
					IMX283_ANA_GAIN_PGC_MAX &&
			       dg < IMX283_DGTL_GAIN_MAX_CODE)
				dg++;

			db10 -= dg * IMX283_DGTL_GAIN_DB10_STEP;

			imx283_reghold(imx283, true);
			ret = imx283_write_reg(imx283, IMX283_REG_ANALOG_GAIN, 2,
					       imx283_gain_code(db10));
			if (!ret)
				ret = imx283_write_reg(imx283,
						       IMX283_REG_DIGITAL_GAIN,
						       1, dg);
			imx283_reghold(imx283, false);
		}
		break;
	case V4L2_CID_EXPOSURE:
		imx283_reghold(imx283, true);
		ret = imx283_set_exposure(imx283, ctrl->val);
		imx283_reghold(imx283, false);
		break;
	case V4L2_CID_VBLANK:
		imx283_reghold(imx283, true);
		ret = imx283_write_reg(imx283, IMX283_REG_VMAX, 3,
				       imx283->vmax);
		if (!ret)
			ret = imx283_set_exposure(imx283,
						  imx283->exposure->val);
		imx283_reghold(imx283, false);
		break;
	case V4L2_CID_HBLANK:
		imx283_reghold(imx283, true);
		ret = imx283_write_reg(imx283, IMX283_REG_HMAX, 2,
				       imx283->hmax);
		imx283_reghold(imx283, false);
		break;
	case V4L2_CID_VFLIP:
		break;
	case V4L2_CID_BLACK_LEVEL:
		ret = imx283_write_reg(imx283, IMX283_REG_BLKLEVEL, 1,
				       ctrl->val >> IMX283_BLKLEVEL_SHIFT);
		break;
	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			imx283_write_reg(imx283, IMX283_REG_BLKLEVEL, 1, 0);
			imx283_write_reg(imx283, IMX283_REG_TPG_PATSEL, 1,
					 imx283_test_pattern_val[ctrl->val]);
			ret = imx283_write_reg(imx283, IMX283_REG_TPG_CTRL, 1,
					       IMX283_TPG_CTRL_ENABLE);
		} else {
			imx283_write_reg(imx283, IMX283_REG_TPG_CTRL, 1, 0x00);
			ret = imx283_write_reg(imx283, IMX283_REG_BLKLEVEL, 1,
					       imx283->black_level->cur.val >>
					       IMX283_BLKLEVEL_SHIFT);
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

static int imx283_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx283 *imx283 =
		container_of(ctrl->handler, struct imx283, ctrl_handler);
	const struct imx283_base_config *cfg = imx283->base_cfg;

	if (!cfg)
		return -EINVAL;

	switch (ctrl->id) {
	case CHC5SENSOR_CID_OB_ON_IMAGE_DT:
		ctrl->val = 1;
		return 0;
	case CHC5SENSOR_CID_OB_LEFT:
		ctrl->val = (s32)cfg->ob_size_h;
		return 0;
	case CHC5SENSOR_CID_OB_TOP:
		ctrl->val = (s32)cfg->ob_size_v;
		return 0;
	case CHC5SENSOR_CID_OB_RIGHT:
	case CHC5SENSOR_CID_OB_BOTTOM:
		ctrl->val = 0;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops imx283_ctrl_ops = {
	.s_ctrl		 = imx283_set_ctrl,
	.g_volatile_ctrl = imx283_g_volatile_ctrl,
};

#define IMX283_OB_CTRL(_id, _name)					\
	{								\
		.ops	= &imx283_ctrl_ops,				\
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

static const struct v4l2_ctrl_config imx283_ob_ctrls[] = {
	IMX283_OB_CTRL(CHC5SENSOR_CID_OB_LEFT,   "Optical Black Left"),
	IMX283_OB_CTRL(CHC5SENSOR_CID_OB_TOP,    "Optical Black Top"),
	IMX283_OB_CTRL(CHC5SENSOR_CID_OB_RIGHT,  "Optical Black Right"),
	IMX283_OB_CTRL(CHC5SENSOR_CID_OB_BOTTOM, "Optical Black Bottom"),
	IMX283_OB_CTRL(CHC5SENSOR_CID_OB_ON_IMAGE_DT, "Optical Black On Image DT"),
};

static int imx283_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx283 *imx283 = to_imx283(sd);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (code->index >= (ARRAY_SIZE(codes) / 2))
			return -EINVAL;

		mutex_lock(&imx283->mutex);
		code->code = imx283_get_format_code(imx283,
						    codes[code->index * 2]);
		mutex_unlock(&imx283->mutex);
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx283_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx283 *imx283 = to_imx283(sd);
	bool code_ok;

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index > 0)
			return -EINVAL;

		mutex_lock(&imx283->mutex);
		code_ok = fse->code == imx283_get_format_code(imx283, fse->code);
		mutex_unlock(&imx283->mutex);
		if (!code_ok)
			return -EINVAL;

		fse->min_width =
			base_configs[IMX283_BASE_MODE_2X2_BIN].min_width +
			base_configs[IMX283_BASE_MODE_2X2_BIN].ob_size_h;
		fse->max_width = IMX283_PIXEL_ARRAY_WIDTH +
			base_configs[IMX283_BASE_MODE_FULL_12].ob_size_h;
		fse->min_height =
			base_configs[IMX283_BASE_MODE_2X2_BIN].min_height +
			base_configs[IMX283_BASE_MODE_2X2_BIN].ob_size_v;
		fse->max_height = IMX283_PIXEL_ARRAY_HEIGHT +
			base_configs[IMX283_BASE_MODE_FULL_12].ob_size_v;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX283_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX283_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx283_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx283_update_image_pad_format(struct imx283 *imx283,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = imx283_wire_width(imx283);
	fmt->format.height = imx283_wire_height(imx283);
	fmt->format.field = V4L2_FIELD_NONE;
	imx283_reset_colorspace(&fmt->format);
}

static void imx283_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX283_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX283_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx283_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx283 *imx283 = to_imx283(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx283->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx283_get_format_code(imx283, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx283_update_image_pad_format(imx283, fmt);
			fmt->format.code =
			       imx283_get_format_code(imx283, imx283->fmt_code);
		} else {
			imx283_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx283->mutex);
	return 0;
}

static void imx283_set_framing_limits(struct imx283 *imx283)
{
	const struct imx283_base_config *cfg = imx283->base_cfg;
	unsigned int out_h = imx283->out_height;
	unsigned int wire_h = imx283_wire_height(imx283);
	unsigned int frm_length_min, frm_length_default;
	unsigned int hblank_min, hblank_max;

	imx283->hmax_min = imx283_cfg_hmax_min(imx283, cfg);
	imx283->hmax = imx283->hmax_min;

	hblank_min = imx283_hblank_min(imx283);
	hblank_max = hblank_min + (IMX283_HMAX_MAX - imx283->hmax_min) *
				  IMX283_PIXELS_PER_CLOCK;
	__v4l2_ctrl_modify_range(imx283->hblank, hblank_min, hblank_max,
				 IMX283_PIXELS_PER_CLOCK, hblank_min);
	__v4l2_ctrl_s_ctrl(imx283->hblank, hblank_min);

	imx283->vmax_min = imx283_vmax_min(cfg, out_h);
	frm_length_min = imx283->vmax_min;

	if (frm_length_min < wire_h)
		frm_length_min = wire_h;

	frm_length_default = div_u64((u64)IMX283_INTERNAL_CLOCK,
				     imx283->hmax * 30);
	if (frm_length_default < frm_length_min)
		frm_length_default = frm_length_min;
	if (frm_length_default > IMX283_VMAX_MAX)
		frm_length_default = IMX283_VMAX_MAX;

	__v4l2_ctrl_modify_range(imx283->vblank,
				 frm_length_min - wire_h,
				 IMX283_VMAX_MAX - wire_h,
				 1, frm_length_default - wire_h);
	__v4l2_ctrl_s_ctrl(imx283->vblank, frm_length_default - wire_h);

	imx283->vmax = wire_h + imx283->vblank->val;

	imx283_adjust_exposure_range(imx283);
}

static int imx283_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx283_base_config *cfg;
	struct imx283 *imx283 = to_imx283(sd);
	unsigned int req_width, req_height, probe_w, probe_h, bin_ob, bin_ob_h;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx283->mutex);

	if (fmt->pad == IMAGE_PAD) {
		fmt->format.code = imx283_get_format_code(imx283,
							  fmt->format.code);

		req_width = fmt->format.width;
		req_height = fmt->format.height;

		bin_ob = base_configs[IMX283_BASE_MODE_2X2_BIN].ob_size_v;
		probe_h = req_height > bin_ob ? req_height - bin_ob : req_height;

		bin_ob_h = base_configs[IMX283_BASE_MODE_2X2_BIN].ob_size_h;
		probe_w = req_width > bin_ob_h ? req_width - bin_ob_h : req_width;

		cfg = imx283_config_from_ratio(&imx283->crop_req,
					       probe_w, probe_h,
					       fmt->format.code);

		if (cfg->binning && imx283_code_is_10bit(fmt->format.code))
			fmt->format.code = imx283_get_format_code(imx283,
						MEDIA_BUS_FMT_SRGGB12_1X12);

		if (req_width > cfg->ob_size_h)
			req_width -= cfg->ob_size_h;
		if (req_height > cfg->ob_size_v)
			req_height -= cfg->ob_size_v;

		imx283_clamp_align(&req_width, &req_height, cfg);

		fmt->format.width = req_width + cfg->ob_size_h;
		fmt->format.height = req_height + cfg->ob_size_v;
		fmt->format.field = V4L2_FIELD_NONE;
		imx283_reset_colorspace(&fmt->format);

		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx283->base_cfg = cfg;
			imx283->out_width = req_width;
			imx283->out_height = req_height;
			imx283->compose.width = req_width;
			imx283->compose.height = req_height;
			imx283->fmt_code = fmt->format.code;

			imx283_tighten_crop(imx283, cfg);

			imx283_set_framing_limits(imx283);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx283_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx283->mutex);

	return 0;
}

static const struct v4l2_rect *
__imx283_get_pad_crop(struct imx283 *imx283,
		      struct v4l2_subdev_state *sd_state, unsigned int pad,
		      enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_state_get_crop(sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx283->crop;
	}

	return NULL;
}

static const struct v4l2_rect *
__imx283_get_pad_compose(struct imx283 *imx283,
			 struct v4l2_subdev_state *sd_state, unsigned int pad,
			 enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_state_get_compose(sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &imx283->compose;
	}

	return NULL;
}

static int imx283_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx283 *imx283 = to_imx283(sd);
	const struct v4l2_rect *r;

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&imx283->mutex);
		r = __imx283_get_pad_crop(imx283, sd_state, sel->pad,
					  sel->which);
		if (r)
			sel->r = *r;
		mutex_unlock(&imx283->mutex);
		return r ? 0 : -EINVAL;

	case V4L2_SEL_TGT_COMPOSE:
		mutex_lock(&imx283->mutex);
		r = __imx283_get_pad_compose(imx283, sd_state, sel->pad,
					     sel->which);
		if (r) {
			sel->r = *r;
			sel->r.width  += imx283->base_cfg->ob_size_h;
			sel->r.height += imx283->base_cfg->ob_size_v;
		}
		mutex_unlock(&imx283->mutex);
		return r ? 0 : -EINVAL;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX283_NATIVE_WIDTH;
		sel->r.height = IMX283_NATIVE_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX283_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX283_PIXEL_ARRAY_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX283_PIXEL_ARRAY_WIDTH +
			base_configs[IMX283_BASE_MODE_FULL_12].ob_size_h;
		sel->r.height = IMX283_PIXEL_ARRAY_HEIGHT +
			base_configs[IMX283_BASE_MODE_FULL_12].ob_size_v;
		return 0;
	}

	return -EINVAL;
}

static int imx283_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx283 *imx283 = to_imx283(sd);
	const struct imx283_base_config *cfg;

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&imx283->mutex);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_rect crop = sel->r;
		struct v4l2_rect req_win;
		unsigned int compose_w, compose_h;

		imx283_clamp_crop(&crop);
		req_win = crop;

		cfg = imx283_config_from_ratio(&crop, imx283->compose.width,
					       imx283->compose.height,
					       imx283->fmt_code);

		compose_w = cfg->binning ? crop.width / 2 : crop.width;
		compose_h = cfg->binning ? crop.height / 2 : crop.height;

		imx283_clamp_align(&compose_w, &compose_h, cfg);

		crop.width = cfg->binning ? compose_w * 2 : compose_w;
		crop.height = cfg->binning ? compose_h * 2 : compose_h;
		imx283_clamp_crop(&crop);

		if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
			*v4l2_subdev_state_get_crop(sd_state, sel->pad) = crop;
		} else {
			imx283->crop = crop;
			imx283->crop_req = req_win;
			imx283->compose.width = compose_w;
			imx283->compose.height = compose_h;
			imx283->base_cfg = cfg;
			imx283->out_width = compose_w;
			imx283->out_height = compose_h;
			imx283_sync_code_to_config(imx283, cfg);
			imx283_set_framing_limits(imx283);
		}

		sel->r = crop;

	} else {
		unsigned int compose_w = sel->r.width;
		unsigned int compose_h = sel->r.height;
		unsigned int probe_w, probe_h, ob_w, ob_h;

		ob_w = base_configs[IMX283_BASE_MODE_2X2_BIN].ob_size_h;
		ob_h = base_configs[IMX283_BASE_MODE_2X2_BIN].ob_size_v;
		probe_w = compose_w > ob_w ? compose_w - ob_w : compose_w;
		probe_h = compose_h > ob_h ? compose_h - ob_h : compose_h;

		cfg = imx283_config_from_ratio(&imx283->crop_req,
					       probe_w, probe_h,
					       imx283->fmt_code);

		ob_w = cfg->ob_size_h;
		ob_h = cfg->ob_size_v;
		if (compose_w > ob_w)
			compose_w -= ob_w;
		if (compose_h > ob_h)
			compose_h -= ob_h;

		imx283_clamp_align(&compose_w, &compose_h, cfg);

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY) {
			imx283->compose.left = 0;
			imx283->compose.top = 0;
			imx283->compose.width = compose_w;
			imx283->compose.height = compose_h;
			imx283->base_cfg = cfg;
			imx283->out_width = compose_w;
			imx283->out_height = compose_h;

			imx283_tighten_crop(imx283, cfg);

			cfg = imx283_config_from_ratio(&imx283->crop,
						       compose_w, compose_h,
						       imx283->fmt_code);
			imx283->base_cfg = cfg;
			imx283_sync_code_to_config(imx283, cfg);

			imx283_set_framing_limits(imx283);
		} else {
			struct v4l2_rect *try_compose =
				v4l2_subdev_state_get_compose(sd_state,
							    sel->pad);

			try_compose->left = 0;
			try_compose->top = 0;
			try_compose->width = compose_w;
			try_compose->height = compose_h;
		}

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = compose_w + ob_w;
		sel->r.height = compose_h + ob_h;
	}

	mutex_unlock(&imx283->mutex);

	return 0;
}

static int imx283_standby_cancel(struct imx283 *imx283)
{
	const struct imx283_inck_cfg *inck = imx283->inck;
	unsigned int i;
	int ret;

	ret = imx283_write_reg(imx283, IMX283_REG_STANDBY, 1,
			       IMX283_STANDBY_CONFIG);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_PLRD1, 1,
				       inck->plrd1);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_PLRD2, 2,
				       inck->plrd2);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_PLRD3, 1,
				       inck->plrd3);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_PLRD4, 1,
				       inck->plrd4);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_PLSTMG08, 1,
				       IMX283_PLSTMG08_VAL);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_PLSTMG02, 1,
				       IMX283_PLSTMG02_VAL);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_STBPL, 1,
				       IMX283_STBPL_NORMAL);

	if (imx283->link_freq_idx == IMX283_LINK_FREQ_720MBPS)
		for (i = 0; i < ARRAY_SIZE(imx283_regs_720mbps) && !ret; i++)
			ret = imx283_write_reg(imx283,
					       imx283_regs_720mbps[i].addr, 1,
					       imx283_regs_720mbps[i].val);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = imx283_write_reg(imx283, IMX283_REG_STANDBY, 1,
			       IMX283_STANDBY_RUN);
	if (ret)
		return ret;

	usleep_range(19000, 20000);

	ret = imx283_write_reg(imx283, IMX283_REG_CLAMP, 1, IMX283_CLPSQRST);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_XMSTA, 1, 0x00);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_SYNCDRV, 1,
				       IMX283_SYNCDRV_XHS_XVS);

	return ret;
}

static int imx283_start_streaming(struct imx283 *imx283)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	int ret;

	ret = imx283_standby_cancel(imx283);
	if (ret) {
		dev_err(&client->dev, "%s failed to cancel standby\n",
			__func__);
		return ret;
	}

	ret = imx283_write_reg(imx283, IMX283_REG_SMD, 1, 0x00);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_SVR, 2, 0x0000);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_MDSEL7, 2, 0x0000);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_MDSEL18, 2, 0x0000);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_DIGITAL_GAIN, 1,
				       0x00);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_EBD_X_OUT_SIZE, 2,
				       0x0000);
	if (!ret)
		ret = imx283_write_reg(imx283, IMX283_REG_BLKLEVEL, 1,
				       IMX283_BLKLEVEL_REG_DEFAULT);
	if (ret) {
		dev_err(&client->dev, "%s failed to set common settings\n",
			__func__);
		return ret;
	}

	ret = imx283_write_dynamic_regs(imx283);
	if (ret) {
		dev_err(&client->dev, "%s failed to set dynamic regs\n",
			__func__);
		return ret;
	}

	return __v4l2_ctrl_handler_setup(imx283->sd.ctrl_handler);
}

static void imx283_stop_streaming(struct imx283 *imx283)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	int ret, ret2;

	ret = imx283_write_reg(imx283, IMX283_REG_XMSTA, 1, 0x01);
	usleep_range(20000, 21000);
	ret2 = imx283_write_reg(imx283, IMX283_REG_STANDBY, 1,
				IMX283_STANDBY_STOP);
	if (ret || ret2)
		dev_err(&client->dev, "%s failed to stop stream (%d/%d)\n",
			__func__, ret, ret2);
}

static int imx283_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx283 *imx283 = to_imx283(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx283->mutex);
	if (imx283->streaming == enable) {
		mutex_unlock(&imx283->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = imx283_start_streaming(imx283);
		if (ret)
			goto err_rpm_put;
	} else {
		imx283_stop_streaming(imx283);
		pm_runtime_put(&client->dev);
	}

	imx283->streaming = enable;

	__v4l2_ctrl_grab(imx283->vflip, enable);

	mutex_unlock(&imx283->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx283->mutex);

	return ret;
}

static int imx283_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx283 *imx283 = to_imx283(sd);
	int ret;

	ret = regulator_bulk_enable(IMX283_NUM_SUPPLIES,
				    imx283->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx283->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx283->reset_gpio, 1);
	usleep_range(IMX283_XCLR_MIN_DELAY_US,
		     IMX283_XCLR_MIN_DELAY_US + IMX283_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(IMX283_NUM_SUPPLIES, imx283->supplies);
	return ret;
}

static int imx283_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx283 *imx283 = to_imx283(sd);

	gpiod_set_value_cansleep(imx283->reset_gpio, 0);
	clk_disable_unprepare(imx283->xclk);
	regulator_bulk_disable(IMX283_NUM_SUPPLIES, imx283->supplies);

	return 0;
}

static int __maybe_unused imx283_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx283 *imx283 = to_imx283(sd);

	if (imx283->streaming)
		imx283_stop_streaming(imx283);

	return 0;
}

static int __maybe_unused imx283_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx283 *imx283 = to_imx283(sd);
	int ret;

	if (imx283->streaming) {
		ret = imx283_start_streaming(imx283);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx283_stop_streaming(imx283);
	imx283->streaming = 0;
	return ret;
}

static int imx283_get_regulators(struct imx283 *imx283)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	unsigned int i;

	for (i = 0; i < IMX283_NUM_SUPPLIES; i++)
		imx283->supplies[i].supply = imx283_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX283_NUM_SUPPLIES,
				       imx283->supplies);
}

static int imx283_identify_module(struct imx283 *imx283)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	int ret;
	u32 standby, blklevel;

	ret = imx283_read_reg(imx283, IMX283_REG_STANDBY, 1, &standby);
	if (!ret)
		ret = imx283_read_reg(imx283, IMX283_REG_BLKLEVEL, 1,
				      &blklevel);
	if (ret) {
		dev_err(&client->dev,
			"failed to read sensor (POR probe), error %d\n", ret);
		return ret;
	}

	if (standby != IMX283_POR_STANDBY_VAL ||
	    blklevel != IMX283_BLKLEVEL_REG_DEFAULT)
		dev_warn(&client->dev,
			 "unexpected POR state 0x%x/0x%x (expected 0x%x/0x%x)\n",
			 standby, blklevel, IMX283_POR_STANDBY_VAL,
			 IMX283_BLKLEVEL_REG_DEFAULT);

	dev_info(&client->dev, "IMX283 found on %s\n",
		 dev_name(&client->adapter->dev));

	return 0;
}

static const struct v4l2_subdev_core_ops imx283_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx283_video_ops = {
	.s_stream = imx283_set_stream,
};

static const struct v4l2_subdev_pad_ops imx283_pad_ops = {
	.enum_mbus_code = imx283_enum_mbus_code,
	.get_fmt = imx283_get_pad_format,
	.set_fmt = imx283_set_pad_format,
	.get_selection = imx283_get_selection,
	.set_selection = imx283_set_selection,
	.enum_frame_size = imx283_enum_frame_size,
};

static const struct v4l2_subdev_ops imx283_subdev_ops = {
	.core = &imx283_core_ops,
	.video = &imx283_video_ops,
	.pad = &imx283_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx283_internal_ops = {
	.open = imx283_open,
};

static int imx283_init_controls(struct imx283 *imx283)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx283->sd);
	struct v4l2_fwnode_device_properties props;
	unsigned int i;
	int ret;

	ctrl_hdlr = &imx283->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 20);
	if (ret)
		return ret;

	mutex_init(&imx283->mutex);
	ctrl_hdlr->lock = &imx283->mutex;

	imx283->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       IMX283_PIXEL_RATE,
					       IMX283_PIXEL_RATE, 1,
					       IMX283_PIXEL_RATE);
	if (imx283->pixel_rate)
		imx283->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx283->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx283_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(link_freqs) - 1,
				       imx283->link_freq_idx, link_freqs);
	if (imx283->link_freq)
		imx283->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx283->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					   V4L2_CID_VBLANK, 0,
					   IMX283_VMAX_MAX, 1, 0);
	imx283->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					   V4L2_CID_HBLANK, 0,
					   IMX283_HMAX_MAX *
					   IMX283_PIXELS_PER_CLOCK, 1, 0);

	imx283->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX283_EXPOSURE_MIN,
					     IMX283_VMAX_MAX - 11,
					     IMX283_EXPOSURE_STEP,
					     IMX283_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX283_ANA_GAIN_MIN, IMX283_ANA_GAIN_MAX,
			  IMX283_ANA_GAIN_STEP, IMX283_ANA_GAIN_DEFAULT);

	imx283->black_level = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
						V4L2_CID_BLACK_LEVEL,
						0, IMX283_BLKLEVEL_MAX, 1,
						IMX283_BLKLEVEL_DEFAULT);

	imx283->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx283_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx283->vflip)
		imx283->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx283_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx283_test_pattern_menu) - 1,
				     0, 0, imx283_test_pattern_menu);

	for (i = 0; i < ARRAY_SIZE(imx283_ob_ctrls); i++)
		v4l2_ctrl_new_custom(ctrl_hdlr, &imx283_ob_ctrls[i], NULL);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx283_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx283->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&imx283->mutex);
	imx283_set_framing_limits(imx283);
	mutex_unlock(&imx283->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx283->mutex);

	return ret;
}

static void imx283_free_controls(struct imx283 *imx283)
{
	v4l2_ctrl_handler_free(imx283->sd.ctrl_handler);
	mutex_destroy(&imx283->mutex);
}

static int imx283_check_hwcfg(struct device *dev, struct imx283 *imx283)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	unsigned int i;
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

	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(dev, "only 4 data lanes supported, got %d\n",
			ep_cfg.bus.mipi_csi2.num_data_lanes);
		goto error_out;
	}

	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	for (i = 0; i < ARRAY_SIZE(link_freqs); i++)
		if (ep_cfg.link_frequencies[0] == link_freqs[i])
			break;
	if (i == ARRAY_SIZE(link_freqs)) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
		goto error_out;
	}
	imx283->link_freq_idx = i;

	if (link_freqs[i] > IMX283_PLATFORM_MAX_LINK_FREQ)
		dev_warn(dev,
			 "link-frequency %lld Hz not supported\n",
			 link_freqs[i]);

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static const struct of_device_id imx283_dt_ids[] = {
	{ .compatible = "sony,imx283" },
	{  }
};

static int imx283_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx283 *imx283;
	unsigned int i;
	int ret;

	imx283 = devm_kzalloc(&client->dev, sizeof(*imx283), GFP_KERNEL);
	if (!imx283)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx283->sd, client, &imx283_subdev_ops);

	for (i = 0; i < ARRAY_SIZE(base_configs); i++) {
		unsigned int step = base_configs[i].h_step;

		if ((IMX283_HTRIM_RECORDING_START - IMX283_HTRIM_HOST) % step ||
		    IMX283_CROP_LEFT_STEP % step || IMX283_WIDTH_STEP % step) {
			dev_err(dev,
				"config %u: HTRIMMING step %u does not divide the global geometry steps\n",
				i, step);
			return -EINVAL;
		}
	}

	if (imx283_check_hwcfg(dev, imx283))
		return -EINVAL;

	imx283->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx283->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx283->xclk);
	}

	imx283->xclk_freq = clk_get_rate(imx283->xclk);
	for (i = 0; i < ARRAY_SIZE(imx283_inck_table); i++) {
		if (imx283_inck_table[i].xclk_hz == imx283->xclk_freq) {
			imx283->inck = &imx283_inck_table[i];
			break;
		}
	}
	if (!imx283->inck) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx283->xclk_freq);
		return -EINVAL;
	}

	ret = imx283_get_regulators(imx283);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	imx283->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(imx283->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx283->reset_gpio),
				     "failed to get reset GPIO\n");

	ret = imx283_power_on(dev);
	if (ret)
		return ret;

	ret = imx283_identify_module(imx283);
	if (ret)
		goto error_power_off;

	imx283_set_default_format(imx283);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = imx283_init_controls(imx283);
	if (ret)
		goto error_pm;

	imx283->sd.internal_ops = &imx283_internal_ops;
	imx283->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx283->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx283->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx283->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx283->sd.entity, NUM_PADS, imx283->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx283->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx283->sd.entity);

error_handler_free:
	imx283_free_controls(imx283);

error_pm:
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx283_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return ret;

error_power_off:
	imx283_power_off(&client->dev);

	return ret;
}

static void imx283_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx283 *imx283 = to_imx283(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx283_free_controls(imx283);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx283_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

MODULE_DEVICE_TABLE(of, imx283_dt_ids);

static const struct dev_pm_ops imx283_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx283_suspend, imx283_resume)
	SET_RUNTIME_PM_OPS(imx283_power_off, imx283_power_on, NULL)
};

static struct i2c_driver imx283_i2c_driver = {
	.driver = {
		.name = "imx283",
		.of_match_table	= imx283_dt_ids,
		.pm = &imx283_pm_ops,
	},
	.probe = imx283_probe,
	.remove = imx283_remove,
};

module_i2c_driver(imx283_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("Sony IMX283 sensor driver");
MODULE_LICENSE("GPL");
