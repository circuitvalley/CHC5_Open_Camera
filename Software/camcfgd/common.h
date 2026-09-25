/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * common.h - Shared definitions for camcfgd
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_COMMON_H
#define CAMCFGD_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#define CHC5_I2C_PROTO_VER          5

#define CHC5_DYNCFG_OFFSET          0x00
#define CHC5_READBACK_OFFSET        0x40
#define CHC5_AUXCFG_OFFSET          0x80

#define CHC5_SLAVE_BANK_SIZE        0x100
#define CHC5_SLAVE_CNT_OFFSET       0x400
#define CHC5_SLAVE_FILE_SIZE        0x600
#define CHC5_I2C_AUX_VER            1

typedef struct __attribute__((packed)) imgsensor_cfg_s {
    uint16_t    width;
    uint16_t    height;
    uint16_t    offset_x;
    uint16_t    offset_y;
    uint32_t    exposure_us;
    uint16_t    analog_gain;
    uint16_t    fps;
    uint8_t     test_pattern;
    uint8_t     stream_enable;
    uint8_t     binning;
    uint32_t    pixel_format;
    uint16_t    black_level;
    uint8_t     auto_flags;
    uint8_t     ae_metering_mode;
    uint8_t     ae_target;
    uint32_t    ae_exp_lower_us;
    uint32_t    ae_exp_upper_us;
    uint16_t    ae_roi_x;
    uint16_t    ae_roi_y;
    uint16_t    ae_roi_w;
    uint16_t    ae_roi_h;
    uint16_t    balance_ratio_r;
    uint16_t    balance_ratio_b;
    uint8_t     awb_color_temp;
    uint8_t     awb_gain_min_x100;
    uint8_t     awb_gain_max_x10;
    uint8_t     awb_rate_x100;
    uint8_t     flip_flags;
    uint8_t     link_bw_mode;
    uint32_t    link_bw_limit;
    uint16_t    gamma_x100;
    uint8_t     aux_seq;
    uint8_t     proto_ver;
    uint8_t     proto_flags;
    uint8_t     write_seq;
} imgsensor_cfg_t;

_Static_assert(sizeof(imgsensor_cfg_t) == 64,
               "imgsensor_cfg_t must be 64 bytes");

