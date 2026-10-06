/*
 * sensor_chc5.h - CHC5 camera head I2C protocol definitions
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#ifndef _SENSOR_CHC5_H
#define _SENSOR_CHC5_H

#include <stddef.h>
#include <cyu3types.h>

#define CHC5_I2C_PROTO_VER  11

#define SENSOR_ADDR_WR      0x20
#define SENSOR_ADDR_RD      0x21
#define I2C_SLAVEADDR_MASK  0xFE

#define SENSOR_RESET_GPIO   22
#define DEBUG1_GPIO         23
#define DEBUG_GPIO          27

#define _countof(a)         (sizeof(a) / sizeof((a)[0]))
#define GET_WORD_MSB(x)     (((x) >> 8) & 0xFF)
#define GET_WORD_LSB(x)     ( (x)       & 0xFF)
#define CHC5_SENSOR_ID      0xC5
#define CAMERA_ID           CHC5_SENSOR_ID

#define CHC5_DYNCFG_I2C_REG     0x00
#define CHC5_READBACK_I2C_REG   0x40

#define PFNC_MONO8          0x01080001
#define PFNC_MONO10         0x01100003
#define PFNC_MONO12         0x01100005
#define PFNC_BAYERRG8       0x01080009
#define PFNC_BAYERRG10      0x0110000D
#define PFNC_BAYERRG12      0x01100011
#define PFNC_YUV422_8       0x02100032
#define PFNC_YUV422_8_UYVY  0x0210001F
#define PFNC_RGB8           0x02180014

typedef struct __attribute__((packed)) chc5_i2c_cfg_s {
    uint16_t    width;
    uint16_t    height;
    uint16_t    offset_x;
    uint16_t    offset_y;
    uint32_t    exposure_us;
    uint32_t    fps_mhz;
    uint32_t    pixel_format;
    uint16_t    analog_gain;
    uint16_t    black_level;
    uint8_t     stream_enable;
    uint8_t     tp_bin;
    uint8_t     auto_flags;
    uint8_t     ae_metering_mode;
    uint8_t     ae_target;
    uint8_t     flip_flags;
    uint16_t    gamma_x100;
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
    uint8_t     reserved_38[4];
    uint8_t     crc8;
    uint8_t     aux_seq;
    uint8_t     proto_ver;
    uint8_t     write_seq;
} chc5_i2c_cfg_t;

#define CHC5_TPBIN_TP_MASK          0x0F
#define CHC5_TPBIN_BINX_SHIFT       4
#define CHC5_TPBIN_BINY_SHIFT       6
#define CHC5_TPBIN_BIN_MASK         0x03
#define CHC5_FLIP_FLAGS_MASK        0x0F

#define CHC5_I2C_OFF(f, o) \
    typedef char _chc5_i2c_off_##f[(offsetof(chc5_i2c_cfg_t, f) == (o)) ? 1 : -1]
CHC5_I2C_OFF(width,            0x00);
CHC5_I2C_OFF(height,           0x02);
CHC5_I2C_OFF(offset_x,         0x04);
CHC5_I2C_OFF(offset_y,         0x06);
CHC5_I2C_OFF(exposure_us,      0x08);
CHC5_I2C_OFF(fps_mhz,          0x0C);
CHC5_I2C_OFF(pixel_format,     0x10);
CHC5_I2C_OFF(analog_gain,      0x14);
CHC5_I2C_OFF(black_level,      0x16);
CHC5_I2C_OFF(stream_enable,    0x18);
CHC5_I2C_OFF(tp_bin,           0x19);
CHC5_I2C_OFF(auto_flags,       0x1A);
CHC5_I2C_OFF(ae_metering_mode, 0x1B);
CHC5_I2C_OFF(ae_target,        0x1C);
CHC5_I2C_OFF(flip_flags,       0x1D);
CHC5_I2C_OFF(gamma_x100,       0x1E);
CHC5_I2C_OFF(ae_exp_lower_us,  0x20);
CHC5_I2C_OFF(ae_exp_upper_us,  0x24);
CHC5_I2C_OFF(ae_roi_x,         0x28);
CHC5_I2C_OFF(ae_roi_h,         0x2E);
CHC5_I2C_OFF(balance_ratio_r,  0x30);
CHC5_I2C_OFF(balance_ratio_b,  0x32);
CHC5_I2C_OFF(awb_color_temp,   0x34);
CHC5_I2C_OFF(awb_rate_x100,    0x37);
CHC5_I2C_OFF(reserved_38,      0x38);
CHC5_I2C_OFF(crc8,             0x3C);
CHC5_I2C_OFF(aux_seq,          0x3D);
CHC5_I2C_OFF(proto_ver,        0x3E);
CHC5_I2C_OFF(write_seq,        0x3F);

#define CHC5_GAMMA_NO_CHANGE    0
#define CHC5_GAMMA_OFF          1
#define CHC5_GAMMA_SRGB         2
#define CHC5_GAMMA_USER_MIN     25
#define CHC5_GAMMA_USER_MAX     400
#define CHC5_GAMMA_CURVE_ADJ    0x8000

#define STREAM_CMD_MASK     0x03
#define STREAM_CMD_NONE     0x00
#define STREAM_CMD_START    0x01
#define STREAM_CMD_STOP     0x02
#define STREAM_ACTIVE_FLAG  0x10
#define STREAM_STATE_OFF    0x00
#define STREAM_STATE_ON     STREAM_ACTIVE_FLAG

#define AUTO_EXPOSURE_MASK      0x03
#define AUTO_EXPOSURE_SHIFT     0
#define AUTO_GAIN_MASK          0x0C
#define AUTO_GAIN_SHIFT         2
#define AUTO_WB_MASK            0x30
#define AUTO_WB_SHIFT           4
#define AUTO_CMD_INVALID        0x00
#define AUTO_CMD_ENABLE         0x01
#define AUTO_CMD_DISABLE        0x02
#define AUTO_CMD_ONCE           0x03

typedef struct __attribute__((packed)) imgsensor_readback_s {
    uint8_t     rb_seq;
    uint8_t     ae_status;
    uint16_t    _reserved0;
    uint32_t    exposure_us;
    uint16_t    analog_gain;
    uint16_t    _reserved_0a;
    int16_t     temp_c_x10;
    uint8_t     _reserved1[8];
    uint8_t     applied_seq;
    uint8_t     rb_flags;
    uint32_t    fps_mhz;
    uint8_t     _reserved2[3];
    uint8_t     rb_seq_echo;
} imgsensor_readback_t;

#define RB_AE_STATUS_EXP_ACTIVE     (1u << 0)
#define RB_AE_STATUS_GAIN_ACTIVE    (1u << 1)
#define RB_AE_STATUS_CONVERGED      (1u << 2)

#define RB_F_APPLIED_SEQ            0x01

typedef char _chc5_cfg_size_check      [sizeof(chc5_i2c_cfg_t)      == 64 ? 1 : -1];
typedef char _chc5_readback_size_check [sizeof(imgsensor_readback_t) == 32 ? 1 : -1];
typedef char _chc5_rb_applied_seq_check[offsetof(imgsensor_readback_t, applied_seq) == 0x16 ? 1 : -1];
typedef char _chc5_rb_fps_mhz_check    [offsetof(imgsensor_readback_t, fps_mhz)     == 0x18 ? 1 : -1];

#define CHC5_CFG_CRC_OFF    0x3C
uint8_t chc5_crc8(const uint8_t *blk, uint8_t skip);

#define SENSOR_MODE0_WIDTH   (unsigned int)640
#define SENSOR_MODE0_HEIGHT  (unsigned int)480
#define SENSOR_MODE0_FPS     (unsigned int)200
#define SENSOR_MODE1_WIDTH   (unsigned int)1332
#define SENSOR_MODE1_HEIGHT  (unsigned int)990
#define SENSOR_MODE1_FPS     (unsigned int)100
#define SENSOR_MODE2_WIDTH   (unsigned int)640
#define SENSOR_MODE2_HEIGHT  (unsigned int)127
#define SENSOR_MODE2_FPS     (unsigned int)1200
#define SENSOR_MODE3_WIDTH   (unsigned int)1920
#define SENSOR_MODE3_HEIGHT  (unsigned int)1080
#define SENSOR_MODE3_FPS     (unsigned int)35
#define SENSOR_MODE4_WIDTH   (unsigned int)4056
#define SENSOR_MODE4_HEIGHT  (unsigned int)3040
#define SENSOR_MODE4_FPS_MIN (unsigned int)5
#define SENSOR_MODE4_FPS     (unsigned int)10

uint8_t             SensorI2cBusTest(void);
void                SensorInit(void);
CyBool_t            sensor_stream_stop(void);
CyBool_t            sensor_is_streaming(void);
void                sensor_set_streaming(CyBool_t active);
CyU3PReturnStatus_t sensor_write_config(void);
uint8_t             sensor_last_write_seq(void);
CyBool_t            sensor_resend_stop(void);
void sensor_i2c_error_read(void);
void sensor_i2c_lock_init(void);
CyBool_t            sensor_resend_start(CyBool_t restart);
#if UVC_TEST_HOOKS
#define TEST_HOOK_LOSE_START        0x01
#define TEST_HOOK_FAIL_START        0x02
extern volatile uint8_t g_test_start_hook;
#endif
CyU3PReturnStatus_t sensor_i2c_write(uint8_t reg, uint8_t *data, uint8_t size);
CyU3PReturnStatus_t sensor_i2c_read(uint8_t reg, uint8_t *data, uint8_t size);

extern chc5_i2c_cfg_t *sensor_cfg;
extern chc5_i2c_cfg_t *selected_img_mode;

void     sensor_build_config_from_modes(void);
void     sensor_handle_uvc_control(uint8_t frame_index, uint32_t interval);
void     sensor_set_analog_gain(uint16_t gain);
uint16_t sensor_get_analog_gain(void);
void     sensor_set_exposure(uint32_t exposure_us);
uint32_t sensor_get_exposure(void);
void     sensor_set_test_pattern(uint8_t pattern);
uint8_t  sensor_get_test_pattern(void);
void     sensor_set_gamma_x100(uint16_t v);
void     sensor_set_auto_flags(uint8_t flags);
uint8_t  sensor_get_auto_flags(void);

#endif
