// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX294 sensor driver
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
#include <linux/property.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/moduleparam.h>

int debug = 0;
module_param(debug, int, 0660);
MODULE_PARM_DESC(debug, "Debug flag");

struct imx294_link_cfg {
	u16 plrd1;
	u8 plrd15;
	bool write_3ac4;
};

static const struct imx294_link_cfg imx294_link_cfgs[] = {
	{ .plrd1 = 160, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 144, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 288, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 288, .plrd15 = 0x02, .write_3ac4 = true },
	{ .plrd1 = 288, .plrd15 = 0x04, .write_3ac4 = false },
	{ .plrd1 = 176, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 192, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 200, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 208, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 216, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 224, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 240, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 256, .plrd15 = 0x02, .write_3ac4 = false },
	{ .plrd1 = 272, .plrd15 = 0x02, .write_3ac4 = false },
};

static const s64 imx294_link_freqs[] = {
	480000000,
	528000000,
	576000000,
	600000000,
};

#define IMX294_PLRD1_FROM_LINK_FREQ(f)	((u16)div_u64((u64)(f), 3000000u))

static u8 imx294_sys_mode(void)
{
	return 1;
}

static u32 imx294_sys_divider(void)
{
	return 24;
}

#define DEBUG_PRINTK(fmt, ...) do { if (debug) printk(KERN_DEBUG "%s: " fmt, __this_module.name, ##__VA_ARGS__); } while(0)

#define IMX294_REG_CHIP_ID		0x3000
#define IMX294_CHIP_ID			0x0000

#define IMX294_REG_MODE_SELECT		0x3000
#define IMX294_MODE_STANDBY		0x01
#define IMX294_MODE_STREAMING		0x00

#define IMX294_XCLK_FREQ		24000000

#define IMX294_REG_VMAX		0x30A9
#define IMX294_VMAX_MAX		0xfffff

#define IMX294_REG_HMAX		0x30AC
#define IMX294_HMAX_MAX		0xffff

#define IMX294_PIXEL_RATE	600000000ULL

#define IMX294_REG_HTRIMMING_EN		0x3035
#define IMX294_REG_HTRIMMING_START	0x3036
#define IMX294_REG_HTRIMMING_END	0x3038
#define IMX294_HTRIM_BASE		48
#define IMX294_HTRIM_START_STEP		12
#define IMX294_HTRIM_END_STEP		4
#define IMX294_HTRIM_MIN_WIDTH		36
#define IMX294_HTRIM_END_STEP_BIN2	24
#define IMX294_HTRIM_MIN_WIDTH_BIN2	72

#define IMX294_HNUM_MAX			4224U

#define IMX294_REG_HCOUNT1     0x3084
#define IMX294_REG_HCOUNT2     0x3086
#define IMX294_REG_PSSLVS1 0x332C
#define IMX294_REG_PSSLVS2 0x334A
#define IMX294_REG_PSSLVS3 0x35B6
#define IMX294_REG_PSSLVS4 0x35B8
#define IMX294_REG_PSSLVS0 0x36BC

#define IMX294_REG_SHR		0x302C
#define IMX294_SHR_MIN		11

#define IMX294_EXPOSURE_MIN			52
#define IMX294_EXPOSURE_STEP		1
#define IMX294_EXPOSURE_DEFAULT		1000
#define IMX294_EXPOSURE_MAX		49865

#define IMX294_REG_ANALOG_GAIN		0x300A
#define IMX294_ANA_GAIN_MIN		0
#define IMX294_ANA_GAIN_MAX		270
#define IMX294_ANA_GAIN_STEP		1
#define IMX294_ANA_GAIN_DEFAULT		0

#define IMX294_REG_BLACK_LEVEL		0x3042
#define IMX294_BLACK_LEVEL_SHIFT	2
#define IMX294_BLACK_LEVEL_MIN		0
#define IMX294_BLACK_LEVEL_MAX		1020
#define IMX294_BLACK_LEVEL_STEP		4
#define IMX294_BLACK_LEVEL_DEFAULT	200

static const u16 imx294_pgc_code[IMX294_ANA_GAIN_MAX + 1] = {
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

static unsigned int imx294_gain_code(unsigned int db10)
{
	if (db10 > IMX294_ANA_GAIN_MAX)
		db10 = IMX294_ANA_GAIN_MAX;

	return imx294_pgc_code[db10];
}

#define IMX294_EMBEDDED_LINE_WIDTH 16384
#define IMX294_NUM_EMBEDDED_LINES 1

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

#define IMX294_PIXEL_ARRAY_LEFT		IMX294_HTRIM_BASE
#define IMX294_PIXEL_ARRAY_TOP		16U
#define IMX294_PIXEL_ARRAY_WIDTH	4176U
#define IMX294_PIXEL_ARRAY_HEIGHT	2824U
#define IMX294_NATIVE_WIDTH		(IMX294_PIXEL_ARRAY_LEFT + \
					 IMX294_PIXEL_ARRAY_WIDTH)
#define IMX294_NATIVE_HEIGHT		(IMX294_PIXEL_ARRAY_TOP + \
					 IMX294_PIXEL_ARRAY_HEIGHT)

struct imx294_reg {
	u16 address;
	u8 val;
};

struct IMX294_reg_list {
	unsigned int num_of_regs;
	const struct imx294_reg *regs;
};

struct imx294_mode {
	unsigned int width;

	unsigned int height;

	uint64_t min_HMAX;

	uint64_t min_VMAX;

	uint64_t default_HMAX;

	uint64_t default_VMAX;

    uint64_t VMAX_scale;

	uint64_t min_SHR;

	unsigned int hmax_crop_num;
	unsigned int hmax_crop_den;
	unsigned int hmax_crop_add_x10;
	unsigned int hmax_crop_floor;

	unsigned int px_per_clk_x10;

	unsigned int hnum;

	unsigned int hbin;
	unsigned int vbin;

    unsigned int integration_offset;

	struct v4l2_rect recording;

	struct IMX294_reg_list reg_list;
};

