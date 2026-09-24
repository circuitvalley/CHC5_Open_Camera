// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony IMX477 sensor driver
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

static int dpc_enable = 1;
module_param(dpc_enable, int, 0644);
MODULE_PARM_DESC(dpc_enable, "Enable on-sensor DPC");

static int trigger_mode;
module_param(trigger_mode, int, 0644);
MODULE_PARM_DESC(trigger_mode, "Set vsync trigger mode: 1=source, 2=sink");

#define IMX477_REG_VALUE_08BIT		1
#define IMX477_REG_VALUE_16BIT		2

#define IMX477_REG_CHIP_ID		0x0016
#define IMX477_CHIP_ID			0x0477
#define IMX378_CHIP_ID			0x0378

#define IMX477_REG_MODE_SELECT		0x0100
#define IMX477_MODE_STANDBY		0x00
#define IMX477_MODE_STREAMING		0x01

#define IMX477_REG_ORIENTATION		0x101

#define IMX477_XCLK_FREQ		24000000

#define IMX477_DEFAULT_LINK_FREQ	624000000

#define IMX477_PIXEL_RATE		840000000

#define IMX477_REG_FRAME_LENGTH		0x0340
#define IMX477_FRAME_LENGTH_MAX		0xffdc

#define IMX477_REG_LINE_LENGTH		0x0342
#define IMX477_LINE_LENGTH_MAX		0xfff0

#define IMX477_LONG_EXP_SHIFT_MAX	7
#define IMX477_LONG_EXP_SHIFT_REG	0x3100

#define IMX477_REG_EXPOSURE		0x0202
#define IMX477_EXPOSURE_OFFSET		22
#define IMX477_EXPOSURE_MIN		4
#define IMX477_EXPOSURE_STEP		1
#define IMX477_EXPOSURE_DEFAULT		0x640
#define IMX477_EXPOSURE_MAX		(IMX477_FRAME_LENGTH_MAX - \
					 IMX477_EXPOSURE_OFFSET)

#define IMX477_VBLANK_MIN		45

#define IMX477_REG_ANALOG_GAIN		0x0204

#define IMX477_ANA_GAIN_PGA_MAX		270
#define IMX477_ANA_GAIN_MIN		0
#define IMX477_ANA_GAIN_MAX		750
#define IMX477_ANA_GAIN_STEP		1
#define IMX477_ANA_GAIN_DEFAULT		0

static const u16 imx477_pga_code[IMX477_ANA_GAIN_PGA_MAX + 1] = {
	   0,   12,   23,   35,   46,   57,   68,   79,
	  90,  101,  111,  122,  132,  142,  152,  162,
	 172,  182,  192,  201,  211,  220,  229,  238,
	 247,  256,  265,  274,  282,  291,  299,  307,
	 316,  324,  332,  340,  347,  355,  363,  370,
	 378,  385,  393,  400,  407,  414,  421,  428,
	 435,  441,  448,  455,  461,  468,  474,  480,
	 487,  493,  499,  505,  511,  517,  522,  528,
	 534,  539,  545,  551,  556,  561,  567,  572,
	 577,  582,  587,  592,  597,  602,  607,  612,
	 616,  621,  626,  630,  635,  639,  644,  648,
	 652,  656,  661,  665,  669,  673,  677,  681,
	 685,  689,  693,  696,  700,  704,  708,  711,
	 715,  718,  722,  725,  729,  732,  735,  739,
	 742,  745,  748,  752,  755,  758,  761,  764,
	 767,  770,  773,  776,  778,  781,  784,  787,
	 789,  792,  795,  797,  800,  803,  805,  808,
	 810,  813,  815,  817,  820,  822,  824,  827,
	 829,  831,  833,  836,  838,  840,  842,  844,
	 846,  848,  850,  852,  854,  856,  858,  860,
	 862,  864,  865,  867,  869,  871,  873,  874,
	 876,  878,  879,  881,  883,  884,  886,  887,
	 889,  891,  892,  894,  895,  897,  898,  899,
	 901,  902,  904,  905,  906,  908,  909,  910,
	 912,  913,  914,  916,  917,  918,  919,  920,
	 922,  923,  924,  925,  926,  927,  928,  930,
	 931,  932,  933,  934,  935,  936,  937,  938,
	 939,  940,  941,  942,  943,  944,  945,  945,
	 946,  947,  948,  949,  950,  951,  952,  952,
	 953,  954,  955,  956,  956,  957,  958,  959,
	 959,  960,  961,  962,  962,  963,  964,  964,
	 965,  966,  966,  967,  968,  968,  969,  970,
	 970,  971,  971,  972,  973,  973,  974,  974,
	 975,  976,  976,  977,  977,  978,  978,
};

static unsigned int imx477_ana_gain_code(unsigned int db10)
{
	if (db10 > IMX477_ANA_GAIN_PGA_MAX)
		db10 = IMX477_ANA_GAIN_PGA_MAX;

	return imx477_pga_code[db10];
}

#define IMX477_REG_DIGITAL_GAIN		0x020e
#define IMX477_DGTL_GAIN_MIN		0x0100
#define IMX477_DGTL_GAIN_MAX		0xffff
#define IMX477_DGTL_GAIN_DEFAULT	0x0100
#define IMX477_DGTL_GAIN_STEP		1

#define IMX477_DGTL_GAIN_DB10_MAX	(IMX477_ANA_GAIN_MAX - \
					 IMX477_ANA_GAIN_PGA_MAX)

static const u16 imx477_dgtl_gain_g[IMX477_DGTL_GAIN_DB10_MAX + 1] = {
	  256,   259,   262,   265,   268,   271,   274,   277,
	  281,   284,   287,   291,   294,   297,   301,   304,
	  308,   311,   315,   319,   322,   326,   330,   334,
	  337,   341,   345,   349,   353,   357,   362,   366,
	  370,   374,   379,   383,   387,   392,   396,   401,
	  406,   410,   415,   420,   425,   430,   435,   440,
	  445,   450,   455,   461,   466,   471,   477,   482,
	  488,   493,   499,   505,   511,   517,   523,   529,
	  535,   541,   547,   554,   560,   567,   573,   580,
	  586,   593,   600,   607,   614,   621,   628,   636,
	  643,   650,   658,   666,   673,   681,   689,   697,
	  705,   713,   722,   730,   738,   747,   756,   764,
	  773,   782,   791,   800,   810,   819,   828,   838,
	  848,   858,   867,   877,   888,   898,   908,   919,
	  929,   940,   951,   962,   973,   985,   996,  1007,
	 1019,  1031,  1043,  1055,  1067,  1080,  1092,  1105,
	 1117,  1130,  1144,  1157,  1170,  1184,  1197,  1211,
	 1225,  1239,  1254,  1268,  1283,  1298,  1313,  1328,
	 1344,  1359,  1375,  1391,  1407,  1423,  1440,  1456,
	 1473,  1490,  1507,  1525,  1543,  1560,  1578,  1597,
	 1615,  1634,  1653,  1672,  1691,  1711,  1731,  1751,
	 1771,  1792,  1812,  1833,  1855,  1876,  1898,  1920,
	 1942,  1964,  1987,  2010,  2033,  2057,  2081,  2105,
	 2129,  2154,  2179,  2204,  2230,  2255,  2282,  2308,
	 2335,  2362,  2389,  2417,  2445,  2473,  2502,  2531,
	 2560,  2590,  2620,  2650,  2681,  2712,  2743,  2775,
	 2807,  2839,  2872,  2906,  2939,  2973,  3008,  3043,
	 3078,  3113,  3149,  3186,  3223,  3260,  3298,  3336,
	 3375,  3414,  3453,  3493,  3534,  3575,  3616,  3658,
	 3700,  3743,  3787,  3830,  3875,  3920,  3965,  4011,
	 4057,  4104,  4152,  4200,  4249,  4298,  4348,  4398,
	 4449,  4500,  4552,  4605,  4658,  4712,  4767,  4822,
	 4878,  4934,  4992,  5049,  5108,  5167,  5227,  5287,
	 5349,  5411,  5473,  5537,  5601,  5666,  5731,  5797,
	 5865,  5933,  6001,  6071,  6141,  6212,  6284,  6357,
	 6430,  6505,  6580,  6656,  6733,  6811,  6890,  6970,
	 7051,  7132,  7215,  7299,  7383,  7469,  7555,  7643,
	 7731,  7821,  7911,  8003,  8095,  8189,  8284,  8380,
	 8477,  8575,  8674,  8775,  8876,  8979,  9083,  9188,
	 9295,  9402,  9511,  9621,  9733,  9846,  9960, 10075,
	10192, 10310, 10429, 10550, 10672, 10795, 10920, 11047,
	11175, 11304, 11435, 11568, 11701, 11837, 11974, 12113,
	12253, 12395, 12538, 12684, 12830, 12979, 13129, 13281,
	13435, 13591, 13748, 13907, 14068, 14231, 14396, 14563,
	14731, 14902, 15074, 15249, 15426, 15604, 15785, 15968,
	16153, 16340, 16529, 16720, 16914, 17110, 17308, 17508,
	17711, 17916, 18123, 18333, 18546, 18760, 18978, 19197,
	19420, 19644, 19872, 20102, 20335, 20570, 20808, 21049,
	21293, 21540, 21789, 22041, 22297, 22555, 22816, 23080,
	23347, 23618, 23891, 24168, 24448, 24731, 25017, 25307,
	25600, 25896, 26196, 26500, 26806, 27117, 27431, 27749,
	28070, 28395, 28724, 29056, 29393, 29733, 30077, 30426,
	30778, 31134, 31495, 31860, 32228, 32602, 32979, 33361,
	33747, 34138, 34533, 34933, 35338, 35747, 36161, 36580,
	37003, 37432, 37865, 38304, 38747, 39196, 39650, 40109,
	40573, 41043, 41518, 41999, 42485, 42977, 43475, 43978,
	44488, 45003, 45524, 46051, 46584, 47124, 47669, 48221,
	48780, 49345, 49916, 50494, 51079, 51670, 52268, 52874,
	53486, 54105, 54732, 55366, 56007, 56655, 57311, 57975,
	58646, 59325, 60012, 60707, 61410, 62121, 62841, 63568,
	64304,
};