_Static_assert(offsetof(imgsensor_cfg_t, flip_flags)       == 0x34, "flip_flags offset");
_Static_assert(offsetof(imgsensor_cfg_t, link_bw_mode)     == 0x35, "link_bw_mode offset");
_Static_assert(offsetof(imgsensor_cfg_t, link_bw_limit)    == 0x36, "link_bw_limit offset");
_Static_assert(offsetof(imgsensor_cfg_t, width)            == 0x00, "width offset");
_Static_assert(offsetof(imgsensor_cfg_t, height)           == 0x02, "height offset");
_Static_assert(offsetof(imgsensor_cfg_t, offset_x)         == 0x04, "offset_x offset");
_Static_assert(offsetof(imgsensor_cfg_t, offset_y)         == 0x06, "offset_y offset");
_Static_assert(offsetof(imgsensor_cfg_t, exposure_us)      == 0x08, "exposure_us offset");
_Static_assert(offsetof(imgsensor_cfg_t, analog_gain)      == 0x0C, "analog_gain offset");
_Static_assert(offsetof(imgsensor_cfg_t, fps)              == 0x0E, "fps offset");
_Static_assert(offsetof(imgsensor_cfg_t, test_pattern)     == 0x10, "test_pattern offset");
_Static_assert(offsetof(imgsensor_cfg_t, stream_enable)    == 0x11, "stream_enable offset");
_Static_assert(offsetof(imgsensor_cfg_t, binning)          == 0x12, "binning offset");
_Static_assert(offsetof(imgsensor_cfg_t, pixel_format)     == 0x13, "pixel_format offset");
_Static_assert(offsetof(imgsensor_cfg_t, black_level)      == 0x17, "black_level offset");
_Static_assert(offsetof(imgsensor_cfg_t, auto_flags)       == 0x19, "auto_flags offset");
_Static_assert(offsetof(imgsensor_cfg_t, ae_metering_mode) == 0x1A, "ae_metering_mode offset");
_Static_assert(offsetof(imgsensor_cfg_t, ae_target)        == 0x1B, "ae_target offset");
_Static_assert(offsetof(imgsensor_cfg_t, ae_exp_lower_us)  == 0x1C, "ae_exp_lower_us offset");
_Static_assert(offsetof(imgsensor_cfg_t, ae_exp_upper_us)  == 0x20, "ae_exp_upper_us offset");
_Static_assert(offsetof(imgsensor_cfg_t, ae_roi_x)         == 0x24, "ae_roi_x offset");
_Static_assert(offsetof(imgsensor_cfg_t, ae_roi_h)         == 0x2A, "ae_roi_h offset");
_Static_assert(offsetof(imgsensor_cfg_t, balance_ratio_r)  == 0x2C, "balance_ratio_r offset");
_Static_assert(offsetof(imgsensor_cfg_t, balance_ratio_b)  == 0x2E, "balance_ratio_b offset");
_Static_assert(offsetof(imgsensor_cfg_t, awb_color_temp)   == 0x30, "awb_color_temp offset");
_Static_assert(offsetof(imgsensor_cfg_t, awb_gain_min_x100)== 0x31, "awb_gain_min_x100 offset");
_Static_assert(offsetof(imgsensor_cfg_t, awb_gain_max_x10) == 0x32, "awb_gain_max_x10 offset");
_Static_assert(offsetof(imgsensor_cfg_t, awb_rate_x100)    == 0x33, "awb_rate_x100 offset");
_Static_assert(offsetof(imgsensor_cfg_t, proto_ver)        == 0x3D, "proto_ver offset");
_Static_assert(offsetof(imgsensor_cfg_t, proto_flags)      == 0x3E, "proto_flags offset");
_Static_assert(offsetof(imgsensor_cfg_t, write_seq)        == 0x3F, "write_seq offset");
_Static_assert(offsetof(imgsensor_cfg_t, aux_seq)          == 0x3C, "aux_seq offset");
_Static_assert(offsetof(imgsensor_cfg_t, gamma_x100)       == 0x3A, "gamma_x100 offset");

#define CHC5_PF_GAMMA           0x01

#define CHC5_GAMMA_NO_CHANGE    0
#define CHC5_GAMMA_OFF          1
#define CHC5_GAMMA_SRGB         2
#define CHC5_GAMMA_USER_MIN     25
#define CHC5_GAMMA_USER_MAX     400
#define CHC5_GAMMA_CURVE_ADJ    0x8000

typedef struct __attribute__((packed)) imgsensor_readback_s {
    uint8_t     rb_seq;
    uint8_t     ae_status;
    uint16_t    _reserved0;
    uint32_t    exposure_us;
    uint16_t    analog_gain;
    uint16_t    fps_x100;
    int16_t     temp_c_x10;
    uint8_t     aux_status;
    uint8_t     line_levels;
    uint32_t    pixfmt_usb;
    uint8_t     cfa_order;
    uint8_t     flip_echo;
    uint8_t     _reserved1[9];
    uint8_t     rb_seq_echo;
} imgsensor_readback_t;

_Static_assert(sizeof(imgsensor_readback_t) == 32,
               "imgsensor_readback_t must be 32 bytes");

typedef struct __attribute__((packed)) chc5_camio_i2c_s {
    uint8_t     seq;
    uint8_t     aux_ver;
    uint8_t     flags;
    uint8_t     in_function;
    uint8_t     in_activation;
    uint8_t     out_source;
    uint8_t     strobe_src;
    uint8_t     reserved_07;
    uint8_t     reserved_08;
    uint8_t     sync_role;
    uint8_t     sync_trig_route;
    uint8_t     reserved0;
    uint16_t    trigger_divider;
    uint16_t    reserved1;
    uint32_t    trigger_delay_us;
    uint32_t    strobe_delay_us;
    uint32_t    reserved_18;
    uint32_t    strobe_duration_us;
    uint32_t    strobe_minon_us;
    uint32_t    reserved_24;
    uint8_t     reserved2[23];
    uint8_t     seq_echo;
} chc5_camio_i2c_t;

