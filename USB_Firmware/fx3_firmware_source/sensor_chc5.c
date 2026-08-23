/*
 * sensor_chc5.c - CHC5 camera head I2C config/readback driver
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#include <cyu3system.h>
#include <cyu3os.h>
#include <cyu3dma.h>
#include <cyu3error.h>
#include <cyu3uart.h>
#include <cyu3i2c.h>
#include <cyu3types.h>
#include <cyu3gpio.h>
#include <cyu3utils.h>
#include "sensor_chc5.h"
#include "uvc_modes.h"

static imgsensor_cfg_t sensor_cfg_buf = { 0 };

imgsensor_cfg_t *sensor_cfg        = NULL;
imgsensor_cfg_t *selected_img_mode = NULL;

static void SensorI2CAccessDelay(CyU3PReturnStatus_t status)
{
    if (status == CY_U3P_SUCCESS)
        CyU3PBusyWait(50);
}

CyU3PReturnStatus_t sensor_i2c_write(uint8_t reg, uint8_t *data, uint8_t size)
{
    CyU3PReturnStatus_t status;
    CyU3PI2cPreamble_t preamble;

    preamble.buffer[0] = SENSOR_ADDR_WR;
    preamble.buffer[1] = reg;
    preamble.length    = 2;
    preamble.ctrlMask  = 0x0000;

    status = CyU3PI2cTransmitBytes(&preamble, data, size, 0);
    SensorI2CAccessDelay(status);
    if (status != CY_U3P_SUCCESS)
        CyU3PDebugPrint(4, "WR fail 0x%x\r\n", status);
    return status;
}

CyU3PReturnStatus_t sensor_i2c_read(uint8_t reg, uint8_t *data, uint8_t size)
{
    CyU3PReturnStatus_t status;
    CyU3PI2cPreamble_t preamble;

    preamble.length    = 3;
    preamble.buffer[0] = SENSOR_ADDR_RD & I2C_SLAVEADDR_MASK;
    preamble.buffer[1] = reg;
    preamble.buffer[2] = SENSOR_ADDR_RD;
    preamble.ctrlMask  = 1 << 1;

    status = CyU3PI2cReceiveBytes(&preamble, data, size, 0);
    SensorI2CAccessDelay(status);
    if (status != CY_U3P_SUCCESS)
        CyU3PDebugPrint(4, "RD fail 0x%x\r\n", status);
    return status;
}

static uint8_t         g_write_seq = 0;
static imgsensor_cfg_t sensor_cfg_last;
static CyBool_t        g_cfg_last_valid = CyFalse;

#define I2C_MAX_WR_CHUNK  15u
static CyU3PReturnStatus_t sensor_i2c_write_retry(uint8_t reg, uint8_t *data, uint8_t size)
{
    uint8_t done = 0;
    while (done < size)
    {
        uint8_t chunk = (uint8_t)((size - done > I2C_MAX_WR_CHUNK) ? I2C_MAX_WR_CHUNK
                                                                   : (size - done));
        uint8_t at = (uint8_t)(reg + done);
        CyU3PReturnStatus_t status = sensor_i2c_write(at, data + done, chunk);
        if (status != CY_U3P_SUCCESS)
        {
            CyU3PDebugPrint(4, "[CFG] wr@0x%x fail 0x%x retry\r\n", at, status);
            CyU3PBusyWait(100);
            status = sensor_i2c_write(at, data + done, chunk);
            if (status != CY_U3P_SUCCESS)
            {
                CyU3PDebugPrint(1, "[CFG] wr@0x%x retry fail 0x%x\r\n", at, status);
                return status;
            }
        }
        done += chunk;
    }
    return CY_U3P_SUCCESS;
}

CyU3PReturnStatus_t sensor_write_config(void)
{
    CyU3PReturnStatus_t status;
    uint8_t *cur  = (uint8_t *)sensor_cfg;
    uint8_t *prev = (uint8_t *)&sensor_cfg_last;
    uint16_t footer_off = (uint16_t)((uint8_t *)&sensor_cfg->proto_ver - cur);
    uint16_t footer_len = (uint16_t)(sizeof(*sensor_cfg) - footer_off);
    int      first = -1, last = -1;
    uint16_t i;

    sensor_cfg->proto_ver   = CHC5_I2C_PROTO_VER;
    sensor_cfg->proto_flags = 0;
    g_write_seq++;
    sensor_cfg->write_seq = g_write_seq;

    if (g_cfg_last_valid &&
        (sensor_cfg->stream_enable & STREAM_CMD_MASK) != STREAM_CMD_START)
    {
        for (i = 0; i < footer_off; i++)
            if (cur[i] != prev[i]) { if (first < 0) first = (int)i; last = (int)i; }
    }
    else { first = 0; last = (int)footer_off - 1; }

    CyU3PDebugPrint(4, "[CFG] s=%d se=%d span[%d..%d] %dx%d exp=%d g=%d af=0x%x\r\n",
        g_write_seq, sensor_cfg->stream_enable, first, last,
        sensor_cfg->width, sensor_cfg->height,
        (uint32_t)sensor_cfg->exposure_us, sensor_cfg->analog_gain,
        sensor_cfg->auto_flags);

    if (first >= 0)
    {
        status = sensor_i2c_write_retry((uint8_t)(CHC5_DYNCFG_I2C_REG + first),
                                        cur + first, (uint8_t)(last - first + 1));
        if (status != CY_U3P_SUCCESS)
            return status;
    }

    status = sensor_i2c_write_retry((uint8_t)(CHC5_DYNCFG_I2C_REG + footer_off),
                                    cur + footer_off, (uint8_t)footer_len);
    if (status == CY_U3P_SUCCESS)
    {
        CyU3PMemCopy(prev, cur, sizeof(*sensor_cfg));
        g_cfg_last_valid = CyTrue;
    }
    return status;
}

uint8_t SensorI2cBusTest(void)
{
    return CY_U3P_SUCCESS;
}

void SensorInit(void)
{
    SensorI2cBusTest();
    sensor_cfg        = &sensor_cfg_buf;
    selected_img_mode = sensor_cfg;
}

static CyBool_t g_streaming = CyFalse;

CyBool_t sensor_is_streaming(void)          { return g_streaming; }
void     sensor_set_streaming(CyBool_t a)   { g_streaming = a; }

CyBool_t sensor_stream_stop(void)
{
    if (!g_streaming)
        return CyFalse;
    sensor_cfg->stream_enable = STREAM_ACTIVE_FLAG | STREAM_CMD_STOP;
    sensor_write_config();
    g_streaming = CyFalse;
    return CyTrue;
}


void sensor_build_config_from_modes(void)
{
    selected_img_mode = sensor_cfg;
    if (g_uvc_nmodes >= 1)
    {
        const uvc_mode_t *m = &g_uvc_modes[0];
        uint16_t fps = m->fps ? m->fps : 30;
        sensor_cfg->width        = m->width;
        sensor_cfg->height       = m->height;
        sensor_cfg->fps          = fps;
        sensor_cfg->binning      = m->binning;
        sensor_cfg->pixel_format = m->pixel_format;
        sensor_cfg->exposure_us  = 1000000u / fps;
        sensor_cfg->black_level  = 256;
    }
}

void sensor_handle_uvc_control(uint8_t frame_index, uint32_t interval)
{
    if (frame_index >= 1 && frame_index <= g_uvc_nmodes)
    {
        const uvc_mode_t *m = &g_uvc_modes[frame_index - 1];
        sensor_cfg->width        = m->width;
        sensor_cfg->height       = m->height;
        sensor_cfg->binning      = m->binning;
        sensor_cfg->pixel_format = m->pixel_format;
        if (interval)
        {
            uint16_t fps = (uint16_t)(10000000u / interval);
            if (fps) sensor_cfg->fps = fps;
        }
        else if (m->fps)
        {
            sensor_cfg->fps = m->fps;
        }
    }
}

void     sensor_set_analog_gain(uint16_t gain)   { sensor_cfg->analog_gain = gain; }
uint16_t sensor_get_analog_gain(void)            { return sensor_cfg->analog_gain; }
void     sensor_set_exposure(uint32_t us)        { sensor_cfg->exposure_us = us; }
uint32_t sensor_get_exposure(void)               { return sensor_cfg->exposure_us; }
void     sensor_set_test_pattern(uint8_t p)      { sensor_cfg->test_pattern = p; }
uint8_t  sensor_get_test_pattern(void)           { return sensor_cfg->test_pattern; }
void     sensor_set_auto_flags(uint8_t f)        { sensor_cfg->auto_flags = f; }
uint8_t  sensor_get_auto_flags(void)             { return sensor_cfg->auto_flags; }