static const struct imx294_reg mode_common_regs[] = {

    {0x3033,0x30},
    {0x303C,0x01},

    {0x31E8,160},
    {0x31E9,0x00},

    {0x3122,0x02},
    {0x3129,0x90},
    {0x312A,0x02},

    {0x311F,0x00},
    {0x3123,0x00},
    {0x3124,0x00},
    {0x3125,0x01},
    {0x3127,0x02},
    {0x312D,0x02},

    {0x3000,0x12},
    {0x310B,0x00},

    {0x3047,0x01},
    {0x304E,0x0B},
    {0x304F,0x24},
    {0x3062,0x25},
    {0x3064,0x78},
    {0x3065,0x33},
    {0x3067,0x71},
    {0x3088,0x75},
    {0x308A,0x09},
    {0x308B,0x01},
    {0x308C,0x61},
    {0x3146,0x00},
    {0x3234,0x32},
    {0x3235,0x00},
    {0x3248,0xBC},
    {0x3249,0x00},
    {0x3250,0xBC},
    {0x3251,0x00},
    {0x3258,0xBC},
    {0x3259,0x00},
    {0x3260,0xBC},
    {0x3261,0x00},
    {0x3274,0x13},
    {0x3275,0x00},
    {0x3276,0x1F},
    {0x3277,0x00},
    {0x3278,0x30},
    {0x3279,0x00},
    {0x327C,0x13},
    {0x327D,0x00},
    {0x327E,0x1F},
    {0x327F,0x00},
    {0x3280,0x30},
    {0x3281,0x00},
    {0x3284,0x13},
    {0x3285,0x00},
    {0x3286,0x1F},
    {0x3287,0x00},
    {0x3288,0x30},
    {0x3289,0x00},
    {0x328C,0x13},
    {0x328D,0x00},
    {0x328E,0x1F},
    {0x328F,0x00},
    {0x3290,0x30},
    {0x3291,0x00},
    {0x32AE,0x00},
    {0x32AF,0x00},
    {0x32CA,0x5A},
    {0x32CB,0x00},
    {0x332F,0x00},
    {0x334C,0x01},
    {0x335A,0x79},
    {0x335B,0x00},
    {0x335E,0x56},
    {0x335F,0x00},
    {0x3360,0x6A},
    {0x3361,0x00},
    {0x336A,0x56},
    {0x336B,0x00},
    {0x33D6,0x79},
    {0x33D7,0x00},
    {0x340C,0x6E},
    {0x340D,0x00},
    {0x3448,0x7E},
    {0x3449,0x00},
    {0x348E,0x6F},
    {0x348F,0x00},
    {0x3492,0x11},
    {0x34C4,0x5A},
    {0x34C5,0x00},
    {0x3506,0x56},
    {0x3507,0x00},
    {0x350C,0x56},
    {0x350D,0x00},
    {0x350E,0x58},
    {0x350F,0x00},
    {0x3549,0x04},
    {0x355D,0x03},
    {0x355E,0x03},
    {0x3574,0x56},
    {0x3575,0x00},
    {0x3587,0x01},
    {0x35D0,0x5E},
    {0x35D1,0x00},
    {0x35D4,0x63},
    {0x35D5,0x00},
    {0x366A,0x1A},
    {0x366B,0x16},
    {0x366C,0x10},
    {0x366D,0x09},
    {0x366E,0x00},
    {0x366F,0x00},
    {0x3670,0x00},
    {0x3671,0x00},
    {0x3676,0x83},
    {0x3677,0x03},
    {0x3678,0x00},
    {0x3679,0x04},
    {0x367A,0x2C},
    {0x367B,0x05},
    {0x367C,0x00},
    {0x367D,0x06},
    {0x367E,0x00},
    {0x367F,0x07},
    {0x3680,0x4B},
    {0x3681,0x07},
    {0x3690,0x27},
    {0x3691,0x00},
    {0x3692,0x65},
    {0x3693,0x00},
    {0x3694,0x4F},
    {0x3695,0x00},
    {0x3696,0xA1},
    {0x3697,0x00},
    {0x382B,0x68},
    {0x3C00,0x01},
    {0x3C01,0x01},
    {0x3686,0x00},
    {0x3687,0x00},
    {0x36BE,0x01},
    {0x36BF,0x00},
    {0x36C0,0x01},
    {0x36C1,0x00},
    {0x36C2,0x01},
    {0x36C3,0x00},
    {0x36C4,0x01},
    {0x36C5,0x01},
    {0x36C6,0x01},

    {0x3134,0xAF},
    {0x3135,0x00},
    {0x3136,0xC7},
    {0x3137,0x00},
    {0x3138,0x7F},
    {0x3139,0x00},
    {0x313A,0x6F},
    {0x313B,0x00},
    {0x313C,0x6F},
    {0x313D,0x00},
    {0x313E,0xCF},
    {0x313F,0x01},
    {0x3140,0x77},
    {0x3141,0x00},
    {0x3142,0x5F},
    {0x3143,0x00},

    {0x3004,0x1A},
    {0x3005,0x06},
    {0x3006,0x00},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x60},
    {0x3039,0x10},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x01},
    {0x30A8,0x02},
    {0x30E2,0x00},
    {0x312F,0x00},
    {0x3130,0x80},
    {0x3131,0x08},
    {0x3132,0x80},
    {0x3133,0x08},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},

    {0x300E,0x00},
    {0x300F,0x00},

    {0x302C,0x10},
    {0x302D,0x00},

    {0x30A9,0x88},
    {0x30AA,0x13},
    {0x30AB,0x00},

    {0x30AC,0xB0},
    {0x30AD,0x04},
    {0x3084,0xB0},
    {0x3085,0x04},
    {0x3086,0xB0},
    {0x3087,0x04},

    {0x332C,0x00},
    {0x332D,0x00},
    {0x334A,0x00},
    {0x334B,0x00},
    {0x35B6,0x00},
    {0x35B7,0x00},
    {0x35B8,0x00},
    {0x35B9,0x00},
    {0x36BC,0x00},
    {0x36BD,0x00},

    {0xFFFE,0x0A},

    {0x3000,0x02},
    {0x35E5,0x92},
    {0x35E5,0x9A},
    {0x3000,0x00},

    
    {0xFFFE,0x0A},

    {0x3033,0x20},
    {0x3017,0xA8},
};

static const struct imx294_reg mode_00_14bit_regs[] = {
    {0x3004,0x00},
    {0x3005,0x0B},
    {0x3006,0x02},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x00},
    {0x3039,0x0F},
    {0x3068,0x44},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x01},
    {0x30A8,0x03},
    {0x30E2,0x00},
    {0x312F,0x00},
    {0x3130,0x08},
    {0x3131,0x0B},
    {0x3132,0x08},
    {0x3133,0x0B},
    {0x357F,0x0A},
    {0x3580,0x09},
    {0x3581,0x07},
    {0x3583,0x51},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_reg mode_00_regs[] = {
    {0x3004,0x00},
    {0x3005,0x06},
    {0x3006,0x02},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x00},
    {0x3039,0x0F},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x01},
    {0x30A8,0x02},
    {0x30E2,0x00},
    {0x312F,0x00},
    {0x3130,0x08},
    {0x3131,0x0B},
    {0x3132,0x08},
    {0x3133,0x0B},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_reg mode_01_regs[] = {
    {0x3004,0x1A},
    {0x3005,0x06},
    {0x3006,0x00},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x60},
    {0x3039,0x10},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x01},
    {0x30A8,0x02},
    {0x30E2,0x00},
    {0x312F,0x00},
    {0x3130,0x80},
    {0x3131,0x08},
    {0x3132,0x80},
    {0x3133,0x08},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_reg mode_01A_regs[] = {
    {0x3004,0x01},
    {0x3005,0x06},
    {0x3006,0x00},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x80},
    {0x3039,0x10},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x01},
    {0x3081,0x01},
    {0x30A8,0x02},
    {0x30E2,0x00},
    {0x312F,0x00},
    {0x3130,0x80},
    {0x3131,0x08},
    {0x3132,0x80},
    {0x3133,0x08},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x7D},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_reg mode_01B_regs[] = {
    {0x3004,0x02},
    {0x3005,0x06},
    {0x3006,0x01},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x50},
    {0x3039,0x0F},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x01},
    {0x30A8,0x02},
    {0x30E2,0x00},
    {0x312F,0x00},
    {0x3130,0x80},
    {0x3131,0x08},
    {0x3132,0x80},
    {0x3133,0x08},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_reg mode_03_regs[] = {
    {0x3004,0xA8},
    {0x3005,0x2A},
    {0x3006,0x00},
    {0x3007,0xA0},
    {0x3019,0x00},
    {0x3030,0x77},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x80},
    {0x3039,0x10},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x01},
    {0x30A8,0x02},
    {0x30E2,0x02},
    {0x312F,0x00},
    {0x3130,0x48},
    {0x3131,0x04},
    {0x3132,0x48},
    {0x3133,0x04},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_reg mode_04_regs[] = {
    {0x3004,0x0A},
    {0x3005,0x26},
    {0x3006,0x00},
    {0x3007,0xA1},
    {0x3019,0x00},
    {0x3030,0x33},
    {0x3034,0x00},
    {0x3035,0x01},
    {0x3036,0x30},
    {0x3037,0x00},
    {0x3038,0x80},
    {0x3039,0x10},
    {0x3068,0x1A},
    {0x3069,0x00},
    {0x3080,0x00},
    {0x3081,0x00},
    {0x30A8,0x02},
    {0x30E2,0x03},
    {0x312F,0x00},
    {0x3130,0x48},
    {0x3131,0x04},
    {0x3132,0x48},
    {0x3133,0x04},
    {0x357F,0x0C},
    {0x3580,0x0A},
    {0x3581,0x08},
    {0x3583,0x72},
    {0x3600,0x90},
    {0x3601,0x00},
    {0x3846,0x00},
    {0x3847,0x00},
    {0x384A,0x00},
    {0x384B,0x00},
};