_Static_assert(sizeof(chc5_camio_i2c_t) == 64,
               "chc5_camio_i2c_t must be 64 bytes");
_Static_assert(offsetof(chc5_camio_i2c_t, seq)              == 0x00, "camio seq offset");
_Static_assert(offsetof(chc5_camio_i2c_t, trigger_delay_us) == 0x10, "camio trig_delay offset");
_Static_assert(offsetof(chc5_camio_i2c_t, reserved_24) == 0x24, "camio timebase offset");
_Static_assert(offsetof(chc5_camio_i2c_t, seq_echo)         == 0x3F, "camio seq_echo offset");

#define CHC5_AUX_F_ENABLE       (1u << 0)
#define CHC5_AUX_F_IN_INVERT    (1u << 1)
#define CHC5_AUX_F_STROBE_EN    (1u << 2)
#define CHC5_AUX_F_STROBE_INV   (1u << 3)
#define CHC5_AUX_F_OUT_INVERT   (1u << 4)
#define CHC5_AUX_F_OUT_USER     (1u << 5)
#define CHC5_AUX_F_ROLE_BY_I2C  (1u << 6)
_Static_assert(offsetof(imgsensor_readback_t, rb_seq)      == 0x00, "rb_seq offset");
_Static_assert(offsetof(imgsensor_readback_t, exposure_us) == 0x04, "rb exposure offset");
_Static_assert(offsetof(imgsensor_readback_t, pixfmt_usb)  == 0x10, "rb pixfmt_usb offset");
_Static_assert(offsetof(imgsensor_readback_t, cfa_order)   == 0x14, "rb cfa_order offset");
_Static_assert(offsetof(imgsensor_readback_t, flip_echo)   == 0x15, "rb flip_echo offset");
_Static_assert(offsetof(imgsensor_readback_t, rb_seq_echo) == 0x1F, "rb_seq_echo offset");

#define RB_AE_STATUS_EXP_ACTIVE     (1u << 0)
#define RB_AE_STATUS_GAIN_ACTIVE    (1u << 1)
#define RB_AE_STATUS_CONVERGED      (1u << 2)

#define AUTO_FLAG_EXPOSURE(f)   ((f) & 0x03)
#define AUTO_FLAG_GAIN(f)       (((f) >> 2) & 0x03)
#define AUTO_FLAG_WB(f)         (((f) >> 4) & 0x03)

#define AUTO_NOP                0
#define AUTO_ENABLE             1
#define AUTO_DISABLE            2
#define AUTO_ONCE               3

#define AUTO_FLAGS_EXPOSURE(v)  ((v) & 0x03)
#define AUTO_FLAGS_GAIN(v)      (((v) & 0x03) << 2)
#define AUTO_FLAGS_WB(v)        (((v) & 0x03) << 4)

#define FLIP_FLAG_X(f)          ((f) & 0x03)
#define FLIP_FLAG_Y(f)          (((f) >> 2) & 0x03)
#define FLIP_FLAGS_X(v)         ((v) & 0x03)
#define FLIP_FLAGS_Y(v)         (((v) & 0x03) << 2)

#define CFG_SRC_NONE   0
#define CFG_SRC_USB    1
#define CFG_SRC_GIGE   2
#define CFG_SRC_WEB    3

#define BW_USB3_MAX     (350u * 1000000u)
#define BW_ETH_MAX      (117u * 1000000u)
#define BW_PIXPACK_MAX  (1200u * 1000000u)

 #define TIMING_MAXIMIZE_HBLANK 

#define DIRTY_SENSOR_CTRL    (1u << 0)
#define DIRTY_PIPELINE_FMT   (1u << 1)
#define DIRTY_STREAM         (1u << 2)
#define DIRTY_AE_CFG         (1u << 4)
#define DIRTY_PIXPACK        (1u << 5)
#define DIRTY_CAMIO          (1u << 6)
#define DIRTY_GAMMA          (1u << 7)

struct camcfg_state_s {
    imgsensor_cfg_t sensor_cfg;
    uint8_t     sensor_cfg_source;
    bool        stream_active;
    float       fps_precise;
    float       max_fps;