static unsigned int imx477_dgtl_gain_code(unsigned int db10)
{
	if (db10 > IMX477_DGTL_GAIN_DB10_MAX)
		db10 = IMX477_DGTL_GAIN_DB10_MAX;

	return imx477_dgtl_gain_g[db10];
}

#define IMX477_REG_PEDESTAL_EN		0x3030
#define IMX477_REG_PEDESTAL_VALUE	0x3032
#define IMX477_BLACK_LEVEL_MIN		0
#define IMX477_BLACK_LEVEL_MAX		0x0fff
#define IMX477_BLACK_LEVEL_DEFAULT	0x0100
#define IMX477_BLACK_LEVEL_STEP		1

#define IMX477_REG_TEST_PATTERN		0x0600
#define IMX477_TEST_PATTERN_DISABLE	0
#define IMX477_TEST_PATTERN_SOLID_COLOR	1
#define IMX477_TEST_PATTERN_COLOR_BARS	2
#define IMX477_TEST_PATTERN_GREY_COLOR	3
#define IMX477_TEST_PATTERN_PN9		4

#define IMX477_REG_TEST_PATTERN_R	0x0602
#define IMX477_REG_TEST_PATTERN_GR	0x0604
#define IMX477_REG_TEST_PATTERN_B	0x0606
#define IMX477_REG_TEST_PATTERN_GB	0x0608
#define IMX477_TEST_PATTERN_COLOUR_MIN	0
#define IMX477_TEST_PATTERN_COLOUR_MAX	0x0fff
#define IMX477_TEST_PATTERN_COLOUR_STEP	1
#define IMX477_TEST_PATTERN_R_DEFAULT	IMX477_TEST_PATTERN_COLOUR_MAX
#define IMX477_TEST_PATTERN_GR_DEFAULT	0
#define IMX477_TEST_PATTERN_B_DEFAULT	0
#define IMX477_TEST_PATTERN_GB_DEFAULT	0

#define IMX477_REG_MC_MODE		0x3f0b
#define IMX477_REG_MS_SEL		0x3041
#define IMX477_REG_XVS_IO_CTRL		0x3040
#define IMX477_REG_EXTOUT_EN		0x4b81

#define IMX477_EMBEDDED_LINE_WIDTH 16384
#define IMX477_NUM_EMBEDDED_LINES 1

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

#define IMX477_NATIVE_WIDTH		4072U
#define IMX477_NATIVE_HEIGHT		3176U
#define IMX477_PIXEL_ARRAY_LEFT		8U
#define IMX477_PIXEL_ARRAY_TOP		16U
#define IMX477_PIXEL_ARRAY_WIDTH	4056U
#define IMX477_PIXEL_ARRAY_HEIGHT	3040U

#define IMX477_MIN_WIDTH		64U
#define IMX477_MIN_HEIGHT		64U
#define IMX477_WIDTH_STEP		8U
#define IMX477_HEIGHT_STEP		2U
#define IMX477_BIN_HEIGHT_STEP		8U

#define IMX477_REG_X_ADD_STA		0x0344
#define IMX477_REG_Y_ADD_STA		0x0346
#define IMX477_REG_X_ADD_END		0x0348
#define IMX477_REG_Y_ADD_END		0x034a
#define IMX477_REG_X_OUT_SIZE		0x034c
#define IMX477_REG_Y_OUT_SIZE		0x034e
#define IMX477_REG_DIG_CROP_X_OFF	0x0408
#define IMX477_REG_DIG_CROP_Y_OFF	0x040a
#define IMX477_REG_DIG_CROP_WIDTH	0x040c
#define IMX477_REG_DIG_CROP_HEIGHT	0x040e
#define IMX477_REG_BINNING_MODE		0x0900
#define IMX477_REG_BINNING_TYPE		0x0901
#define IMX477_REG_SCALE_M		0x0405
#define IMX477_REG_GRP_PARAM_HOLD	0x0104

struct imx477_reg {
	u16 address;
	u8 val;
};

struct imx477_reg_list {
	unsigned int num_of_regs;
	const struct imx477_reg *regs;
};

enum imx477_base_mode {
	IMX477_BASE_MODE_FULL,
	IMX477_BASE_MODE_2X2_BIN,
};

struct imx477_base_config {
	enum imx477_base_mode id;
	unsigned int line_length_pix;
	bool binning;
	unsigned int max_width;
	unsigned int max_height;
	unsigned int height_step;
	unsigned int sensor_width;
	unsigned int sensor_height;
	struct imx477_reg_list reg_list;
};

enum {
	IMX477_LINK_FREQ_624MHZ,
	IMX477_LINK_FREQ_600MHZ,
	IMX477_LINK_FREQ_540MHZ,
	IMX477_LINK_FREQ_450MHZ,
};

static const s64 link_freqs[] = {
	[IMX477_LINK_FREQ_624MHZ] = 624000000,
	[IMX477_LINK_FREQ_600MHZ] = 600000000,
	[IMX477_LINK_FREQ_540MHZ] = 540000000,
	[IMX477_LINK_FREQ_450MHZ] = 450000000,
};

static const struct imx477_reg link_624Mhz_regs[] = {
	{0x030E, 0x00},
	{0x030F, 0x68},
};

static const struct imx477_reg link_600Mhz_regs[] = {
	{0x030E, 0x00},
	{0x030F, 0x64},
};

static const struct imx477_reg link_540Mhz_regs[] = {
	{0x030E, 0x00},
	{0x030F, 0x5A},
};

static const struct imx477_reg link_450Mhz_regs[] = {
	{0x030E, 0x00},
	{0x030F, 0x4B},
};

static const struct imx477_reg_list link_freq_regs[] = {
	[IMX477_LINK_FREQ_624MHZ] = {
		.regs = link_624Mhz_regs,
		.num_of_regs = ARRAY_SIZE(link_624Mhz_regs)
	},
	[IMX477_LINK_FREQ_600MHZ] = {
		.regs = link_600Mhz_regs,
		.num_of_regs = ARRAY_SIZE(link_600Mhz_regs)
	},
	[IMX477_LINK_FREQ_540MHZ] = {
		.regs = link_540Mhz_regs,
		.num_of_regs = ARRAY_SIZE(link_540Mhz_regs)
	},
	[IMX477_LINK_FREQ_450MHZ] = {
		.regs = link_450Mhz_regs,
		.num_of_regs = ARRAY_SIZE(link_450Mhz_regs)
	},
};