static const struct imx294_mode supported_modes_14bit[] = {
    {
        .width = 3792,
        .height = 2824,
        .min_HMAX = 1730,
        .px_per_clk_x10 = 64,
        .hnum = 3840,
        .hbin = 1,
        .vbin = 1,
        .min_VMAX = 1444,
        .default_HMAX = 1875,
        .default_VMAX = 1600,
        .VMAX_scale = 2,
        .min_SHR = 5,
        .integration_offset = 551,
        .recording = {
            .left = 40,
            .top = 8,
            .width = 3704,
            .height = 2776,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_00_14bit_regs),
            .regs = mode_00_14bit_regs,
        },
    },
    {
        .width = 2088,
        .height = 1096,
        .min_HMAX = 706,
        .hnum = 4224,
        .hbin = 2,
        .vbin = 2,
        .min_VMAX = 1148,
        .default_HMAX = 715,
        .default_VMAX = 1680,
        .VMAX_scale = 1,
        .min_SHR = 5,
        .integration_offset = 256,
        .recording = {
            .left = 18,
            .top = 10,
            .width = 2048,
            .height = 1080,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_03_regs),
            .regs = mode_03_regs,
        },
    },
};

static const struct imx294_mode supported_modes_12bit[] = {
	{
		.width = 4144,
		.height = 2176,
		.min_HMAX = 1122,
		.hmax_crop_num = 1,
		.hmax_crop_den = 4,
		.hmax_crop_add_x10 = 853,
		.hmax_crop_floor = 706,
		.hnum = 4192,
		.hbin = 1,
		.vbin = 1,
		.min_VMAX = 1111,
		.default_HMAX = 1200,
		.default_VMAX = 2500,
        .VMAX_scale = 2,
		.min_SHR = 5,
        .integration_offset = 256,
		.recording = {
			.left = 36,
			.top = 12,
			.width = 4096,
			.height = 2160,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_01_regs),
			.regs = mode_01_regs,
		},
	},
    {
        .width = 4176,
        .height = 2176,
        .min_HMAX = 1192,
        .hnum = 4224,
        .hbin = 1,
        .vbin = 1,
        .min_VMAX = 1111,
        .default_HMAX = 1200,
        .default_VMAX = 2500,
        .VMAX_scale = 2,
        .min_SHR = 5,
        .integration_offset = 361,
        .recording = {
            .left = 36,
            .top = 12,
            .width = 4096,
            .height = 2160,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_01A_regs),
            .regs = mode_01A_regs,
        },
    },
    {
        .width = 3872,
        .height = 2176,
        .min_HMAX = 1055,
        .hmax_crop_num = 1,
        .hmax_crop_den = 4,
        .hmax_crop_add_x10 = 853,
        .hmax_crop_floor = 706,
        .hnum = 3920,
        .hbin = 1,
        .vbin = 1,
        .min_VMAX = 1111,
        .default_HMAX = 1200,
        .default_VMAX = 2500,
        .VMAX_scale = 2,
        .min_SHR = 5,
        .integration_offset = 256,
        .recording = {
            .left = 20,
            .top = 12,
            .width = 3840,
            .height = 2160,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_01B_regs),
            .regs = mode_01B_regs,
        },
    },
    {
        .width = 3792,
        .height = 2824,
        .min_HMAX = 1034,
        .hmax_crop_num = 1,
        .hmax_crop_den = 4,
        .hmax_crop_add_x10 = 853,
        .hmax_crop_floor = 706,
        .hnum = 3840,
        .hbin = 1,
        .vbin = 1,
        .min_VMAX = 1444,
        .default_HMAX = 1875,
        .default_VMAX = 1600,
        .VMAX_scale = 2,
        .min_SHR = 5,
        .integration_offset = 551,
        .recording = {
            .left = 40,
            .top = 8,
            .width = 3704,
            .height = 2776,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_00_regs),
            .regs = mode_00_regs,
        },
    },
    {
        .width = 2088,
        .height = 1096,
        .min_HMAX = 706,
        .hnum = 4224,
        .hbin = 2,
        .vbin = 2,
        .min_VMAX = 1148,
        .default_HMAX = 715,
        .default_VMAX = 1680,
        .VMAX_scale = 1,
        .min_SHR = 3,
        .integration_offset = 256,
        .recording = {
            .left = 18,
            .top = 10,
            .width = 2048,
            .height = 1080,
        },
        .reg_list = {
            .num_of_regs = ARRAY_SIZE(mode_04_regs),
            .regs = mode_04_regs,
        },
    },
};

static const u32 codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SRGGB14_1X14,
	MEDIA_BUS_FMT_SGRBG14_1X14,
	MEDIA_BUS_FMT_SGBRG14_1X14,
	MEDIA_BUS_FMT_SBGGR14_1X14,

};

static const char * const imx294_supply_name[] = {
	"VANA",
	"VDIG",
	"VDDL",
};

#define imx294_NUM_SUPPLIES ARRAY_SIZE(imx294_supply_name)

#define imx294_XCLR_MIN_DELAY_US	100000
#define imx294_XCLR_DELAY_RANGE_US	1000

struct imx294_compatible_data {
	unsigned int chip_id;
	struct IMX294_reg_list extra_regs;
};

struct imx294 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	unsigned int htrim_width;

	unsigned int vtrim_height;

	unsigned int crop_left;

	struct v4l2_rect sel_crop;
	struct v4l2_rect sel_compose;

	bool binning;

	struct clk *xclk;
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[imx294_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	const struct imx294_mode *mode;

	u32 limits_inck;

	u64 limits_hblank_min;

	uint16_t HMAX;
	uint32_t VMAX;
	struct mutex mutex;

	bool streaming;

	bool common_regs_written;

	u32 link_cfg;

	int link_freq_idx;

	struct v4l2_ctrl *link_freq;

	const struct imx294_compatible_data *compatible_data;
};

static inline struct imx294 *to_imx294(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx294, sd);
}

static u16 imx294_eff_plrd1(const struct imx294 *imx294,
			    const struct imx294_link_cfg *lt)
{
	if (imx294->link_freq_idx >= 0)
		return IMX294_PLRD1_FROM_LINK_FREQ(
			imx294_link_freqs[imx294->link_freq_idx]);
	return lt->plrd1;
}

static inline void get_mode_table(unsigned int code,
				  const struct imx294_mode **mode_list,
				  unsigned int *num_modes)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB14_1X14:
	case MEDIA_BUS_FMT_SGRBG14_1X14:
	case MEDIA_BUS_FMT_SGBRG14_1X14:
	case MEDIA_BUS_FMT_SBGGR14_1X14:
		*mode_list = supported_modes_14bit;
		*num_modes = ARRAY_SIZE(supported_modes_14bit);
		break;
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		*mode_list = supported_modes_12bit;
		*num_modes = ARRAY_SIZE(supported_modes_12bit);
		break;
	default:
		*mode_list = NULL;
		*num_modes = 0;
	}
}