    uint32_t    pixfmt_usb;
    uint32_t    pixfmt_eth;
    bool        usb_active;
    bool        eth_active;

    bool        fmt_pending;
    bool        pixpack_pending;

    bool        ae_exposure_auto;
    bool        ae_gain_auto;
    bool        ae_once_exposure;
    bool        ae_once_gain;

    struct {
        uint16_t hdmi_max_fps;
        uint8_t  hdmi_crop_auto;
        uint16_t hdmi_crop_x;
        uint16_t hdmi_crop_y;
        float    ae_target_brightness;
        uint8_t  ae_metering_mode;
        uint8_t  ae_speed;
        uint8_t  ae_priority;
        uint8_t  ae_highlight;
        uint8_t  ae_flicker;
        uint8_t  ae_once;
    } imaging;

    uint16_t    ae_current_gain;
    uint32_t    ae_current_exposure_us;
    uint32_t    ae_brightness_raw;

    uint32_t    config_version;

    uint8_t     live_cfa_order;
    uint8_t     live_cfa_flips;
};

#define CAMCFGD_SOCK_PATH    "/var/run/camcfgd.sock"

#define IPC_MSG_SET_SENSOR_CFG    0x01
#define IPC_MSG_GET_CONFIG        0x02
#define IPC_MSG_SET_GENREG        0x03
#define IPC_MSG_STREAM_CTRL       0x06
#define IPC_MSG_REPLY_OK          0x80
#define IPC_MSG_REPLY_ERR         0x81

#define IPC_MAX_PAYLOAD  4096

#define USB_CHARDEV_PATH   "/sys/bus/i2c/devices/1-0010/slave-chc5"

#define CHC5_CID_BLACK_LEVEL    0x0098090b

#define HDMI_MAX_WIDTH      1920
#define HDMI_MAX_HEIGHT     1080
#define HDMI_FPS_MAX        60

#include "chc5-v4l2-controls.h"

#define CHC5_VDMA_CID_HDMI_ENABLE  CHC5_VDMA_CID_ENABLE

#define GAIN_DB_RANGE           96.0

#define GAIN_DB_FALLBACK        72.0
#define GAIN_DB_PER_STOP        6.0206
#define GAIN_DB10_PER_DB        10.0

extern int    g_sensor_mono;

extern int    g_bayer_order;
extern int    g_orient_hflip;
extern int    g_orient_vflip;

extern bool     g_sensor_caps_looked;
extern uint32_t g_sensor_width;
extern uint32_t g_sensor_height;
extern uint32_t g_width_min;
extern uint32_t g_height_min;
extern uint32_t g_width_inc;
extern uint32_t g_height_inc;
extern uint32_t g_binning_width;
extern uint32_t g_binning_width_min;
extern uint32_t g_binning_height_min;
extern uint32_t g_offset_x_inc;
extern uint32_t g_offset_y_inc;
extern uint32_t g_binning_width_inc;
extern uint32_t g_binning_height_inc;
extern uint32_t g_binning_offset_x_inc;
extern uint32_t g_binning_offset_y_inc;
extern uint32_t g_binning_height;
extern uint32_t g_binning_max_h;
extern uint32_t g_binning_max_v;
extern uint32_t g_sensor_bit_depth;

extern uint32_t g_exposure_min_us;

static inline uint32_t align_down(uint32_t v, uint32_t inc)
{
    return (inc > 1u) ? (v - (v % inc)) : v;
}

static inline double gain_code_to_db(uint16_t code)
{ return (double)code / GAIN_DB10_PER_DB; }
static inline double gain_code_to_ev(uint16_t code)
{ return gain_code_to_db(code) / GAIN_DB_PER_STOP; }

static inline bool pixfmt_is_valid(uint32_t pfnc)
{ return pfnc != 0u && pfnc != 0xFFFFFFFFu; }

#define EXPOSURE_US_MAX         30000000u