static const struct imx477_reg mode_common_regs[] = {
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x0138, 0x01},
	{0xe000, 0x00},
	{0xe07a, 0x01},
	{0x0808, 0x02},
	{0x4ae9, 0x18},
	{0x4aea, 0x08},
	{0xf61c, 0x04},
	{0xf61e, 0x04},
	{0x4ae9, 0x21},
	{0x4aea, 0x80},
	{0x38a8, 0x1f},
	{0x38a9, 0xff},
	{0x38aa, 0x1f},
	{0x38ab, 0xff},
	{0x5078, 0x01},
	{0x55d4, 0x00},
	{0x55d5, 0x00},
	{0x55d6, 0x07},
	{0x55d7, 0xff},
	{0x55e8, 0x07},
	{0x55e9, 0xff},
	{0x55ea, 0x00},
	{0x55eb, 0x00},
	{0x574c, 0x07},
	{0x574d, 0xff},
	{0x574e, 0x00},
	{0x574f, 0x00},
	{0x5754, 0x00},
	{0x5755, 0x00},
	{0x5756, 0x07},
	{0x5757, 0xff},
	{0x5973, 0x04},
	{0x5974, 0x01},
	{0x5d13, 0xc3},
	{0x5d14, 0x58},
	{0x5d15, 0xa3},
	{0x5d16, 0x1d},
	{0x5d17, 0x65},
	{0x5d18, 0x8c},
	{0x5d1a, 0x06},
	{0x5d1b, 0xa9},
	{0x5d1c, 0x45},
	{0x5d1d, 0x3a},
	{0x5d1e, 0xab},
	{0x5d1f, 0x15},
	{0x5d21, 0x0e},
	{0x5d22, 0x52},
	{0x5d23, 0xaa},
	{0x5d24, 0x7d},
	{0x5d25, 0x57},
	{0x5d26, 0xa8},
	{0x5d37, 0x5a},
	{0x5d38, 0x5a},
	{0x5d77, 0x7f},
	{0x7b75, 0x0e},
	{0x7b76, 0x0b},
	{0x7b77, 0x08},
	{0x7b78, 0x0a},
	{0x7b79, 0x47},
	{0x7b7c, 0x00},
	{0x7b7d, 0x00},
	{0x8d1f, 0x00},
	{0x8d27, 0x00},
	{0x9004, 0x03},
	{0x9200, 0x50},
	{0x9201, 0x6c},
	{0x9202, 0x71},
	{0x9203, 0x00},
	{0x9204, 0x71},
	{0x9205, 0x01},
	{0x9371, 0x6a},
	{0x9373, 0x6a},
	{0x9375, 0x64},
	{0x991a, 0x00},
	{0x996b, 0x8c},
	{0x996c, 0x64},
	{0x996d, 0x50},
	{0x9a4c, 0x06},
	{0x9a4d, 0x06},
	{0xa001, 0x0a},
	{0xa003, 0x0a},
	{0xa005, 0x0a},
	{0xa006, 0x01},
	{0xa007, 0xc0},
	{0xa009, 0xc0},
	{0x3d8a, 0x01},
	{0x4421, 0x04},
	{0x7b3b, 0x01},
	{0x7b4c, 0x00},
	{0x9905, 0x00},
	{0x9907, 0x00},
	{0x9909, 0x00},
	{0x990b, 0x00},
	{0x9944, 0x3c},
	{0x9947, 0x3c},
	{0x994a, 0x8c},
	{0x994b, 0x50},
	{0x994c, 0x1b},
	{0x994d, 0x8c},
	{0x994e, 0x50},
	{0x994f, 0x1b},
	{0x9950, 0x8c},
	{0x9951, 0x1b},
	{0x9952, 0x0a},
	{0x9953, 0x8c},
	{0x9954, 0x1b},
	{0x9955, 0x0a},
	{0x9a13, 0x04},
	{0x9a14, 0x04},
	{0x9a19, 0x00},
	{0x9a1c, 0x04},
	{0x9a1d, 0x04},
	{0x9a26, 0x05},
	{0x9a27, 0x05},
	{0x9a2c, 0x01},
	{0x9a2d, 0x03},
	{0x9a2f, 0x05},
	{0x9a30, 0x05},
	{0x9a41, 0x00},
	{0x9a46, 0x00},
	{0x9a47, 0x00},
	{0x9c17, 0x35},
	{0x9c1d, 0x31},
	{0x9c29, 0x50},
	{0x9c3b, 0x2f},
	{0x9c41, 0x6b},
	{0x9c47, 0x2d},
	{0x9c4d, 0x40},
	{0x9c6b, 0x00},
	{0x9c71, 0xc8},
	{0x9c73, 0x32},
	{0x9c75, 0x04},
	{0x9c7d, 0x2d},
	{0x9c83, 0x40},
	{0x9c94, 0x3f},
	{0x9c95, 0x3f},
	{0x9c96, 0x3f},
	{0x9c97, 0x00},
	{0x9c98, 0x00},
	{0x9c99, 0x00},
	{0x9c9a, 0x3f},
	{0x9c9b, 0x3f},
	{0x9c9c, 0x3f},
	{0x9ca0, 0x0f},
	{0x9ca1, 0x0f},
	{0x9ca2, 0x0f},
	{0x9ca3, 0x00},
	{0x9ca4, 0x00},
	{0x9ca5, 0x00},
	{0x9ca6, 0x1e},
	{0x9ca7, 0x1e},
	{0x9ca8, 0x1e},
	{0x9ca9, 0x00},
	{0x9caa, 0x00},
	{0x9cab, 0x00},
	{0x9cac, 0x09},
	{0x9cad, 0x09},
	{0x9cae, 0x09},
	{0x9cbd, 0x50},
	{0x9cbf, 0x50},
	{0x9cc1, 0x50},
	{0x9cc3, 0x40},
	{0x9cc5, 0x40},
	{0x9cc7, 0x40},
	{0x9cc9, 0x0a},
	{0x9ccb, 0x0a},
	{0x9ccd, 0x0a},
	{0x9d17, 0x35},
	{0x9d1d, 0x31},
	{0x9d29, 0x50},
	{0x9d3b, 0x2f},
	{0x9d41, 0x6b},
	{0x9d47, 0x42},
	{0x9d4d, 0x5a},
	{0x9d6b, 0x00},
	{0x9d71, 0xc8},
	{0x9d73, 0x32},
	{0x9d75, 0x04},
	{0x9d7d, 0x42},
	{0x9d83, 0x5a},
	{0x9d94, 0x3f},
	{0x9d95, 0x3f},
	{0x9d96, 0x3f},
	{0x9d97, 0x00},
	{0x9d98, 0x00},
	{0x9d99, 0x00},
	{0x9d9a, 0x3f},
	{0x9d9b, 0x3f},
	{0x9d9c, 0x3f},
	{0x9d9d, 0x1f},
	{0x9d9e, 0x1f},
	{0x9d9f, 0x1f},
	{0x9da0, 0x0f},
	{0x9da1, 0x0f},
	{0x9da2, 0x0f},
	{0x9da3, 0x00},
	{0x9da4, 0x00},
	{0x9da5, 0x00},
	{0x9da6, 0x1e},
	{0x9da7, 0x1e},
	{0x9da8, 0x1e},
	{0x9da9, 0x00},
	{0x9daa, 0x00},
	{0x9dab, 0x00},
	{0x9dac, 0x09},
	{0x9dad, 0x09},
	{0x9dae, 0x09},
	{0x9dc9, 0x0a},
	{0x9dcb, 0x0a},
	{0x9dcd, 0x0a},
	{0x9e17, 0x35},
	{0x9e1d, 0x31},
	{0x9e29, 0x50},
	{0x9e3b, 0x2f},
	{0x9e41, 0x6b},
	{0x9e47, 0x2d},
	{0x9e4d, 0x40},
	{0x9e6b, 0x00},
	{0x9e71, 0xc8},
	{0x9e73, 0x32},
	{0x9e75, 0x04},
	{0x9e94, 0x0f},
	{0x9e95, 0x0f},
	{0x9e96, 0x0f},
	{0x9e97, 0x00},
	{0x9e98, 0x00},
	{0x9e99, 0x00},
	{0x9ea0, 0x0f},
	{0x9ea1, 0x0f},
	{0x9ea2, 0x0f},
	{0x9ea3, 0x00},
	{0x9ea4, 0x00},
	{0x9ea5, 0x00},
	{0x9ea6, 0x3f},
	{0x9ea7, 0x3f},
	{0x9ea8, 0x3f},
	{0x9ea9, 0x00},
	{0x9eaa, 0x00},
	{0x9eab, 0x00},
	{0x9eac, 0x09},
	{0x9ead, 0x09},
	{0x9eae, 0x09},
	{0x9ec9, 0x0a},
	{0x9ecb, 0x0a},
	{0x9ecd, 0x0a},
	{0x9f17, 0x35},
	{0x9f1d, 0x31},
	{0x9f29, 0x50},
	{0x9f3b, 0x2f},
	{0x9f41, 0x6b},
	{0x9f47, 0x42},
	{0x9f4d, 0x5a},
	{0x9f6b, 0x00},
	{0x9f71, 0xc8},
	{0x9f73, 0x32},
	{0x9f75, 0x04},
	{0x9f94, 0x0f},
	{0x9f95, 0x0f},
	{0x9f96, 0x0f},
	{0x9f97, 0x00},
	{0x9f98, 0x00},
	{0x9f99, 0x00},
	{0x9f9a, 0x2f},
	{0x9f9b, 0x2f},
	{0x9f9c, 0x2f},
	{0x9f9d, 0x00},
	{0x9f9e, 0x00},
	{0x9f9f, 0x00},
	{0x9fa0, 0x0f},
	{0x9fa1, 0x0f},
	{0x9fa2, 0x0f},
	{0x9fa3, 0x00},
	{0x9fa4, 0x00},
	{0x9fa5, 0x00},
	{0x9fa6, 0x1e},
	{0x9fa7, 0x1e},
	{0x9fa8, 0x1e},
	{0x9fa9, 0x00},
	{0x9faa, 0x00},
	{0x9fab, 0x00},
	{0x9fac, 0x09},
	{0x9fad, 0x09},
	{0x9fae, 0x09},
	{0x9fc9, 0x0a},
	{0x9fcb, 0x0a},
	{0x9fcd, 0x0a},
	{0xa14b, 0xff},
	{0xa151, 0x0c},
	{0xa153, 0x50},
	{0xa155, 0x02},
	{0xa157, 0x00},
	{0xa1ad, 0xff},
	{0xa1b3, 0x0c},
	{0xa1b5, 0x50},
	{0xa1b9, 0x00},
	{0xa24b, 0xff},
	{0xa257, 0x00},
	{0xa2ad, 0xff},
	{0xa2b9, 0x00},
	{0xb21f, 0x04},
	{0xb35c, 0x00},
	{0xb35e, 0x08},
	{0x0112, 0x0c},
	{0x0113, 0x0c},
	{0x0114, 0x01},
	{0x0350, 0x00},
	{0xbcf1, 0x02},
	{0x3FF9, 0x00},
	{0x020E, 0x01}, {0x020F, 0x00},
	{0x0210, 0x01}, {0x0211, 0x00},
	{0x0212, 0x01}, {0x0213, 0x00},
	{0x0214, 0x01}, {0x0215, 0x00},
	{0x0216, 0x01}, {0x0217, 0x00},
};