static int imx294_read_reg(struct imx294 *imx294, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
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
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int imx294_write_reg_1byte(struct imx294 *imx294, u16 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	u8 buf[3];

	put_unaligned_be16(reg, buf);
	buf[2]  = val;
	if (i2c_master_send(client, buf, 3) != 3)
		return -EIO;

	return 0;
}

static int imx294_write_reg_2byte(struct imx294 *imx294, u16 reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	u8 buf[4];

	put_unaligned_be16(reg, buf);
	buf[2]  = val;
	buf[3]  = val>>8;
	if (i2c_master_send(client, buf, 4) != 4)
		return -EIO;

	return 0;
}

static int imx294_write_reg_3byte(struct imx294 *imx294, u16 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	u8 buf[5];

	put_unaligned_be16(reg, buf);
	buf[2]  = val;
	buf[3]  = val>>8;
	buf[4]  = val>>16;
	if (i2c_master_send(client, buf, 5) != 5)
		return -EIO;

	return 0;
}

static unsigned int imx294_vtrim_calc(const struct imx294_mode *mode,
				      unsigned int want);

static int imx294_write_regs(struct imx294 *imx294,
			     const struct imx294_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	u32 lt_mode = imx294->link_cfg;
	unsigned int vsize = (imx294->vtrim_height
			      ? imx294_vtrim_calc(imx294->mode,
						  imx294->vtrim_height)
			      : 0);
	const struct imx294_link_cfg *lt =
		&imx294_link_cfgs[lt_mode < ARRAY_SIZE(imx294_link_cfgs) ?
				   lt_mode : 0];
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		if (regs[i].address == 0xFFFE) {
			usleep_range(regs[i].val*1000,(regs[i].val+1)*1000);
		}
		else{
			u8 val = regs[i].val;

			switch (regs[i].address) {
			case 0x303C:
				val = imx294_sys_mode();
				break;
			case 0x31E8:
				val = imx294_eff_plrd1(imx294, lt) & 0xff;
				break;
			case 0x31E9:
				val = imx294_eff_plrd1(imx294, lt) >> 8;
				break;
			case 0x312D:
				val = lt->plrd15;
				break;
			case 0x3129:
				val = imx294_eff_plrd1(imx294, lt) / 2;
				break;
			case 0x3130:
			case 0x3132:
				if (vsize)
					val = vsize & 0xff;
				break;
			case 0x3131:
			case 0x3133:
				if (vsize)
					val = (vsize >> 8) & 0x1f;
				break;
			}

			ret = imx294_write_reg_1byte(imx294, regs[i].address, val);
			if (!ret && regs[i].address == 0x36C5 && lt->write_3ac4)
				ret = imx294_write_reg_1byte(imx294, 0x3AC4, 0x01);
			if (ret) {
				dev_err_ratelimited(&client->dev,
						    "Failed to write reg 0x%4.4x. error = %d\n",
						    regs[i].address, ret);

				return ret;
			}
		}
	}

	return 0;
}

static u32 imx294_get_format_code(struct imx294 *imx294, u32 code)
{
	unsigned int i;

	lockdep_assert_held(&imx294->mutex);

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == code)
			break;

	if (i >= ARRAY_SIZE(codes))
		i = 0;

	if (imx294->vflip && imx294->hflip)
		i = (i & ~3) | (imx294->vflip->val ? 2 : 0) |
		    (imx294->hflip->val ? 1 : 0);

	return codes[i];
}

static void imx294_set_default_format(struct imx294 *imx294)
{
	imx294->mode = &supported_modes_12bit[0];
	imx294->fmt_code = MEDIA_BUS_FMT_SRGGB12_1X12;
	imx294->crop_left = UINT_MAX;

	imx294->binning = false;
	memset(&imx294->sel_crop, 0, sizeof(imx294->sel_crop));
	memset(&imx294->sel_compose, 0, sizeof(imx294->sel_compose));
}

static int imx294_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx294 *imx294 = to_imx294(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;

	mutex_lock(&imx294->mutex);

	try_fmt_img->width = supported_modes_12bit[0].width;
	try_fmt_img->height = supported_modes_12bit[0].height;
	try_fmt_img->code = imx294_get_format_code(imx294,
						   MEDIA_BUS_FMT_SRGGB12_1X12);
	try_fmt_img->field = V4L2_FIELD_NONE;

	try_fmt_meta->width = IMX294_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX294_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = IMX294_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX294_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx294->mutex);

	return 0;
}

static u64 calculate_v4l2_cid_exposure(u64 hmax, u64 vmax, u64 shr, u64 svr, u64 offset, u64 vmax_scale) {
    u64 numerator;

    if (!vmax_scale)
        vmax_scale = 1;

    numerator = ((vmax * (svr + 1) - shr) * hmax + offset) * vmax_scale;

    do_div(numerator, hmax);
    numerator = clamp_t(uint32_t, numerator, 0, 0xFFFFFFFF);
    return numerator;
}

static void calculate_min_max_v4l2_cid_exposure(u64 hmax, u64 vmax, u64 min_shr, u64 svr, u64 offset, u64 vmax_scale, u64 *min_exposure, u64 *max_exposure) {
    u64 max_shr = (svr + 1) * vmax - 4;
    max_shr = min_t(uint64_t, max_shr, 0xFFFF);

    *min_exposure = calculate_v4l2_cid_exposure(hmax, vmax, max_shr, svr, offset, vmax_scale);
    *max_exposure = calculate_v4l2_cid_exposure(hmax, vmax, min_shr, svr, offset, vmax_scale);
}

#define IMX294_INCK_REF_HZ	72000000u
#define IMX294_PLRD1_REF	288u
#define IMX294_SYS_DIV_REF	24u

static u32 imx294_inck_hz(struct imx294 *imx294)
{
	u32 lt_mode = imx294->link_cfg;
	const struct imx294_link_cfg *lt =
		&imx294_link_cfgs[lt_mode < ARRAY_SIZE(imx294_link_cfgs) ?
				   lt_mode : 0];
	u64 hz = (u64)IMX294_INCK_REF_HZ * imx294_eff_plrd1(imx294, lt) *
		 IMX294_SYS_DIV_REF;

	do_div(hz, IMX294_PLRD1_REF * imx294_sys_divider());
	return (u32)hz;
}

static uint32_t calculate_shr(uint32_t exposure, uint32_t hmax, uint64_t vmax, uint32_t svr, uint32_t offset, uint32_t min_shr, u64 vmax_scale) {
    uint64_t temp;
    uint64_t frame = vmax * (svr + 1);
    uint32_t shr;

    if (!vmax_scale)
        vmax_scale = 1;

    temp = (uint64_t)exposure * hmax;
    do_div(temp, vmax_scale);
    temp = (temp > offset) ? temp - offset : 0;
    do_div(temp, hmax);

    shr = (temp >= frame) ? 0 : (uint32_t)(frame - temp);

    if (shr < min_shr)
        shr = min_shr;

    return shr;
}

static unsigned int imx294_htrim_calc_at(const struct imx294_mode *mode,
					unsigned int want, unsigned int left,
					unsigned int *start_out,
					unsigned int *end_out)
{
	unsigned int hnum = mode->hnum;
	unsigned int hbin = mode->hbin ? mode->hbin : 1;
	unsigned int end_step = hbin > 1 ? IMX294_HTRIM_END_STEP_BIN2
					 : IMX294_HTRIM_END_STEP;
	unsigned int min_span = hbin > 1 ? IMX294_HTRIM_MIN_WIDTH_BIN2
					 : IMX294_HTRIM_MIN_WIDTH;
	unsigned int full, span, start, end;

	if (!hnum || hnum <= IMX294_HTRIM_BASE) {
		*start_out = 0;
		*end_out = 0;
		return mode->width;
	}

	full = (hnum - IMX294_HTRIM_BASE) / hbin;
	if (!want || want >= full) {
		*start_out = IMX294_HTRIM_BASE;
		*end_out = hnum;
		return full;
	}

	span = want * hbin;
	if (span < min_span)
		span = min_span;

	if (left != UINT_MAX) {
		unsigned int max_left = (hnum - IMX294_HTRIM_BASE) - span;

		start = IMX294_HTRIM_BASE + min(left, max_left);
	} else {
		start = IMX294_HTRIM_BASE +
			((hnum - IMX294_HTRIM_BASE) - span) / 2;
	}
	start = IMX294_HTRIM_BASE +
		((start - IMX294_HTRIM_BASE) / IMX294_HTRIM_START_STEP) *
		IMX294_HTRIM_START_STEP;

	end = start + span;
	if (end > hnum)
		end = hnum;
	end = hnum - ((hnum - end) / end_step) * end_step;

	if (end - start < min_span)
		end = start + min_span;
	if (end > hnum)
		end = hnum;

	*start_out = start;
	*end_out = end;
	return (end - start) / hbin;
}

static u32 imx294_min_hmax_calc(const struct imx294_mode *mode,
				unsigned int span)
{
	u64 law;

	if (!mode->hmax_crop_den || !span)
		return (u32)mode->min_HMAX;

	law = (u64)span * mode->hmax_crop_num * 10 +
	      (u64)mode->hmax_crop_add_x10 * mode->hmax_crop_den;
	law = div_u64(law + mode->hmax_crop_den * 10 - 1,
		      mode->hmax_crop_den * 10);

	if (law < mode->hmax_crop_floor)
		law = mode->hmax_crop_floor;

	if (law > mode->min_HMAX)
		law = mode->min_HMAX;

	return (u32)law;
}