#define GIGE_REG_WIDTH          0xB000
#define GIGE_REG_HEIGHT         0xB004
#define GIGE_REG_OFFSET_X       0xB008
#define GIGE_REG_OFFSET_Y       0xB00C
#define GIGE_REG_BINNING_H      0xB010
#define GIGE_REG_BINNING_V      0xB014
#define GIGE_REG_PIXFMT         0xB018
#define GIGE_REG_REVERSE_X      0xB020
#define GIGE_REG_REVERSE_Y      0xB024
#define GIGE_REG_TEST_PATTERN   0xB028
#define GIGE_REG_ACQ_MODE       0xC000
#define GIGE_REG_ACQ_START      0xC004
#define GIGE_REG_ACQ_STOP       0xC008
#define GIGE_REG_EXPOSURE_TIME  0xC034
#define GIGE_REG_EXPOSURE_AUTO  0xC03C
#define GIGE_REG_AE_EXP_LOWER   0xC040
#define GIGE_REG_AE_EXP_UPPER   0xC048
#define GIGE_REG_FPS_ENABLE     0xC018
#define GIGE_REG_FPS            0xC01C
#define GIGE_REG_GAIN           0xD004
#define GIGE_REG_GAIN_AUTO      0xD00C
#define GIGE_REG_BALANCE_WHITE_AUTO 0xD040
#define GIGE_REG_BALANCE_RATIO_SEL  0xD044
#define GIGE_REG_BALANCE_RATIO      0xD048
#define GIGE_REG_AWB_LOWER_LIMIT    0xD04C
#define GIGE_REG_AWB_UPPER_LIMIT    0xD054
#define GIGE_REG_AWB_RATE           0xD058
#define GIGE_REG_COLOR_TEMP         0xD05C
#define GIGE_REG_SATURATION         0xD060
#define GIGE_REG_LINK_BW_LIMIT      0xD064
#define GIGE_REG_LINK_BW_MODE       0xD068
#define GIGE_REG_BLACK_LEVEL    0xD034
#define GIGE_REG_GAMMA_ENABLE   0xD028
#define GIGE_REG_GAMMA          0xD02C
#define GIGE_REG_GAMMA_SELECTOR 0xD06C
#define GIGE_REG_AEROI_OFFX     0xC080
#define GIGE_REG_AEROI_OFFY     0xC084
#define GIGE_REG_AEROI_W        0xC088
#define GIGE_REG_AEROI_H        0xC08C
#define GIGE_REG_AE_TARGET      0xC0A0
#define GIGE_REG_AE_METERING    0xC0A8

#ifndef GVSP_PIX_MONO8
#define GVSP_PIX_MONO8          0x01080001
#endif
#ifndef GVSP_PIX_MONO12
#define GVSP_PIX_MONO12         0x01100005
#endif
#ifndef GVSP_PIX_MONO12P
#define GVSP_PIX_MONO12P        0x010C0047
#endif
#ifndef GVSP_PIX_BAYERRG8
#define GVSP_PIX_BAYERRG8       0x01080009
#endif
#ifndef GVSP_PIX_BAYERRG10
#define GVSP_PIX_BAYERRG10      0x0110000D
#endif
#ifndef GVSP_PIX_BAYERRG12
#define GVSP_PIX_BAYERRG12      0x01100011
#endif
#ifndef GVSP_PIX_BAYERRG12P
#define GVSP_PIX_BAYERRG12P     0x010C0059
#endif
#ifndef GVSP_PIX_BAYERRG14
#define GVSP_PIX_BAYERRG14      0x0110010A
#endif
#ifndef GVSP_PIX_BAYERRG14P
#define GVSP_PIX_BAYERRG14P     0x010E0106
#endif
#ifndef GVSP_PIX_BAYERRG10P
#define GVSP_PIX_BAYERRG10P     0x010A0058
#endif
#ifndef GVSP_PIX_BAYERGR16
#define GVSP_PIX_BAYERGR16      0x0110002E
#endif
#ifndef GVSP_PIX_BAYERRG16
#define GVSP_PIX_BAYERRG16      0x0110002F
#endif
#ifndef GVSP_PIX_BAYERGB16
#define GVSP_PIX_BAYERGB16      0x01100030
#endif
#ifndef GVSP_PIX_BAYERBG16
#define GVSP_PIX_BAYERBG16      0x01100031
#endif

