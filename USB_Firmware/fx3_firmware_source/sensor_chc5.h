/*
 * sensor_chc5.h - CHC5 camera head I2C protocol definitions
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#ifndef _SENSOR_CHC5_H
#define _SENSOR_CHC5_H

#include <cyu3types.h>

#define CHC5_I2C_PROTO_VER  5

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
    uint8_t     reserved[9];
    uint8_t     proto_ver;
    uint8_t     proto_flags;
    uint8_t     write_seq;
} imgsensor_cfg_t;

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
    uint16_t    fps_x100;
    int16_t     temp_c_x10;
    uint8_t     _reserved1[17];
    uint8_t     rb_seq_echo;
} imgsensor_readback_t;

#define RB_AE_STATUS_EXP_ACTIVE     (1u << 0)
#define RB_AE_STATUS_GAIN_ACTIVE    (1u << 1)
#define RB_AE_STATUS_CONVERGED      (1u << 2)

typedef char _chc5_cfg_size_check      [sizeof(imgsensor_cfg_t)      == 64 ? 1 : -1];
typedef char _chc5_readback_size_check [sizeof(imgsensor_readback_t) == 32 ? 1 : -1];

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
CyU3PReturnStatus_t sensor_i2c_write(uint8_t reg, uint8_t *data, uint8_t size);
CyU3PReturnStatus_t sensor_i2c_read(uint8_t reg, uint8_t *data, uint8_t size);

extern imgsensor_cfg_t *sensor_cfg;
extern imgsensor_cfg_t *selected_img_mode;

void     sensor_build_config_from_modes(void);
void     sensor_handle_uvc_control(uint8_t frame_index, uint32_t interval);
void     sensor_set_analog_gain(uint16_t gain);
uint16_t sensor_get_analog_gain(void);
void     sensor_set_exposure(uint32_t exposure_us);
uint32_t sensor_get_exposure(void);
void     sensor_set_test_pattern(uint8_t pattern);
uint8_t  sensor_get_test_pattern(void);
void     sensor_set_auto_flags(uint8_t flags);
uint8_t  sensor_get_auto_flags(void);

#endif