static u64 imx294_hmax_from_ll(struct imx294 *imx294,
			       const struct imx294_mode *mode, u64 line_length)
{
	u64 hmax = line_length * mode->VMAX_scale * imx294_inck_hz(imx294);

	do_div(hmax, IMX294_PIXEL_RATE);
	return hmax;
}

static u64 imx294_ll_from_hmax(struct imx294 *imx294,
			       const struct imx294_mode *mode, u64 hmax)
{
	u32 div = (u32)(mode->VMAX_scale * imx294_inck_hz(imx294));
	u64 ll  = hmax * IMX294_PIXEL_RATE + div - 1;

	do_div(ll, div);
	return ll;
}

static u32 imx294_min_vmax_calc(const struct imx294_mode *mode,
				unsigned int crop_height)
{
	unsigned int full = mode->recording.height;
	u64 cut;

	if (!crop_height || crop_height >= full)
		return (u32)mode->min_VMAX;

	cut = full - crop_height;
	cut = div_u64(cut + mode->VMAX_scale - 1, mode->VMAX_scale);

	if (cut >= mode->min_VMAX)
		return 1;

	return (u32)(mode->min_VMAX - cut);
}

#define IMX294_HEIGHT_STEP	8u

static unsigned int imx294_vtrim_calc(const struct imx294_mode *mode,
				      unsigned int want)
{
	unsigned int max = mode->height & ~(IMX294_HEIGHT_STEP - 1);

	if (!want || want >= max)
		return max;
	want &= ~(IMX294_HEIGHT_STEP - 1);
	return want < IMX294_HEIGHT_STEP ? IMX294_HEIGHT_STEP : want;
}