#ifndef GVSP_PIX_BAYERBG8
#define GVSP_PIX_BAYERBG8       0x0108000B
#endif
#ifndef GVSP_PIX_BAYERBG10
#define GVSP_PIX_BAYERBG10      0x0110000F
#endif
#ifndef GVSP_PIX_BAYERBG10P
#define GVSP_PIX_BAYERBG10P     0x010A0052
#endif
#ifndef GVSP_PIX_BAYERBG12
#define GVSP_PIX_BAYERBG12      0x01100013
#endif
#ifndef GVSP_PIX_BAYERBG12P
#define GVSP_PIX_BAYERBG12P     0x010C0053
#endif
#ifndef GVSP_PIX_BAYERBG14
#define GVSP_PIX_BAYERBG14      0x0110010C
#endif
#ifndef GVSP_PIX_BAYERBG14P
#define GVSP_PIX_BAYERBG14P     0x010E0108
#endif
#ifndef GVSP_PIX_BAYERGR8
#define GVSP_PIX_BAYERGR8       0x01080008
#endif
#ifndef GVSP_PIX_BAYERGR10
#define GVSP_PIX_BAYERGR10      0x0110000C
#endif
#ifndef GVSP_PIX_BAYERGR10P
#define GVSP_PIX_BAYERGR10P     0x010A0056
#endif
#ifndef GVSP_PIX_BAYERGR12
#define GVSP_PIX_BAYERGR12      0x01100010
#endif
#ifndef GVSP_PIX_BAYERGR12P
#define GVSP_PIX_BAYERGR12P     0x010C0057
#endif
#ifndef GVSP_PIX_BAYERGR14
#define GVSP_PIX_BAYERGR14      0x01100109
#endif
#ifndef GVSP_PIX_BAYERGR14P
#define GVSP_PIX_BAYERGR14P     0x010E0105
#endif
#ifndef GVSP_PIX_BAYERGB8
#define GVSP_PIX_BAYERGB8       0x0108000A
#endif
#ifndef GVSP_PIX_BAYERGB10
#define GVSP_PIX_BAYERGB10      0x0110000E
#endif
#ifndef GVSP_PIX_BAYERGB10P
#define GVSP_PIX_BAYERGB10P     0x010A0054
#endif
#ifndef GVSP_PIX_BAYERGB12
#define GVSP_PIX_BAYERGB12      0x01100012
#endif
#ifndef GVSP_PIX_BAYERGB12P
#define GVSP_PIX_BAYERGB12P     0x010C0055
#endif
#ifndef GVSP_PIX_BAYERGB14
#define GVSP_PIX_BAYERGB14      0x0110010B
#endif
#ifndef GVSP_PIX_BAYERGB14P
#define GVSP_PIX_BAYERGB14P     0x010E0107
#endif
#ifndef GVSP_PIX_YUV422_8
#define GVSP_PIX_YUV422_8       0x02100032
#endif
#ifndef GVSP_PIX_YCBCR422_8
#define GVSP_PIX_YCBCR422_8     0x0210003B
#endif
#ifndef GVSP_PIX_RGB565P
#define GVSP_PIX_RGB565P        0x02100035
#endif
#ifndef GVSP_PIX_BGR8
#define GVSP_PIX_BGR8           0x02180015
#endif
#ifndef GVSP_PIX_BGRA8
#define GVSP_PIX_BGRA8          0x02200017
#endif

#include <stdio.h>
#include <time.h>

#define LOG(tag, fmt, ...) \
    do { \
        struct timespec _ts; \
        clock_gettime(CLOCK_REALTIME, &_ts); \
        printf("[%04lld.%03lld] [" tag "] " fmt "\n", \
               (long long)(_ts.tv_sec % 10000), \
               (long long)(_ts.tv_nsec / 1000000), ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)

#define LOG_INFO(fmt, ...)   LOG("CFGD ", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   LOG("WARN ", fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)    LOG("ERROR", fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)    LOG("DEBUG", fmt, ##__VA_ARGS__)

#endif