static const struct imx477_reg base_full_tuning_regs[] = {
	{0x00e3, 0x00},
	{0x00e4, 0x00},
	{0x00fc, 0x0a},
	{0x00fd, 0x0a},
	{0x00fe, 0x0a},
	{0x00ff, 0x0a},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x3140, 0x02},
	{0x3c00, 0x00},
	{0x3c01, 0x03},
	{0x3c02, 0xa2},
	{0x3f0d, 0x01},
	{0x5748, 0x07},
	{0x5749, 0xff},
	{0x574a, 0x00},
	{0x574b, 0x00},
	{0x7b75, 0x0a},
	{0x7b76, 0x0c},
	{0x7b77, 0x07},
	{0x7b78, 0x06},
	{0x7b79, 0x3c},
	{0x7b53, 0x01},
	{0x9369, 0x5a},
	{0x936b, 0x55},
	{0x936d, 0x28},
	{0x9304, 0x00},
	{0x9305, 0x00},
	{0x9e9a, 0x2f},
	{0x9e9b, 0x2f},
	{0x9e9c, 0x2f},
	{0x9e9d, 0x00},
	{0x9e9e, 0x00},
	{0x9e9f, 0x00},
	{0xa2a9, 0x60},
	{0xa2b7, 0x00},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5e},
	{0x0309, 0x0c},
	{0x030b, 0x01},
	{0x030d, 0x02},
	{0x0310, 0x01},
	{0x0820, 0x09},
	{0x0821, 0xc0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x080a, 0x00},
	{0x080b, 0x7f},
	{0x080c, 0x00},
	{0x080d, 0x4f},
	{0x080e, 0x00},
	{0x080f, 0x77},
	{0x0810, 0x00},
	{0x0811, 0x5f},
	{0x0812, 0x00},
	{0x0813, 0x57},
	{0x0814, 0x00},
	{0x0815, 0x4f},
	{0x0816, 0x01},
	{0x0817, 0x27},
	{0x0818, 0x00},
	{0x0819, 0x3f},
	{0xe04c, 0x00},
	{0xe04d, 0x7f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3e20, 0x01},
	{0x3e37, 0x00},
	{0x3f50, 0x00},
	{0x3f56, 0x02},
	{0x3f57, 0xae},
};

static const struct imx477_reg base_2x2bin_tuning_regs[] = {
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x3140, 0x02},
	{0x3c00, 0x00},
	{0x3c01, 0x03},
	{0x3c02, 0xa2},
	{0x3f0d, 0x01},
	{0x5748, 0x07},
	{0x5749, 0xff},
	{0x574a, 0x00},
	{0x574b, 0x00},
	{0x7b53, 0x01},
	{0x9369, 0x73},
	{0x936b, 0x64},
	{0x936d, 0x5f},
	{0x9304, 0x00},
	{0x9305, 0x00},
	{0x9e9a, 0x2f},
	{0x9e9b, 0x2f},
	{0x9e9c, 0x2f},
	{0x9e9d, 0x00},
	{0x9e9e, 0x00},
	{0x9e9f, 0x00},
	{0xa2a9, 0x60},
	{0xa2b7, 0x00},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5e},
	{0x0309, 0x0c},
	{0x030b, 0x01},
	{0x030d, 0x02},
	{0x0310, 0x01},
	{0x0820, 0x09},
	{0x0821, 0xc0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x080a, 0x00},
	{0x080b, 0x7f},
	{0x080c, 0x00},
	{0x080d, 0x4f},
	{0x080e, 0x00},
	{0x080f, 0x77},
	{0x0810, 0x00},
	{0x0811, 0x5f},
	{0x0812, 0x00},
	{0x0813, 0x57},
	{0x0814, 0x00},
	{0x0815, 0x4f},
	{0x0816, 0x01},
	{0x0817, 0x27},
	{0x0818, 0x00},
	{0x0819, 0x3f},
	{0xe04c, 0x00},
	{0xe04d, 0x7f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3e20, 0x01},
	{0x3e37, 0x00},
	{0x3f50, 0x00},
	{0x3f56, 0x01},
	{0x3f57, 0x6c},
};

static const struct imx477_base_config base_configs[] = {
	[IMX477_BASE_MODE_FULL] = {
		.id = IMX477_BASE_MODE_FULL,
		.line_length_pix = 0x5dc0,
		.binning = false,
		.max_width = 4056,
		.max_height = 3040,
		.height_step = IMX477_HEIGHT_STEP,
		.sensor_width = 4056,
		.sensor_height = 3040,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(base_full_tuning_regs),
			.regs = base_full_tuning_regs,
		},
	},
	[IMX477_BASE_MODE_2X2_BIN] = {
		.id = IMX477_BASE_MODE_2X2_BIN,
		.line_length_pix = 0x31c4,
		.binning = true,
		.max_width = 2024,
		.max_height = 1520,
		.height_step = IMX477_BIN_HEIGHT_STEP,
		.sensor_width = 4056,
		.sensor_height = 3040,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(base_2x2bin_tuning_regs),
			.regs = base_2x2bin_tuning_regs,
		},
	},
};

#define IMX477_NUM_BASE_CONFIGS ARRAY_SIZE(base_configs)

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

static const char * const imx477_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

static const int imx477_test_pattern_val[] = {
	IMX477_TEST_PATTERN_DISABLE,
	IMX477_TEST_PATTERN_COLOR_BARS,
	IMX477_TEST_PATTERN_SOLID_COLOR,
	IMX477_TEST_PATTERN_GREY_COLOR,
	IMX477_TEST_PATTERN_PN9,
};

static const char * const imx477_supply_name[] = {
	"VANA",
	"VDIG",
	"VDDL",
};

#define IMX477_NUM_SUPPLIES ARRAY_SIZE(imx477_supply_name)

#define IMX477_XCLR_MIN_DELAY_US	8000
#define IMX477_XCLR_DELAY_RANGE_US	1000

struct imx477_compatible_data {
	unsigned int chip_id;
	struct imx477_reg_list extra_regs;
};

struct imx477 {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	unsigned int fmt_code;

	struct clk *xclk;
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX477_NUM_SUPPLIES];

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

	const struct imx477_base_config *base_cfg;

	unsigned int out_width;
	unsigned int out_height;

	struct v4l2_rect crop;
	struct v4l2_rect compose;

	unsigned int line_length_pix;

	int trigger_mode_of;

	struct mutex mutex;

	bool streaming;
	bool common_regs_written;

	unsigned int long_exp_shift;

	const struct imx477_compatible_data *compatible_data;
};

static inline struct imx477 *to_imx477(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx477, sd);
}