static int imx294_set_ctrl(struct v4l2_ctrl *ctrl)
{
	unsigned int eff_w, hs, he;
	struct imx294 *imx294 =
		container_of(ctrl->handler, struct imx294, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	const struct imx294_mode *mode = imx294->mode;
    u64 shr, vblk, tmp;
	int ret = 0;
        u64 hmax;
	if (ctrl->id == V4L2_CID_VBLANK){
		u64 current_exposure, max_exposure, min_exposure, vmax;

        vmax = ((u64)imx294_vtrim_calc(mode, imx294->vtrim_height) + ctrl->val);
        do_div(vmax, mode->VMAX_scale);

		imx294 -> VMAX = vmax;
		
		calculate_min_max_v4l2_cid_exposure(imx294 -> HMAX, imx294 -> VMAX, (u64)mode->min_SHR, 0, mode->integration_offset, mode->VMAX_scale, &min_exposure, &max_exposure);

		current_exposure = imx294->exposure->val;
		current_exposure = clamp_t(uint32_t, current_exposure, min_exposure, max_exposure);

		DEBUG_PRINTK("exposure_max:%lld, exposure_min:%lld, current_exposure:%lld\n",max_exposure, min_exposure, current_exposure);
		DEBUG_PRINTK("\tVMAX:%d, HMAX:%d\n",imx294->VMAX, imx294->HMAX);
		__v4l2_ctrl_modify_range(imx294->exposure, min_exposure,max_exposure, 1,current_exposure);
	}

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		{
		DEBUG_PRINTK("V4L2_CID_EXPOSURE : %d\n",ctrl->val);
		DEBUG_PRINTK("\tvblank:%d, hblank:%d\n",imx294->vblank->val, imx294->hblank->val);
		DEBUG_PRINTK("\tVMAX:%d, HMAX:%d\n",imx294->VMAX, imx294->HMAX);
		shr = calculate_shr(ctrl->val, imx294->HMAX, imx294->VMAX, 0, mode->integration_offset, mode->min_SHR, mode->VMAX_scale);
		DEBUG_PRINTK("\tSHR:%lld\n",shr);
		ret = imx294_write_reg_2byte(imx294, IMX294_REG_SHR, shr);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		DEBUG_PRINTK("V4L2_CID_ANALOGUE_GAIN : %d (0.1 dB)\n",ctrl->val);
		ret = imx294_write_reg_2byte(imx294, IMX294_REG_ANALOG_GAIN,
					     imx294_gain_code(ctrl->val));
		break;
	case V4L2_CID_BLACK_LEVEL:
		DEBUG_PRINTK("V4L2_CID_BLACK_LEVEL : %d (12-bit LSB)\n", ctrl->val);
		ret = imx294_write_reg_1byte(imx294, IMX294_REG_BLACK_LEVEL,
					     (u8)(ctrl->val >> IMX294_BLACK_LEVEL_SHIFT));
		break;
	case V4L2_CID_VBLANK:
		{
		DEBUG_PRINTK("V4L2_CID_VBLANK : %d\n",ctrl->val);
        tmp = ((u64)imx294_vtrim_calc(mode, imx294->vtrim_height) + ctrl->val);
        do_div(tmp, mode->VMAX_scale);
		imx294 -> VMAX = tmp;
		DEBUG_PRINTK("\tVMAX : %d\n",imx294 -> VMAX);
		ret = imx294_write_reg_3byte(imx294, IMX294_REG_VMAX, imx294 -> VMAX);
        vblk = (imx294->VMAX > mode->min_VMAX)
             ? imx294->VMAX - mode->min_VMAX
             : 0;
        DEBUG_PRINTK("\tvblk : %lld\n",vblk);
        ret = imx294_write_reg_2byte(imx294, IMX294_REG_PSSLVS1, vblk);
        ret = imx294_write_reg_2byte(imx294, IMX294_REG_PSSLVS2, vblk);
        ret = imx294_write_reg_2byte(imx294, IMX294_REG_PSSLVS3, vblk);
        if(vblk <= 5){
            ret = imx294_write_reg_2byte(imx294, IMX294_REG_PSSLVS4, 0);
        }
        else{
            ret = imx294_write_reg_2byte(imx294, IMX294_REG_PSSLVS4, vblk - 5);
        }
        ret = imx294_write_reg_2byte(imx294, IMX294_REG_PSSLVS0, vblk);
		}
		break;
	case V4L2_CID_HBLANK:
		{
		DEBUG_PRINTK("V4L2_CID_HBLANK : %d\n",ctrl->val);
		eff_w = imx294_htrim_calc_at(mode, imx294->htrim_width,
					     imx294->crop_left, &hs, &he);
		hmax = imx294_hmax_from_ll(imx294, mode, (u64)eff_w + ctrl->val);
		imx294 -> HMAX = hmax;
		DEBUG_PRINTK("\tHMAX : %d\n",imx294 -> HMAX);
		ret = imx294_write_reg_2byte(imx294, IMX294_REG_HMAX, hmax);
        ret = imx294_write_reg_2byte(imx294, IMX294_REG_HCOUNT1, hmax);
        ret = imx294_write_reg_2byte(imx294, IMX294_REG_HCOUNT2, hmax);
		}
		break;
	default:
		dev_err(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx294_ctrl_ops = {
	.s_ctrl = imx294_set_ctrl,
};

static int imx294_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx294 *imx294 = to_imx294(sd);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (code->index >= (ARRAY_SIZE(codes) / 4))
			return -EINVAL;

		code->code = imx294_get_format_code(imx294,
						    codes[code->index * 4]);
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx294_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx294 *imx294 = to_imx294(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		const struct imx294_mode *mode_list;
		unsigned int num_modes;

		get_mode_table(fse->code, &mode_list, &num_modes);

		if (fse->index >= num_modes)
			return -EINVAL;

		if (fse->code != imx294_get_format_code(imx294, fse->code))
			return -EINVAL;

		fse->min_width = mode_list[fse->index].width;
		fse->max_width = fse->min_width;
		fse->min_height = mode_list[fse->index].height;
		fse->max_height = fse->min_height;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX294_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX294_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx294_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx294_update_image_pad_format(struct imx294 *imx294,
					   const struct imx294_mode *mode,
					   struct v4l2_subdev_format *fmt)
{
	unsigned int htrim_s, htrim_e;

	fmt->format.width = imx294_htrim_calc_at(mode, imx294->htrim_width,
						 imx294->crop_left,
						 &htrim_s, &htrim_e);
	fmt->format.height = imx294_vtrim_calc(mode, imx294->vtrim_height);
	fmt->format.field = V4L2_FIELD_NONE;
	imx294_reset_colorspace(&fmt->format);
}

static void imx294_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX294_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX294_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx294_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx294 *imx294 = to_imx294(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx294->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx294_get_format_code(imx294, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx294_update_image_pad_format(imx294, imx294->mode,
						       fmt);
			fmt->format.code =
			       imx294_get_format_code(imx294, imx294->fmt_code);
		} else {
			imx294_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx294->mutex);
	return 0;
}

static void imx294_set_framing_limits(struct imx294 *imx294)
{
	const struct imx294_mode *mode = imx294->mode;
	u64 def_hblank, hblank_min, hblank_max;
	unsigned int eff_width, eff_height, htrim_s, htrim_e;
	u64 pixel_rate;

	imx294->limits_inck = imx294_inck_hz(imx294);

	imx294->VMAX = mode->default_VMAX;
	imx294->HMAX = mode->default_HMAX;

	eff_width  = imx294_htrim_calc_at(mode, imx294->htrim_width,
					  imx294->crop_left,
					  &htrim_s, &htrim_e);
	eff_height = imx294_vtrim_calc(mode, imx294->vtrim_height);

	pixel_rate = IMX294_PIXEL_RATE;
	DEBUG_PRINTK("Pixel Rate : %lld\n",pixel_rate);

	def_hblank  = imx294_ll_from_hmax(imx294, mode, mode->default_HMAX) - eff_width;
	hblank_min = imx294_ll_from_hmax(imx294, mode,
					 imx294_min_hmax_calc(mode,
							      htrim_e - htrim_s));
	hblank_min = (hblank_min > eff_width) ? hblank_min - eff_width : 0;

	imx294->limits_hblank_min = hblank_min;
	hblank_max = imx294_ll_from_hmax(imx294, mode, IMX294_HMAX_MAX) - eff_width;

	if (def_hblank < hblank_min)
		def_hblank = hblank_min;
	if (def_hblank > hblank_max)
		def_hblank = hblank_max;

	__v4l2_ctrl_modify_range(imx294->hblank, hblank_min,
				 hblank_max, 1, def_hblank);

	__v4l2_ctrl_s_ctrl(imx294->hblank, def_hblank);

	__v4l2_ctrl_modify_range(imx294->vblank,
				 imx294_min_vmax_calc(mode, eff_height) *
					mode->VMAX_scale - eff_height,
				 IMX294_VMAX_MAX*mode->VMAX_scale - eff_height,
				 1, mode->default_VMAX*mode->VMAX_scale - eff_height);
	__v4l2_ctrl_s_ctrl(imx294->vblank,
			   mode->default_VMAX*mode->VMAX_scale - eff_height);

	__v4l2_ctrl_modify_range(imx294->pixel_rate, pixel_rate, pixel_rate, 1,
				 pixel_rate);

	DEBUG_PRINTK("Setting default HBLANK : %lld, VBLANK : %lld with PixelRate: %lld\n",def_hblank,mode->default_VMAX*mode->VMAX_scale - mode->height, pixel_rate);

}

static bool imx294_limits_stale(struct imx294 *imx294)
{
	const struct imx294_mode *mode = imx294->mode;
	unsigned int eff_w, hs, he;
	u64 ll;

	if (!mode)
		return false;
	if (imx294->limits_inck != imx294_inck_hz(imx294))
		return true;

	eff_w = imx294_htrim_calc_at(mode, imx294->htrim_width,
				     imx294->crop_left, &hs, &he);
	ll = imx294_ll_from_hmax(imx294, mode,
				 imx294_min_hmax_calc(mode, he - hs));
	ll = (ll > eff_w) ? ll - eff_w : 0;

	return ll != imx294->limits_hblank_min;
}

static const struct imx294_mode *
imx294_find_mode(const struct imx294_mode *list, unsigned int num,
		 bool want_binning, unsigned int w, unsigned int h)
{
	const struct imx294_mode *best = NULL;
	unsigned int best_err = UINT_MAX;
	unsigned int pass, i;

	for (pass = 0; pass < 2 && !best; pass++) {
		for (i = 0; i < num; i++) {
			const struct imx294_mode *m = &list[i];
			unsigned int err;

			if (pass == 0 && (m->hbin > 1) != want_binning)
				continue;

			err = abs_diff(m->width, w) + abs_diff(m->height, h);
			if (err <= best_err) {
				best_err = err;
				best = m;
			}
		}
	}

	return best;
}

static int imx294_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx294_mode *mode;
	struct imx294 *imx294 = to_imx294(sd);
	unsigned int req_width, crop_width, htrim_s, htrim_e;
	unsigned int req_height, crop_height;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx294->mutex);

	if (fmt->pad == IMAGE_PAD) {
		const struct imx294_mode *mode_list;
		unsigned int num_modes;

		fmt->format.code = imx294_get_format_code(imx294,
							  fmt->format.code);

		get_mode_table(fmt->format.code, &mode_list, &num_modes);

		req_width = fmt->format.width;
		req_height = fmt->format.height;

		mode = imx294_find_mode(mode_list, num_modes,
					imx294->binning,
					fmt->format.width,
					fmt->format.height);
		if (!mode) {
			mutex_unlock(&imx294->mutex);
			return -EINVAL;
		}

		crop_width = imx294_htrim_calc_at(mode, req_width,
						  imx294->crop_left,
						  &htrim_s, &htrim_e);
		crop_height = imx294_vtrim_calc(mode, req_height);

		imx294_update_image_pad_format(imx294, mode, fmt);
		fmt->format.width = crop_width;
		fmt->format.height = crop_height;

		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			bool geom_changed =
				(imx294->htrim_width  != req_width ||
				 imx294->vtrim_height != req_height ||
				 imx294_limits_stale(imx294));

			imx294->htrim_width = req_width;
			imx294->vtrim_height = req_height;

			imx294->sel_compose.left   = 0;
			imx294->sel_compose.top    = 0;
			imx294->sel_compose.width  = req_width;
			imx294->sel_compose.height = req_height;

			if (imx294->mode != mode) {
				imx294->mode = mode;
				imx294->fmt_code = fmt->format.code;
				geom_changed = true;
			}
			if (geom_changed)
				imx294_set_framing_limits(imx294);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx294_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx294->mutex);

	return 0;
}
static const struct v4l2_rect *
__imx294_get_try_crop(struct imx294 *imx294,
		      struct v4l2_subdev_state *sd_state, unsigned int pad)
{
	return v4l2_subdev_state_get_crop(sd_state, pad);
}

static int imx294_apply_htrimming(struct imx294 *imx294)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	const struct imx294_mode *mode = imx294->mode;
	unsigned int start, end, w;
	int ret;

	w = imx294_htrim_calc_at(mode, imx294->htrim_width, imx294->crop_left,
				 &start, &end);
	if (!end)
		return 0;

	ret = imx294_write_reg_2byte(imx294, IMX294_REG_HTRIMMING_START, start);
	if (!ret)
		ret = imx294_write_reg_2byte(imx294, IMX294_REG_HTRIMMING_END,
					     end);
	if (!ret)
		ret = imx294_write_reg_1byte(imx294, IMX294_REG_HTRIMMING_EN, 1);
	if (ret) {
		dev_err(&client->dev, "failed to program HTRIMMING\n");
		return ret;
	}

	dev_info(&client->dev,
		 "HTRIMMING: START=%u END=%u (%u columns) -> %u px emitted, binning %ux%u (mode native %u, HNUM %u)\n",
		 start, end, end - start, w,
		 mode->hbin ? : 1, mode->vbin ? : 1, mode->width, mode->hnum);
	return 0;
}

static int imx294_start_streaming(struct imx294 *imx294)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	const struct IMX294_reg_list *reg_list;
	int ret;

	if (!imx294->common_regs_written) {
		ret = imx294_write_regs(imx294, mode_common_regs,
					ARRAY_SIZE(mode_common_regs));
		if (ret) {
			dev_err(&client->dev, "%s failed to set common settings\n",
				__func__);
			return ret;
		}
		imx294->common_regs_written = true;
	}

	if (imx294_limits_stale(imx294)) {
		s32 hb = imx294->hblank->val;
		s32 vb = imx294->vblank->val;

		dev_info(&client->dev,
			 "framing limits stale (inck %u -> %u Hz, hblank_min %llu): recomputing\n",
			 imx294->limits_inck, imx294_inck_hz(imx294),
			 imx294->limits_hblank_min);
		imx294_set_framing_limits(imx294);

		__v4l2_ctrl_s_ctrl(imx294->hblank,
				   clamp_t(s32, hb, imx294->hblank->minimum,
					   imx294->hblank->maximum));
		__v4l2_ctrl_s_ctrl(imx294->vblank,
				   clamp_t(s32, vb, imx294->vblank->minimum,
					   imx294->vblank->maximum));
	}

	reg_list = &imx294->mode->reg_list;
	ret = imx294_write_regs(imx294, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	ret = imx294_apply_htrimming(imx294);
	if (ret)
		return ret;

	ret =  __v4l2_ctrl_handler_setup(imx294->sd.ctrl_handler);
	if (ret)
		return ret;

	return ret;
}

static void imx294_stop_streaming(struct imx294 *imx294)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	int ret;

	ret = imx294_write_reg_1byte(imx294, IMX294_REG_MODE_SELECT, IMX294_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);
}

static int imx294_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx294 *imx294 = to_imx294(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx294->mutex);
	if (imx294->streaming == enable) {
		mutex_unlock(&imx294->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = imx294_start_streaming(imx294);
		if (ret)
			goto err_rpm_put;
	} else {
		imx294_stop_streaming(imx294);
		pm_runtime_put(&client->dev);
	}

	imx294->streaming = enable;
	mutex_unlock(&imx294->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx294->mutex);

	return ret;
}

static int imx294_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx294 *imx294 = to_imx294(sd);
	int ret;

	ret = regulator_bulk_enable(imx294_NUM_SUPPLIES,
				    imx294->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx294->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx294->reset_gpio, 1);
	usleep_range(imx294_XCLR_MIN_DELAY_US,
		     imx294_XCLR_MIN_DELAY_US + imx294_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(imx294_NUM_SUPPLIES, imx294->supplies);
	return ret;
}

static int imx294_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx294 *imx294 = to_imx294(sd);

	gpiod_set_value_cansleep(imx294->reset_gpio, 0);
	regulator_bulk_disable(imx294_NUM_SUPPLIES, imx294->supplies);
	clk_disable_unprepare(imx294->xclk);

	imx294->common_regs_written = false;

	return 0;
}