static int imx477_read_reg(struct imx477 *imx477, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
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

static int imx477_write_reg(struct imx477 *imx477, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int imx477_write_regs(struct imx477 *imx477,
			     const struct imx477_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imx477_write_reg(imx477, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static u32 imx477_get_format_code(struct imx477 *imx477, u32 code)
{
	unsigned int i;

	lockdep_assert_held(&imx477->mutex);

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == code)
			break;

	if (i >= ARRAY_SIZE(codes))
		i = 0;

	i = (i & ~3) | (imx477->vflip->val ? 2 : 0) |
	    (imx477->hflip->val ? 1 : 0);

	return codes[i];
}

static const struct imx477_base_config *
imx477_binning_from_ratio(const struct v4l2_rect *crop,
			  unsigned int compose_w, unsigned int compose_h)
{
	if (compose_w <= crop->width / 2 &&
	    compose_h <= crop->height / 2 &&
	    compose_w <= base_configs[IMX477_BASE_MODE_2X2_BIN].max_width &&
	    compose_h <= base_configs[IMX477_BASE_MODE_2X2_BIN].max_height)
		return &base_configs[IMX477_BASE_MODE_2X2_BIN];

	return &base_configs[IMX477_BASE_MODE_FULL];
}

static void imx477_clamp_align(unsigned int *width, unsigned int *height,
			       const struct imx477_base_config *cfg)
{
	*width = clamp(*width, IMX477_MIN_WIDTH, cfg->max_width);
	*height = clamp(*height, IMX477_MIN_HEIGHT, cfg->max_height);

	*width = rounddown(*width, IMX477_WIDTH_STEP);
	if (*width < IMX477_MIN_WIDTH)
		*width = IMX477_MIN_WIDTH;

	*height = rounddown(*height, cfg->height_step);
	if (*height < IMX477_MIN_HEIGHT)
		*height = IMX477_MIN_HEIGHT;
}

static void imx477_clamp_crop(struct v4l2_rect *crop)
{
	if (crop->left < 0)
		crop->left = 0;
	if (crop->top < 0)
		crop->top = 0;

	crop->left = rounddown(crop->left, 8);
	crop->top = rounddown(crop->top, 8);

	if (crop->width < IMX477_MIN_WIDTH)
		crop->width = IMX477_MIN_WIDTH;
	if (crop->height < IMX477_MIN_HEIGHT)
		crop->height = IMX477_MIN_HEIGHT;
	if (crop->width > IMX477_PIXEL_ARRAY_WIDTH)
		crop->width = IMX477_PIXEL_ARRAY_WIDTH;
	if (crop->height > IMX477_PIXEL_ARRAY_HEIGHT)
		crop->height = IMX477_PIXEL_ARRAY_HEIGHT;

	crop->width = rounddown(crop->width, 8);
	crop->height &= ~1;

	if (crop->left + crop->width > IMX477_PIXEL_ARRAY_WIDTH)
		crop->left = IMX477_PIXEL_ARRAY_WIDTH - crop->width;
	if (crop->top + crop->height > IMX477_PIXEL_ARRAY_HEIGHT)
		crop->top = IMX477_PIXEL_ARRAY_HEIGHT - crop->height;

	crop->left = rounddown(crop->left, 8);
	crop->top = rounddown(crop->top, 8);
}

static void imx477_tighten_crop(struct imx477 *imx477,
				const struct imx477_base_config *cfg)
{
	if (cfg->binning) {
		imx477->crop.width = imx477->compose.width * 2;
		imx477->crop.height = imx477->compose.height * 2;
	} else {
		imx477->crop.width = imx477->compose.width;
		imx477->crop.height = imx477->compose.height;
	}
	imx477_clamp_crop(&imx477->crop);
}

static int imx477_write_dynamic_regs(struct imx477 *imx477)
{
	const struct imx477_base_config *cfg = imx477->base_cfg;
	struct v4l2_rect *crop = &imx477->crop;
	unsigned int out_w = imx477->out_width;
	unsigned int out_h = imx477->out_height;
	int ret = 0;

	ret = imx477_write_reg(imx477, IMX477_REG_GRP_PARAM_HOLD,
			       IMX477_REG_VALUE_08BIT, 1);
	if (ret)
		return ret;

	ret = imx477_write_reg(imx477, IMX477_REG_LINE_LENGTH,
			       IMX477_REG_VALUE_16BIT, imx477->line_length_pix);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_X_ADD_STA,
			       IMX477_REG_VALUE_16BIT, crop->left);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_Y_ADD_STA,
			       IMX477_REG_VALUE_16BIT, crop->top);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_X_ADD_END,
			       IMX477_REG_VALUE_16BIT,
			       crop->left + crop->width - 1);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_Y_ADD_END,
			       IMX477_REG_VALUE_16BIT,
			       crop->top + crop->height - 1);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_BINNING_MODE,
			       IMX477_REG_VALUE_08BIT,
			       cfg->binning ? 0x01 : 0x00);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_BINNING_TYPE,
			       IMX477_REG_VALUE_08BIT,
			       cfg->binning ? 0x22 : 0x11);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, 0x0902,
			       IMX477_REG_VALUE_08BIT, 0x02);
	if (ret)
		goto release_hold;

	if (cfg->binning) {
		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_X_OFF,
				       IMX477_REG_VALUE_16BIT, 0);
		if (ret)
			goto release_hold;

		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_Y_OFF,
				       IMX477_REG_VALUE_16BIT, 0);
		if (ret)
			goto release_hold;

		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_WIDTH,
				       IMX477_REG_VALUE_16BIT, crop->width);
		if (ret)
			goto release_hold;

		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_HEIGHT,
				       IMX477_REG_VALUE_16BIT, crop->height);
		if (ret)
			goto release_hold;
	} else {
		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_X_OFF,
				       IMX477_REG_VALUE_16BIT,
				       (crop->width - out_w) / 2);
		if (ret)
			goto release_hold;

		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_Y_OFF,
				       IMX477_REG_VALUE_16BIT,
				       (crop->height - out_h) / 2);
		if (ret)
			goto release_hold;

		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_WIDTH,
				       IMX477_REG_VALUE_16BIT, out_w);
		if (ret)
			goto release_hold;

		ret = imx477_write_reg(imx477, IMX477_REG_DIG_CROP_HEIGHT,
				       IMX477_REG_VALUE_16BIT, out_h);
		if (ret)
			goto release_hold;
	}

	ret = imx477_write_reg(imx477, 0x0401,
			       IMX477_REG_VALUE_08BIT, 0x00);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, 0x0404,
			       IMX477_REG_VALUE_08BIT, 0x00);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_SCALE_M,
			       IMX477_REG_VALUE_08BIT,
			       cfg->binning ? 0x20 : 0x10);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_X_OUT_SIZE,
			       IMX477_REG_VALUE_16BIT, out_w);
	if (ret)
		goto release_hold;

	ret = imx477_write_reg(imx477, IMX477_REG_Y_OUT_SIZE,
			       IMX477_REG_VALUE_16BIT, out_h);
	if (ret)
		goto release_hold;

release_hold:
	imx477_write_reg(imx477, IMX477_REG_GRP_PARAM_HOLD,
			 IMX477_REG_VALUE_08BIT, 0);

	return ret;
}

static void imx477_set_default_format(struct imx477 *imx477)
{
	imx477->base_cfg = &base_configs[IMX477_BASE_MODE_FULL];
	imx477->out_width = IMX477_PIXEL_ARRAY_WIDTH;
	imx477->out_height = IMX477_PIXEL_ARRAY_HEIGHT;
	imx477->line_length_pix = imx477->base_cfg->line_length_pix;
	imx477->fmt_code = MEDIA_BUS_FMT_SRGGB12_1X12;

	imx477->crop.left = 0;
	imx477->crop.top = 0;
	imx477->crop.width = IMX477_PIXEL_ARRAY_WIDTH;
	imx477->crop.height = IMX477_PIXEL_ARRAY_HEIGHT;

	imx477->compose.left = 0;
	imx477->compose.top = 0;
	imx477->compose.width = IMX477_PIXEL_ARRAY_WIDTH;
	imx477->compose.height = IMX477_PIXEL_ARRAY_HEIGHT;
}

static int imx477_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx477 *imx477 = to_imx477(sd);
	struct v4l2_mbus_framefmt *try_fmt_img =
		v4l2_subdev_state_get_format(fh->state, IMAGE_PAD);
	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_state_get_format(fh->state, METADATA_PAD);
	struct v4l2_rect *try_crop;
	struct v4l2_rect *try_compose;

	mutex_lock(&imx477->mutex);

	try_fmt_img->width = IMX477_PIXEL_ARRAY_WIDTH;
	try_fmt_img->height = IMX477_PIXEL_ARRAY_HEIGHT;
	try_fmt_img->code = imx477_get_format_code(imx477,
						   MEDIA_BUS_FMT_SRGGB12_1X12);
	try_fmt_img->field = V4L2_FIELD_NONE;

	try_fmt_meta->width = IMX477_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = IMX477_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	try_crop = v4l2_subdev_state_get_crop(fh->state, IMAGE_PAD);
	try_crop->left = 0;
	try_crop->top = 0;
	try_crop->width = IMX477_PIXEL_ARRAY_WIDTH;
	try_crop->height = IMX477_PIXEL_ARRAY_HEIGHT;

	try_compose = v4l2_subdev_state_get_compose(fh->state, IMAGE_PAD);
	try_compose->left = 0;
	try_compose->top = 0;
	try_compose->width = IMX477_PIXEL_ARRAY_WIDTH;
	try_compose->height = IMX477_PIXEL_ARRAY_HEIGHT;

	mutex_unlock(&imx477->mutex);

	return 0;
}