static int __maybe_unused imx294_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx294 *imx294 = to_imx294(sd);

	if (imx294->streaming)
		imx294_stop_streaming(imx294);

	return 0;
}

static int __maybe_unused imx294_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx294 *imx294 = to_imx294(sd);
	int ret;

	if (imx294->streaming) {
		ret = imx294_start_streaming(imx294);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx294_stop_streaming(imx294);
	imx294->streaming = 0;
	return ret;
}

static int imx294_get_regulators(struct imx294 *imx294)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	unsigned int i;

	for (i = 0; i < imx294_NUM_SUPPLIES; i++)
		imx294->supplies[i].supply = imx294_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       imx294_NUM_SUPPLIES,
				       imx294->supplies);
}

static int imx294_identify_module(struct imx294 *imx294, u32 expected_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	int ret;
	u32 val;

	ret = imx294_read_reg(imx294, IMX294_REG_CHIP_ID,
			      1, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x, with error %d\n",
			expected_id, ret);
		return ret;
	}

	dev_info(&client->dev, "Device found\n");

	return 0;
}

static bool imx294_ratio_is_binning(const struct v4l2_rect *crop,
				    const struct v4l2_rect *compose)
{
	if (!crop->width || !crop->height || !compose->width || !compose->height)
		return false;

	return compose->width * 2 <= crop->width &&
	       compose->height * 2 <= crop->height;
}

static const struct imx294_mode *imx294_reselect_mode(struct imx294 *imx294)
{
	const struct imx294_mode *mode_list, *mode;
	unsigned int num_modes;

	get_mode_table(imx294->fmt_code, &mode_list, &num_modes);
	if (!mode_list || !num_modes)
		return NULL;

	imx294->binning = imx294_ratio_is_binning(&imx294->sel_crop,
						  &imx294->sel_compose);

	if (!imx294->sel_compose.width || !imx294->sel_compose.height)
		return imx294->mode;

	mode = imx294_find_mode(mode_list, num_modes, imx294->binning,
				imx294->sel_compose.width,
				imx294->sel_compose.height);
	return mode;
}

static int imx294_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx294 *imx294 = to_imx294(sd);
	const struct imx294_mode *mode;
	unsigned int hs, he, w, left, full, phys_lines;
	unsigned int h = 0;
	bool active = sel->which == V4L2_SUBDEV_FORMAT_ACTIVE;

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&imx294->mutex);
	mode = imx294->mode;

	if (!mode || !mode->hnum || mode->hnum <= IMX294_HTRIM_BASE) {
		mutex_unlock(&imx294->mutex);
		return -EINVAL;
	}

	if (sel->target == V4L2_SEL_TGT_CROP) {
		w    = sel->r.width;
		left = sel->r.left;
		if (w > IMX294_HNUM_MAX - IMX294_HTRIM_BASE)
			w = IMX294_HNUM_MAX - IMX294_HTRIM_BASE;
		if (left > IMX294_HNUM_MAX - IMX294_HTRIM_BASE)
			left = 0;

		if (active) {
			imx294->sel_crop.left   = left;
			imx294->sel_crop.top    = 0;
			imx294->sel_crop.width  = w;
			imx294->sel_crop.height = sel->r.height;

			mode = imx294_reselect_mode(imx294) ? : mode;
			imx294->mode = mode;
		}

		full = mode->hnum - IMX294_HTRIM_BASE;
		if (!w || w > full)
			w = full;
		if (left >= full)
			left = 0;

		w = imx294_htrim_calc_at(mode, w / (mode->hbin ? : 1), left,
					 &hs, &he);

		if (active) {
			imx294->crop_left   = hs - IMX294_HTRIM_BASE;
			imx294->htrim_width = w;
			imx294_set_framing_limits(imx294);
		}
	} else {
		w = sel->r.width;
		h = sel->r.height;

		if (active) {
			imx294->sel_compose.left   = 0;
			imx294->sel_compose.top    = 0;
			imx294->sel_compose.width  = w;
			imx294->sel_compose.height = h;

			mode = imx294_reselect_mode(imx294) ? : mode;
			imx294->mode = mode;
		}

		w = imx294_htrim_calc_at(mode, w,
					 active ? imx294->crop_left : UINT_MAX,
					 &hs, &he);
		h = imx294_vtrim_calc(mode, h);

		if (active) {
			imx294->htrim_width  = w;
			imx294->vtrim_height = h;
			imx294_set_framing_limits(imx294);
		}
	}

	phys_lines = mode->height * (mode->vbin ? : 1);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		sel->r.left   = hs - IMX294_HTRIM_BASE;
		sel->r.top    = 0;
		sel->r.width  = he - hs;
		sel->r.height = phys_lines;
	} else {
		sel->r.left   = 0;
		sel->r.top    = 0;
		sel->r.width  = w;
		sel->r.height = h;
	}

	mutex_unlock(&imx294->mutex);

	DEBUG_PRINTK("set_selection: %s -> %u,%u/%ux%u (HTRIMMING %u..%u, bin %ux%u)\n",
		     sel->target == V4L2_SEL_TGT_CROP ? "crop" : "compose",
		     sel->r.left, sel->r.top, sel->r.width, sel->r.height,
		     hs, he, mode->hbin ? : 1, mode->vbin ? : 1);
	return 0;
}

static int imx294_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		struct imx294 *imx294 = to_imx294(sd);
		unsigned int hs, he;

		mutex_lock(&imx294->mutex);

		if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
			sel->r = *__imx294_get_try_crop(imx294, sd_state,
							sel->pad);
			mutex_unlock(&imx294->mutex);
			return 0;
		}

		imx294_htrim_calc_at(imx294->mode, imx294->htrim_width,
				     imx294->crop_left, &hs, &he);

		sel->r.left   = hs - IMX294_HTRIM_BASE;
		sel->r.top    = 0;
		sel->r.width  = he - hs;
		sel->r.height = imx294->mode->height *
				(imx294->mode->vbin ? : 1);

		mutex_unlock(&imx294->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX294_NATIVE_WIDTH;
		sel->r.height = IMX294_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX294_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX294_PIXEL_ARRAY_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_COMPOSE: {
		struct imx294 *imx294 = to_imx294(sd);

		mutex_lock(&imx294->mutex);
		sel->r.left   = 0;
		sel->r.top    = 0;
		sel->r.width  = imx294->htrim_width ? :
				imx294->mode->width;
		sel->r.height = imx294_vtrim_calc(imx294->mode,
						  imx294->vtrim_height);
		mutex_unlock(&imx294->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS: {
		struct imx294 *imx294 = to_imx294(sd);

		mutex_lock(&imx294->mutex);
		sel->r.left   = 0;
		sel->r.top    = 0;
		sel->r.width  = imx294->mode->width;
		sel->r.height = imx294->mode->height;
		mutex_unlock(&imx294->mutex);

		return 0;
	}
	}

	return -EINVAL;
}

static const struct v4l2_subdev_core_ops imx294_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx294_video_ops = {
	.s_stream = imx294_set_stream,
};

static const struct v4l2_subdev_pad_ops imx294_pad_ops = {
	.enum_mbus_code = imx294_enum_mbus_code,
	.get_fmt = imx294_get_pad_format,
	.set_fmt = imx294_set_pad_format,
	.get_selection = imx294_get_selection,
	.set_selection = imx294_set_selection,
	.enum_frame_size = imx294_enum_frame_size,
};

static const struct v4l2_subdev_ops imx294_subdev_ops = {
	.core = &imx294_core_ops,
	.video = &imx294_video_ops,
	.pad = &imx294_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx294_internal_ops = {
	.open = imx294_open,
};

static int imx294_init_controls(struct imx294 *imx294)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx294->sd);
	struct v4l2_fwnode_device_properties props;
	int ret;

	ctrl_hdlr = &imx294->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 17);
	if (ret)
		return ret;

	mutex_init(&imx294->mutex);
	ctrl_hdlr->lock = &imx294->mutex;

	imx294->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx294_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       0xffff,
					       0xffff, 1,
					       0xffff);
	imx294->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx294_ctrl_ops,
					   V4L2_CID_VBLANK, 0, 0xfffff, 1, 0);
	imx294->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx294_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	imx294->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx294_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX294_EXPOSURE_MIN,
					     IMX294_EXPOSURE_MAX,
					     IMX294_EXPOSURE_STEP,
					     IMX294_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx294_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX294_ANA_GAIN_MIN, IMX294_ANA_GAIN_MAX,
			  IMX294_ANA_GAIN_STEP, IMX294_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx294_ctrl_ops, V4L2_CID_BLACK_LEVEL,
			  IMX294_BLACK_LEVEL_MIN, IMX294_BLACK_LEVEL_MAX,
			  IMX294_BLACK_LEVEL_STEP, IMX294_BLACK_LEVEL_DEFAULT);

	if (imx294->link_freq_idx >= 0) {
		imx294->link_freq =
			v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx294_ctrl_ops,
					       V4L2_CID_LINK_FREQ,
					       ARRAY_SIZE(imx294_link_freqs) - 1,
					       imx294->link_freq_idx,
					       imx294_link_freqs);
		if (imx294->link_freq)
			imx294->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
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

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx294_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx294->sd.ctrl_handler = ctrl_hdlr;

	imx294_set_framing_limits(imx294);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx294->mutex);

	return ret;
}

static void imx294_free_controls(struct imx294 *imx294)
{
	v4l2_ctrl_handler_free(imx294->sd.ctrl_handler);
	mutex_destroy(&imx294->mutex);
}

static const struct imx294_compatible_data imx294_compatible = {
	.chip_id = IMX294_CHIP_ID,
	.extra_regs = {
		.num_of_regs = 0,
		.regs = NULL
	}
};

static const struct of_device_id imx294_dt_ids[] = {
	{ .compatible = "sony,imx294", .data = &imx294_compatible },
	{  }
};

static int imx294_check_hwcfg(struct device *dev, struct imx294 *imx294)
{
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *endpoint;
	unsigned int i;
	int ret = 0;

	imx294->link_freq_idx = -1;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_warn(dev, "no endpoint node; using the default link settings\n");
		return 0;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_warn(dev, "could not parse endpoint; using the default link settings\n");
		goto out;
	}

	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(dev, "only 4 data lanes supported, got %d\n",
			ep_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto out_free;
	}

	if (!ep_cfg.nr_of_link_frequencies) {
		dev_warn(dev, "no link-frequencies in DT; using the default link settings\n");
		goto out_free;
	}

	for (i = 0; i < ARRAY_SIZE(imx294_link_freqs); i++) {
		if (imx294_link_freqs[i] == ep_cfg.link_frequencies[0]) {
			imx294->link_freq_idx = i;
			dev_info(dev, "link-frequency %lld Hz (%llu Mbps/lane), PLRD1 %u\n",
				 imx294_link_freqs[i],
				 div_u64((u64)imx294_link_freqs[i], 500000u),
				 IMX294_PLRD1_FROM_LINK_FREQ(imx294_link_freqs[i]));
			goto out_free;
		}
	}

	dev_warn(dev,
		 "link-frequency %lld Hz not supported (max %lld), using the default link settings\n",
		 ep_cfg.link_frequencies[0],
		 imx294_link_freqs[ARRAY_SIZE(imx294_link_freqs) - 1]);

out_free:
	v4l2_fwnode_endpoint_free(&ep_cfg);
out:
	fwnode_handle_put(endpoint);
	return ret;
}

static int imx294_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx294 *imx294;
	const struct of_device_id *match;
	int ret;

	imx294 = devm_kzalloc(&client->dev, sizeof(*imx294), GFP_KERNEL);
	if (!imx294)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx294->sd, client, &imx294_subdev_ops);

	ret = imx294_check_hwcfg(dev, imx294);
	if (ret)
		return ret;

	match = of_match_device(imx294_dt_ids, dev);
	if (!match)
		return -ENODEV;
	imx294->compatible_data =
		(const struct imx294_compatible_data *)match->data;

	imx294->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx294->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx294->xclk);
	}

	imx294->xclk_freq = clk_get_rate(imx294->xclk);
	if (imx294->xclk_freq != IMX294_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx294->xclk_freq);
		return -EINVAL;
	}

	ret = imx294_get_regulators(imx294);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	imx294->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	
	ret = imx294_power_on(dev);
	if (ret)
		return ret;

	ret = imx294_identify_module(imx294, imx294->compatible_data->chip_id);
	if (ret)
		goto error_power_off;

	imx294_set_default_format(imx294);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = imx294_init_controls(imx294);
	if (ret)
		goto error_power_off;

	imx294->sd.internal_ops = &imx294_internal_ops;
	imx294->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx294->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx294->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx294->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx294->sd.entity, NUM_PADS, imx294->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx294->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx294->sd.entity);

error_handler_free:
	imx294_free_controls(imx294);

error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	imx294_power_off(&client->dev);

	return ret;
}

static void imx294_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx294 *imx294 = to_imx294(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx294_free_controls(imx294);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx294_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

}

MODULE_DEVICE_TABLE(of, imx294_dt_ids);

static const struct dev_pm_ops imx294_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx294_suspend, imx294_resume)
	SET_RUNTIME_PM_OPS(imx294_power_off, imx294_power_on, NULL)
};

static struct i2c_driver imx294_i2c_driver = {
	.driver = {
		.name = "imx294",
		.of_match_table	= imx294_dt_ids,
		.pm = &imx294_pm_ops,
	},
	.probe = imx294_probe,
	.remove = imx294_remove,
};

module_i2c_driver(imx294_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("Sony IMX294 sensor driver");
MODULE_LICENSE("GPL");