static void imx477_adjust_exposure_range(struct imx477 *imx477)
{
	int exposure_max, exposure_def;

	exposure_max = imx477->out_height + imx477->vblank->val -
		       IMX477_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx477->exposure->val);
	__v4l2_ctrl_modify_range(imx477->exposure, imx477->exposure->minimum,
				 exposure_max, imx477->exposure->step,
				 exposure_def);
}

static int imx477_set_frame_length(struct imx477 *imx477, unsigned int val)
{
	int ret = 0;

	imx477->long_exp_shift = 0;

	while (val > IMX477_FRAME_LENGTH_MAX) {
		imx477->long_exp_shift++;
		val >>= 1;
	}

	ret = imx477_write_reg(imx477, IMX477_REG_FRAME_LENGTH,
			       IMX477_REG_VALUE_16BIT, val);
	if (ret)
		return ret;

	return imx477_write_reg(imx477, IMX477_LONG_EXP_SHIFT_REG,
				IMX477_REG_VALUE_08BIT, imx477->long_exp_shift);
}

static int imx477_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx477 *imx477 =
		container_of(ctrl->handler, struct imx477, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK)
		imx477_adjust_exposure_range(imx477);

	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN: {
		unsigned int db10 = ctrl->val;
		unsigned int ana, dgtl;

		if (db10 <= IMX477_ANA_GAIN_PGA_MAX) {
			ana  = imx477_ana_gain_code(db10);
			dgtl = IMX477_DGTL_GAIN_MIN;
		} else {
			ana  = imx477_ana_gain_code(IMX477_ANA_GAIN_PGA_MAX);
			dgtl = imx477_dgtl_gain_code(db10 -
						     IMX477_ANA_GAIN_PGA_MAX);
			if (dgtl > IMX477_DGTL_GAIN_MAX)
				dgtl = IMX477_DGTL_GAIN_MAX;
		}

		ret = imx477_write_reg(imx477, IMX477_REG_ANALOG_GAIN,
				       IMX477_REG_VALUE_16BIT, ana);
		if (!ret)
			ret = imx477_write_reg(imx477, IMX477_REG_DIGITAL_GAIN,
					       IMX477_REG_VALUE_16BIT, dgtl);
		break;
	}
	case V4L2_CID_EXPOSURE:
		ret = imx477_write_reg(imx477, IMX477_REG_EXPOSURE,
				       IMX477_REG_VALUE_16BIT, ctrl->val >>
							imx477->long_exp_shift);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx477_write_reg(imx477, IMX477_REG_DIGITAL_GAIN,
				       IMX477_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx477_write_reg(imx477, IMX477_REG_TEST_PATTERN,
				       IMX477_REG_VALUE_16BIT,
				       imx477_test_pattern_val[ctrl->val]);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		ret = imx477_write_reg(imx477, IMX477_REG_TEST_PATTERN_R,
				       IMX477_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		ret = imx477_write_reg(imx477, IMX477_REG_TEST_PATTERN_GR,
				       IMX477_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		ret = imx477_write_reg(imx477, IMX477_REG_TEST_PATTERN_B,
				       IMX477_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		ret = imx477_write_reg(imx477, IMX477_REG_TEST_PATTERN_GB,
				       IMX477_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = imx477_write_reg(imx477, IMX477_REG_ORIENTATION, 1,
				       imx477->hflip->val |
				       imx477->vflip->val << 1);
		break;
	case V4L2_CID_VBLANK:
		ret = imx477_set_frame_length(imx477,
					      imx477->out_height + ctrl->val);
		break;
	case V4L2_CID_HBLANK:
		ret = imx477_write_reg(imx477, IMX477_REG_LINE_LENGTH, 2,
				       imx477->out_width + ctrl->val);
		break;
	case V4L2_CID_BLACK_LEVEL:
		ret = imx477_write_reg(imx477, IMX477_REG_PEDESTAL_EN,
				       IMX477_REG_VALUE_08BIT,
				       ctrl->val ? 0x01 : 0x00);
		if (!ret)
			ret = imx477_write_reg(imx477, IMX477_REG_PEDESTAL_VALUE,
					       IMX477_REG_VALUE_16BIT, ctrl->val);
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

static const struct v4l2_ctrl_ops imx477_ctrl_ops = {
	.s_ctrl = imx477_set_ctrl,
};

static int imx477_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx477 *imx477 = to_imx477(sd);

	if (code->pad >= NUM_PADS)
		return -EINVAL;

	if (code->pad == IMAGE_PAD) {
		if (code->index >= (ARRAY_SIZE(codes) / 4))
			return -EINVAL;

		code->code = imx477_get_format_code(imx477,
						    codes[code->index * 4]);
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int imx477_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx477 *imx477 = to_imx477(sd);

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	if (fse->pad == IMAGE_PAD) {
		if (fse->index > 0)
			return -EINVAL;

		if (fse->code != imx477_get_format_code(imx477, fse->code))
			return -EINVAL;

		fse->min_width = IMX477_MIN_WIDTH;
		fse->max_width = IMX477_PIXEL_ARRAY_WIDTH;
		fse->min_height = IMX477_MIN_HEIGHT;
		fse->max_height = IMX477_PIXEL_ARRAY_HEIGHT;
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = IMX477_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = IMX477_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return 0;
}

static void imx477_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx477_update_image_pad_format(struct imx477 *imx477,
					   struct v4l2_subdev_format *fmt)
{
	fmt->format.width = imx477->out_width;
	fmt->format.height = imx477->out_height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx477_reset_colorspace(&fmt->format);
}

static void imx477_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = IMX477_EMBEDDED_LINE_WIDTH;
	fmt->format.height = IMX477_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int imx477_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx477 *imx477 = to_imx477(sd);

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx477->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_state_get_format(sd_state,
						   fmt->pad);
		try_fmt->code = fmt->pad == IMAGE_PAD ?
				imx477_get_format_code(imx477, try_fmt->code) :
				MEDIA_BUS_FMT_SENSOR_DATA;
		fmt->format = *try_fmt;
	} else {
		if (fmt->pad == IMAGE_PAD) {
			imx477_update_image_pad_format(imx477, fmt);
			fmt->format.code =
			       imx477_get_format_code(imx477, imx477->fmt_code);
		} else {
			imx477_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx477->mutex);
	return 0;
}

static void imx477_set_framing_limits(struct imx477 *imx477)
{
	unsigned int frm_length_min, frm_length_default, hblank_min;
	unsigned int out_w = imx477->out_width;
	unsigned int out_h = imx477->out_height;
	unsigned int line_length_min;

	{
		u64 tmp = link_freqs[imx477->link_freq_idx];
		u32 link_mhz, num, den;

		do_div(tmp, 1000000);
		link_mhz = (u32)tmp;
		num = out_w * 12 * (IMX477_PIXEL_RATE / 1000000);
		den = link_mhz * 2 * imx477->num_lanes;

		line_length_min = num / den;
	}
	line_length_min += 500;

	if (line_length_min < 5932)
		line_length_min = 5932;

	if (!imx477->base_cfg->binning) {
		if (imx477->num_lanes == 2 &&
		    imx477->crop.width >= IMX477_PIXEL_ARRAY_WIDTH) {
			if (line_length_min < imx477->base_cfg->line_length_pix)
				line_length_min = imx477->base_cfg->line_length_pix;
		} else {
			if (line_length_min < 6862)
				line_length_min = 6862;
		}
	} else {
		if (line_length_min < 6720)
			line_length_min = 6720;
	}

	imx477->line_length_pix = line_length_min;

	frm_length_min = out_h + IMX477_VBLANK_MIN;

	frm_length_default = IMX477_PIXEL_RATE /
			     (imx477->line_length_pix * 30);
	if (frm_length_default < frm_length_min)
		frm_length_default = frm_length_min;

	imx477->long_exp_shift = 0;

	__v4l2_ctrl_modify_range(imx477->vblank,
				 frm_length_min - out_h,
				 ((1 << IMX477_LONG_EXP_SHIFT_MAX) *
					IMX477_FRAME_LENGTH_MAX) - out_h,
				 1, frm_length_default - out_h);

	__v4l2_ctrl_s_ctrl(imx477->vblank, frm_length_default - out_h);

	hblank_min = imx477->line_length_pix - out_w;
	__v4l2_ctrl_modify_range(imx477->hblank, hblank_min,
				 IMX477_LINE_LENGTH_MAX - out_w, 1, hblank_min);
	__v4l2_ctrl_s_ctrl(imx477->hblank, hblank_min);
}

static int imx477_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	const struct imx477_base_config *cfg;
	struct imx477 *imx477 = to_imx477(sd);
	unsigned int req_width, req_height;

	if (fmt->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&imx477->mutex);

	if (fmt->pad == IMAGE_PAD) {
		fmt->format.code = imx477_get_format_code(imx477,
							  fmt->format.code);

		req_width = fmt->format.width;
		req_height = fmt->format.height;

		cfg = imx477_binning_from_ratio(&imx477->crop,
						req_width, req_height);

		imx477_clamp_align(&req_width, &req_height, cfg);

		fmt->format.width = req_width;
		fmt->format.height = req_height;
		fmt->format.field = V4L2_FIELD_NONE;
		imx477_reset_colorspace(&fmt->format);

		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx477->base_cfg = cfg;
			imx477->out_width = req_width;
			imx477->out_height = req_height;
			imx477->fmt_code = fmt->format.code;

			imx477_set_framing_limits(imx477);
		}
	} else {
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			framefmt = v4l2_subdev_state_get_format(sd_state,
							      fmt->pad);
			*framefmt = fmt->format;
		} else {
			imx477_update_metadata_pad_format(fmt);
		}
	}

	mutex_unlock(&imx477->mutex);

	return 0;
}

static int imx477_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx477 *imx477 = to_imx477(sd);

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		mutex_lock(&imx477->mutex);
		sel->r = imx477->crop;
		mutex_unlock(&imx477->mutex);
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		mutex_lock(&imx477->mutex);
		sel->r = imx477->compose;
		mutex_unlock(&imx477->mutex);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX477_NATIVE_WIDTH;
		sel->r.height = IMX477_NATIVE_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX477_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX477_PIXEL_ARRAY_HEIGHT;
		return 0;

	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX477_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX477_PIXEL_ARRAY_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int imx477_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx477 *imx477 = to_imx477(sd);
	const struct imx477_base_config *cfg;

	if (sel->pad != IMAGE_PAD)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&imx477->mutex);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		struct v4l2_rect crop = sel->r;
		unsigned int compose_w, compose_h;

		imx477_clamp_crop(&crop);

		compose_w = imx477->compose.width;
		compose_h = imx477->compose.height;

		cfg = imx477_binning_from_ratio(&crop, compose_w, compose_h);

		if (compose_w > cfg->max_width)
			compose_w = cfg->max_width;
		if (compose_h > cfg->max_height)
			compose_h = cfg->max_height;

		compose_w = rounddown(compose_w, IMX477_WIDTH_STEP);
		compose_h = rounddown(compose_h, cfg->height_step);
		if (compose_w < IMX477_MIN_WIDTH)
			compose_w = IMX477_MIN_WIDTH;
		if (compose_h < IMX477_MIN_HEIGHT)
			compose_h = IMX477_MIN_HEIGHT;

		if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
			*v4l2_subdev_state_get_crop(sd_state, sel->pad) = crop;
		} else {
			imx477->crop = crop;
			imx477->compose.width = compose_w;
			imx477->compose.height = compose_h;
			imx477->base_cfg = cfg;
			imx477->out_width = compose_w;
			imx477->out_height = compose_h;
			imx477_set_framing_limits(imx477);
		}

		sel->r = crop;

	} else {
		unsigned int compose_w = sel->r.width;
		unsigned int compose_h = sel->r.height;

		cfg = imx477_binning_from_ratio(&imx477->crop,
						compose_w, compose_h);

		if (compose_w > cfg->max_width)
			compose_w = cfg->max_width;
		if (compose_h > cfg->max_height)
			compose_h = cfg->max_height;

		compose_w = rounddown(compose_w, IMX477_WIDTH_STEP);
		compose_h = rounddown(compose_h, cfg->height_step);
		if (compose_w < IMX477_MIN_WIDTH)
			compose_w = IMX477_MIN_WIDTH;
		if (compose_h < IMX477_MIN_HEIGHT)
			compose_h = IMX477_MIN_HEIGHT;

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY) {
			imx477->compose.left = 0;
			imx477->compose.top = 0;
			imx477->compose.width = compose_w;
			imx477->compose.height = compose_h;
			imx477->base_cfg = cfg;
			imx477->out_width = compose_w;
			imx477->out_height = compose_h;

			imx477_tighten_crop(imx477, cfg);

			cfg = imx477_binning_from_ratio(&imx477->crop,
							compose_w, compose_h);
			imx477->base_cfg = cfg;

			imx477_set_framing_limits(imx477);
		}

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = compose_w;
		sel->r.height = compose_h;
	}

	mutex_unlock(&imx477->mutex);

	return 0;
}

static int imx477_start_streaming(struct imx477 *imx477)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	const struct imx477_reg_list *reg_list, *freq_regs;
	const struct imx477_reg_list *extra_regs;
	int ret, tm;

	if (!imx477->common_regs_written) {
		ret = imx477_write_regs(imx477, mode_common_regs,
					ARRAY_SIZE(mode_common_regs));
		if (!ret) {
			extra_regs = &imx477->compatible_data->extra_regs;
			ret = imx477_write_regs(imx477,	extra_regs->regs,
						extra_regs->num_of_regs);
		}

		if (!ret) {
			freq_regs = &link_freq_regs[imx477->link_freq_idx];
			ret = imx477_write_regs(imx477, freq_regs->regs,
						freq_regs->num_of_regs);
		}

		if (ret) {
			dev_err(&client->dev, "%s failed to set common settings\n",
				__func__);
			return ret;
		}

		imx477->common_regs_written = true;
	}

	reg_list = &imx477->base_cfg->reg_list;
	ret = imx477_write_regs(imx477, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set base mode\n", __func__);
		return ret;
	}

	if (imx477->num_lanes == 4) {
		imx477_write_reg(imx477, 0x0114,
				 IMX477_REG_VALUE_08BIT, 0x03);
	}
	if (imx477->link_freq_idx != IMX477_LINK_FREQ_624MHZ) {
		u64 tmp = link_freqs[imx477->link_freq_idx];
		u32 iopck_mhz, req_link;

		do_div(tmp, 1000000);
		iopck_mhz = (u32)tmp * 2;
		req_link = iopck_mhz * 2;

		imx477_write_reg(imx477, 0x0820,
				 IMX477_REG_VALUE_08BIT,
				 (req_link >> 8) & 0xFF);
		imx477_write_reg(imx477, 0x0821,
				 IMX477_REG_VALUE_08BIT,
				 req_link & 0xFF);
		imx477_write_reg(imx477, 0x0822,
				 IMX477_REG_VALUE_08BIT, 0x00);
		imx477_write_reg(imx477, 0x0823,
				 IMX477_REG_VALUE_08BIT, 0x00);
	}

	ret = imx477_write_dynamic_regs(imx477);
	if (ret) {
		dev_err(&client->dev, "%s failed to set dynamic regs\n",
			__func__);
		return ret;
	}

	imx477_write_reg(imx477, 0x0b05, IMX477_REG_VALUE_08BIT, !!dpc_enable);
	imx477_write_reg(imx477, 0x0b06, IMX477_REG_VALUE_08BIT, !!dpc_enable);

	ret = __v4l2_ctrl_handler_setup(imx477->sd.ctrl_handler);
	if (ret)
		return ret;

	tm = (imx477->trigger_mode_of >= 0) ? imx477->trigger_mode_of : trigger_mode;
	imx477_write_reg(imx477, IMX477_REG_MC_MODE,
			 IMX477_REG_VALUE_08BIT, (tm > 0) ? 1 : 0);
	imx477_write_reg(imx477, IMX477_REG_MS_SEL,
			 IMX477_REG_VALUE_08BIT, (tm <= 1) ? 1 : 0);
	imx477_write_reg(imx477, IMX477_REG_XVS_IO_CTRL,
			 IMX477_REG_VALUE_08BIT, (tm == 1) ? 1 : 0);
	imx477_write_reg(imx477, IMX477_REG_EXTOUT_EN,
			 IMX477_REG_VALUE_08BIT, (tm == 1) ? 1 : 0);

	return imx477_write_reg(imx477, IMX477_REG_MODE_SELECT,
				IMX477_REG_VALUE_08BIT, IMX477_MODE_STREAMING);
}

static void imx477_stop_streaming(struct imx477 *imx477)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	int ret;

	ret = imx477_write_reg(imx477, IMX477_REG_MODE_SELECT,
			       IMX477_REG_VALUE_08BIT, IMX477_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	imx477_write_reg(imx477, IMX477_REG_EXTOUT_EN,
			 IMX477_REG_VALUE_08BIT, 0);
}

static int imx477_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx477 *imx477 = to_imx477(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx477->mutex);
	if (imx477->streaming == enable) {
		mutex_unlock(&imx477->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		ret = imx477_start_streaming(imx477);
		if (ret)
			goto err_rpm_put;
	} else {
		imx477_stop_streaming(imx477);
		pm_runtime_put(&client->dev);
	}

	imx477->streaming = enable;

	__v4l2_ctrl_grab(imx477->vflip, enable);
	__v4l2_ctrl_grab(imx477->hflip, enable);

	mutex_unlock(&imx477->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx477->mutex);

	return ret;
}

static int imx477_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx477 *imx477 = to_imx477(sd);
	int ret;

	ret = regulator_bulk_enable(IMX477_NUM_SUPPLIES,
				    imx477->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx477->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx477->reset_gpio, 1);
	usleep_range(IMX477_XCLR_MIN_DELAY_US,
		     IMX477_XCLR_MIN_DELAY_US + IMX477_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(IMX477_NUM_SUPPLIES, imx477->supplies);
	return ret;
}

static int imx477_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx477 *imx477 = to_imx477(sd);

	gpiod_set_value_cansleep(imx477->reset_gpio, 0);
	regulator_bulk_disable(IMX477_NUM_SUPPLIES, imx477->supplies);
	clk_disable_unprepare(imx477->xclk);

	imx477->common_regs_written = false;

	return 0;
}

static int __maybe_unused imx477_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx477 *imx477 = to_imx477(sd);

	if (imx477->streaming)
		imx477_stop_streaming(imx477);

	return 0;
}

static int __maybe_unused imx477_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx477 *imx477 = to_imx477(sd);
	int ret;

	if (imx477->streaming) {
		ret = imx477_start_streaming(imx477);
		if (ret)
			goto error;
	}

	return 0;

error:
	imx477_stop_streaming(imx477);
	imx477->streaming = 0;
	return ret;
}

static int imx477_get_regulators(struct imx477 *imx477)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	unsigned int i;

	for (i = 0; i < IMX477_NUM_SUPPLIES; i++)
		imx477->supplies[i].supply = imx477_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX477_NUM_SUPPLIES,
				       imx477->supplies);
}

static int imx477_identify_module(struct imx477 *imx477, u32 expected_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	int ret;
	u32 val;

	ret = imx477_read_reg(imx477, IMX477_REG_CHIP_ID,
			      IMX477_REG_VALUE_16BIT, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x, with error %d\n",
			expected_id, ret);
		return ret;
	}

	if (val != expected_id) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			expected_id, val);
		return -EIO;
	}

	dev_info(&client->dev, "Device found is imx%x\n", val);

	return 0;
}

static const struct v4l2_subdev_core_ops imx477_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx477_video_ops = {
	.s_stream = imx477_set_stream,
};

static const struct v4l2_subdev_pad_ops imx477_pad_ops = {
	.enum_mbus_code = imx477_enum_mbus_code,
	.get_fmt = imx477_get_pad_format,
	.set_fmt = imx477_set_pad_format,
	.get_selection = imx477_get_selection,
	.set_selection = imx477_set_selection,
	.enum_frame_size = imx477_enum_frame_size,
};

static const struct v4l2_subdev_ops imx477_subdev_ops = {
	.core = &imx477_core_ops,
	.video = &imx477_video_ops,
	.pad = &imx477_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx477_internal_ops = {
	.open = imx477_open,
};

static int imx477_init_controls(struct imx477 *imx477)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct i2c_client *client = v4l2_get_subdevdata(&imx477->sd);
	struct v4l2_fwnode_device_properties props;
	unsigned int i;
	int ret;

	ctrl_hdlr = &imx477->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 16);
	if (ret)
		return ret;

	mutex_init(&imx477->mutex);
	ctrl_hdlr->lock = &imx477->mutex;

	imx477->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       IMX477_PIXEL_RATE,
					       IMX477_PIXEL_RATE, 1,
					       IMX477_PIXEL_RATE);
	if (imx477->pixel_rate)
		imx477->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx477->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx477_ctrl_ops,
				       V4L2_CID_LINK_FREQ, 0, 0,
				       &link_freqs[imx477->link_freq_idx]);
	if (imx477->link_freq)
		imx477->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx477->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
					   V4L2_CID_VBLANK, IMX477_VBLANK_MIN,
					   0xffff, 1, IMX477_VBLANK_MIN);
	imx477->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
					   V4L2_CID_HBLANK, 0, 0xffff, 1, 0);

	imx477->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX477_EXPOSURE_MIN,
					     IMX477_EXPOSURE_MAX,
					     IMX477_EXPOSURE_STEP,
					     IMX477_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX477_ANA_GAIN_MIN, IMX477_ANA_GAIN_MAX,
			  IMX477_ANA_GAIN_STEP, IMX477_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops, V4L2_CID_BLACK_LEVEL,
			  IMX477_BLACK_LEVEL_MIN, IMX477_BLACK_LEVEL_MAX,
			  IMX477_BLACK_LEVEL_STEP, IMX477_BLACK_LEVEL_DEFAULT);

	imx477->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx477->hflip)
		imx477->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx477->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx477->vflip)
		imx477->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx477_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx477_test_pattern_menu) - 1,
				     0, 0, imx477_test_pattern_menu);
	for (i = 0; i < 4; i++) {
		v4l2_ctrl_new_std(ctrl_hdlr, &imx477_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_RED + i,
				  IMX477_TEST_PATTERN_COLOUR_MIN,
				  IMX477_TEST_PATTERN_COLOUR_MAX,
				  IMX477_TEST_PATTERN_COLOUR_STEP,
				  IMX477_TEST_PATTERN_COLOUR_MAX);
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

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx477_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx477->sd.ctrl_handler = ctrl_hdlr;

	mutex_lock(&imx477->mutex);
	imx477_set_framing_limits(imx477);
	mutex_unlock(&imx477->mutex);

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx477->mutex);

	return ret;
}

static void imx477_free_controls(struct imx477 *imx477)
{
	v4l2_ctrl_handler_free(imx477->sd.ctrl_handler);
	mutex_destroy(&imx477->mutex);
}

static int imx477_check_hwcfg(struct device *dev, struct imx477 *imx477)
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
	imx477->num_lanes = ep_cfg.bus.mipi_csi2.num_data_lanes;

	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	for (i = 0; i < ARRAY_SIZE(link_freqs); i++) {
		if (link_freqs[i] == ep_cfg.link_frequencies[0]) {
			imx477->link_freq_idx = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(link_freqs)) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
			ret = -EINVAL;
			goto error_out;
	}

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static const struct imx477_compatible_data imx477_compatible = {
	.chip_id = IMX477_CHIP_ID,
	.extra_regs = {
		.num_of_regs = 0,
		.regs = NULL
	}
};

static const struct imx477_reg imx378_regs[] = {
	{0x3e35, 0x01},
	{0x4421, 0x08},
	{0x3ff9, 0x00},
};

static const struct imx477_compatible_data imx378_compatible = {
	.chip_id = IMX378_CHIP_ID,
	.extra_regs = {
		.num_of_regs = ARRAY_SIZE(imx378_regs),
		.regs = imx378_regs
	}
};

static const struct of_device_id imx477_dt_ids[] = {
	{ .compatible = "sony,imx477", .data = &imx477_compatible },
	{ .compatible = "sony,imx378", .data = &imx378_compatible },
	{  }
};

static int imx477_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx477 *imx477;
	const struct of_device_id *match;
	int ret;
	u32 tm_of;

	imx477 = devm_kzalloc(&client->dev, sizeof(*imx477), GFP_KERNEL);
	if (!imx477)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx477->sd, client, &imx477_subdev_ops);

	match = of_match_device(imx477_dt_ids, dev);
	if (!match)
		return -ENODEV;
	imx477->compatible_data =
		(const struct imx477_compatible_data *)match->data;

	if (imx477_check_hwcfg(dev, imx477))
		return -EINVAL;

	ret = of_property_read_u32(dev->of_node, "trigger-mode", &tm_of);
	imx477->trigger_mode_of = (ret == 0) ? tm_of : -1;

	imx477->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx477->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx477->xclk);
	}

	imx477->xclk_freq = clk_get_rate(imx477->xclk);
	if (imx477->xclk_freq != IMX477_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx477->xclk_freq);
		return -EINVAL;
	}

	ret = imx477_get_regulators(imx477);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	imx477->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	ret = imx477_power_on(dev);
	if (ret)
		return ret;

	ret = imx477_identify_module(imx477, imx477->compatible_data->chip_id);
	if (ret)
		goto error_power_off;

	imx477_set_default_format(imx477);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = imx477_init_controls(imx477);
	if (ret)
		goto error_power_off;

	imx477->sd.internal_ops = &imx477_internal_ops;
	imx477->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx477->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx477->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	imx477->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx477->sd.entity, NUM_PADS, imx477->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&imx477->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&imx477->sd.entity);

error_handler_free:
	imx477_free_controls(imx477);

error_power_off:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	imx477_power_off(&client->dev);

	return ret;
}

static void imx477_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx477 *imx477 = to_imx477(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx477_free_controls(imx477);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx477_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

MODULE_DEVICE_TABLE(of, imx477_dt_ids);

static const struct dev_pm_ops imx477_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imx477_suspend, imx477_resume)
	SET_RUNTIME_PM_OPS(imx477_power_off, imx477_power_on, NULL)
};

static struct i2c_driver imx477_i2c_driver = {
	.driver = {
		.name = "imx477",
		.of_match_table	= imx477_dt_ids,
		.pm = &imx477_pm_ops,
	},
	.probe = imx477_probe,
	.remove = imx477_remove,
};

module_i2c_driver(imx477_i2c_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("Sony IMX477 sensor driver");
MODULE_LICENSE("GPL");
